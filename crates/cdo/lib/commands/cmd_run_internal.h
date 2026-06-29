#ifndef CDO_CMD_RUN_INTERNAL_H
#define CDO_CMD_RUN_INTERNAL_H

#include "commands/cmd_run.h"
#include "model/workspace.h"
#include "core/cli.h"
#include "pal/pal.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Select which crate to run. Returns pointer to crate or NULL on error.
/// If opts has positional_args[0], uses that; otherwise auto-selects.
const Crate* run_select_crate(const Workspace* ws, const CdoOptions* opts);

/// Prepare staging folder: clear prior contents, copy exe, DLLs, res/, shd/.
/// staging_dir: path to .cdo/<crate>/run/
/// exe_path: path to built executable in build/<profile>/<crate>/<exe>
/// Returns 0 on success, non-zero on error.
int run_prepare_staging(const Workspace* ws, const Crate* crate, const char* profile, const char* staging_dir, const char* exe_path);

/// Recursively copy a directory tree into the destination, preserving structure.
/// Returns 0 on success, non-zero on error.
int run_copy_dir_recursive(const char* src_dir, const char* dst_dir);

#ifdef __cplusplus
}
#endif

#endif // CDO_CMD_RUN_INTERNAL_H
