// compiler_detect.c â€” Compiler probing and detection logic
// Extracted from compiler.c as part of the source restructure.
#include "compiler_internal.h"
#include "core/compiler.h"
#include "pal/pal.h"
#include "core/log.h"

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
// try_vendored_tools
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
        if (pal_path_exists(gcc_probes[i]) == 0) {
            // Found a vendored GCC â€” try to run it
            if (try_compiler(gcc_probes[i], "--version", info->path, sizeof(info->path),
                             info->version, sizeof(info->version)) == 0) {
                info->family = COMPILER_GCC;
                strncpy(info->linker_path, info->path, sizeof(info->linker_path) - 1);
                info->linker_path[sizeof(info->linker_path) - 1] = '\0';
                cdo_log_debug("Detected vendored compiler: GCC %s at %s", info->version, info->path);
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
        if (pal_path_exists(clang_probes[i]) == 0) {
            if (try_compiler(clang_probes[i], "--version", info->path, sizeof(info->path),
                             info->version, sizeof(info->version)) == 0) {
                info->family = COMPILER_CLANG;
                strncpy(info->linker_path, info->path, sizeof(info->linker_path) - 1);
                info->linker_path[sizeof(info->linker_path) - 1] = '\0';
                cdo_log_debug("Detected vendored compiler: Clang %s at %s", info->version, info->path);
                return 0;
            }
        }
    }

    return -1;
}

// ---------------------------------------------------------------------------
// compiler_detect
// ---------------------------------------------------------------------------

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
        cdo_log_debug("Detected compiler: MSVC %s", info->version);
        return 0;
    }

    if (try_compiler("gcc", "--version", info->path, sizeof(info->path),
                     info->version, sizeof(info->version)) == 0) {
        info->family = COMPILER_GCC;
        // GCC acts as linker driver
        strncpy(info->linker_path, info->path, sizeof(info->linker_path) - 1);
        info->linker_path[sizeof(info->linker_path) - 1] = '\0';
        cdo_log_debug("Detected compiler: GCC %s", info->version);
        return 0;
    }

    if (try_compiler("clang", "--version", info->path, sizeof(info->path),
                     info->version, sizeof(info->version)) == 0) {
        info->family = COMPILER_CLANG;
        // Clang acts as linker driver
        strncpy(info->linker_path, info->path, sizeof(info->linker_path) - 1);
        info->linker_path[sizeof(info->linker_path) - 1] = '\0';
        cdo_log_debug("Detected compiler: Clang %s", info->version);
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
        cdo_log_debug("Detected compiler: GCC %s", info->version);
        return 0;
    }

    if (try_compiler("clang", "--version", info->path, sizeof(info->path),
                     info->version, sizeof(info->version)) == 0) {
        info->family = COMPILER_CLANG;
        // Clang acts as linker driver
        strncpy(info->linker_path, info->path, sizeof(info->linker_path) - 1);
        info->linker_path[sizeof(info->linker_path) - 1] = '\0';
        cdo_log_debug("Detected compiler: Clang %s", info->version);
        return 0;
    }
#endif

    // Fallback: try vendored tools in .cdo/tools/
    if (try_vendored_tools(info) == 0) {
        return 0;
    }

    cdo_log_warn("No compiler detected on system PATH or vendored tools");
    return -1;
}
