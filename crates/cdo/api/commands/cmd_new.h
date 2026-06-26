#ifndef CDO_COMMANDS_CMD_NEW_H
#define CDO_COMMANDS_CMD_NEW_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Create a new project from a template in a new directory.
/// Expects positional_args[0] = template name, positional_args[1] = project name (optional).
/// Supports --list to display available templates.
/// Refuses to create in a non-empty directory unless --force is provided.
int cmd_new(const CdoOptions* opts);

/// Initialize a new project from a template in the current directory.
/// Same as cmd_new but operates in-place (current working directory).
int cmd_init(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_NEW_H
