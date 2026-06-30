#ifndef CDO_COMMANDS_CMD_TOOL_H
#define CDO_COMMANDS_CMD_TOOL_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the tool install subcommand.
/// Downloads, caches, and extracts a tool.
/// Reads positional tool name from CliParseResult.
/// Returns 0 on success, non-zero on failure.
int cmd_tool_install(const CliParseResult* result, void* ctx);

/// Execute the tool list subcommand.
/// Lists installed tools.
/// No specific args needed.
/// Returns 0 on success, non-zero on failure.
int cmd_tool_list(const CliParseResult* result, void* ctx);

/// Execute the tool remove subcommand.
/// Removes an installed tool.
/// Reads positional tool name from CliParseResult.
/// Returns 0 on success, non-zero on failure.
int cmd_tool_remove(const CliParseResult* result, void* ctx);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_TOOL_H
