#include "commands/cmd_clean.h"
#include "core/cache.h"
#include "core/handler_ctx.h"
#include "model/workspace.h"
#include "core/log.h"
#include "pal/pal.h"
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define get_cwd _getcwd
#else
#include <unistd.h>
#define get_cwd getcwd
#endif

#define BUILD_DIR "build"

// ---------------------------------------------------------------------------
// Argument extraction helpers (match main_new.cpp pattern)
// ---------------------------------------------------------------------------

/// Find a named argument in the parse result. Returns NULL if not found.
static const CliArgValue* find_arg(const CliParseResult* result, const char* name) {
    for (int i = 0; i < result->arg_value_count; i++) {
        if (result->arg_values[i].name && strcmp(result->arg_values[i].name, name) == 0) {
            return &result->arg_values[i];
        }
    }
    return NULL;
}

/// Get a bool argument value. Returns false if not present.
static bool get_arg_bool(const CliParseResult* result, const char* name) {
    const CliArgValue* v = find_arg(result, name);
    return (v && v->present && v->type == CLI_ARG_BOOL) ? v->value.bool_val : false;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Attempt to remove a directory. Returns 0 on success, 1 on failure.
/// Prints "Nothing to clean" if the path does not exist.
static int clean_path(const char* path) {
    int exists = pal_path_exists(path);
    if (exists != 0) {
        cdo_log_info("Nothing to clean");
        return 0;
    }

    int rc = pal_rmdir_r(path);
    if (rc != PAL_OK) {
        cdo_log_error("Failed to clean '%s'", path);
        return 1;
    }

    cdo_log_info("Cleaned '%s'", path);
    return 0;
}

/// Clear the build cache. Loads workspace to get cache config, then calls cache_clear.
static int clean_cache(void) {
    char cwd[260];
    if (get_cwd(cwd, sizeof(cwd)) == NULL) {
        cdo_log_error("Failed to get current working directory");
        return 1;
    }

    Workspace ws = {0};
    if (workspace_load(cwd, &ws) == 0) {
        if (ws.cache_config.enabled) {
            cache_init(&ws.cache_config, ws.root_path);
            int rc = cache_clear(&ws.cache_config);
            if (rc == 0) {
                cdo_log_info("Cleared build cache");
            } else {
                cdo_log_warn("Failed to clear build cache");
            }
        } else {
            cdo_log_info("Cache is disabled, nothing to clear");
        }
        workspace_free(&ws);
    } else {
        // Fallback: try the default cache path
        CacheConfig default_config = {0};
        strncpy(default_config.path, ".cdo/cache/objects", sizeof(default_config.path) - 1);
        default_config.max_size_bytes = 2147483648LL;
        default_config.enabled = true;
        strncpy(default_config.backend, "builtin", sizeof(default_config.backend) - 1);
        cache_init(&default_config, ".");
        int rc = cache_clear(&default_config);
        if (rc == 0) {
            cdo_log_info("Cleared build cache");
        } else {
            cdo_log_warn("Failed to clear build cache");
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// New CLI framework handler
// ---------------------------------------------------------------------------

int cmd_clean(const CliParseResult* result, void* ctx) {
    (void)ctx; // CdoHandlerCtx* â€” not used directly yet (output goes through global)

    // Extract args from CliParseResult
    bool cache_flag = get_arg_bool(result, "cache");

    int overall_result = 0;

    if (result->positional_count > 0) {
        // Clean specific crate(s)
        for (int i = 0; i < result->positional_count; i++) {
            char path[1024];
            int join_rc = pal_path_join(path, sizeof(path), BUILD_DIR, result->positional_values[i]);
            if (join_rc != 0) {
                cdo_log_error("Path too long for crate '%s'", result->positional_values[i]);
                return 1;
            }
            int rc = clean_path(path);
            if (rc != 0) {
                return rc;
            }
        }
    } else {
        // Clean entire build directory
        overall_result = clean_path(BUILD_DIR);
    }

    // If --cache flag present, also clear the build cache
    if (cache_flag) {
        int cache_rc = clean_cache();
        if (cache_rc != 0 && overall_result == 0) {
            overall_result = cache_rc;
        }
    }

    return overall_result;
}

// ---------------------------------------------------------------------------
// End of cmd_clean.c
