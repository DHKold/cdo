#include "core/compiler.h"
#include "core/threadpool.h"
#include "core/scanner.h"
#include "pal/pal.h"
#include "core/output.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Extract a version string (e.g., "13.2.0" or "17.0.1") from output text.
/// Scans for the first occurrence of a digit followed by dot-separated numbers.
/// Writes into ver_buf (up to ver_buf_size - 1 chars).
static void extract_version(const char* output, char* ver_buf, size_t ver_buf_size) {
    ver_buf[0] = '\0';
    if (!output) return;

    const char* p = output;
    while (*p) {
        // Look for a digit that starts a version-like pattern: X.Y...
        if (isdigit((unsigned char)*p)) {
            const char* start = p;
            // Walk forward while we see digits or dots
            while (*p && (isdigit((unsigned char)*p) || *p == '.')) {
                p++;
            }
            // Must contain at least one dot to be a version (not just a bare number)
            size_t len = (size_t)(p - start);
            if (len > 1 && memchr(start, '.', len) != NULL) {
                if (len >= ver_buf_size) len = ver_buf_size - 1;
                memcpy(ver_buf, start, len);
                ver_buf[len] = '\0';
                // Trim trailing dots
                while (len > 0 && ver_buf[len - 1] == '.') {
                    ver_buf[--len] = '\0';
                }
                return;
            }
            continue;
        }
        p++;
    }
}

/// Try to run a compiler with --version and capture its output.
/// Returns 0 if the compiler was found and ran successfully (exit code 0).
static int try_compiler(const char* program, const char* flag,
                        char* path_out, size_t path_size,
                        char* version_out, size_t version_size) {
    const char* args[] = { flag };
    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.program = program;
    opts.args = args;
    opts.arg_count = 1;
    opts.capture_output = true;

    PalSpawnResult result;
    memset(&result, 0, sizeof(result));

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        pal_spawn_result_free(&result);
        return -1;
    }

    // Use stdout first, fall back to stderr (clang often writes to stderr)
    const char* output = result.stdout_buf;
    if (!output || output[0] == '\0') {
        output = result.stderr_buf;
    }

    // Extract version from output
    extract_version(output, version_out, version_size);

    // Store the program name as the path (it resolves via PATH)
    if (path_out && path_size > 0) {
        size_t plen = strlen(program);
        if (plen >= path_size) plen = path_size - 1;
        memcpy(path_out, program, plen);
        path_out[plen] = '\0';
    }

    pal_spawn_result_free(&result);
    return 0;
}

#ifdef _WIN32
/// Check if cl.exe is available by trying to run it with no arguments.
/// MSVC's cl.exe prints a banner to stderr and returns 0 when given no source files,
/// or returns non-zero. We just check if it can be spawned.
static int try_msvc(char* path_out, size_t path_size,
                    char* version_out, size_t version_size) {
    // cl.exe with no args prints version info to stderr and exits 0 or non-zero
    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.program = "cl.exe";
    opts.args = NULL;
    opts.arg_count = 0;
    opts.capture_output = true;

    PalSpawnResult result;
    memset(&result, 0, sizeof(result));

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        pal_spawn_result_free(&result);
        return -1;
    }

    // cl.exe may return 0 or non-zero, but if it spawned successfully we found it.
    // It prints its banner (including version) to stderr.
    const char* output = result.stderr_buf;
    if (!output || output[0] == '\0') {
        output = result.stdout_buf;
    }

    if (!output || output[0] == '\0') {
        // Could not get any output - might not be MSVC
        pal_spawn_result_free(&result);
        return -1;
    }

    // Extract version from the banner
    extract_version(output, version_out, version_size);

    if (path_out && path_size > 0) {
        const char* name = "cl.exe";
        size_t plen = strlen(name);
        if (plen >= path_size) plen = path_size - 1;
        memcpy(path_out, name, plen);
        path_out[plen] = '\0';
    }

    pal_spawn_result_free(&result);
    return 0;
}
#endif

// ---------------------------------------------------------------------------
// compiler_detect
// ---------------------------------------------------------------------------

/// Try to detect a compiler from the vendored tools directory (.cdo/tools/).
/// Searches for known toolchain layouts (e.g., w64devkit/bin/gcc).
/// Returns 0 if found and fills info, -1 otherwise.
static int try_vendored_tools(CompilerInfo* info) {
    // Known vendored toolchain patterns to probe (relative to cwd)
    static const char* gcc_probes[] = {
        ".cdo/tools/w64devkit/bin/gcc.exe",
        ".cdo/tools/w64devkit/bin/gcc",
        ".cdo/tools/mingw64/bin/gcc.exe",
        ".cdo/tools/mingw64/bin/gcc",
        NULL
    };

    for (int i = 0; gcc_probes[i] != NULL; i++) {
        if (pal_path_exists(gcc_probes[i]) == 1) {
            // Found a vendored GCC — try to run it
            if (try_compiler(gcc_probes[i], "--version", info->path, sizeof(info->path),
                             info->version, sizeof(info->version)) == 0) {
                info->family = COMPILER_GCC;
                strncpy(info->linker_path, info->path, sizeof(info->linker_path) - 1);
                info->linker_path[sizeof(info->linker_path) - 1] = '\0';
                cdo_debug("Detected vendored compiler: GCC %s at %s", info->version, info->path);
                return 0;
            }
        }
    }

    static const char* clang_probes[] = {
        ".cdo/tools/llvm/bin/clang.exe",
        ".cdo/tools/llvm/bin/clang",
        NULL
    };

    for (int i = 0; clang_probes[i] != NULL; i++) {
        if (pal_path_exists(clang_probes[i]) == 1) {
            if (try_compiler(clang_probes[i], "--version", info->path, sizeof(info->path),
                             info->version, sizeof(info->version)) == 0) {
                info->family = COMPILER_CLANG;
                strncpy(info->linker_path, info->path, sizeof(info->linker_path) - 1);
                info->linker_path[sizeof(info->linker_path) - 1] = '\0';
                cdo_debug("Detected vendored compiler: Clang %s at %s", info->version, info->path);
                return 0;
            }
        }
    }

    return -1;
}

int compiler_detect(CompilerInfo* info) {
    if (!info) return -1;

    memset(info, 0, sizeof(CompilerInfo));
    info->family = COMPILER_UNKNOWN;

#ifdef _WIN32
    // Windows: try MSVC first, then GCC (MinGW), then Clang on PATH
    if (try_msvc(info->path, sizeof(info->path),
                 info->version, sizeof(info->version)) == 0) {
        info->family = COMPILER_MSVC;
        // MSVC linker is link.exe
        strncpy(info->linker_path, "link.exe", sizeof(info->linker_path) - 1);
        info->linker_path[sizeof(info->linker_path) - 1] = '\0';
        cdo_debug("Detected compiler: MSVC %s", info->version);
        return 0;
    }

    if (try_compiler("gcc", "--version", info->path, sizeof(info->path),
                     info->version, sizeof(info->version)) == 0) {
        info->family = COMPILER_GCC;
        // GCC acts as linker driver
        strncpy(info->linker_path, info->path, sizeof(info->linker_path) - 1);
        info->linker_path[sizeof(info->linker_path) - 1] = '\0';
        cdo_debug("Detected compiler: GCC %s", info->version);
        return 0;
    }

    if (try_compiler("clang", "--version", info->path, sizeof(info->path),
                     info->version, sizeof(info->version)) == 0) {
        info->family = COMPILER_CLANG;
        // Clang acts as linker driver
        strncpy(info->linker_path, info->path, sizeof(info->linker_path) - 1);
        info->linker_path[sizeof(info->linker_path) - 1] = '\0';
        cdo_debug("Detected compiler: Clang %s", info->version);
        return 0;
    }

#else
    // POSIX: try GCC first, then Clang
    if (try_compiler("gcc", "--version", info->path, sizeof(info->path),
                     info->version, sizeof(info->version)) == 0) {
        info->family = COMPILER_GCC;
        // GCC acts as linker driver
        strncpy(info->linker_path, info->path, sizeof(info->linker_path) - 1);
        info->linker_path[sizeof(info->linker_path) - 1] = '\0';
        cdo_debug("Detected compiler: GCC %s", info->version);
        return 0;
    }

    if (try_compiler("clang", "--version", info->path, sizeof(info->path),
                     info->version, sizeof(info->version)) == 0) {
        info->family = COMPILER_CLANG;
        // Clang acts as linker driver
        strncpy(info->linker_path, info->path, sizeof(info->linker_path) - 1);
        info->linker_path[sizeof(info->linker_path) - 1] = '\0';
        cdo_debug("Detected compiler: Clang %s", info->version);
        return 0;
    }
#endif

    // Fallback: try vendored tools in .cdo/tools/
    if (try_vendored_tools(info) == 0) {
        return 0;
    }

    cdo_warn("No compiler detected on system PATH or vendored tools");
    return -1;
}

// ---------------------------------------------------------------------------
// compiler_compute_dirty_set
// ---------------------------------------------------------------------------

int compiler_compute_dirty_set(const BuildUnit* units, int unit_count,
                               int* dirty_out) {
    if (!units || unit_count < 0 || !dirty_out) return -1;
    if (unit_count == 0) return 0;

    int dirty_count = 0;

    for (int i = 0; i < unit_count; i++) {
        const BuildUnit* u = &units[i];
        bool dirty = false;

        // Rule (c): object file does not exist
        if (!u->object_exists) {
            dirty = true;
        }
        // Rule (a): source mtime is newer than object mtime
        else if (u->source_mtime > u->object_mtime) {
            dirty = true;
        }
        // Rule (b): any header dependency mtime is newer than object mtime
        else if (u->header_mtimes && u->header_dep_count > 0) {
            for (int h = 0; h < u->header_dep_count; h++) {
                if (u->header_mtimes[h] > u->object_mtime) {
                    dirty = true;
                    break;
                }
            }
        }

        if (dirty) {
            dirty_out[dirty_count++] = i;
        }
    }

    return dirty_count;
}

// ---------------------------------------------------------------------------
// compiler_compute_dirty — filesystem-aware incremental compilation
// ---------------------------------------------------------------------------

/// Parse a GCC/Clang-style .d dependency file to extract header dependencies.
/// The format is: target: dep1 dep2 dep3 ...
/// Lines may use backslash-newline continuation.
/// Returns 0 on success, non-zero on failure. Caller frees *deps and each entry.
static int parse_depfile(const char* content, size_t len,
                         char*** deps, int* dep_count) {
    *deps = NULL;
    *dep_count = 0;

    if (!content || len == 0) return 0;

    // Skip past the first ':' (the target portion)
    const char* p = content;
    const char* end = content + len;
    while (p < end && *p != ':') p++;
    if (p >= end) return 0; // no colon found, skip
    p++; // skip ':'

    // Now parse space-separated dependency paths.
    // Handle backslash-newline continuation and backslash-space escapes.
    int capacity = 16;
    char** result = (char**)malloc((size_t)capacity * sizeof(char*));
    if (!result) return -1;

    int count = 0;
    char path_buf[1024];
    int path_len = 0;

    while (p < end) {
        char c = *p;

        // Backslash handling
        if (c == '\\') {
            if (p + 1 < end && p[1] == '\n') {
                // continuation line, skip both characters
                p += 2;
                continue;
            }
            if (p + 1 < end && p[1] == '\r' && p + 2 < end && p[2] == '\n') {
                // Windows continuation: \<CR><LF>
                p += 3;
                continue;
            }
            if (p + 1 < end && p[1] == ' ') {
                // escaped space — part of the filename
                if (path_len < (int)sizeof(path_buf) - 1) {
                    path_buf[path_len++] = ' ';
                }
                p += 2;
                continue;
            }
            // Regular backslash (e.g., Windows path separator)
            if (path_len < (int)sizeof(path_buf) - 1) {
                path_buf[path_len++] = c;
            }
            p++;
            continue;
        }

        // Whitespace separates dependencies
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (path_len > 0) {
                // Terminate and store the path
                path_buf[path_len] = '\0';

                // Skip the first entry (the source file itself that also
                // appears as a dependency in some .d files)
                // We include all entries — the caller will handle dedup if needed.
                if (count >= capacity) {
                    capacity *= 2;
                    char** tmp = (char**)realloc(result, (size_t)capacity * sizeof(char*));
                    if (!tmp) { goto fail; }
                    result = tmp;
                }
                result[count] = (char*)malloc((size_t)(path_len + 1));
                if (!result[count]) { goto fail; }
                memcpy(result[count], path_buf, (size_t)(path_len + 1));
                count++;
                path_len = 0;
            }
            p++;
            continue;
        }

        // Regular character
        if (path_len < (int)sizeof(path_buf) - 1) {
            path_buf[path_len++] = c;
        }
        p++;
    }

    // Handle last token if any
    if (path_len > 0) {
        path_buf[path_len] = '\0';
        if (count >= capacity) {
            capacity *= 2;
            char** tmp = (char**)realloc(result, (size_t)capacity * sizeof(char*));
            if (!tmp) { goto fail; }
            result = tmp;
        }
        result[count] = (char*)malloc((size_t)(path_len + 1));
        if (!result[count]) { goto fail; }
        memcpy(result[count], path_buf, (size_t)(path_len + 1));
        count++;
    }

    *deps = result;
    *dep_count = count;
    return 0;

fail:
    for (int i = 0; i < count; i++) free(result[i]);
    free(result);
    *deps = NULL;
    *dep_count = 0;
    return -1;
}

/// Check if a file extension indicates a compilable source (not a header).
static bool is_compilable_source(const char* path) {
    const char* ext = pal_path_ext(path);
    if (!ext) return false;
    return (strcmp(ext, ".c") == 0 ||
            strcmp(ext, ".cpp") == 0 ||
            strcmp(ext, ".cxx") == 0 ||
            strcmp(ext, ".cc") == 0);
}

/// Derive a relative path from a source file relative to the crate's src/ dir.
/// E.g., if crate_path="/proj/mycrate" and source="/proj/mycrate/src/foo/bar.c",
/// then rel_out = "foo/bar" (no extension).
/// Returns 0 on success, -1 on failure.
static int derive_stem(const char* source_path, const char* crate_src_prefix,
                       size_t prefix_len, char* stem_out, size_t stem_size) {
    // Source path should start with crate_src_prefix
    if (strncmp(source_path, crate_src_prefix, prefix_len) != 0) {
        return -1;
    }

    const char* rel = source_path + prefix_len;
    // Skip leading separator
    if (*rel == '/' || *rel == '\\') rel++;

    // Copy without extension
    const char* ext = pal_path_ext(rel);
    size_t ext_len = ext ? strlen(ext) : 0;
    size_t rel_len = strlen(rel);
    size_t stem_len = rel_len - ext_len;

    if (stem_len >= stem_size) return -1;
    memcpy(stem_out, rel, stem_len);
    stem_out[stem_len] = '\0';
    return 0;
}

int compiler_compute_dirty(const Crate* crate, const char* build_dir,
                           int** dirty_indices, int* dirty_count) {
    if (!crate || !build_dir || !dirty_indices || !dirty_count) return -1;

    *dirty_indices = NULL;
    *dirty_count = 0;

    // Resolve crate path: if relative, join with workspace root (we receive
    // build_dir already as an absolute path like "build/<crate_name>").
    // The crate->path is relative to the workspace root. We need the absolute
    // crate path for scanning. The build_dir is the output directory for this crate.

    // Scan source files in the crate
    FileList sources;
    memset(&sources, 0, sizeof(sources));
    int rc = scanner_scan_sources(crate->path, NULL, 0, &sources);
    if (rc != 0) {
        cdo_warn("Failed to scan sources for crate '%s', falling back to full rebuild",
                 crate->name);
        // Fall back: return empty dirty set (caller should do full rebuild)
        // Actually for fallback, we signal that ALL files are dirty.
        // But we can't know how many there are. Return error to signal full rebuild.
        return -1;
    }

    if (sources.count == 0) {
        filelist_free(&sources);
        return 0; // nothing to build
    }

    // Filter to only compilable sources (exclude .h, .hpp)
    int source_count = 0;
    for (int i = 0; i < sources.count; i++) {
        if (is_compilable_source(sources.paths[i])) {
            source_count++;
        }
    }

    if (source_count == 0) {
        filelist_free(&sources);
        return 0;
    }

    // Compute the crate src/ prefix for deriving relative stems
    char crate_src[1024];
    if (pal_path_join(crate_src, sizeof(crate_src), crate->path, "src") != 0) {
        filelist_free(&sources);
        return -1;
    }
    pal_path_normalize(crate_src);
    size_t crate_src_len = strlen(crate_src);

    // Allocate BuildUnit array
    BuildUnit* units = (BuildUnit*)calloc((size_t)source_count, sizeof(BuildUnit));
    if (!units) {
        filelist_free(&sources);
        return -1;
    }

    int unit_idx = 0;
    bool fallback_full_rebuild = false;

    for (int i = 0; i < sources.count && !fallback_full_rebuild; i++) {
        if (!is_compilable_source(sources.paths[i])) continue;

        BuildUnit* u = &units[unit_idx];
        const char* src = sources.paths[i];

        // Store source path
        size_t src_len = strlen(src);
        if (src_len >= sizeof(u->source_path)) src_len = sizeof(u->source_path) - 1;
        memcpy(u->source_path, src, src_len);
        u->source_path[src_len] = '\0';

        // Get source mtime
        if (pal_file_mtime(src, &u->source_mtime) != PAL_OK) {
            cdo_warn("Cannot stat source file '%s', triggering full rebuild", src);
            fallback_full_rebuild = true;
            break;
        }

        // Derive stem for object/dep paths
        char stem[512];
        if (derive_stem(src, crate_src, crate_src_len, stem, sizeof(stem)) != 0) {
            // If we can't derive, just use the filename without extension
            const char* filename = strrchr(src, '/');
            if (!filename) filename = strrchr(src, '\\');
            if (!filename) filename = src;
            else filename++;
            const char* ext = pal_path_ext(filename);
            size_t fname_len = ext ? (size_t)(ext - filename) : strlen(filename);
            if (fname_len >= sizeof(stem)) fname_len = sizeof(stem) - 1;
            memcpy(stem, filename, fname_len);
            stem[fname_len] = '\0';
        }

        // Build object path: build_dir/<stem>.o (or .obj on MSVC, but .o is standard for MinGW/GCC)
        char obj_path[1024];
        char obj_rel[560];
        snprintf(obj_rel, sizeof(obj_rel), "%s.o", stem);
        if (pal_path_join(obj_path, sizeof(obj_path), build_dir, obj_rel) != 0) {
            fallback_full_rebuild = true;
            break;
        }
        pal_path_normalize(obj_path);

        // Check if object exists and get its mtime
        if (pal_path_exists(obj_path)) {
            u->object_exists = true;
            if (pal_file_mtime(obj_path, &u->object_mtime) != PAL_OK) {
                cdo_warn("Cannot stat object file '%s', triggering full rebuild", obj_path);
                fallback_full_rebuild = true;
                break;
            }
        } else {
            u->object_exists = false;
            u->object_mtime = 0;
        }

        // Parse dependency file for header deps
        char dep_path[1024];
        char dep_rel[560];
        snprintf(dep_rel, sizeof(dep_rel), "%s.d", stem);
        if (pal_path_join(dep_path, sizeof(dep_path), build_dir, dep_rel) != 0) {
            fallback_full_rebuild = true;
            break;
        }
        pal_path_normalize(dep_path);

        u->header_mtimes = NULL;
        u->header_dep_count = 0;

        if (pal_path_exists(dep_path)) {
            char* dep_content = NULL;
            size_t dep_len = 0;
            if (pal_file_read(dep_path, &dep_content, &dep_len) == PAL_OK && dep_content) {
                char** header_paths = NULL;
                int header_count = 0;
                if (parse_depfile(dep_content, dep_len, &header_paths, &header_count) == 0 &&
                    header_count > 0) {
                    // Gather mtimes for all header dependencies
                    u->header_mtimes = (uint64_t*)calloc((size_t)header_count, sizeof(uint64_t));
                    if (u->header_mtimes) {
                        int valid_headers = 0;
                        for (int h = 0; h < header_count; h++) {
                            // Normalize header path for consistent lookup
                            pal_path_normalize(header_paths[h]);
                            uint64_t hdr_mtime = 0;
                            if (pal_file_mtime(header_paths[h], &hdr_mtime) == PAL_OK) {
                                u->header_mtimes[valid_headers++] = hdr_mtime;
                            }
                            // If a header no longer exists, the file was deleted —
                            // this makes the dep file stale, so treat as needing rebuild.
                            // We just won't add it, meaning the source mtime vs object
                            // mtime comparison will still apply.
                            // Actually, a deleted header means the dep info is invalid.
                            // Mark as needing rebuild by setting mtime to max.
                            else {
                                u->header_mtimes[valid_headers++] = UINT64_MAX;
                            }
                        }
                        u->header_dep_count = valid_headers;
                    }
                    // Free header path strings
                    for (int h = 0; h < header_count; h++) free(header_paths[h]);
                    free(header_paths);
                }
                free(dep_content);
            }
            // If reading/parsing the dep file fails, we just proceed without header
            // deps — the source mtime check will still catch changes to the source itself.
        }

        unit_idx++;
    }

    // If we hit a fallback condition, mark everything dirty
    if (fallback_full_rebuild) {
        // Free any allocated header_mtimes
        for (int i = 0; i < unit_idx; i++) {
            free(units[i].header_mtimes);
        }
        free(units);

        // Return all indices as dirty
        *dirty_indices = (int*)malloc((size_t)source_count * sizeof(int));
        if (!*dirty_indices) {
            filelist_free(&sources);
            return -1;
        }
        for (int i = 0; i < source_count; i++) {
            (*dirty_indices)[i] = i;
        }
        *dirty_count = source_count;
        filelist_free(&sources);
        cdo_debug("Full rebuild triggered for crate '%s' (%d files)", crate->name, source_count);
        return 0;
    }

    // Compute dirty set
    int* dirty_out = (int*)malloc((size_t)source_count * sizeof(int));
    if (!dirty_out) {
        for (int i = 0; i < source_count; i++) free(units[i].header_mtimes);
        free(units);
        filelist_free(&sources);
        return -1;
    }

    int n_dirty = compiler_compute_dirty_set(units, source_count, dirty_out);
    if (n_dirty < 0) {
        // Error in dirty set computation — fallback to full rebuild
        free(dirty_out);
        for (int i = 0; i < source_count; i++) free(units[i].header_mtimes);
        free(units);
        filelist_free(&sources);
        return -1;
    }

    // Clean up BuildUnit header_mtimes
    for (int i = 0; i < source_count; i++) {
        free(units[i].header_mtimes);
    }
    free(units);
    filelist_free(&sources);

    if (n_dirty == 0) {
        free(dirty_out);
        *dirty_indices = NULL;
        *dirty_count = 0;
    } else {
        // Allocate exact-size array for caller
        *dirty_indices = (int*)malloc((size_t)n_dirty * sizeof(int));
        if (!*dirty_indices) {
            free(dirty_out);
            return -1;
        }
        memcpy(*dirty_indices, dirty_out, (size_t)n_dirty * sizeof(int));
        *dirty_count = n_dirty;
        free(dirty_out);
    }

    cdo_debug("Dirty set for crate '%s': %d of %d files need rebuild",
              crate->name, *dirty_count, source_count);
    return 0;
}

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
// compiler_link — link object files into final artifact
// ---------------------------------------------------------------------------

// Maximum number of arguments for the linker command
#define MAX_LINK_ARGS 1024

/// Determine the link mode from the output path and shared flag.
typedef enum {
    LINK_MODE_EXECUTABLE,
    LINK_MODE_STATIC_LIB,
    LINK_MODE_SHARED_LIB,
} LinkMode;

static LinkMode determine_link_mode(const LinkJob* job) {
    const char* ext = pal_path_ext(job->output_path);
    if (ext && (strcmp(ext, ".a") == 0 || strcmp(ext, ".lib") == 0)) {
        return LINK_MODE_STATIC_LIB;
    }
    if (job->shared) {
        return LINK_MODE_SHARED_LIB;
    }
    return LINK_MODE_EXECUTABLE;
}

/// Ensure the parent directory of the output path exists.
static int ensure_output_dir(const char* output_path) {
    char dir_buf[1024];
    size_t path_len = strlen(output_path);
    if (path_len >= sizeof(dir_buf)) return -1;
    memcpy(dir_buf, output_path, path_len + 1);

    // Find last separator to get the directory portion
    char* last_sep = NULL;
    for (char* p = dir_buf; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (last_sep) {
        *last_sep = '\0';
        return pal_mkdir_p(dir_buf);
    }
    return 0;
}

/// Link using ar (static library archiver) for GCC/Clang.
static int link_static_gcc(const LinkJob* job) {
    const char* args[MAX_LINK_ARGS];
    int n = 0;

    // ar rcs <output> <objects...>
    if (n >= MAX_LINK_ARGS) return -1;
    args[n++] = "rcs";

    if (n >= MAX_LINK_ARGS) return -1;
    args[n++] = job->output_path;

    for (int i = 0; i < job->object_count; i++) {
        if (n >= MAX_LINK_ARGS) return -1;
        args[n++] = job->object_paths[i];
    }

    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.program = "ar";
    opts.args = args;
    opts.arg_count = n;
    opts.capture_output = true;

    PalSpawnResult result;
    memset(&result, 0, sizeof(result));

    cdo_debug("Archiving: ar rcs %s (%d objects)", job->output_path, job->object_count);

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        cdo_error("Failed to spawn archiver (ar)");
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        if (result.stderr_buf && result.stderr_buf[0] != '\0') {
            cdo_error("%s", result.stderr_buf);
        }
        cdo_error("Archiving failed (exit code %d)", result.exit_code);
        pal_spawn_result_free(&result);
        return result.exit_code;
    }

    pal_spawn_result_free(&result);
    return 0;
}

/// Link using lib.exe (static library archiver) for MSVC.
static int link_static_msvc(const LinkJob* job, const CompilerInfo* info) {
    const char* args[MAX_LINK_ARGS];
    int n = 0;

    // lib.exe /OUT:<output> <objects...>
    if (n >= MAX_LINK_ARGS) return -1;
    args[n++] = "/nologo";

    if (n >= MAX_LINK_ARGS) return -1;
    static char out_buf[1024];
    snprintf(out_buf, sizeof(out_buf), "/OUT:%s", job->output_path);
    args[n++] = out_buf;

    for (int i = 0; i < job->object_count; i++) {
        if (n >= MAX_LINK_ARGS) return -1;
        args[n++] = job->object_paths[i];
    }

    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.program = "lib.exe";
    opts.args = args;
    opts.arg_count = n;
    opts.capture_output = true;

    PalSpawnResult result;
    memset(&result, 0, sizeof(result));

    cdo_debug("Archiving: lib.exe /OUT:%s (%d objects)", job->output_path, job->object_count);

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        cdo_error("Failed to spawn archiver (lib.exe)");
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        if (result.stderr_buf && result.stderr_buf[0] != '\0') {
            cdo_error("%s", result.stderr_buf);
        }
        cdo_error("Archiving failed (exit code %d)", result.exit_code);
        pal_spawn_result_free(&result);
        return result.exit_code;
    }

    pal_spawn_result_free(&result);
    (void)info;
    return 0;
}

/// Link using GCC/Clang as linker driver (executable or shared library).
static int link_gcc_clang(const LinkJob* job, const CompilerInfo* info, LinkMode mode) {
    const char* args[MAX_LINK_ARGS];
    int n = 0;

    // Output: -o <path>
    if (n + 1 >= MAX_LINK_ARGS) return -1;
    args[n++] = "-o";
    args[n++] = job->output_path;

    // Object files
    for (int i = 0; i < job->object_count; i++) {
        if (n >= MAX_LINK_ARGS) return -1;
        args[n++] = job->object_paths[i];
    }

    // Library search paths: -L<path>
    for (int i = 0; i < job->lib_path_count; i++) {
        if (n >= MAX_LINK_ARGS) return -1;
        args[n++] = "-L";
        if (n >= MAX_LINK_ARGS) return -1;
        args[n++] = job->lib_paths[i];
    }

    // Link libraries: -l<lib>
    for (int i = 0; i < job->link_lib_count; i++) {
        if (n >= MAX_LINK_ARGS) return -1;
        args[n++] = "-l";
        if (n >= MAX_LINK_ARGS) return -1;
        args[n++] = job->link_libs[i];
    }

    // Shared library flag
    if (mode == LINK_MODE_SHARED_LIB) {
        if (n >= MAX_LINK_ARGS) return -1;
        args[n++] = "-shared";
    }

    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.program = info->path;
    opts.args = args;
    opts.arg_count = n;
    opts.capture_output = true;

    PalSpawnResult result;
    memset(&result, 0, sizeof(result));

    const char* mode_str = (mode == LINK_MODE_SHARED_LIB) ? "shared library" : "executable";
    cdo_debug("Linking %s: %s (%d objects)", mode_str, job->output_path, job->object_count);

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        cdo_error("Failed to spawn linker (%s)", info->path);
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        if (result.stderr_buf && result.stderr_buf[0] != '\0') {
            cdo_error("%s", result.stderr_buf);
        }
        if (result.stdout_buf && result.stdout_buf[0] != '\0') {
            cdo_error("%s", result.stdout_buf);
        }
        cdo_error("Linking failed: %s (exit code %d)", job->output_path, result.exit_code);
        pal_spawn_result_free(&result);
        return result.exit_code;
    }

    pal_spawn_result_free(&result);
    return 0;
}

/// Link using MSVC link.exe (executable or shared library).
static int link_msvc(const LinkJob* job, const CompilerInfo* info, LinkMode mode) {
    const char* args[MAX_LINK_ARGS];
    int n = 0;

    // /nologo
    if (n >= MAX_LINK_ARGS) return -1;
    args[n++] = "/nologo";

    // /OUT:<output>
    if (n >= MAX_LINK_ARGS) return -1;
    static char msvc_out_buf[1024];
    snprintf(msvc_out_buf, sizeof(msvc_out_buf), "/OUT:%s", job->output_path);
    args[n++] = msvc_out_buf;

    // /DLL for shared libraries
    if (mode == LINK_MODE_SHARED_LIB) {
        if (n >= MAX_LINK_ARGS) return -1;
        args[n++] = "/DLL";
    }

    // Object files
    for (int i = 0; i < job->object_count; i++) {
        if (n >= MAX_LINK_ARGS) return -1;
        args[n++] = job->object_paths[i];
    }

    // Library search paths: /LIBPATH:<path>
    static char libpath_bufs[64][512];
    for (int i = 0; i < job->lib_path_count && i < 64; i++) {
        if (n >= MAX_LINK_ARGS) return -1;
        snprintf(libpath_bufs[i], sizeof(libpath_bufs[i]), "/LIBPATH:%s", job->lib_paths[i]);
        args[n++] = libpath_bufs[i];
    }

    // Link libraries (MSVC expects lib names with .lib extension)
    static char lib_bufs[64][256];
    for (int i = 0; i < job->link_lib_count && i < 64; i++) {
        if (n >= MAX_LINK_ARGS) return -1;
        // If the library name already ends with .lib, use as-is; otherwise append .lib
        const char* lib = job->link_libs[i];
        const char* lib_ext = pal_path_ext(lib);
        if (lib_ext && strcmp(lib_ext, ".lib") == 0) {
            args[n++] = lib;
        } else {
            snprintf(lib_bufs[i], sizeof(lib_bufs[i]), "%s.lib", lib);
            args[n++] = lib_bufs[i];
        }
    }

    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.program = info->linker_path;
    opts.args = args;
    opts.arg_count = n;
    opts.capture_output = true;

    PalSpawnResult result;
    memset(&result, 0, sizeof(result));

    const char* mode_str = (mode == LINK_MODE_SHARED_LIB) ? "shared library" : "executable";
    cdo_debug("Linking %s: %s (%d objects)", mode_str, job->output_path, job->object_count);

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        cdo_error("Failed to spawn linker (%s)", info->linker_path);
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        if (result.stderr_buf && result.stderr_buf[0] != '\0') {
            cdo_error("%s", result.stderr_buf);
        }
        if (result.stdout_buf && result.stdout_buf[0] != '\0') {
            cdo_error("%s", result.stdout_buf);
        }
        cdo_error("Linking failed: %s (exit code %d)", job->output_path, result.exit_code);
        pal_spawn_result_free(&result);
        return result.exit_code;
    }

    pal_spawn_result_free(&result);
    (void)info;
    return 0;
}

int compiler_link(const LinkJob* job, const CompilerInfo* info) {
    if (!job || !info) return -1;
    if (!job->output_path || job->object_count <= 0 || !job->object_paths) {
        cdo_error("Invalid link job: missing output path or object files");
        return -1;
    }
    if (info->family == COMPILER_UNKNOWN) {
        cdo_error("Cannot link: no compiler detected");
        return -1;
    }

    // Ensure the output directory exists
    if (ensure_output_dir(job->output_path) != 0) {
        cdo_error("Failed to create output directory for: %s", job->output_path);
        return -1;
    }

    LinkMode mode = determine_link_mode(job);

    cdo_trace("Link mode: %s, output: %s",
              mode == LINK_MODE_STATIC_LIB ? "static-lib" :
              mode == LINK_MODE_SHARED_LIB ? "shared-lib" : "executable",
              job->output_path);

    int rc = 0;

    if (mode == LINK_MODE_STATIC_LIB) {
        if (info->family == COMPILER_MSVC) {
            rc = link_static_msvc(job, info);
        } else {
            rc = link_static_gcc(job);
        }
    } else {
        // Executable or shared library
        if (info->family == COMPILER_MSVC) {
            rc = link_msvc(job, info, mode);
        } else {
            rc = link_gcc_clang(job, info, mode);
        }
    }

    if (rc == 0) {
        cdo_info("Linked: %s", job->output_path);
    }

    return rc;
}

// ---------------------------------------------------------------------------
// Test wrappers (CDO_TESTING only)
// ---------------------------------------------------------------------------

#ifdef CDO_TESTING
int compiler_test_build_gcc_args(const CompileJob* job, const CompilerInfo* info,
                                 const char** args, int max_args) {
    return build_gcc_clang_args(job, info, args, max_args);
}

int compiler_test_build_msvc_args(const CompileJob* job, const char** args, int max_args) {
    return build_msvc_args(job, args, max_args);
}
#endif
