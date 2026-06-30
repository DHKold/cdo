#ifndef CDO_COMMANDS_CMD_NEW_H
#define CDO_COMMANDS_CMD_NEW_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Create a new project from a template in a new directory.
/// Extracts positional NAME (required), positional TEMPLATE (optional).
/// Supports --list to display available templates.
/// Refuses to create in a non-empty directory unless --force is provided.
int cmd_new(const CliParseResult* result, void* ctx);

/// Initialize a new project from a template in the current directory.
/// Extracts --venv (bool), positional template (optional).
int cmd_init(const CliParseResult* result, void* ctx);

/// Initialize a virtual environment in the given workspace root.
/// Creates .cdo/ directory structure, copies current executable,
/// and generates activation scripts.
/// If .cdo/ already exists, preserves tools/ and cache/ but
/// regenerates scripts and binary.
/// Returns 0 on success, non-zero on failure.
int venv_init(const char* workspace_root);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_NEW_H
