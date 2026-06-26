#ifndef CDO_COMMANDS_CMD_CLEAN_H
#define CDO_COMMANDS_CMD_CLEAN_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the clean command.
/// - If positional_count > 0: delete build/<crate_name>/ for each named crate.
/// - Otherwise: delete the entire build/ directory.
/// Returns 0 on success, non-zero on failure.
int cmd_clean(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_CLEAN_H
