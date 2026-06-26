#include "commands/cmd_run.h"
#include "commands/cmd_build.h"
#include "core/output.h"
#include "core/workspace.h"
#include "pal/pal.h"
#include <string.h>

#define BUILD_DIR "build"

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
static const Crate* select_run_crate(const Workspace* ws, const CdoOptions* opts) {
    if (opts->positional_count > 0) {
        const char* name = opts->positional_args[0];
        const Crate* crate = find_crate_by_name(ws, name);
        if (!crate) {
            cdo_error("Crate '%s' not found in workspace", name);
            return NULL;
        }
        if (crate->type != CRATE_EXECUTABLE) {
            cdo_error("Crate '%s' is not an executable", name);
            return NULL;
        }
        return crate;
    }

    // Auto-select: find all executable crates
    const Crate* exec_crates[64];
    int exec_count = 0;

    for (int i = 0; i < ws->crate_count; i++) {
        if (ws->crates[i].type == CRATE_EXECUTABLE) {
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
    const Crate* crate = select_run_crate(&ws, opts);
    if (!crate) {
        workspace_free(&ws);
        return 1;
    }

    // Build the crate via cmd_build.
    // Construct a temporary CdoOptions that targets this specific crate.
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

    // Construct path to the built executable:
    // build/<profile>/<crate_name>/<crate_name>[.exe on Windows]
    const char* profile = opts->release ? "release" :
                          (opts->profile && opts->profile[0]) ? opts->profile : "debug";

    char exe_path[1024];
    char profile_dir[512];
    char crate_dir[512];

    rc = pal_path_join(profile_dir, sizeof(profile_dir), BUILD_DIR, profile);
    if (rc != 0) {
        cdo_error("Path too long for profile '%s'", profile);
        workspace_free(&ws);
        return 1;
    }

    rc = pal_path_join(crate_dir, sizeof(crate_dir), profile_dir, crate->name);
    if (rc != 0) {
        cdo_error("Path too long for crate '%s'", crate->name);
        workspace_free(&ws);
        return 1;
    }

#ifdef _WIN32
    char exe_name[128];
    snprintf(exe_name, sizeof(exe_name), "%s.exe", crate->name);
#else
    const char* exe_name = crate->name;
#endif

    rc = pal_path_join(exe_path, sizeof(exe_path), crate_dir, exe_name);
    if (rc != 0) {
        cdo_error("Path too long for executable '%s'", crate->name);
        workspace_free(&ws);
        return 1;
    }

    // Spawn the executable, forwarding argv_rest
    PalSpawnOpts spawn_opts;
    memset(&spawn_opts, 0, sizeof(spawn_opts));
    spawn_opts.program = exe_path;
    spawn_opts.args = opts->argv_rest;
    spawn_opts.arg_count = opts->argc_rest;
    spawn_opts.capture_output = false;

    PalSpawnResult spawn_result;
    memset(&spawn_result, 0, sizeof(spawn_result));

    cdo_info("Running '%s'", crate->name);

    rc = pal_spawn(&spawn_opts, &spawn_result);
    if (rc != 0) {
        cdo_error("Failed to execute '%s'", exe_path);
        workspace_free(&ws);
        return 1;
    }

    int exit_code = spawn_result.exit_code;
    pal_spawn_result_free(&spawn_result);
    workspace_free(&ws);

    return exit_code;
}
