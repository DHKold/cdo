/**
 * bundle.h - Shared runtime bundling utility.
 *
 * Collects all runtime artifacts for an executable crate (exe, DLLs, res/, shd/)
 * into a staging directory. Used by both `cdo run` and `cdo install`.
 */
#ifndef CDO_COMMANDS_BUNDLE_H
#define CDO_COMMANDS_BUNDLE_H

#include "model/workspace.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Options controlling how resources and shaders are placed in the bundle.
typedef struct {
    const char* resource_base;  // Base folder for resources relative to staging dir (NULL or "." = staging root)
    const char* shader_base;    // Base folder for shaders relative to staging dir (NULL or "." = staging root)
} BundleOpts;

/// Prepare a runtime bundle: clear staging_dir, then copy exe, DLLs, res/, shd/.
///
/// Resources are placed at <staging_dir>/<resource_base>/... (contents of res/ flattened).
/// Shaders are placed at <staging_dir>/<shader_base>/... (contents of shd/ flattened).
/// If opts is NULL, defaults apply (resource_base=".", shader_base=".").
///
/// @param ws         Loaded workspace
/// @param crate      The executable crate to bundle
/// @param profile    Build profile name ("debug" or "release")
/// @param staging_dir  Destination directory for the bundle
/// @param exe_path   Path to the built executable binary
/// @param opts       Bundle options (NULL for defaults)
/// @return 0 on success, non-zero on error
int bundle_prepare(const Workspace* ws, const Crate* crate, const char* profile, const char* staging_dir, const char* exe_path, const BundleOpts* opts);

/// Recursively copy a directory tree into the destination, preserving structure.
/// @return 0 on success, non-zero on error
int bundle_copy_dir_recursive(const char* src_dir, const char* dst_dir);

/// Select which executable crate to operate on.
/// If positional_count > 0, looks up by name. Otherwise auto-selects the single exe crate.
/// @return Pointer to crate or NULL on error (logs error message).
const Crate* bundle_select_exe_crate(const Workspace* ws, const char* const* positional_values, int positional_count);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_BUNDLE_H
