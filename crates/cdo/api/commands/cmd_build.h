#ifndef CDO_COMMANDS_CMD_BUILD_H
#define CDO_COMMANDS_CMD_BUILD_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the build command (new CLI framework signature).
/// Builds all crates (dependency order) when no positional args are provided,
/// or builds specified crates + transitive deps when names are given.
/// Extracts --release, --profile, --jobs, --no-cache, and positional crate names
/// from the CliParseResult.
/// Returns 0 on success, non-zero on failure.
int cmd_build(const CliParseResult* result, void* ctx);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_BUILD_H
