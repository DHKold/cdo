#ifndef CDO_CMD_BUILD_INTERNAL_H
#define CDO_CMD_BUILD_INTERNAL_H

#include "commands/cmd_build.h"
#include "core/workspace.h"
#include "core/compiler.h"
#include "core/output.h"
#include "core/cli.h"
#include "pal/pal.h"

#include <stdbool.h>

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Build Profile
// ---------------------------------------------------------------------------

/// Maximum number of defines or extra flags a profile can hold.
#define BUILD_PROFILE_MAX_DEFINES 32
#define BUILD_PROFILE_MAX_FLAGS   32

/// Build profile loaded from workspace manifest [workspace.profiles.<name>].
typedef struct {
    bool        optimize;
    bool        debug_info;
    char*       defines[BUILD_PROFILE_MAX_DEFINES];
    int         define_count;
    char*       extra_flags[BUILD_PROFILE_MAX_FLAGS];
    int         extra_flag_count;
    bool        loaded;   // true if profile was found and loaded from manifest
} BuildProfile;

/// Free heap-allocated strings within a BuildProfile.
void build_profile_free(BuildProfile* p);

/// Load a build profile by name from the workspace manifest.
/// Reads [workspace.profiles.<profile_name>] from the cdo.toml at ws_root.
/// If the profile section is not found, falls back to built-in defaults:
///   "debug"   -> optimize=false, debug=true,  defines=["DEBUG"]
///   "release" -> optimize=true,  debug=false, defines=["NDEBUG"]
/// Returns 0 on success, non-zero on read/parse failure (profile defaults still set).
int build_profile_load(const char* ws_root, const char* profile_name,
                       BuildProfile* out);

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

/// Determine the build directory path for a crate within the workspace.
/// Format: <ws_root>/build/<profile>/<crate_name>/
void build_dir_for_crate(const Workspace* ws, const Crate* crate,
                         const char* profile, char* out, size_t out_size);

/// Determine the output artifact path for a crate.
void output_path_for_crate(const Workspace* ws, const Crate* crate,
                           const char* profile, char* out, size_t out_size);

/// Replace the file extension of a path with .o and place in build_dir.
void object_path_from_source(const char* source, const char* build_dir,
                             char* out, size_t out_size);

// ---------------------------------------------------------------------------
// Misc utilities
// ---------------------------------------------------------------------------

/// Determine parallelism level from options.
int resolve_jobs(const CdoOptions* opts);

/// Copy catalog TOML files from {workspace_root}/catalogs/ to
/// {build_dir}/catalogs/ so that the built binary can find built-in catalogs
/// relative to its output directory.
/// Returns the number of files copied, or -1 on error.
int deploy_catalog_files(const char* ws_root, const char* build_dir);

// ---------------------------------------------------------------------------
// Legacy crate build (crates without module directories)
// ---------------------------------------------------------------------------

/// Context for building a legacy (non-module) crate, passed to build_legacy_crate().
typedef struct {
    const Workspace*   ws;
    Crate*             crate;
    const CompilerInfo* compiler;
    const char*        profile;
    const BuildProfile* build_prof;
    int                jobs;
    const char**       coverage_flags;
    int                coverage_flag_count;
    ProgressBar*       progress;
    int*               completed_units;
    const char*        crate_full_path;
    const char*        build_dir;
    const char**       dep_include_paths;
    int                dep_include_count;
    const char**       dep_lib_paths;
    int                dep_lib_count;
    const char**       dep_link_libs;
    int                dep_link_lib_count;
} LegacyBuildCtx;

/// Build a legacy (src/-based, non-module) crate.
/// Returns 0 on success, 1 on failure, or -1 if the crate should be skipped (no sources).
int build_legacy_crate(const LegacyBuildCtx* ctx);

// ---------------------------------------------------------------------------
// Module build functions (split across cmd_build_*.c)
// ---------------------------------------------------------------------------

/// Build the Shared_Library_Module for a crate.
int build_shared_library_module(const Workspace* ws, Crate* crate,
                                const CompilerInfo* compiler,
                                const char* profile,
                                const BuildProfile* build_prof,
                                int jobs,
                                const char** coverage_flags,
                                int coverage_flag_count,
                                ProgressBar* progress,
                                int* completed_units);

/// Build the Library_Module for a crate.
int build_library_module(const Workspace* ws, Crate* crate,
                         const CompilerInfo* compiler,
                         const char* profile,
                         const BuildProfile* build_prof,
                         int jobs,
                         const char** coverage_flags,
                         int coverage_flag_count,
                         ProgressBar* progress,
                         int* completed_units);

/// Build the Executable_Module for a crate.
int build_executable_module(const Workspace* ws, Crate* crate,
                            const CompilerInfo* compiler,
                            const char* profile,
                            const BuildProfile* build_prof,
                            int jobs,
                            const char** coverage_flags,
                            int coverage_flag_count,
                            ProgressBar* progress,
                            int* completed_units);

/// Build the Test_Module for a crate.
int build_test_module(const Workspace* ws, Crate* crate,
                      const CompilerInfo* compiler,
                      const char* profile,
                      const BuildProfile* build_prof,
                      int jobs,
                      const char** coverage_flags,
                      int coverage_flag_count,
                      ProgressBar* progress,
                      int* completed_units);

/// Build (copy) the Resource_Module for a crate.
/// Performs incremental copy from res/ to build/<profile>/<crate>/res/.
/// Removes stale files not present in source.
int build_resource_module(const Workspace* ws, Crate* crate,
                          const char* profile,
                          ProgressBar* progress,
                          int* completed_units);

/// Build (compile) the Shader_Module for a crate using DXC.
/// Performs incremental compilation of .hlsl → .dxil.
int build_shader_module(const Workspace* ws, Crate* crate,
                        const char* profile,
                        const BuildProfile* build_prof,
                        bool force,
                        ProgressBar* progress,
                        int* completed_units);

/// Propagate dependency module outputs (res, shd, dyn) into the
/// dependent crate's build directory. Detects conflicts.
int propagate_dep_modules(const Workspace* ws, Crate* crate,
                          const char* profile);

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// Inline helpers shared across all cmd_build_*.c translation units
// ---------------------------------------------------------------------------

/// Convert integer C standard to string flag (e.g., 17 -> "c17").
static inline const char* c_standard_str(int std_val) {
    switch (std_val) {
        case 11: return "c11";
        case 17: return "c17";
        case 23: return "c23";
        default: return "c17";
    }
}

/// Convert integer C++ standard to string flag (e.g., 20 -> "c++20").
static inline const char* cpp_standard_str(int std_val) {
    switch (std_val) {
        case 17: return "c++17";
        case 20: return "c++20";
        case 23: return "c++23";
        default: return "c++20";
    }
}

/// Determine the active profile name from options.
static inline const char* resolve_profile(const CdoOptions* opts) {
    if (opts->profile && opts->profile[0] != '\0') {
        return opts->profile;
    }
    if (opts->release) {
        return "release";
    }
    return "debug";
}

/// Check if a source file is a C++ file based on extension.
static inline bool is_cpp_source(const char* path) {
    const char* ext = pal_path_ext(path);
    if (!ext) return false;
    return (strcmp(ext, ".cpp") == 0 || strcmp(ext, ".cxx") == 0 ||
            strcmp(ext, ".cc") == 0  || strcmp(ext, ".CPP") == 0);
}

#endif // CDO_CMD_BUILD_INTERNAL_H
