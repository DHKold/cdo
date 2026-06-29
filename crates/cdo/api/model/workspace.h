#ifndef CDO_MODEL_WORKSPACE_H
#define CDO_MODEL_WORKSPACE_H

#include <stdbool.h>
#include <stddef.h>
#include "model/module.h"
#include "model/cache_config.h"
#include "model/fmt_settings.h"
#include "model/hooks.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Crate Types ---
typedef enum {
    CRATE_EXECUTABLE,
    CRATE_STATIC_LIB,
    CRATE_SHARED_LIB,
    CRATE_TEST,
} CrateType;

// --- Crate ---
typedef struct Crate {
    char            name[64];
    char            path[260];      // relative to workspace root
    CrateType       type;           // DEPRECATED: ignored, kept for backward compat. Use modules[] instead.
    int             c_standard;     // 11, 17, 23
    int             cpp_standard;   // 17, 20, 23

    // Module layout (replaces CrateType)
    Module          modules[MODULE_KIND_COUNT];     // indexed by ModuleKind
    int             module_count;   // number of present modules (1-7)
    bool            has_lib;        // shortcut: modules[MODULE_LIB].present
    bool            has_api;        // shortcut: modules[MODULE_API].present
    bool            has_res;        // shortcut: modules[MODULE_RES].present
    bool            has_shd;        // shortcut: modules[MODULE_SHD].present

    // Dependencies (unchanged)
    int             dep_count;
    int*            dep_indices;    // indices into workspace crate array
    int             dev_dep_count;
    int*            dev_dep_indices; // indices into workspace crate array (dev-only deps)
    char**          link_libs;      // platform link libraries (e.g., "winhttp", "pthread")
    int             link_lib_count;
    char**          defines;        // crate-level defines from [build].defines
    int             define_count;

    // Lifecycle hooks
    HookSet         hooks;          // crate-level lifecycle hooks (parsed from [hooks])
} Crate;

// --- Workspace ---
typedef struct Workspace {
    char            root_path[260];
    int             crate_count;
    Crate*          crates;
    int*            build_order;    // indices into crates[]
    int             build_order_count;
    CacheConfig     cache_config;   // Build cache configuration (parsed from [workspace.settings.cache])
    FmtSettings     format_settings; // Format settings (parsed from [workspace.settings.format])
    HookSet         ws_hooks;       // Workspace-level lifecycle hooks (parsed from [workspace.hooks])
} Workspace;

/// Load workspace from cdo.toml (or cdo.yaml / cdo.json fallback) at the
/// given root directory. Discovers crates, parses their manifests, and
/// populates the Workspace struct.
/// Returns 0 on success, non-zero on error.
int workspace_load(const char* root_path, Workspace* ws);

/// Resolve the build order for all or specific crates using topological sort.
/// If crate_names is NULL or count is 0, resolves all crates.
/// Returns 0 on success, non-zero on error (e.g., circular dependency).
int workspace_resolve(Workspace* ws, const char** crate_names, int count);

/// Resolve inter-crate module dependencies.
/// Validates that target crates of dependencies have a Library_Module,
/// resolves transitive dependencies, and detects cycles.
/// Returns 0 on success, non-zero on error.
int workspace_resolve_module_deps(Workspace* ws);

/// Free all workspace resources (crates array, dep_indices, build_order).
void workspace_free(Workspace* ws);

#ifdef __cplusplus
}
#endif

#endif // CDO_MODEL_WORKSPACE_H
