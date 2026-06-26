#ifndef CDO_COMMANDS_CMD_DOCTOR_H
#define CDO_COMMANDS_CMD_DOCTOR_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the doctor command.
/// Runs a series of environment health checks:
///   1. C/C++ compiler on PATH
///   2. Workspace manifest (cdo.toml) is syntactically valid
///   3. Crate manifests (crate.toml) for each crate are valid
///   4. Dependencies resolved and lock file present
///   5. Declared tools installed in .cdo/tools/
///
/// Prints color-coded [PASS]/[WARN]/[FAIL] per check.
/// If --fix is passed as a positional argument, attempts auto-repair:
///   - Regenerate lock file
///   - Install missing tools
///
/// Returns 0 if all checks pass, non-zero if any check fails.
int cmd_doctor(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_DOCTOR_H
