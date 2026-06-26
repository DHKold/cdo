#ifndef CDO_CORE_MODULE_H
#define CDO_CORE_MODULE_H

#include <stdbool.h>
#include "scanner.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Module Kinds ---
typedef enum {
    MODULE_LIB,     // lib/ -> static library (.a / .lib)
    MODULE_EXE,     // exe/ -> executable
    MODULE_DYN,     // dyn/ -> shared library (.dll / .so)
    MODULE_TST,     // tst/ -> test executable
    MODULE_API,     // api/ -> header-only (no compilation)
} ModuleKind;

#define MODULE_KIND_COUNT 5

// --- Module ---
typedef struct {
    ModuleKind      kind;
    char            dir_path[260];      // absolute path to module directory
    FileList        sources;            // discovered .c/.cpp files (empty for API)
    char            artifact_path[260]; // output artifact path
    bool            present;            // true if directory exists
} Module;

// Forward declaration to avoid circular include with workspace.h
struct Workspace;

/// Return a human-readable string for the given module kind
/// (e.g., "lib", "exe", "dyn", "tst", "api").
const char* module_kind_to_string(ModuleKind kind);

/// Return the platform-appropriate artifact file extension for the given
/// module kind (e.g., ".a"/".lib", ".exe"/"", ".dll"/".so").
/// Returns NULL for MODULE_API (no artifact produced).
const char* module_artifact_extension(ModuleKind kind);

/// Compute the full artifact filename for a crate module.
/// Writes the result into `buf` (max `buf_size` bytes).
/// Example: crate_name="mycrate", kind=MODULE_LIB -> "libmycrate.a" (Unix)
///          or "mycrate.lib" (Windows).
/// Returns 0 on success, non-zero if buffer too small or kind is MODULE_API.
int module_artifact_name(const char* crate_name, ModuleKind kind,
                         char* buf, int buf_size);

/// Compute the full artifact output path for a crate module:
///   build/<profile>/<crate_name>/<artifact_filename>
/// The artifact lives at the crate level, NOT inside the module subfolder.
/// Also creates both the crate-level output directory and the module's
/// object-file subdirectory (build/<profile>/<crate_name>/<module_kind>/).
///
/// @param ws_root      Workspace root path
/// @param crate_name   Name of the crate
/// @param kind         Module kind (MODULE_LIB, MODULE_EXE, MODULE_DYN, MODULE_TST)
/// @param profile      Build profile name (e.g., "debug", "release")
/// @param out_artifact_path  Buffer for the computed artifact path (at crate level)
/// @param artifact_path_size Size of out_artifact_path buffer
/// @param out_obj_dir  Buffer for the computed object-file directory (module subfolder)
/// @param obj_dir_size Size of out_obj_dir buffer
/// @return 0 on success, non-zero on error (MODULE_API, buffer too small, or mkdir failure)
int module_compute_artifact_path(const char* ws_root, const char* crate_name,
                                 ModuleKind kind, const char* profile,
                                 char* out_artifact_path, int artifact_path_size,
                                 char* out_obj_dir, int obj_dir_size);

/// Compute include paths for a given module, considering implicit
/// dependencies on the crate's lib/ and api/ modules.
/// Returns allocated array of include path strings (caller frees).
/// Returns 0 on success, non-zero on error.
int module_include_paths(const Crate* crate, ModuleKind kind,
                         const struct Workspace* ws, char*** paths, int* count);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_MODULE_H
