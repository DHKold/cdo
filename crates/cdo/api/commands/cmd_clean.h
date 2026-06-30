#ifndef CDO_COMMANDS_CMD_CLEAN_H
#define CDO_COMMANDS_CMD_CLEAN_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the clean command (new CLI framework handler).
/// Extracts --cache (bool) from CliParseResult.
/// Removes build artifacts; optionally clears the object cache.
/// Returns 0 on success, non-zero on failure.
int cmd_clean(const CliParseResult* result, void* ctx);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_CLEAN_H
