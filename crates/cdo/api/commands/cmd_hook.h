#ifndef CDO_COMMANDS_CMD_HOOK_H
#define CDO_COMMANDS_CMD_HOOK_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the hook command.
/// Subcommands:
///   list              — List all configured hooks
///   run <point> [<crate>] — Manually trigger a hook
/// Returns 0 on success, non-zero on failure.
int cmd_hook(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_HOOK_H
