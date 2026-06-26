#ifndef CDO_COMMANDS_CMD_DEPS_H
#define CDO_COMMANDS_CMD_DEPS_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the add command.
/// For each package name in positional_args:
///   - Searches configured registries for the package
///   - Downloads to local cache
///   - Adds to the crate manifest [dependencies] section
///   - Regenerates the lock file
/// Returns 0 on success, non-zero on failure.
int cmd_add(const CdoOptions* opts);

/// Execute the remove command.
/// For each package name in positional_args:
///   - Removes the dependency from the crate manifest
///   - Regenerates the lock file
/// Returns 0 on success, non-zero on failure.
int cmd_remove(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_DEPS_H
