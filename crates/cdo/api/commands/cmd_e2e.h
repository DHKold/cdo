/**
 * cmd_e2e.h - End-to-end test command.
 *
 * Discovers crates with MODULE_E2E, builds their e2e executables, and runs
 * them as subprocesses, parsing JSON Lines test protocol output. Supports
 * filtering, listing, profile selection, job control, and temp preservation.
 */
#ifndef CDO_COMMANDS_CMD_E2E_H
#define CDO_COMMANDS_CMD_E2E_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the e2e command.
/// Discovers crates with MODULE_E2E, builds and runs their e2e executables.
/// Supports --filter, --list, --release, --profile, --jobs, --verbose,
/// --timeout, --keep-temps, and positional crate name filtering.
/// Returns 0 if all tests passed, 1 if any failed, 2 on infrastructure error.
int cmd_e2e(const CliParseResult* result, void* ctx);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_E2E_H
