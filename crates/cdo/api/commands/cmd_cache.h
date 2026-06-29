#ifndef CDO_COMMANDS_CMD_CACHE_H
#define CDO_COMMANDS_CMD_CACHE_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the cache command.
/// Subcommands (passed as positional args):
///   stats  — display cache size, entry count, oldest entry
///   clear  — remove all cached object files and report freed space
/// Returns 0 on success, non-zero on failure.
int cmd_cache(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_CACHE_H
