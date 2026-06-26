#ifndef CDO_COMMANDS_CMD_CATALOG_H
#define CDO_COMMANDS_CMD_CATALOG_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the catalog command.
/// Subcommands: list [--tools|--packages], search <query>
/// Returns 0 on success, non-zero on failure.
int cmd_catalog(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif /* CDO_COMMANDS_CMD_CATALOG_H */
