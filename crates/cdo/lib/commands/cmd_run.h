#ifndef CDO_COMMANDS_CMD_RUN_H
#define CDO_COMMANDS_CMD_RUN_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the run command.
/// Builds the specified (or auto-selected) executable crate, then launches it.
/// Arguments after -- are forwarded to the spawned process.
/// Returns the exit code of the spawned process, or non-zero on build/launch failure.
int cmd_run(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_RUN_H
