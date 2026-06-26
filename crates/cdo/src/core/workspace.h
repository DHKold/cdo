#ifndef CDO_CORE_WORKSPACE_H
#define CDO_CORE_WORKSPACE_H

#include <stddef.h>

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
typedef struct {
    char            name[64];
    char            path[260];      // relative to workspace root
    CrateType       type;
    int             c_standard;     // 11, 17, 23
    int             cpp_standard;   // 17, 20, 23
    int             dep_count;
    int*            dep_indices;    // indices into workspace crate array
    int             dev_dep_count;
    int*            dev_dep_indices; // indices into workspace crate array (dev-only deps)
    char**          link_libs;      // platform link libraries (e.g., "winhttp", "pthread")
    int             link_lib_count;
    char**          defines;        // crate-level defines from [build].defines
    int             define_count;
} Crate;

// --- Workspace ---
typedef struct {
    char            root_path[260];
    int             crate_count;
    Crate*          crates;
    int*            build_order;    // indices into crates[]
    int             build_order_count;
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

/// Free all workspace resources (crates array, dep_indices, build_order).
void workspace_free(Workspace* ws);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_WORKSPACE_H
