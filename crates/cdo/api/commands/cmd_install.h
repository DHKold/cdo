/**
 * cmd_install.h - Install/uninstall commands for system-wide binary installation.
 *
 * Builds a release binary with all runtime dependencies and installs it as a
 * self-contained app bundle to ~/.cdo/apps/ with a launcher in ~/.cdo/bin/.
 */
#ifndef CDO_COMMANDS_CMD_INSTALL_H
#define CDO_COMMANDS_CMD_INSTALL_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the install command.
/// Builds the crate in release mode, bundles runtime artifacts, and installs
/// the complete app bundle to the target directory with a launcher on PATH.
/// Returns 0 on success, non-zero on failure.
int cmd_install(const CliParseResult* result, void* ctx);

/// Execute the uninstall command.
/// Removes a previously installed app bundle and its launcher.
/// Returns 0 on success, non-zero on failure.
int cmd_uninstall(const CliParseResult* result, void* ctx);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_INSTALL_H
