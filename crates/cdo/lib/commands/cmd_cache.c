#include "commands/cmd_cache.h"
#include "core/cache.h"
#include "model/workspace.h"
#include "core/output.h"
#include "pal/pal.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

int cmd_cache(const CdoOptions* opts) {
    if (!opts) return 1;

    if (opts->positional_count < 1) {
        cdo_error("Usage: cdo cache <stats|clear>");
        return 1;
    }

    const char* subcmd = opts->positional_args[0];

    // Load workspace to get cache config
    char cwd[260];
#ifdef _WIN32
    if (_getcwd(cwd, sizeof(cwd)) == NULL) {
#else
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
#endif
        cdo_error("failed to determine current working directory");
        return 1;
    }

    Workspace ws = {0};
    if (workspace_load(cwd, &ws) != 0) {
        cdo_error("failed to load workspace");
        return 1;
    }

    if (cache_init(&ws.cache_config, ws.root_path) != 0) {
        cdo_error("failed to initialize cache");
        workspace_free(&ws);
        return 1;
    }

    int result = 0;

    if (strcmp(subcmd, "stats") == 0) {
        // External backend: defer to the external tool's stats command
        if (strcmp(ws.cache_config.backend, "builtin") != 0) {
            if (strcmp(ws.cache_config.backend, "ccache") == 0) {
                cdo_info("External cache (ccache) in use. Run 'ccache -s' for stats.");
            } else if (strcmp(ws.cache_config.backend, "sccache") == 0) {
                cdo_info("External cache (sccache) in use. Run 'sccache --show-stats' for stats.");
            } else {
                cdo_info("External cache (%s) in use. Check its documentation for stats.", ws.cache_config.backend);
            }
            workspace_free(&ws);
            return 0;
        }

        int64_t total_size = 0;
        int entry_count = 0;
        uint64_t oldest_mtime = 0;

        if (cache_get_stats(&ws.cache_config, &total_size, &entry_count, &oldest_mtime) != 0) {
            cdo_error("failed to read cache stats");
            workspace_free(&ws);
            return 1;
        }

        // Format size with appropriate unit
        if (total_size >= 1073741824LL) {
            cdo_info("Cache size: %.2f GB (%d entries)", (double)total_size / 1073741824.0, entry_count);
        } else if (total_size >= 1048576LL) {
            cdo_info("Cache size: %.2f MB (%d entries)", (double)total_size / 1048576.0, entry_count);
        } else if (total_size >= 1024LL) {
            cdo_info("Cache size: %.2f KB (%d entries)", (double)total_size / 1024.0, entry_count);
        } else {
            cdo_info("Cache size: %lld bytes (%d entries)", (long long)total_size, entry_count);
        }

        // Print oldest entry age if there are entries
        if (entry_count > 0 && oldest_mtime > 0) {
            uint64_t now = (uint64_t)time(NULL);
            uint64_t age_seconds = (now > oldest_mtime) ? (now - oldest_mtime) : 0;
            if (age_seconds >= 86400) {
                cdo_info("Oldest entry: %llu days ago", (unsigned long long)(age_seconds / 86400));
            } else if (age_seconds >= 3600) {
                cdo_info("Oldest entry: %llu hours ago", (unsigned long long)(age_seconds / 3600));
            } else {
                cdo_info("Oldest entry: %llu minutes ago", (unsigned long long)(age_seconds / 60));
            }
        }

        cdo_info("Cache path: %s", ws.cache_config.path);

    } else if (strcmp(subcmd, "clear") == 0) {
        if (cache_clear(&ws.cache_config) != 0) {
            cdo_error("failed to clear cache");
            result = 1;
        }

    } else {
        cdo_error("Unknown cache subcommand: '%s'", subcmd);
        cdo_error("Usage: cdo cache <stats|clear>");
        result = 1;
    }

    workspace_free(&ws);
    return result;
}
