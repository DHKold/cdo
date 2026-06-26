#ifndef CDO_COMMANDS_CMD_DEPS_H
#define CDO_COMMANDS_CMD_DEPS_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the deps command group.
/// Dispatches to subcommands: add, remove, list.
///   cdo deps add <name> [--dev]     Add a dependency
///   cdo deps remove <name> [--dev]  Remove a dependency
///   cdo deps list                   List all dependencies with scope labels
/// Returns 0 on success, non-zero on failure.
int cmd_deps(const CdoOptions* opts);

/// Execute the add command (legacy: cdo add <package>).
/// For each package name in positional_args:
///   - Searches configured registries for the package
///   - Downloads to local cache
///   - Adds to the crate manifest [dependencies] section
///   - Regenerates the lock file
/// Returns 0 on success, non-zero on failure.
int cmd_add(const CdoOptions* opts);

/// Execute the remove command (legacy: cdo remove <package>).
/// For each package name in positional_args:
///   - Removes the dependency from the crate manifest
///   - Regenerates the lock file
/// Returns 0 on success, non-zero on failure.
int cmd_remove(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_DEPS_H
