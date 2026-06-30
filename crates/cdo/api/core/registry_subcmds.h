/**
 * registry_subcmds.h - Subcommand registration for parent commands.
 *
 * Registers subcommands under: deps, catalog, cache, tool.
 * Called after root commands are registered (task 3.2) to attach subcommands
 * with their specific options and positional specs.
 */
#ifndef CDO_CORE_REGISTRY_SUBCMDS_H
#define CDO_CORE_REGISTRY_SUBCMDS_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Register all subcommands on parent commands that have them.
/// Must be called after root commands (deps, catalog, cache, tool) are registered.
/// Returns 0 on success, non-zero if any registration fails.
int cdo_register_subcommands(CliCmdRegistry* reg);

#ifdef __cplusplus
}
#endif

#endif /* CDO_CORE_REGISTRY_SUBCMDS_H */
