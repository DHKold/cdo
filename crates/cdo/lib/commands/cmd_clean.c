#include "commands/cmd_clean.h"
#include "core/cache.h"
#include "model/workspace.h"
#include "core/output.h"
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

/// Attempt to remove a directory. Returns 0 on success, 1 on failure.
/// Prints "Nothing to clean" if the path does not exist.
static int clean_path(const char* path) {
    int exists = pal_path_exists(path);
    if (exists != 0) {
        cdo_info("Nothing to clean");
        return 0;
    }

    int rc = pal_rmdir_r(path);
    if (rc != PAL_OK) {
        cdo_error("Failed to clean '%s'", path);
        return 1;
    }

    cdo_info("Cleaned '%s'", path);
    return 0;
}

/// Clear the build cache. Loads workspace to get cache config, then calls cache_clear.
static int clean_cache(void) {
    char cwd[260];
    if (get_cwd(cwd, sizeof(cwd)) == NULL) {
        cdo_error("Failed to get current working directory");
        return 1;
    }

    Workspace ws = {0};
    if (workspace_load(cwd, &ws) == 0) {
        if (ws.cache_config.enabled) {
            cache_init(&ws.cache_config, ws.root_path);
            int rc = cache_clear(&ws.cache_config);
            if (rc == 0) {
                cdo_info("Cleared build cache");
            } else {
                cdo_warn("Failed to clear build cache");
            }
        } else {
            cdo_info("Cache is disabled, nothing to clear");
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
            cdo_info("Cleared build cache");
        } else {
            cdo_warn("Failed to clear build cache");
        }
    }
    return 0;
}

int cmd_clean(const CdoOptions* opts) {
    int result = 0;

    if (opts->positional_count > 0) {
        // Clean specific crate(s)
        for (int i = 0; i < opts->positional_count; i++) {
            char path[1024];
            int join_rc = pal_path_join(path, sizeof(path), BUILD_DIR, opts->positional_args[i]);
            if (join_rc != 0) {
                cdo_error("Path too long for crate '%s'", opts->positional_args[i]);
                return 1;
            }
            int rc = clean_path(path);
            if (rc != 0) {
                return rc;
            }
        }
    } else {
        // Clean entire build directory
        result = clean_path(BUILD_DIR);
    }

    // If --cache flag present, also clear the build cache
    if (opts->cache) {
        int cache_rc = clean_cache();
        if (cache_rc != 0 && result == 0) {
            result = cache_rc;
        }
    }

    return result;
}
