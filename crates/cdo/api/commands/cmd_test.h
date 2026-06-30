#ifndef CDO_COMMANDS_CMD_TEST_H
#define CDO_COMMANDS_CMD_TEST_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the test command (new CLI framework handler).
/// Extracts --coverage, --list, --filter, --release, --jobs, positional crate names
/// from CliParseResult. Builds and runs test crates accordingly.
/// Returns 0 if all tests passed, non-zero if any failed or could not build.
int cmd_test(const CliParseResult* result, void* ctx);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_TEST_H
