#ifndef CDO_COMMANDS_CMD_CACHE_H
#define CDO_COMMANDS_CMD_CACHE_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the cache stats subcommand.
/// Displays cache size, entry count, and oldest entry.
/// No specific args needed.
/// Returns 0 on success, non-zero on failure.
int cmd_cache_stats(const CliParseResult* result, void* ctx);

/// Execute the cache clear subcommand.
/// Removes all cached object files and reports freed space.
/// No specific args needed.
/// Returns 0 on success, non-zero on failure.
int cmd_cache_clear(const CliParseResult* result, void* ctx);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_CACHE_H
