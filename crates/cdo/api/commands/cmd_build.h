#ifndef CDO_COMMANDS_CMD_BUILD_H
#define CDO_COMMANDS_CMD_BUILD_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the build command.
/// Builds all crates (dependency order) when no positional args are provided,
/// or builds specified crates + transitive deps when names are given.
/// Respects --release, --profile, and --jobs flags from opts.
/// Returns 0 on success, non-zero on failure.
int cmd_build(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_BUILD_H
