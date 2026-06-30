#ifndef CDO_COMMANDS_CMD_DOCTOR_H
#define CDO_COMMANDS_CMD_DOCTOR_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the doctor command (new CLI framework handler).
/// Checks environment health: compiler, tools, catalog, workspace.
/// Positional args can specify subset of checks to run.
/// Returns 0 if all checks pass, non-zero if any issue detected.
int cmd_doctor(const CliParseResult* result, void* ctx);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_DOCTOR_H
