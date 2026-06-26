#ifndef CDO_COMMANDS_CMD_TEST_H
#define CDO_COMMANDS_CMD_TEST_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the test command.
/// Builds and runs all test crates when no positional args are provided,
/// or builds and runs specified test crate(s) when names are given.
/// Reports build errors without executing; reports non-zero exit as failure.
/// Prints a summary of passed/failed test counts.
/// Returns 0 if all tests passed, non-zero if any failed or could not build.
int cmd_test(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_TEST_H
