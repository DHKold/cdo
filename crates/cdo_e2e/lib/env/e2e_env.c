/// @file e2e_env.c
/// @brief Implementation of e2e test environment functions.

#include "cdo_e2e.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#define get_pid() _getpid()
#else
#include <unistd.h>
#define get_pid() getpid()
#endif

/// Monotonic counter for generating unique directory names across calls.
static long s_env_counter = 0;

/// Atomically increment the counter and return the new value.
static long env_next_counter(void) {
#ifdef _MSC_VER
    return InterlockedIncrement(&s_env_counter);
#elif defined(__GNUC__) || defined(__clang__)
    return __sync_add_and_fetch(&s_env_counter, 1);
#else
    return ++s_env_counter;
#endif
}

/// Get the system temporary directory path.
static const char* env_get_temp_dir(void) {
#ifdef _WIN32
    const char* tmp = getenv("TEMP");
    if (!tmp || tmp[0] == '\0') tmp = getenv("TMP");
    if (!tmp || tmp[0] == '\0') tmp = "C:\\Temp";
    return tmp;
#else
    const char* tmp = getenv("TMPDIR");
    if (!tmp || tmp[0] == '\0') tmp = "/tmp";
    return tmp;
#endif
}

/// Sanitize a test name: replace non-alphanumeric chars (except _ and -) with '_',
/// truncate to max_len characters. Result is written to dest (must be at least max_len+1 bytes).
static void env_sanitize_name(char* dest, size_t max_len, const char* name) {
    if (!name) {
        dest[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i < max_len && name[i] != '\0') {
        char c = name[i];
        if (isalnum((unsigned char)c) || c == '_' || c == '-') {
            dest[i] = c;
        } else {
            dest[i] = '_';
        }
        i++;
    }
    dest[i] = '\0';
}

int e2e_env_create(const char* test_name, E2eEnv* env_out) {
    if (!env_out) return E2E_ERR_INVALID;

    // Sanitize and truncate the test name to 64 characters
    char sanitized[65];
    env_sanitize_name(sanitized, 64, test_name);

    // Get unique suffix components
    long pid = (long)get_pid();
    long counter = env_next_counter();

    // Build the directory name: cdo_e2e_<sanitized>_<pid>_<counter>
    char dirname[260];
    if (sanitized[0] != '\0') {
        snprintf(dirname, sizeof(dirname), "cdo_e2e_%s_%ld_%ld", sanitized, pid, counter);
    } else {
        snprintf(dirname, sizeof(dirname), "cdo_e2e_%ld_%ld", pid, counter);
    }

    // Join with the system temp directory
    const char* tmp_dir = env_get_temp_dir();
    char full_path[260];
    if (pal_path_join(full_path, sizeof(full_path), tmp_dir, dirname) != 0) {
        fprintf(stderr, "e2e_env_create: path too long for temp dir '%s' + '%s'\n", tmp_dir, dirname);
        return E2E_ERR_IO;
    }
    pal_path_normalize(full_path);

    // Create the directory
    if (pal_mkdir_p(full_path) != 0) {
        fprintf(stderr, "e2e_env_create: failed to create directory '%s'\n", full_path);
        return E2E_ERR_IO;
    }

    // Initialize the E2eEnv struct
    memset(env_out, 0, sizeof(E2eEnv));
    strncpy(env_out->root_path, full_path, sizeof(env_out->root_path) - 1);
    env_out->root_path[sizeof(env_out->root_path) - 1] = '\0';
    env_out->env_var_count = 0;
    env_out->keep_temps = false;
    env_out->crate_path[0] = '\0';

    return E2E_OK;
}

/// Check if a relative path would escape the root by resolving `.` and `..` components.
/// Returns true if the path escapes (depth goes negative), false if it stays within root.
static bool path_escapes_root(const char* rel_path) {
    int depth = 0;
    const char* p = rel_path;
    while (*p) {
        // Skip separators
        while (*p == '/' || *p == '\\') p++;
        if (*p == '\0') break;

        // Find end of component
        const char* end = p;
        while (*end && *end != '/' && *end != '\\') end++;

        size_t comp_len = (size_t)(end - p);
        if (comp_len == 2 && p[0] == '.' && p[1] == '.') {
            depth--;
            if (depth < 0) return true;
        } else if (comp_len == 1 && p[0] == '.') {
            // Current directory reference, no change to depth
        } else {
            depth++;
        }
        p = end;
    }
    return false;
}

/// Extract the parent directory from a full path into dest.
/// E.g. "/a/b/c.txt" -> "/a/b". If no separator found, dest becomes empty string.
static void path_parent(char* dest, size_t dest_size, const char* full_path) {
    if (!dest || dest_size == 0) return;
    dest[0] = '\0';
    if (!full_path) return;

    size_t len = strlen(full_path);
    // Walk backwards to find the last separator
    while (len > 0 && full_path[len - 1] != '/' && full_path[len - 1] != '\\') {
        len--;
    }
    // Strip the trailing separator (but keep at least one char for root paths like "/")
    if (len > 1) len--;

    if (len >= dest_size) len = dest_size - 1;
    memcpy(dest, full_path, len);
    dest[len] = '\0';
}

int e2e_env_write_file(E2eEnv* env, const char* rel_path, const void* content, size_t content_len) {
    if (!env || !rel_path) {
        fprintf(stderr, "e2e_env_write_file: NULL env or rel_path\n");
        return E2E_ERR_INVALID;
    }

    // Reject paths that would escape the environment root
    if (path_escapes_root(rel_path)) {
        fprintf(stderr, "e2e_env_write_file: path '%s' escapes environment root\n", rel_path);
        return E2E_ERR_INVALID;
    }

    // Build full path
    char full_path[260];
    if (pal_path_join(full_path, sizeof(full_path), env->root_path, rel_path) != 0) {
        fprintf(stderr, "e2e_env_write_file: path too long for '%s' + '%s'\n", env->root_path, rel_path);
        return E2E_ERR_IO;
    }
    pal_path_normalize(full_path);

    // Ensure parent directory exists
    char parent[260];
    path_parent(parent, sizeof(parent), full_path);
    if (parent[0] != '\0' && pal_path_exists(parent) != 0) {
        if (pal_mkdir_p(parent) != 0) {
            fprintf(stderr, "e2e_env_write_file: failed to create parent directory '%s'\n", parent);
            return E2E_ERR_IO;
        }
    }

    // Write file content
    if (pal_file_write(full_path, (const char*)content, content_len) != 0) {
        fprintf(stderr, "e2e_env_write_file: failed to write file '%s'\n", full_path);
        return E2E_ERR_IO;
    }

    return E2E_OK;
}

int e2e_env_mkdir(E2eEnv* env, const char* rel_path) {
    if (!env || !rel_path) {
        fprintf(stderr, "e2e_env_mkdir: NULL env or rel_path\n");
        return E2E_ERR_INVALID;
    }

    // Reject paths that would escape the environment root
    if (path_escapes_root(rel_path)) {
        fprintf(stderr, "e2e_env_mkdir: path '%s' escapes environment root\n", rel_path);
        return E2E_ERR_INVALID;
    }

    // Build full path
    char full_path[260];
    if (pal_path_join(full_path, sizeof(full_path), env->root_path, rel_path) != 0) {
        fprintf(stderr, "e2e_env_mkdir: path too long for '%s' + '%s'\n", env->root_path, rel_path);
        return E2E_ERR_IO;
    }
    pal_path_normalize(full_path);

    // Create directory (including intermediates)
    if (pal_mkdir_p(full_path) != 0) {
        fprintf(stderr, "e2e_env_mkdir: failed to create directory '%s'\n", full_path);
        return E2E_ERR_IO;
    }

    return E2E_OK;
}

int e2e_env_setvar(E2eEnv* env, const char* key, const char* value) {
    if (!env || !key || !value) return E2E_ERR_INVALID;

    // Check if key already exists — overwrite in place
    for (int i = 0; i < env->env_var_count; i++) {
        if (strcmp(env->env_vars[i].key, key) == 0) {
            strncpy(env->env_vars[i].value, value, sizeof(env->env_vars[i].value) - 1);
            env->env_vars[i].value[sizeof(env->env_vars[i].value) - 1] = '\0';
            return E2E_OK;
        }
    }

    // New key — check capacity
    if (env->env_var_count >= E2E_ENV_MAX_VARS) {
        return E2E_ERR_LIMIT;
    }

    // Add new entry
    E2eEnvVar* slot = &env->env_vars[env->env_var_count];
    strncpy(slot->key, key, sizeof(slot->key) - 1);
    slot->key[sizeof(slot->key) - 1] = '\0';
    strncpy(slot->value, value, sizeof(slot->value) - 1);
    slot->value[sizeof(slot->value) - 1] = '\0';
    env->env_var_count++;

    return E2E_OK;
}

int e2e_env_destroy(E2eEnv* env) {
    if (!env) return E2E_OK;

    // If keep_temps is set, log the path and skip deletion
    if (env->keep_temps) {
        fprintf(stderr, "[e2e] keep_temps: preserving temp dir '%s'\n", env->root_path);
        return E2E_OK;
    }

    // If root_path is empty, nothing to clean up
    if (env->root_path[0] == '\0') {
        return E2E_OK;
    }

    // Recursively delete the temp directory
    if (pal_rmdir_r(env->root_path) != 0) {
        fprintf(stderr, "[e2e] warning: failed to remove temp dir '%s'\n", env->root_path);
    }

    // Clear root_path to prevent double-delete
    env->root_path[0] = '\0';

    return E2E_OK;
}

int e2e_env_set_crate_path(E2eEnv* env, const char* crate_path) {
    if (!env) return E2E_ERR_INVALID;
    if (!crate_path) return E2E_ERR_INVALID;

    size_t len = strlen(crate_path);
    if (len == 0 || len >= sizeof(env->crate_path)) return E2E_ERR_INVALID;

    strncpy(env->crate_path, crate_path, sizeof(env->crate_path) - 1);
    env->crate_path[sizeof(env->crate_path) - 1] = '\0';
    pal_path_normalize(env->crate_path);

    return E2E_OK;
}
