#include "commands/cmd_run.h"
#include "commands/cmd_run_internal.h"
#include "commands/cmd_build.h"
#include "core/output.h"
#include "model/workspace.h"
#include "model/module.h"
#include "pal/pal.h"
#include <string.h>
#include <stdlib.h>

#define BUILD_DIR "build"
#define CDO_DIR   ".cdo"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Find a crate by name in the workspace. Returns pointer or NULL.
static const Crate* find_crate_by_name(const Workspace* ws, const char* name) {
    for (int i = 0; i < ws->crate_count; i++) {
        if (strcmp(ws->crates[i].name, name) == 0) {
            return &ws->crates[i];
        }
    }
    return NULL;
}

/// Select which crate to run. Returns pointer to crate or NULL on error.
const Crate* run_select_crate(const Workspace* ws, const CdoOptions* opts) {
    if (opts->positional_count > 0) {
        const char* name = opts->positional_args[0];
        const Crate* crate = find_crate_by_name(ws, name);
        if (!crate) {
            cdo_error("Crate '%s' not found in workspace", name);
            return NULL;
        }
        if (!crate->modules[MODULE_EXE].present) {
            cdo_error("Crate '%s' has no executable target", name);
            return NULL;
        }
        return crate;
    }

    // Auto-select: find all executable crates
    const Crate* exec_crates[64];
    int exec_count = 0;

    for (int i = 0; i < ws->crate_count; i++) {
        if (ws->crates[i].modules[MODULE_EXE].present) {
            if (exec_count < 64) {
                exec_crates[exec_count++] = &ws->crates[i];
            }
        }
    }

    if (exec_count == 0) {
        cdo_error("No executable crate found in workspace");
        return NULL;
    }

    if (exec_count == 1) {
        return exec_crates[0];
    }

    // Multiple executable crates — list them
    cdo_error("Multiple executable crates found; specify one:");
    for (int i = 0; i < exec_count; i++) {
        cdo_error("  %s", exec_crates[i]->name);
    }
    return NULL;
}

/// Determine profile string from options.
static const char* resolve_run_profile(const CdoOptions* opts) {
    if (opts->release) return "release";
    if (opts->profile && opts->profile[0] != '\0') return opts->profile;
    return "debug";
}

// ---------------------------------------------------------------------------
// Directory copy context for pal_dir_walk callback
// ---------------------------------------------------------------------------

typedef struct {
    const char* src_base;       // normalized source base path
    size_t      src_base_len;   // length of src_base (without trailing slash)
    const char* dst_base;       // destination base directory
    int         error;          // set to non-zero on failure
} DirCopyCtx;

/// pal_dir_walk callback: copy each file preserving relative structure.
static void copy_dir_callback(const char* entry_path, bool is_dir, void* ctx) {
    DirCopyCtx* dc = (DirCopyCtx*)ctx;
    if (dc->error) return;
    if (is_dir) return;

    // Normalize entry path for comparison
    size_t len = strlen(entry_path);
    char* norm_entry = (char*)malloc(len + 1);
    if (!norm_entry) { dc->error = 1; return; }
    memcpy(norm_entry, entry_path, len + 1);
    pal_path_normalize(norm_entry);

    // Compute relative path
    const char* rel = norm_entry + dc->src_base_len;
    if (*rel == '/') rel++;

    // Build destination path
    char dst_path[1024];
    if (pal_path_join(dst_path, sizeof(dst_path), dc->dst_base, rel) != 0) {
        cdo_error("staging: destination path too long for '%s'", rel);
        free(norm_entry);
        dc->error = 1;
        return;
    }

    // Ensure parent directory exists
    char parent_dir[1024];
    strncpy(parent_dir, dst_path, sizeof(parent_dir) - 1);
    parent_dir[sizeof(parent_dir) - 1] = '\0';
    char* last_slash = strrchr(parent_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (pal_mkdir_p(parent_dir) != 0) {
            cdo_error("staging: failed to create directory '%s'", parent_dir);
            free(norm_entry);
            dc->error = 1;
            return;
        }
    }

    // Copy file
    if (pal_file_copy(norm_entry, dst_path) != 0) {
        cdo_error("staging: failed to copy '%s' -> '%s'", norm_entry, dst_path);
        free(norm_entry);
        dc->error = 1;
        return;
    }

    free(norm_entry);
}

/// Recursively copy a directory tree into the destination, preserving structure.
/// Returns 0 on success, non-zero on error.
int run_copy_dir_recursive(const char* src_dir, const char* dst_dir) {
    // Normalize source directory path
    char norm_src[1024];
    strncpy(norm_src, src_dir, sizeof(norm_src) - 1);
    norm_src[sizeof(norm_src) - 1] = '\0';
    pal_path_normalize(norm_src);

    // Strip trailing slash for consistent relative path computation
    size_t src_len = strlen(norm_src);
    while (src_len > 0 && norm_src[src_len - 1] == '/') {
        norm_src[--src_len] = '\0';
    }

    DirCopyCtx ctx;
    ctx.src_base = norm_src;
    ctx.src_base_len = src_len;
    ctx.dst_base = dst_dir;
    ctx.error = 0;

    int rc = pal_dir_walk(norm_src, copy_dir_callback, &ctx);
    if (rc != 0) return 1;
    if (ctx.error) return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// DLL/SO copy context for pal_dir_walk callback
// ---------------------------------------------------------------------------

typedef struct {
    const char* dst_dir;    // staging folder root
    int         error;
} DllCopyCtx;

/// pal_dir_walk callback: copy DLL/SO files to staging root.
static void copy_dll_callback(const char* entry_path, bool is_dir, void* ctx) {
    DllCopyCtx* dc = (DllCopyCtx*)ctx;
    if (dc->error) return;
    if (is_dir) return;

    const char* ext = pal_path_ext(entry_path);
#ifdef _WIN32
    if (strcmp(ext, ".dll") != 0) return;
#else
    if (strcmp(ext, ".so") != 0) return;
#endif

    // Extract filename from path
    const char* filename = strrchr(entry_path, '/');
    if (!filename) filename = strrchr(entry_path, '\\');
    if (filename) filename++;
    else filename = entry_path;

    char dst_path[1024];
    if (pal_path_join(dst_path, sizeof(dst_path), dc->dst_dir, filename) != 0) {
        cdo_error("staging: DLL path too long for '%s'", filename);
        dc->error = 1;
        return;
    }

    if (pal_file_copy(entry_path, dst_path) != 0) {
        cdo_error("staging: failed to copy DLL '%s' -> '%s'", entry_path, dst_path);
        dc->error = 1;
        return;
    }

    cdo_debug("staging: copied DLL '%s'", filename);
}

// ---------------------------------------------------------------------------
// Staging logic
// ---------------------------------------------------------------------------

/// Prepare staging folder: clear, copy exe, DLLs, res/, shd/.
/// Returns 0 on success, non-zero on error.
int run_prepare_staging(const Workspace* ws, const Crate* crate, const char* profile, const char* staging_dir, const char* exe_path) {
    // If staging folder exists from prior run, remove contents
    if (pal_path_exists(staging_dir) == 0) {
        cdo_debug("staging: clearing prior contents at '%s'", staging_dir);
        if (pal_rmdir_r(staging_dir) != 0) {
            cdo_error("staging: failed to clear prior staging folder '%s'", staging_dir);
            return 1;
        }
    }

    // Create staging folder
    if (pal_mkdir_p(staging_dir) != 0) {
        cdo_error("staging: failed to create staging folder '%s'", staging_dir);
        return 1;
    }

    // Copy executable into staging
    char exe_name[128];
#ifdef _WIN32
    snprintf(exe_name, sizeof(exe_name), "%s.exe", crate->name);
#else
    snprintf(exe_name, sizeof(exe_name), "%s", crate->name);
#endif

    char staging_exe[1024];
    if (pal_path_join(staging_exe, sizeof(staging_exe), staging_dir, exe_name) != 0) {
        cdo_error("staging: exe path too long");
        return 1;
    }

    if (pal_file_copy(exe_path, staging_exe) != 0) {
        cdo_error("staging: failed to copy executable '%s' -> '%s'", exe_path, staging_exe);
        return 1;
    }
    cdo_debug("staging: copied executable '%s'", exe_name);

    // Copy DLL/SO files from the crate's build directory
    // (propagate_dep_modules already placed them adjacent to the exe)
    char crate_build_dir[1024];
    {
        char profile_dir[512];
        char build_base[512];
        pal_path_join(build_base, sizeof(build_base), ws->root_path, BUILD_DIR);
        pal_path_join(profile_dir, sizeof(profile_dir), build_base, profile);
        pal_path_join(crate_build_dir, sizeof(crate_build_dir), profile_dir, crate->name);
    }

    DllCopyCtx dll_ctx = { .dst_dir = staging_dir, .error = 0 };
    pal_dir_walk(crate_build_dir, copy_dll_callback, &dll_ctx);
    if (dll_ctx.error) {
        cdo_error("staging: failed to copy shared libraries");
        return 1;
    }

    // Copy res/ directory if it exists in the build output
    char res_src[1024];
    if (pal_path_join(res_src, sizeof(res_src), crate_build_dir, "res") == 0) {
        if (pal_path_exists(res_src) == 0) {
            char res_dst[1024];
            pal_path_join(res_dst, sizeof(res_dst), staging_dir, "res");
            if (pal_mkdir_p(res_dst) != 0) {
                cdo_error("staging: failed to create res/ directory");
                return 1;
            }
            if (run_copy_dir_recursive(res_src, res_dst) != 0) {
                cdo_error("staging: failed to copy res/ directory");
                return 1;
            }
            cdo_debug("staging: copied res/ directory");
        }
    }

    // Copy shd/ directory if it exists in the build output
    char shd_src[1024];
    if (pal_path_join(shd_src, sizeof(shd_src), crate_build_dir, "shd") == 0) {
        if (pal_path_exists(shd_src) == 0) {
            char shd_dst[1024];
            pal_path_join(shd_dst, sizeof(shd_dst), staging_dir, "shd");
            if (pal_mkdir_p(shd_dst) != 0) {
                cdo_error("staging: failed to create shd/ directory");
                return 1;
            }
            if (run_copy_dir_recursive(shd_src, shd_dst) != 0) {
                cdo_error("staging: failed to copy shd/ directory");
                return 1;
            }
            cdo_debug("staging: copied shd/ directory");
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// cmd_run entry point
// ---------------------------------------------------------------------------

int cmd_run(const CdoOptions* opts) {
    // Load workspace
    Workspace ws;
    memset(&ws, 0, sizeof(ws));

    int rc = workspace_load(".", &ws);
    if (rc != 0) {
        cdo_error("Failed to load workspace");
        return 1;
    }

    // Select the crate to run
    const Crate* crate = run_select_crate(&ws, opts);
    if (!crate) {
        workspace_free(&ws);
        return 1;
    }

    // Build the crate via cmd_build.
    const char* build_args[1];
    build_args[0] = crate->name;

    CdoOptions build_opts = *opts;
    build_opts.command = CDO_CMD_BUILD;
    build_opts.positional_args = build_args;
    build_opts.positional_count = 1;
    build_opts.argv_rest = NULL;
    build_opts.argc_rest = 0;

    rc = cmd_build(&build_opts);
    if (rc != 0) {
        cdo_error("Build failed for crate '%s'", crate->name);
        workspace_free(&ws);
        return rc;
    }

    // Determine profile and construct path to the built executable
    const char* profile = resolve_run_profile(opts);

    char exe_path[1024];
    char crate_build_dir[1024];
    {
        char profile_dir[512];
        rc = pal_path_join(profile_dir, sizeof(profile_dir), BUILD_DIR, profile);
        if (rc != 0) {
            cdo_error("Path too long for profile '%s'", profile);
            workspace_free(&ws);
            return 1;
        }
        rc = pal_path_join(crate_build_dir, sizeof(crate_build_dir), profile_dir, crate->name);
        if (rc != 0) {
            cdo_error("Path too long for crate '%s'", crate->name);
            workspace_free(&ws);
            return 1;
        }
    }

    char exe_name[128];
#ifdef _WIN32
    snprintf(exe_name, sizeof(exe_name), "%s.exe", crate->name);
#else
    snprintf(exe_name, sizeof(exe_name), "%s", crate->name);
#endif

    rc = pal_path_join(exe_path, sizeof(exe_path), crate_build_dir, exe_name);
    if (rc != 0) {
        cdo_error("Path too long for executable '%s'", crate->name);
        workspace_free(&ws);
        return 1;
    }

    // Construct staging folder path: .cdo/<crate_name>/run/
    char staging_dir[1024];
    {
        char cdo_crate_dir[512];
        rc = pal_path_join(cdo_crate_dir, sizeof(cdo_crate_dir), CDO_DIR, crate->name);
        if (rc != 0) {
            cdo_error("Staging path too long for crate '%s'", crate->name);
            workspace_free(&ws);
            return 1;
        }
        rc = pal_path_join(staging_dir, sizeof(staging_dir), cdo_crate_dir, "run");
        if (rc != 0) {
            cdo_error("Staging path too long for crate '%s'", crate->name);
            workspace_free(&ws);
            return 1;
        }
    }

    // Prepare staging folder (copy exe, DLLs, res/, shd/)
    rc = run_prepare_staging(&ws, crate, profile, staging_dir, exe_path);
    if (rc != 0) {
        workspace_free(&ws);
        return 1;
    }

    // Spawn the executable from the staging folder with cwd set to staging
    char staging_exe[1024];
    pal_path_join(staging_exe, sizeof(staging_exe), staging_dir, exe_name);

    PalSpawnOpts spawn_opts;
    memset(&spawn_opts, 0, sizeof(spawn_opts));
    spawn_opts.program = staging_exe;
    spawn_opts.args = opts->argv_rest;
    spawn_opts.arg_count = opts->argc_rest;
    spawn_opts.cwd = staging_dir;
    spawn_opts.capture_output = false;
    spawn_opts.timeout_ms = -1; // no timeout for user programs

    PalSpawnResult spawn_result;
    memset(&spawn_result, 0, sizeof(spawn_result));

    cdo_info("Running '%s'", crate->name);

    rc = pal_spawn(&spawn_opts, &spawn_result);
    if (rc != 0) {
        cdo_error("Failed to execute '%s'", staging_exe);
        workspace_free(&ws);
        return 1;
    }

    int exit_code = spawn_result.exit_code;
    pal_spawn_result_free(&spawn_result);
    workspace_free(&ws);

    return exit_code;
}
