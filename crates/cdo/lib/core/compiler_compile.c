#include "compiler_internal.h"
#include "core/compiler.h"
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
    int                 result;     // 0 = success, non-zero = failure
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

    // Dependency tracking: -MMD (generates .d file alongside .o)
    if (n >= max_args) return -1;
    args[n++] = "-MMD";

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

    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.program = compiler_program;
    opts.args = args;
    opts.arg_count = arg_count;
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
    }

    pal_spawn_result_free(&result);
}

int compiler_compile_batch(const CompileJob* jobs, int job_count,
                           const CompilerInfo* info, int parallelism) {
    if (!jobs || job_count <= 0 || !info) return -1;
    if (info->family == COMPILER_UNKNOWN) {
        cdo_error("Cannot compile: no compiler detected");
        return -1;
    }

    // Allocate task contexts
    CompileTaskCtx* contexts = (CompileTaskCtx*)calloc((size_t)job_count,
                                                       sizeof(CompileTaskCtx));
    if (!contexts) {
        cdo_error("Failed to allocate compile task contexts");
        return -1;
    }

    // Initialize contexts and ensure output directories exist
    for (int i = 0; i < job_count; i++) {
        contexts[i].job = &jobs[i];
        contexts[i].info = info;
        contexts[i].result = 0;

        // Ensure the output directory for the object file exists
        if (jobs[i].object_path) {
            char dir_buf[1024];
            size_t path_len = strlen(jobs[i].object_path);
            if (path_len < sizeof(dir_buf)) {
                memcpy(dir_buf, jobs[i].object_path, path_len + 1);
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

    cdo_debug("Compiling %d file(s) with %d thread(s)", job_count, threads);

    ThreadPool* pool = threadpool_create(threads);
    if (!pool) {
        cdo_error("Failed to create thread pool for compilation");
        free(contexts);
        return -1;
    }

    // Submit all compile jobs to the thread pool
    for (int i = 0; i < job_count; i++) {
        int rc = threadpool_submit(pool, compile_task, &contexts[i]);
        if (rc != 0) {
            cdo_error("Failed to submit compile job for: %s",
                      jobs[i].source_path);
            contexts[i].result = -1;
        }
    }

    // Wait for all jobs to complete
    threadpool_wait(pool);
    threadpool_destroy(pool);

    // Check results
    int failures = 0;
    for (int i = 0; i < job_count; i++) {
        if (contexts[i].result != 0) {
            failures++;
        }
    }

    free(contexts);

    if (failures > 0) {
        cdo_error("Compilation failed: %d of %d file(s) had errors",
                  failures, job_count);
        return failures;
    }

    cdo_info("Compiled %d file(s) successfully", job_count);
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
