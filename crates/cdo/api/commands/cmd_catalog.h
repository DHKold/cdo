#ifndef CDO_COMMANDS_CMD_CATALOG_H
#define CDO_COMMANDS_CMD_CATALOG_H

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the catalog list subcommand.
/// Reads --tools (bool) and --packages (bool) from CliParseResult.
/// Returns 0 on success, non-zero on failure.
int cmd_catalog_list(const CliParseResult* result, void* ctx);

/// Execute the catalog search subcommand.
/// Reads positional query from CliParseResult.
/// Returns 0 on success, non-zero on failure.
int cmd_catalog_search(const CliParseResult* result, void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* CDO_COMMANDS_CMD_CATALOG_H */
