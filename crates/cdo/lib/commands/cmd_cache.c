#include "commands/cmd_cache.h"
#include "core/cache.h"
#include "core/cli_arg_access.h"
#include "core/handler_ctx.h"
#include "model/workspace.h"
#include "core/log.h"
#include "pal/pal.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

/* -------------------------------------------------------------------------- */
/* Shared helpers for cache subcommands                                        */
/* -------------------------------------------------------------------------- */

/// Load workspace and initialize cache. On failure, sets result to 1.
/// Caller must call workspace_free(&ws) on success.
static int cache_load_workspace(Workspace* ws) {
    char cwd[260];
#ifdef _WIN32
    if (_getcwd(cwd, sizeof(cwd)) == NULL) {
#else
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
#endif
        cdo_log_error("failed to determine current working directory");
        return 1;
    }

    if (workspace_load(cwd, ws) != 0) {
        cdo_log_error("failed to load workspace");
        return 1;
    }

    if (cache_init(&ws->cache_config, ws->root_path) != 0) {
        cdo_log_error("failed to initialize cache");
        workspace_free(ws);
        return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/* cache stats                                                                 */
/* -------------------------------------------------------------------------- */

int cmd_cache_stats(const CliParseResult* result, void* ctx) {
    (void)result;
    (void)ctx;

    Workspace ws = {0};
    if (cache_load_workspace(&ws) != 0) {
        return 1;
    }

    /* External backend: defer to the external tool's stats command */
    if (strcmp(ws.cache_config.backend, "builtin") != 0) {
        if (strcmp(ws.cache_config.backend, "ccache") == 0) {
            cdo_log_info("External cache (ccache) in use. Run 'ccache -s' for stats.");
        } else if (strcmp(ws.cache_config.backend, "sccache") == 0) {
            cdo_log_info("External cache (sccache) in use. Run 'sccache --show-stats' for stats.");
        } else {
            cdo_log_info("External cache (%s) in use. Check its documentation for stats.", ws.cache_config.backend);
        }
        workspace_free(&ws);
        return 0;
    }

    int64_t total_size = 0;
    int entry_count = 0;
    uint64_t oldest_mtime = 0;

    if (cache_get_stats(&ws.cache_config, &total_size, &entry_count, &oldest_mtime) != 0) {
        cdo_log_error("failed to read cache stats");
        workspace_free(&ws);
        return 1;
    }

    /* Format size with appropriate unit */
    if (total_size >= 1073741824LL) {
        cdo_log_info("Cache size: %.2f GB (%d entries)", (double)total_size / 1073741824.0, entry_count);
    } else if (total_size >= 1048576LL) {
        cdo_log_info("Cache size: %.2f MB (%d entries)", (double)total_size / 1048576.0, entry_count);
    } else if (total_size >= 1024LL) {
        cdo_log_info("Cache size: %.2f KB (%d entries)", (double)total_size / 1024.0, entry_count);
    } else {
        cdo_log_info("Cache size: %lld bytes (%d entries)", (long long)total_size, entry_count);
    }

    /* Print oldest entry age if there are entries */
    if (entry_count > 0 && oldest_mtime > 0) {
        uint64_t now = (uint64_t)time(NULL);
        uint64_t age_seconds = (now > oldest_mtime) ? (now - oldest_mtime) : 0;
        if (age_seconds >= 86400) {
            cdo_log_info("Oldest entry: %llu days ago", (unsigned long long)(age_seconds / 86400));
        } else if (age_seconds >= 3600) {
            cdo_log_info("Oldest entry: %llu hours ago", (unsigned long long)(age_seconds / 3600));
        } else {
            cdo_log_info("Oldest entry: %llu minutes ago", (unsigned long long)(age_seconds / 60));
        }
    }

    cdo_log_info("Cache path: %s", ws.cache_config.path);

    workspace_free(&ws);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* cache clear                                                                 */
/* -------------------------------------------------------------------------- */

int cmd_cache_clear(const CliParseResult* result, void* ctx) {
    (void)result;
    (void)ctx;

    Workspace ws = {0};
    if (cache_load_workspace(&ws) != 0) {
        return 1;
    }

    if (cache_clear(&ws.cache_config) != 0) {
        cdo_log_error("failed to clear cache");
        workspace_free(&ws);
        return 1;
    }

    workspace_free(&ws);
    return 0;
}

/* -------------------------------------------------------------------------- */
// End of cmd_cache.c
