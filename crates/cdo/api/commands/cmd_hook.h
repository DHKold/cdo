#ifndef CDO_COMMANDS_CMD_HOOK_H
#define CDO_COMMANDS_CMD_HOOK_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the hook command.
/// Positional args (0..N): subcommand tokens (list, run <point> [<crate>]).
/// Returns 0 on success, non-zero on failure.
int cmd_hook(const CliParseResult* result, void* ctx);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_HOOK_H
