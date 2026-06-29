#include "core/hooks_exec.h"
#include "core/output.h"
#include "pal/pal.h"

#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <stdlib.h> // _putenv_s
#else
#include <unistd.h> // setenv, unsetenv
#endif

// =============================================================================
// Environment variable helpers (platform-specific)
// =============================================================================

static void hook_setenv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

static void hook_unsetenv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// =============================================================================
// hook_execute — spawn hook command with environment injection and timeout
// =============================================================================

int hook_execute(const HookDef* hook, const HookEnv* env) {
    if (!hook || !hook->present) return 0; // Absent hook → success

    // Inject CDO environment variables into current process (inherited by child)
    if (env) {
        if (env->ws_root)         hook_setenv("CDO_WS_ROOT", env->ws_root);
        if (env->profile)         hook_setenv("CDO_PROFILE", env->profile);
        if (env->build_dir)       hook_setenv("CDO_BUILD_DIR", env->build_dir);
        if (env->crate_name)      hook_setenv("CDO_CRATE_NAME", env->crate_name);
        if (env->crate_path)      hook_setenv("CDO_CRATE_PATH", env->crate_path);
        if (env->crate_build_dir) hook_setenv("CDO_CRATE_BUILD_DIR", env->crate_build_dir);
    }

    // Determine working directory: crate_path for crate hooks, ws_root for workspace hooks
    const char* cwd = NULL;
    if (env) {
        cwd = (env->crate_path && env->crate_path[0]) ? env->crate_path : env->ws_root;
    }

    // Build shell command: "cmd /c <command>" on Windows, "sh -c <command>" on POSIX
#ifdef _WIN32
    char raw_cmd[1100];
    snprintf(raw_cmd, sizeof(raw_cmd), "cmd /c %s", hook->command);
#else
    const char* shell = "sh";
    const char* shell_args[] = { "sh", "-c", hook->command, NULL };
    int shell_argc = 3;
#endif

    // Calculate timeout in milliseconds for PAL
    // HookDef: timeout_sec == 0 means no timeout → PAL -1 (infinite)
    // HookDef: timeout_sec > 0  → PAL timeout_sec * 1000
    int timeout_ms;
    if (hook->timeout_sec == 0) {
        timeout_ms = -1; // No timeout
    } else {
        timeout_ms = hook->timeout_sec * 1000;
    }

    PalSpawnOpts opts = {0};
#ifdef _WIN32
    opts.program = "cmd";
    opts.args = NULL;
    opts.arg_count = 0;
    opts.raw_cmdline = raw_cmd;
#else
    opts.program = shell;
    opts.args = shell_args;
    opts.arg_count = shell_argc;
#endif
    opts.env = NULL;             // Inherit all env vars (including our CDO_ vars)
    opts.cwd = cwd;
    opts.capture_output = false; // Inherit terminal stdin/stdout/stderr
    opts.timeout_ms = timeout_ms;

    PalSpawnResult result = {0};
    int spawn_rc = pal_spawn(&opts, &result);

    // Clean up injected environment variables
    if (env) {
        if (env->ws_root)         hook_unsetenv("CDO_WS_ROOT");
        if (env->profile)         hook_unsetenv("CDO_PROFILE");
        if (env->build_dir)       hook_unsetenv("CDO_BUILD_DIR");
        if (env->crate_name)      hook_unsetenv("CDO_CRATE_NAME");
        if (env->crate_path)      hook_unsetenv("CDO_CRATE_PATH");
        if (env->crate_build_dir) hook_unsetenv("CDO_CRATE_BUILD_DIR");
    }

    // Handle timeout (PAL kills the process internally and returns PAL_ERR_TIMEOUT)
    if (spawn_rc == PAL_ERR_TIMEOUT) {
        cdo_error("Hook '%s' timed out after %d seconds", hook_lifecycle_name(hook->lifecycle), hook->timeout_sec);
        pal_spawn_result_free(&result);
        return -1;
    }

    // Handle spawn failure (e.g., command not found, cwd doesn't exist)
    if (spawn_rc != 0) {
        cdo_error("Hook '%s' failed to start: error %d (command: %s)", hook_lifecycle_name(hook->lifecycle), spawn_rc, hook->command);
        pal_spawn_result_free(&result);
        return -1;
    }

    // Check child exit code
    int exit_code = result.exit_code;
    pal_spawn_result_free(&result);

    if (exit_code != 0) {
        cdo_error("Hook '%s' failed with exit code %d (command: %s)", hook_lifecycle_name(hook->lifecycle), exit_code, hook->command);
        return exit_code;
    }

    return 0;
}
