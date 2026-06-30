#ifndef CDO_COMMANDS_CMD_DEPS_H
#define CDO_COMMANDS_CMD_DEPS_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the deps add subcommand.
/// Adds a dependency to the crate manifest.
/// Reads positional package name, --dev (bool), --version (string) from CliParseResult.
/// Returns 0 on success, non-zero on failure.
int cmd_deps_add(const CliParseResult* result, void* ctx);

/// Execute the deps remove subcommand.
/// Removes a dependency from the crate manifest.
/// Reads positional package name from CliParseResult.
/// Returns 0 on success, non-zero on failure.
int cmd_deps_remove(const CliParseResult* result, void* ctx);

/// Execute the deps list subcommand.
/// Lists all dependencies with scope labels.
/// No specific args needed.
/// Returns 0 on success, non-zero on failure.
int cmd_deps_list(const CliParseResult* result, void* ctx);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_DEPS_H
