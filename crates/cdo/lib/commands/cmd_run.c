/**
 * cmd_run.c - Build and run an executable crate.
 *
 * Delegates bundling (exe + DLLs + res + shd) to the shared bundle utility,
 * then spawns the executable from the staging folder.
 */
#include "commands/cmd_run.h"
#include "commands/cmd_run_internal.h"
#include "commands/cmd_build.h"
#include "commands/bundle.h"
#include "core/log.h"
#include "core/cli_arg_access.h"
#include "model/workspace.h"
#include "model/module.h"
#include "pal/pal.h"
#include <string.h>
#include <stdlib.h>

#define BUILD_DIR "build"
#define CDO_DIR   ".cdo"

// ---------------------------------------------------------------------------
// Legacy wrappers (delegate to shared bundle utilities, preserving ABI)
// ---------------------------------------------------------------------------

const Crate* run_select_crate(const Workspace* ws, const char* const* positional_values, int positional_count) {
    return bundle_select_exe_crate(ws, positional_values, positional_count);
}

int run_copy_dir_recursive(const char* src_dir, const char* dst_dir) {
    return bundle_copy_dir_recursive(src_dir, dst_dir);
}

int run_prepare_staging(const Workspace* ws, const Crate* crate, const char* profile, const char* staging_dir, const char* exe_path) {
    return bundle_prepare(ws, crate, profile, staging_dir, exe_path, NULL);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char* resolve_run_profile(bool release, const char* profile) {
    if (release) return "release";
    if (profile && profile[0] != '\0') return profile;
    return "debug";
}

// ---------------------------------------------------------------------------
// cmd_run entry point
// ---------------------------------------------------------------------------

int cmd_run(const CliParseResult* result, void* ctx) {
    bool release = cli_arg_get_bool(result, "release");
    const char* profile_str = cli_arg_get_str(result, "profile");

    Workspace ws;
    memset(&ws, 0, sizeof(ws));

    int rc = workspace_load(".", &ws);
    if (rc != 0) {
        cdo_log_error("Failed to load workspace");
        return 1;
    }

    const Crate* crate = run_select_crate(&ws, result->positional_values, result->positional_count);
    if (!crate) {
        workspace_free(&ws);
        return 1;
    }

    // Build the crate
    const char* build_positional[1];
    build_positional[0] = crate->name;

    CliArgValue build_arg_buf[4];
    int build_arg_count = 0;

    build_arg_buf[build_arg_count].name = "release";
    build_arg_buf[build_arg_count].type = CLI_ARG_BOOL;
    build_arg_buf[build_arg_count].value.bool_val = release;
    build_arg_buf[build_arg_count].present = release;
    build_arg_count++;

    build_arg_buf[build_arg_count].name = "profile";
    build_arg_buf[build_arg_count].type = CLI_ARG_STRING;
    build_arg_buf[build_arg_count].value.str_val = profile_str;
    build_arg_buf[build_arg_count].present = (profile_str != NULL);
    build_arg_count++;

    CliParseResult build_result = {0};
    build_result.matched_cmd = NULL;
    build_result.arg_values = build_arg_buf;
    build_result.arg_value_count = build_arg_count;
    build_result.positional_values = build_positional;
    build_result.positional_count = 1;
    build_result.rest_args = NULL;
    build_result.rest_count = 0;

    rc = cmd_build(&build_result, ctx);
    if (rc != 0) {
        cdo_log_error("Build failed for crate '%s'", crate->name);
        workspace_free(&ws);
        return rc;
    }

    // Resolve paths
    const char* profile = resolve_run_profile(release, profile_str);

    char exe_path[1024];
    char crate_build_dir[1024];
    {
        char profile_dir[512];
        rc = pal_path_join(profile_dir, sizeof(profile_dir), BUILD_DIR, profile);
        if (rc != 0) { cdo_log_error("Path too long for profile '%s'", profile); workspace_free(&ws); return 1; }
        rc = pal_path_join(crate_build_dir, sizeof(crate_build_dir), profile_dir, crate->name);
        if (rc != 0) { cdo_log_error("Path too long for crate '%s'", crate->name); workspace_free(&ws); return 1; }
    }

    char exe_name[128];
#ifdef _WIN32
    snprintf(exe_name, sizeof(exe_name), "%s.exe", crate->name);
#else
    snprintf(exe_name, sizeof(exe_name), "%s", crate->name);
#endif

    rc = pal_path_join(exe_path, sizeof(exe_path), crate_build_dir, exe_name);
    if (rc != 0) { cdo_log_error("Path too long for executable '%s'", crate->name); workspace_free(&ws); return 1; }

    // Staging folder: .cdo/<crate_name>/run/
    char staging_dir[1024];
    {
        char cdo_crate_dir[512];
        rc = pal_path_join(cdo_crate_dir, sizeof(cdo_crate_dir), CDO_DIR, crate->name);
        if (rc != 0) { cdo_log_error("Staging path too long for crate '%s'", crate->name); workspace_free(&ws); return 1; }
        rc = pal_path_join(staging_dir, sizeof(staging_dir), cdo_crate_dir, "run");
        if (rc != 0) { cdo_log_error("Staging path too long for crate '%s'", crate->name); workspace_free(&ws); return 1; }
    }

    // Bundle runtime artifacts
    rc = run_prepare_staging(&ws, crate, profile, staging_dir, exe_path);
    if (rc != 0) {
        workspace_free(&ws);
        return 1;
    }

    // Spawn the executable
    char staging_exe[1024];
    pal_path_join(staging_exe, sizeof(staging_exe), staging_dir, exe_name);

    PalSpawnOpts spawn_opts;
    memset(&spawn_opts, 0, sizeof(spawn_opts));
    spawn_opts.program = staging_exe;
    spawn_opts.args = result->rest_args;
    spawn_opts.arg_count = result->rest_count;
    spawn_opts.cwd = staging_dir;
    spawn_opts.capture_output = false;
    spawn_opts.timeout_ms = -1;

    PalSpawnResult spawn_result;
    memset(&spawn_result, 0, sizeof(spawn_result));

    cdo_log_info("Running '%s'", crate->name);

    rc = pal_spawn(&spawn_opts, &spawn_result);
    if (rc != 0) {
        cdo_log_error("Failed to execute '%s'", staging_exe);
        workspace_free(&ws);
        return 1;
    }

    int exit_code = spawn_result.exit_code;
    pal_spawn_result_free(&spawn_result);
    workspace_free(&ws);

    return exit_code;
}
