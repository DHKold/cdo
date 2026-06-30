/**
 * registry_setup.h - CLI registry creation and population.
 *
 * Creates a CliCmdRegistry populated with all cdo commands, subcommands,
 * and global options. Used by the entry point to wire declarative CLI parsing.
 */
#ifndef CDO_CORE_REGISTRY_SETUP_H
#define CDO_CORE_REGISTRY_SETUP_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Create and populate the CLI registry with all cdo commands.
/// Returns NULL on allocation failure.
CliCmdRegistry* cdo_registry_create(void);

/// Get the array of global options that must be registered on every command.
/// Sets *count to the number of options in the array.
/// Returns a pointer to a static array (do not free).
const CliArgSpec* cdo_global_options(int* count);

#ifdef __cplusplus
}
#endif

#endif /* CDO_CORE_REGISTRY_SETUP_H */
