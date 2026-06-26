#ifndef CDO_COMMANDS_CMD_TOOL_H
#define CDO_COMMANDS_CMD_TOOL_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the tool command.
/// Subcommands:
///   install <name> [--url <url>] [--version <constraint>] [--refresh]
///     - Download, cache, and extract a tool
///     - If --url is omitted, resolves from catalog
///   list                                     - List installed tools
///   remove <name>                            - Remove an installed tool
/// Returns 0 on success, non-zero on failure.
int cmd_tool(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_TOOL_H
