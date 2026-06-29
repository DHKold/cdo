#include "compiler_internal.h"
#include "core/compiler.h"
#include "core/cache.h"
#include "commons/threadpool.h"
#include "pal/pal.h"
#include "core/output.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
// compiler_compile_batch — generate commands and execute via thread pool
// ---------------------------------------------------------------------------

// Maximum number of arguments we'll pass to the compiler
#define MAX_COMPILE_ARGS 256

/// Determine if a source file is C++ based on its extension.
static bool is_cpp_source(const char* path) {
    const char* ext = pal_path_ext(path);
    if (!ext) return false;
    return (strcmp(ext, ".cpp") == 0 ||
            strcmp(ext, ".cxx") == 0 ||
            strcmp(ext, ".cc") == 0  ||
            strcmp(ext, ".C") == 0);
}

// --- Context passed to each compile task in the thread pool ---
typedef struct {
    const CompileJob*   job;
    const CompilerInfo* info;
    const char*         cache_backend;  // Non-NULL when external cache backend (e.g., "ccache", "sccache") should prefix the compiler
    int                 result;         // 0 = success, non-zero = failure
} CompileTaskCtx;

/// Build the argument list for GCC or Clang.
/// Returns the number of arguments written into args[], or -1 on overflow.
static int build_gcc_clang_args(const CompileJob* job, const CompilerInfo* info,
                                const char** args, int max_args) {
    int n = 0;

    // -c (compile only, no linking)
    if (n >= max_args) return -1;
    args[n++] = "-c";

    // Source file
    if (n >= max_args) return -1;
    args[n++] = job->source_path;

    // Output object file: -o <path>
    if (n >= max_args - 1) return -1;
    args[n++] = "-o";
    args[n++] = job->object_path;

    // Include paths: -I<path>
    for (int i = 0; i < job->include_path_count; i++) {
        if (n >= max_args) return -1;
        // We'll build a string "-I<path>" — use a static buffer pattern.
        // Since pal_spawn copies the args, we need persistent memory.
        // We'll use a simple approach: store the -I prefix inline.
        args[n++] = "-I";
        if (n >= max_args) return -1;
        args[n++] = job->include_paths[i];
    }

    // Defines: -D<define>
    for (int i = 0; i < job->define_count; i++) {
        if (n >= max_args) return -1;
        args[n++] = "-D";
        if (n >= max_args) return -1;
        args[n++] = job->defines[i];
    }

    // Language standard
    // For C++ sources, use cpp_standard; for C sources, use c_standard
    bool cpp = is_cpp_source(job->source_path);
    const char* std_val = cpp ? job->cpp_standard : job->c_standard;
    if (std_val && std_val[0] != '\0') {
        if (n >= max_args) return -1;
        // Build "-std=<value>" in a thread-local buffer
        static _Thread_local char std_buf[64];
        snprintf(std_buf, sizeof(std_buf), "-std=%s", std_val);
        args[n++] = std_buf;
    }

    // Optimization
    if (n >= max_args) return -1;
    args[n++] = job->optimize ? "-O2" : "-O0";

    // Debug info
    if (job->debug_info) {
        if (n >= max_args) return -1;
        args[n++] = "-g";
    }

    // Dependency tracking: -MD -MF <dep_file_path>
    // Generates a .d file listing all included headers (system + user).
    // Dep file path is the object path with .o replaced by .d.
    if (n >= max_args - 2) return -1;
    args[n++] = "-MD";
    args[n++] = "-MF";
    static _Thread_local char dep_file_buf[280];
    {
        size_t olen = strlen(job->object_path);
        if (olen >= sizeof(dep_file_buf)) return -1;
        memcpy(dep_file_buf, job->object_path, olen + 1);
        // Replace trailing ".o" with ".d"
        if (olen >= 2 && dep_file_buf[olen - 2] == '.' && dep_file_buf[olen - 1] == 'o') {
            dep_file_buf[olen - 1] = 'd';
        } else {
            // Fallback: append .d
            snprintf(dep_file_buf, sizeof(dep_file_buf), "%s.d", job->object_path);
        }
    }
    if (n >= max_args) return -1;
    args[n++] = dep_file_buf;

    // Extra flags
    for (int i = 0; i < job->extra_flag_count; i++) {
        if (n >= max_args) return -1;
        args[n++] = job->extra_flags[i];
    }

    (void)info; // info->path is used as the program, not as an arg
    return n;
}

/// Build the argument list for MSVC (cl.exe).
/// Returns the number of arguments written into args[], or -1 on overflow.
static int build_msvc_args(const CompileJob* job, const char** args, int max_args) {
    int n = 0;

    // /c (compile only)
    if (n >= max_args) return -1;
    args[n++] = "/c";

    // Source file
    if (n >= max_args) return -1;
    args[n++] = job->source_path;

    // Output object: /Fo:<path>
    if (n >= max_args) return -1;
    static _Thread_local char fo_buf[280];
    snprintf(fo_buf, sizeof(fo_buf), "/Fo:%s", job->object_path);
    args[n++] = fo_buf;

    // Include paths: /I<path>
    // MSVC accepts /I <path> (separate) or /I<path> (combined)
    for (int i = 0; i < job->include_path_count; i++) {
        if (n >= max_args) return -1;
        args[n++] = "/I";
        if (n >= max_args) return -1;
        args[n++] = job->include_paths[i];
    }

    // Defines: /D <define> (separate args, MSVC accepts /D <value> form)
    for (int i = 0; i < job->define_count; i++) {
        if (n >= max_args) return -1;
        args[n++] = "/D";
        if (n >= max_args) return -1;
        args[n++] = job->defines[i];
    }

    // Language standard
    bool cpp = is_cpp_source(job->source_path);
    const char* std_val = cpp ? job->cpp_standard : job->c_standard;
    if (std_val && std_val[0] != '\0') {
        if (n >= max_args) return -1;
        static _Thread_local char std_buf[64];
        snprintf(std_buf, sizeof(std_buf), "/std:%s", std_val);
        args[n++] = std_buf;
    }

    // Optimization
    if (n >= max_args) return -1;
    args[n++] = job->optimize ? "/O2" : "/Od";

    // Debug info
    if (job->debug_info) {
        if (n >= max_args) return -1;
        args[n++] = "/Zi";
    }

    // Dependency tracking: /showIncludes
    if (n >= max_args) return -1;
    args[n++] = "/showIncludes";

    // Suppress logo
    if (n >= max_args) return -1;
    args[n++] = "/nologo";

    // Extra flags
    for (int i = 0; i < job->extra_flag_count; i++) {
        if (n >= max_args) return -1;
        args[n++] = job->extra_flags[i];
    }

    return n;
}

/// Compute the dep file path from an object path (replace .o/.obj with .d).
/// Writes into out_buf which must be at least 280 bytes.
static void compute_dep_path(const char* object_path, char* out_buf, size_t buf_size) {
    size_t olen = strlen(object_path);
    if (olen >= buf_size) { out_buf[0] = '\0'; return; }
    memcpy(out_buf, object_path, olen + 1);
    // Replace trailing ".obj" with ".d" (MSVC) or ".o" with ".d" (GCC/Clang)
    if (olen >= 4 && strcmp(out_buf + olen - 4, ".obj") == 0) {
        strcpy(out_buf + olen - 4, ".d");
    } else if (olen >= 2 && out_buf[olen - 2] == '.' && out_buf[olen - 1] == 'o') {
        out_buf[olen - 1] = 'd';
    } else {
        snprintf(out_buf, buf_size, "%s.d", object_path);
    }
}

/// Parse MSVC /showIncludes output and write a Makefile-format .d file.
/// Each include line has the form: "Note: including file: <path>"
/// Writes: "object_path: source_path header1 header2 ..."
static void write_msvc_dep_file(const char* stdout_buf, const char* object_path, const char* source_path) {
    if (!stdout_buf || !object_path || !source_path) return;

    char dep_path[280];
    compute_dep_path(object_path, dep_path, sizeof(dep_path));
    if (dep_path[0] == '\0') return;

    FILE* f = fopen(dep_path, "w");
    if (!f) return;

    // Write target: source
    fprintf(f, "%s: %s", object_path, source_path);

    // Parse /showIncludes lines from stdout
    const char* prefix = "Note: including file:";
    size_t prefix_len = strlen(prefix);
    const char* line = stdout_buf;
    while (line && *line) {
        const char* next = strchr(line, '\n');
        size_t line_len = next ? (size_t)(next - line) : strlen(line);

        // Check for the prefix (case-sensitive)
        if (line_len > prefix_len && strncmp(line, prefix, prefix_len) == 0) {
            // Skip prefix and leading whitespace
            const char* path_start = line + prefix_len;
            while (*path_start == ' ' || *path_start == '\t') path_start++;
            size_t path_len = line_len - (size_t)(path_start - line);
            // Trim trailing \r
            while (path_len > 0 && (path_start[path_len - 1] == '\r' || path_start[path_len - 1] == '\n')) path_len--;
            if (path_len > 0) {
                fprintf(f, " \\\n  %.*s", (int)path_len, path_start);
            }
        }

        if (!next) break;
        line = next + 1;
    }

    fprintf(f, "\n");
    fclose(f);
}

/// Thread pool task function: compile a single source file.
static void compile_task(void* arg) {
    CompileTaskCtx* ctx = (CompileTaskCtx*)arg;
    const CompileJob* job = ctx->job;
    const CompilerInfo* info = ctx->info;

    const char* args[MAX_COMPILE_ARGS];
    int arg_count = 0;

    if (info->family == COMPILER_MSVC) {
        arg_count = build_msvc_args(job, args, MAX_COMPILE_ARGS);
    } else {
        // GCC or Clang
        arg_count = build_gcc_clang_args(job, info, args, MAX_COMPILE_ARGS);
    }

    if (arg_count < 0) {
        cdo_error("Too many compiler arguments for: %s", job->source_path);
        ctx->result = -1;
        return;
    }

    // Spawn the compiler process
    // Use C++ compiler for C++ sources
    const char* compiler_program = info->path;
    if (is_cpp_source(job->source_path) && info->family != COMPILER_MSVC) {
        // For GCC: gcc -> g++, for Clang: clang -> clang++
        static _Thread_local char cpp_program[260];
        if (info->family == COMPILER_GCC) {
            size_t plen = strlen(info->path);
            if (plen >= 7 && strcmp(info->path + plen - 7, "gcc.exe") == 0) {
                // "path/to/gcc.exe" -> "path/to/g++.exe"
                snprintf(cpp_program, sizeof(cpp_program), "%s", info->path);
                strcpy(cpp_program + plen - 7, "g++.exe");
                compiler_program = cpp_program;
            } else if (plen >= 3 && strcmp(info->path + plen - 3, "gcc") == 0) {
                // "gcc" or "path/to/gcc" -> "g++" or "path/to/g++"
                snprintf(cpp_program, sizeof(cpp_program), "%s", info->path);
                strcpy(cpp_program + plen - 3, "g++");
                compiler_program = cpp_program;
            } else if (strcmp(info->path, "cc") == 0) {
                compiler_program = "c++";
            }
        } else if (info->family == COMPILER_CLANG) {
            size_t plen = strlen(info->path);
            if (plen >= 9 && strcmp(info->path + plen - 9, "clang.exe") == 0) {
                snprintf(cpp_program, sizeof(cpp_program), "%s", info->path);
                strcpy(cpp_program + plen - 9, "clang++.exe");
                compiler_program = cpp_program;
            } else if (plen >= 5 && strcmp(info->path + plen - 5, "clang") == 0) {
                snprintf(cpp_program, sizeof(cpp_program), "%s", info->path);
                strcpy(cpp_program + plen - 5, "clang++");
                compiler_program = cpp_program;
            } else {
                snprintf(cpp_program, sizeof(cpp_program), "%s++", info->path);
                compiler_program = cpp_program;
            }
        }
    }
    // MSVC uses cl.exe for both C and C++ (detects from extension)

    // External cache backend prefix mode: when an external backend (ccache/sccache)
    // is configured, prefix the compiler invocation so the backend wraps the compiler.
    // e.g., "ccache gcc -c main.c -o main.o" instead of "gcc -c main.c -o main.o"
    const char* spawn_program = compiler_program;
    const char* spawn_args[MAX_COMPILE_ARGS + 1];  // +1 for the original program inserted as first arg
    int spawn_arg_count = arg_count;

    if (ctx->cache_backend != NULL) {
        // Shift all existing args right by 1, inserting the original compiler as first arg
        if (arg_count + 1 > MAX_COMPILE_ARGS) {
            cdo_error("Too many compiler arguments (with cache backend prefix) for: %s", job->source_path);
            ctx->result = -1;
            return;
        }
        spawn_args[0] = compiler_program;
        for (int i = 0; i < arg_count; i++) {
            spawn_args[i + 1] = args[i];
        }
        spawn_program = ctx->cache_backend;
        spawn_arg_count = arg_count + 1;
    } else {
        for (int i = 0; i < arg_count; i++) {
            spawn_args[i] = args[i];
        }
    }

    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.program = spawn_program;
    opts.args = spawn_args;
    opts.arg_count = spawn_arg_count;
    opts.capture_output = true;

    PalSpawnResult result;
    memset(&result, 0, sizeof(result));

    cdo_trace("Compiling: %s", job->source_path);

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        cdo_error("Failed to spawn compiler for: %s", job->source_path);
        ctx->result = -1;
        pal_spawn_result_free(&result);
        return;
    }

    if (result.exit_code != 0) {
        // Print compiler error output
        if (result.stderr_buf && result.stderr_buf[0] != '\0') {
            cdo_error("%s", result.stderr_buf);
        }
        if (result.stdout_buf && result.stdout_buf[0] != '\0') {
            cdo_error("%s", result.stdout_buf);
        }
        cdo_error("Compilation failed: %s (exit code %d)",
                  job->source_path, result.exit_code);
        ctx->result = result.exit_code;
    } else {
        ctx->result = 0;
        cdo_trace("Compiled: %s -> %s", job->source_path, job->object_path);

        // For MSVC: parse /showIncludes output and write a .d dep file
        if (info->family == COMPILER_MSVC && result.stdout_buf) {
            write_msvc_dep_file(result.stdout_buf, job->object_path, job->source_path);
        }
    }

    pal_spawn_result_free(&result);
}

int compiler_compile_batch(const CompileJob* jobs, int job_count,
                           const CompilerInfo* info, int parallelism,
                           const CacheConfig* cache_config, CacheStats* cache_stats,
                           bool no_cache) {
    if (!jobs || job_count <= 0 || !info) return -1;
    if (info->family == COMPILER_UNKNOWN) {
        cdo_error("Cannot compile: no compiler detected");
        return -1;
    }

    // Determine if caching is active (builtin cache only when backend == "builtin")
    bool use_cache = (cache_config != NULL && cache_config->enabled && !no_cache && cache_stats != NULL && strcmp(cache_config->backend, "builtin") == 0);

    // Determine if an external cache backend (ccache/sccache) should prefix compiler commands
    const char* external_backend = NULL;
    if (cache_config != NULL && cache_config->enabled && !no_cache && strcmp(cache_config->backend, "builtin") != 0 && cache_config->backend[0] != '\0') {
        external_backend = cache_config->backend;
    }

    // If caching is active, perform cache lookups before dispatching to thread pool.
    // Build a list of jobs that actually need compilation (cache misses).
    // Also store computed cache keys for use during the store phase after compilation.
    int* miss_indices = NULL;
    int miss_count = 0;
    char (*cache_keys)[CACHE_KEY_HEX_LEN + 1] = NULL;  // Array of keys, indexed by miss slot
    bool* key_valid = NULL;  // Whether cache_keys[i] holds a valid key for miss slot i

    if (use_cache) {
        miss_indices = (int*)malloc((size_t)job_count * sizeof(int));
        cache_keys = (char (*)[CACHE_KEY_HEX_LEN + 1])calloc((size_t)job_count, CACHE_KEY_HEX_LEN + 1);
        key_valid = (bool*)calloc((size_t)job_count, sizeof(bool));
        if (!miss_indices || !cache_keys || !key_valid) {
            cdo_error("Failed to allocate cache lookup indices");
            free(miss_indices);
            free(cache_keys);
            free(key_valid);
            return -1;
        }

        for (int i = 0; i < job_count; i++) {
            const CompileJob* job = &jobs[i];

            // Compute dep file path: replace trailing ".o" with ".d"
            char dep_path[280];
            size_t olen = strlen(job->object_path);
            if (olen < sizeof(dep_path)) {
                memcpy(dep_path, job->object_path, olen + 1);
                if (olen >= 4 && strcmp(dep_path + olen - 4, ".obj") == 0) {
                    strcpy(dep_path + olen - 4, ".d");
                } else if (olen >= 2 && dep_path[olen - 2] == '.' && dep_path[olen - 1] == 'o') {
                    dep_path[olen - 1] = 'd';
                } else {
                    snprintf(dep_path, sizeof(dep_path), "%s.d", job->object_path);
                }
            } else {
                dep_path[0] = '\0';
            }

            // Determine language standard string
            const char* lang_std = NULL;
            if (is_cpp_source(job->source_path)) {
                lang_std = job->cpp_standard;
            } else {
                lang_std = job->c_standard;
            }

            // Build CacheKeyInputs
            CacheKeyInputs key_inputs = {0};
            key_inputs.source_path = job->source_path;
            key_inputs.compiler_path = info->path;
            key_inputs.compiler_version = info->version;
            key_inputs.language_standard = lang_std;
            key_inputs.optimize = job->optimize;
            key_inputs.debug_info = job->debug_info;
            key_inputs.defines = job->defines;
            key_inputs.define_count = job->define_count;
            key_inputs.include_paths = job->include_paths;
            key_inputs.include_path_count = job->include_path_count;
            key_inputs.dep_file_path = (dep_path[0] != '\0') ? dep_path : NULL;

            // Compute cache key
            char cache_key[CACHE_KEY_HEX_LEN + 1];
            int key_rc = cache_compute_key(&key_inputs, cache_key);
            if (key_rc != 0) {
                // Cannot compute key (e.g., no dep file on first build), treat as miss
                int slot = miss_count;
                miss_indices[miss_count++] = i;
                key_valid[slot] = false;
                cache_stats->misses++;
                cdo_trace("Cache key computation failed for: %s (treating as miss)", job->source_path);
                continue;
            }

            // Try cache lookup
            int lookup_rc = cache_lookup(cache_config, cache_key, job->object_path);
            if (lookup_rc == 0) {
                // Cache hit — object was copied to dest
                cache_stats->hits++;
                cdo_trace("Cache hit: %s", job->source_path);
            } else {
                // Cache miss — needs compilation; store key for post-compile store
                int slot = miss_count;
                miss_indices[miss_count++] = i;
                memcpy(cache_keys[slot], cache_key, CACHE_KEY_HEX_LEN + 1);
                key_valid[slot] = true;
                cache_stats->misses++;
                cdo_trace("Cache miss: %s", job->source_path);
            }
        }

        // If all jobs were cache hits, we're done
        if (miss_count == 0) {
            free(miss_indices);
            free(cache_keys);
            free(key_valid);
            cdo_info("All %d file(s) served from cache", job_count);
            return 0;
        }

        cdo_debug("Cache: %d hit(s), %d miss(es) — compiling misses", cache_stats->hits, miss_count);
    }

    // Determine which jobs to compile
    int compile_count = use_cache ? miss_count : job_count;

    // Allocate task contexts
    CompileTaskCtx* contexts = (CompileTaskCtx*)calloc((size_t)compile_count, sizeof(CompileTaskCtx));
    if (!contexts) {
        cdo_error("Failed to allocate compile task contexts");
        free(miss_indices);
        free(cache_keys);
        free(key_valid);
        return -1;
    }

    // Initialize contexts and ensure output directories exist
    for (int i = 0; i < compile_count; i++) {
        int job_idx = use_cache ? miss_indices[i] : i;
        contexts[i].job = &jobs[job_idx];
        contexts[i].info = info;
        contexts[i].cache_backend = external_backend;
        contexts[i].result = 0;

        // Ensure the output directory for the object file exists
        if (jobs[job_idx].object_path) {
            char dir_buf[1024];
            size_t path_len = strlen(jobs[job_idx].object_path);
            if (path_len < sizeof(dir_buf)) {
                memcpy(dir_buf, jobs[job_idx].object_path, path_len + 1);
                // Find last separator to get the directory portion
                char* last_sep = NULL;
                for (char* p = dir_buf; *p; p++) {
                    if (*p == '/' || *p == '\\') last_sep = p;
                }
                if (last_sep) {
                    *last_sep = '\0';
                    pal_mkdir_p(dir_buf);
                }
            }
        }
    }

    // Create thread pool
    int threads = parallelism;
    if (threads <= 0) {
        threads = pal_cpu_count();
        if (threads <= 0) threads = 4; // fallback
    }

    cdo_debug("Compiling %d file(s) with %d thread(s)", compile_count, threads);

    ThreadPool* pool = threadpool_create(threads);
    if (!pool) {
        cdo_error("Failed to create thread pool for compilation");
        free(contexts);
        free(miss_indices);
        free(cache_keys);
        free(key_valid);
        return -1;
    }

    // Submit all compile jobs to the thread pool
    for (int i = 0; i < compile_count; i++) {
        int rc = threadpool_submit(pool, compile_task, &contexts[i]);
        if (rc != 0) {
            int job_idx = use_cache ? miss_indices[i] : i;
            cdo_error("Failed to submit compile job for: %s", jobs[job_idx].source_path);
            contexts[i].result = -1;
        }
    }

    // Wait for all jobs to complete
    threadpool_wait(pool);
    threadpool_destroy(pool);

    // Check results and store successful compilations in cache
    int failures = 0;
    for (int i = 0; i < compile_count; i++) {
        if (contexts[i].result != 0) {
            failures++;
        } else if (use_cache && key_valid[i]) {
            // Compilation succeeded and we have a valid cache key — store the .o in cache
            int job_idx = miss_indices[i];
            int store_rc = cache_store(cache_config, cache_keys[i], jobs[job_idx].object_path);
            if (store_rc == 0) {
                cache_stats->stored++;
                cdo_trace("Cache stored: %s", jobs[job_idx].source_path);
            } else {
                cdo_warn("Cache store failed for: %s (non-fatal)", jobs[job_idx].source_path);
            }
        }
    }

    free(contexts);
    free(miss_indices);
    free(cache_keys);
    free(key_valid);

    if (failures > 0) {
        cdo_error("Compilation failed: %d of %d file(s) had errors", failures, compile_count);
        return failures;
    }

    cdo_info("Compiled %d file(s) successfully", compile_count);
    return 0;
}

// ---------------------------------------------------------------------------
// Test wrappers — expose internal arg builders for property-based testing
// ---------------------------------------------------------------------------

int compiler_test_build_gcc_args(const CompileJob* job, const CompilerInfo* info,
                                 const char** args, int max_args) {
    return build_gcc_clang_args(job, info, args, max_args);
}

int compiler_test_build_msvc_args(const CompileJob* job, const char** args, int max_args) {
    return build_msvc_args(job, args, max_args);
}
