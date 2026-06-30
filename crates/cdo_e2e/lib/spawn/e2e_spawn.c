/// @file e2e_spawn.c
/// @brief Implementation of e2e subprocess execution functions.
///
/// Spawns subprocesses within e2e test environments, capturing stdout/stderr,
/// enforcing timeouts, and merging environment variables from the test context.

#include "cdo_e2e.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/// Default subprocess timeout in milliseconds (2 minutes).
#define E2E_DEFAULT_TIMEOUT_MS 120000

/// Maximum size for a single "KEY=VALUE" environment string.
#define E2E_ENV_ENTRY_MAX 768

// ---------------------------------------------------------------------------
// Environment merging helpers
// ---------------------------------------------------------------------------

/// Count the number of environment variables in the current process environment.
/// On Windows, uses GetEnvironmentStringsA to enumerate.
/// Returns the count of "KEY=VALUE" strings (excluding the terminating empty string).
static int count_parent_env(void) {
#ifdef _WIN32
    char* env_block = GetEnvironmentStringsA();
    if (!env_block) return 0;

    int count = 0;
    const char* p = env_block;
    while (*p) {
        p += strlen(p) + 1;
        count++;
    }
    FreeEnvironmentStringsA(env_block);
    return count;
#else
    extern char** environ;
    int count = 0;
    if (environ) {
        while (environ[count]) count++;
    }
    return count;
#endif
}

/// Check if a "KEY=VALUE" string matches a given key (case-insensitive on Windows).
static bool env_entry_matches_key(const char* entry, const char* key) {
    size_t key_len = strlen(key);
    if (strlen(entry) <= key_len) return false;
    if (entry[key_len] != '=') return false;
#ifdef _WIN32
    return _strnicmp(entry, key, key_len) == 0;
#else
    return strncmp(entry, key, key_len) == 0;
#endif
}

/// Build a merged environment array for the child process.
/// Merges: parent process env + env->env_vars + opts->extra_env.
/// Later entries override earlier ones (extra_env overrides env_vars overrides parent).
/// Returns a NULL-terminated array of "KEY=VALUE" strings (heap-allocated).
/// Caller must free each entry and the array itself.
static const char** build_merged_env(E2eEnv* env, const E2eSpawnOpts* opts, int* out_count) {
    int parent_count = count_parent_env();
    int env_count = env ? env->env_var_count : 0;
    int extra_count = (opts && opts->extra_env) ? opts->extra_env_count : 0;
    int max_entries = parent_count + env_count + extra_count;

    // Allocate array for all possible entries + NULL terminator
    const char** arr = (const char**)calloc((size_t)(max_entries + 1), sizeof(const char*));
    if (!arr) {
        *out_count = 0;
        return NULL;
    }

    int count = 0;

    // Step 1: Copy parent environment entries
#ifdef _WIN32
    char* env_block = GetEnvironmentStringsA();
    if (env_block) {
        const char* p = env_block;
        while (*p) {
            size_t len = strlen(p);
            char* entry = (char*)malloc(len + 1);
            if (entry) {
                memcpy(entry, p, len + 1);
                arr[count++] = entry;
            }
            p += len + 1;
        }
        FreeEnvironmentStringsA(env_block);
    }
#else
    extern char** environ;
    if (environ) {
        for (int i = 0; environ[i]; i++) {
            size_t len = strlen(environ[i]);
            char* entry = (char*)malloc(len + 1);
            if (entry) {
                memcpy(entry, environ[i], len + 1);
                arr[count++] = entry;
            }
        }
    }
#endif

    // Step 2: Merge env->env_vars (override existing keys)
    for (int i = 0; i < env_count; i++) {
        const char* key = env->env_vars[i].key;
        const char* value = env->env_vars[i].value;

        // Build "KEY=VALUE" string
        char entry_buf[E2E_ENV_ENTRY_MAX];
        snprintf(entry_buf, sizeof(entry_buf), "%s=%s", key, value);

        // Check if this key already exists - replace it
        bool replaced = false;
        for (int j = 0; j < count; j++) {
            if (env_entry_matches_key(arr[j], key)) {
                free((void*)arr[j]);
                size_t len = strlen(entry_buf);
                char* new_entry = (char*)malloc(len + 1);
                if (new_entry) {
                    memcpy(new_entry, entry_buf, len + 1);
                    arr[j] = new_entry;
                }
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            size_t len = strlen(entry_buf);
            char* new_entry = (char*)malloc(len + 1);
            if (new_entry) {
                memcpy(new_entry, entry_buf, len + 1);
                arr[count++] = new_entry;
            }
        }
    }

    // Step 3: Merge opts->extra_env (override existing keys)
    for (int i = 0; i < extra_count; i++) {
        const char* key = opts->extra_env[i].key;
        const char* value = opts->extra_env[i].value;

        char entry_buf[E2E_ENV_ENTRY_MAX];
        snprintf(entry_buf, sizeof(entry_buf), "%s=%s", key, value);

        bool replaced = false;
        for (int j = 0; j < count; j++) {
            if (env_entry_matches_key(arr[j], key)) {
                free((void*)arr[j]);
                size_t len = strlen(entry_buf);
                char* new_entry = (char*)malloc(len + 1);
                if (new_entry) {
                    memcpy(new_entry, entry_buf, len + 1);
                    arr[j] = new_entry;
                }
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            size_t len = strlen(entry_buf);
            char* new_entry = (char*)malloc(len + 1);
            if (new_entry) {
                memcpy(new_entry, entry_buf, len + 1);
                arr[count++] = new_entry;
            }
        }
    }

    arr[count] = NULL;
    *out_count = count;
    return arr;
}

/// Free the merged environment array (entries + array itself).
static void free_merged_env(const char** arr, int count) {
    if (!arr) return;
    for (int i = 0; i < count; i++) {
        free((void*)arr[i]);
    }
    free((void*)arr);
}

// ---------------------------------------------------------------------------
// e2e_spawn
// ---------------------------------------------------------------------------

int e2e_spawn(E2eEnv* env, const E2eSpawnOpts* opts, E2eSpawnResult* result) {
    if (!env || !opts || !result) return E2E_ERR_INVALID;

    // Initialize result
    memset(result, 0, sizeof(E2eSpawnResult));

    // Validate executable
    if (!opts->executable || opts->executable[0] == '\0') {
        snprintf(result->error_desc, sizeof(result->error_desc), "executable path is NULL or empty");
        return E2E_ERR_SPAWN;
    }

    // Determine working directory: opts->working_dir overrides env->root_path
    const char* cwd = opts->working_dir;
    if (!cwd || cwd[0] == '\0') {
        cwd = env->root_path;
    }

    // Determine effective timeout
    int timeout_ms = opts->timeout_ms;
    if (timeout_ms <= 0) {
        timeout_ms = E2E_DEFAULT_TIMEOUT_MS;
    }

    // Build merged environment.
    // Optimization: if no env vars are set (env_var_count == 0 and no extra_env),
    // pass NULL to inherit the parent's environment directly (avoids env block rebuild).
    int env_count = 0;
    const char** merged_env = NULL;
    int total_overrides = env->env_var_count + ((opts->extra_env) ? opts->extra_env_count : 0);
    if (total_overrides > 0) {
        merged_env = build_merged_env(env, opts, &env_count);
    }

    // Build PalSpawnOpts
    PalSpawnOpts pal_opts;
    memset(&pal_opts, 0, sizeof(pal_opts));
    pal_opts.program = opts->executable;
    pal_opts.args = opts->args;
    pal_opts.arg_count = opts->arg_count;
    pal_opts.env = merged_env; // NULL-terminated array of "KEY=VALUE" or NULL to inherit
    pal_opts.cwd = cwd;
    pal_opts.capture_output = true;
    pal_opts.timeout_ms = timeout_ms;

    // Build a raw command line for Windows cmd.exe compatibility.
    // cmd.exe has special quoting requirements - using raw_cmdline avoids
    // build_command_line's per-argument quoting which can break cmd /C commands.
    char raw_cmd[4096];
    raw_cmd[0] = '\0';
    int raw_pos = 0;
    raw_pos += snprintf(raw_cmd + raw_pos, sizeof(raw_cmd) - (size_t)raw_pos, "\"%s\"", opts->executable);
    for (int i = 0; i < opts->arg_count && raw_pos < (int)(sizeof(raw_cmd) - 2); i++) {
        raw_pos += snprintf(raw_cmd + raw_pos, sizeof(raw_cmd) - (size_t)raw_pos, " %s", opts->args[i]);
    }
    pal_opts.raw_cmdline = raw_cmd;

    // Spawn the subprocess
    PalSpawnResult pal_result;
    memset(&pal_result, 0, sizeof(pal_result));

    int rc = pal_spawn(&pal_opts, &pal_result);

    // Free the merged environment - no longer needed after spawn
    if (merged_env) {
        free_merged_env(merged_env, env_count);
    }

    if (rc == PAL_ERR_TIMEOUT) {
        // Timeout occurred - process was terminated
        result->exit_code = pal_result.exit_code;
        result->stdout_buf = pal_result.stdout_buf;  // Transfer ownership of partial output
        result->stderr_buf = pal_result.stderr_buf;
        result->timed_out = true;
        return E2E_OK;
    }

    if (rc != PAL_OK) {
        // Spawn failure - could not start the process
        snprintf(result->error_desc, sizeof(result->error_desc), "pal_spawn failed with error code %d for executable '%s'", rc, opts->executable);
        // Clean up any partial PAL result
        if (pal_result.stdout_buf) free(pal_result.stdout_buf);
        if (pal_result.stderr_buf) free(pal_result.stderr_buf);
        return E2E_ERR_SPAWN;
    }

    // Success - transfer results
    result->exit_code = pal_result.exit_code;
    result->stdout_buf = pal_result.stdout_buf;  // Transfer ownership
    result->stderr_buf = pal_result.stderr_buf;  // Transfer ownership
    result->timed_out = false;

    return E2E_OK;
}

// ---------------------------------------------------------------------------
// e2e_spawn_result_free
// ---------------------------------------------------------------------------

void e2e_spawn_result_free(E2eSpawnResult* result) {
    if (!result) return;

    if (result->stdout_buf) {
        free(result->stdout_buf);
        result->stdout_buf = NULL;
    }
    if (result->stderr_buf) {
        free(result->stderr_buf);
        result->stderr_buf = NULL;
    }

    result->exit_code = 0;
    result->timed_out = false;
    result->error_desc[0] = '\0';
}
