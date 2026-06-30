// compiler_link.c â€” Linking logic (static/shared/executable)
// Extracted from compiler.c as part of the source restructure.

#include "compiler_internal.h"
#include "core/compiler.h"
#include "pal/pal.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// compiler_link â€” link object files into final artifact
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

    cdo_log_debug("Archiving: ar rcs %s (%d objects)", job->output_path, job->object_count);

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        cdo_log_error("Failed to spawn archiver (ar)");
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        if (result.stderr_buf && result.stderr_buf[0] != '\0') {
            cdo_log_error("%s", result.stderr_buf);
        }
        cdo_log_error("Archiving failed (exit code %d)", result.exit_code);
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

    cdo_log_debug("Archiving: lib.exe /OUT:%s (%d objects)", job->output_path, job->object_count);

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        cdo_log_error("Failed to spawn archiver (lib.exe)");
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        if (result.stderr_buf && result.stderr_buf[0] != '\0') {
            cdo_log_error("%s", result.stderr_buf);
        }
        cdo_log_error("Archiving failed (exit code %d)", result.exit_code);
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

    // Extra flags (e.g., coverage instrumentation)
    if (job->extra_flags && job->extra_flag_count > 0) {
        for (int i = 0; i < job->extra_flag_count; i++) {
            if (n >= MAX_LINK_ARGS) return -1;
            args[n++] = job->extra_flags[i];
        }
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
    cdo_log_debug("Linking %s: %s (%d objects)", mode_str, job->output_path, job->object_count);

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        cdo_log_error("Failed to spawn linker (%s)", info->path);
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        if (result.stderr_buf && result.stderr_buf[0] != '\0') {
            cdo_log_error("%s", result.stderr_buf);
        }
        if (result.stdout_buf && result.stdout_buf[0] != '\0') {
            cdo_log_error("%s", result.stdout_buf);
        }
        cdo_log_error("Linking failed: %s (exit code %d)", job->output_path, result.exit_code);
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

    // Extra flags (e.g., coverage instrumentation)
    if (job->extra_flags && job->extra_flag_count > 0) {
        for (int i = 0; i < job->extra_flag_count; i++) {
            if (n >= MAX_LINK_ARGS) return -1;
            args[n++] = job->extra_flags[i];
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
    cdo_log_debug("Linking %s: %s (%d objects)", mode_str, job->output_path, job->object_count);

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        cdo_log_error("Failed to spawn linker (%s)", info->linker_path);
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        if (result.stderr_buf && result.stderr_buf[0] != '\0') {
            cdo_log_error("%s", result.stderr_buf);
        }
        if (result.stdout_buf && result.stdout_buf[0] != '\0') {
            cdo_log_error("%s", result.stdout_buf);
        }
        cdo_log_error("Linking failed: %s (exit code %d)", job->output_path, result.exit_code);
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
        cdo_log_error("Invalid link job: missing output path or object files");
        return -1;
    }
    if (info->family == COMPILER_UNKNOWN) {
        cdo_log_error("Cannot link: no compiler detected");
        return -1;
    }

    // Ensure the output directory exists
    if (ensure_output_dir(job->output_path) != 0) {
        cdo_log_error("Failed to create output directory for: %s", job->output_path);
        return -1;
    }

    LinkMode mode = determine_link_mode(job);

    cdo_log_trace("Link mode: %s, output: %s",
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
        cdo_log_info("Linked: %s", job->output_path);
    }

    return rc;
}

// ---------------------------------------------------------------------------
// compiler_link_is_fresh - check if link artifact is up-to-date
// ---------------------------------------------------------------------------

bool compiler_link_is_fresh(const char* output_path, const char** input_paths, int input_count) {
    if (!output_path || !input_paths || input_count <= 0) {
        return false;
    }

    // If output doesn't exist, must link (Requirement 7.3)
    if (pal_path_exists(output_path) != 0) {
        return false;
    }

    // Get output mtime
    uint64_t output_mtime = 0;
    if (pal_file_mtime(output_path, &output_mtime) != 0) {
        cdo_log_debug("link_is_fresh: cannot get mtime for output '%s', assuming stale", output_path);
        return false;
    }

    // Check all inputs: if any input is newer than output, need to re-link
    for (int i = 0; i < input_count; i++) {
        if (!input_paths[i]) continue;

        uint64_t input_mtime = 0;
        if (pal_file_mtime(input_paths[i], &input_mtime) != 0) {
            cdo_log_debug("link_is_fresh: cannot get mtime for input '%s', assuming stale", input_paths[i]);
            return false;
        }

        if (input_mtime > output_mtime) {
            cdo_log_debug("link_is_fresh: input '%s' is newer than output, need re-link", input_paths[i]);
            return false;
        }
    }

    cdo_log_debug("link_is_fresh: all inputs older than output '%s', skip link", output_path);
    return true;
}
