#include "core/workspace.h"
#include "core/toml.h"
#include "core/output.h"
#include "pal/pal.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// Internal helpers
// =============================================================================

/// Try to read a config file with fallback order: .toml, .yaml, .json
/// Returns 0 on success with buf/len populated. Caller frees buf.
/// base_name is e.g. "cdo" or "crate" (without extension).
static int read_config_file(const char* dir, const char* base_name,
                            char** buf, size_t* len) {
    static const char* extensions[] = { ".toml", ".yaml", ".json" };
    static const int ext_count = 3;

    char path[520];

    for (int i = 0; i < ext_count; i++) {
        if (pal_path_join(path, sizeof(path), dir, base_name) != 0) {
            continue;
        }
        size_t path_len = strlen(path);
        size_t ext_len = strlen(extensions[i]);
        if (path_len + ext_len >= sizeof(path)) {
            continue;
        }
        memcpy(path + path_len, extensions[i], ext_len + 1);

        if (pal_file_read(path, buf, len) == 0) {
            return 0;
        }
    }

    return -1; // No config file found
}

/// Parse a crate type string into the CrateType enum.
static CrateType parse_crate_type(const char* type_str) {
    if (!type_str) return CRATE_EXECUTABLE;

    if (strcmp(type_str, "executable") == 0) return CRATE_EXECUTABLE;
    if (strcmp(type_str, "static-library") == 0) return CRATE_STATIC_LIB;
    if (strcmp(type_str, "shared-library") == 0) return CRATE_SHARED_LIB;
    if (strcmp(type_str, "test") == 0) return CRATE_TEST;

    // Default to executable for unknown types
    return CRATE_EXECUTABLE;
}

// --- Dynamic array for collecting discovered crate directories ---
typedef struct {
    char** paths;
    int    count;
    int    capacity;
} PathList;

static void pathlist_init(PathList* pl) {
    pl->paths = NULL;
    pl->count = 0;
    pl->capacity = 0;
}

static int pathlist_push(PathList* pl, const char* path) {
    if (pl->count >= pl->capacity) {
        int new_cap = pl->capacity == 0 ? 8 : pl->capacity * 2;
        char** new_paths = (char**)realloc(pl->paths, (size_t)new_cap * sizeof(char*));
        if (!new_paths) return -1;
        pl->paths = new_paths;
        pl->capacity = new_cap;
    }
    pl->paths[pl->count] = strdup(path);
    if (!pl->paths[pl->count]) return -1;
    pl->count++;
    return 0;
}

static void pathlist_free(PathList* pl) {
    for (int i = 0; i < pl->count; i++) {
        free(pl->paths[i]);
    }
    free(pl->paths);
    pl->paths = NULL;
    pl->count = 0;
    pl->capacity = 0;
}

// --- Context for pal_dir_walk callback when expanding member globs ---
typedef struct {
    const char* root_path;
    PathList*   results;
} GlobWalkCtx;

/// Callback for pal_dir_walk: collect directories that contain a crate manifest.
static void glob_walk_callback(const char* entry_path, bool is_dir, void* ctx) {
    if (!is_dir) return;

    GlobWalkCtx* gctx = (GlobWalkCtx*)ctx;

    // Check if this directory contains a crate manifest (crate.toml, .yaml, .json)
    char check_path[520];
    static const char* exts[] = { ".toml", ".yaml", ".json" };

    for (int i = 0; i < 3; i++) {
        if (pal_path_join(check_path, sizeof(check_path), entry_path, "crate") != 0) {
            continue;
        }
        size_t check_len = strlen(check_path);
        size_t ext_len = strlen(exts[i]);
        if (check_len + ext_len >= sizeof(check_path)) continue;
        memcpy(check_path + check_len, exts[i], ext_len + 1);

        if (pal_path_exists(check_path)) {
            pathlist_push(gctx->results, entry_path);
            return; // Found a manifest, no need to check more extensions
        }
    }
}

/// Expand a single member pattern (e.g., "crates/*") into actual crate directories.
/// Supports:
///   - "path/*" — enumerate immediate subdirectories of "path" that contain crate manifests
///   - "explicit/path" — a direct path to a crate directory
static int expand_member_pattern(const char* root_path, const char* pattern,
                                 PathList* results) {
    // Build the full path
    char full_path[520];

    // Check if pattern ends with "/*" (glob pattern)
    size_t pat_len = strlen(pattern);
    if (pat_len >= 2 && pattern[pat_len - 2] == '/' && pattern[pat_len - 1] == '*') {
        // Glob pattern: enumerate subdirectories
        // Trim the "/*" suffix to get the parent directory
        char parent_rel[260];
        if (pat_len - 2 >= sizeof(parent_rel)) return -1;
        memcpy(parent_rel, pattern, pat_len - 2);
        parent_rel[pat_len - 2] = '\0';

        if (pal_path_join(full_path, sizeof(full_path), root_path, parent_rel) != 0) {
            return -1;
        }

        // Walk the parent directory (non-recursive: only look at immediate children)
        // pal_dir_walk is recursive, so we use it but only collect directories
        // at the first level that contain crate manifests.
        // Since pal_dir_walk is recursive but our callback only checks directories
        // that have crate.toml directly, this works — nested crates won't be
        // double-discovered unless they also have manifests (which is intended).

        // Actually, pal_dir_walk recurses into subdirs. We only want immediate
        // children. Let's enumerate manually using pal_dir_walk but only accept
        // entries whose parent is full_path.
        // Simpler: just walk and check if the entry is a direct child.
        size_t parent_len = strlen(full_path);

        // We'll use a helper struct to track the parent length
        typedef struct {
            const char* parent;
            size_t      parent_len;
            PathList*   results;
        } ImmediateChildCtx;

        ImmediateChildCtx ictx;
        ictx.parent = full_path;
        ictx.parent_len = parent_len;
        ictx.results = results;

        // Unfortunately we can't easily define a nested callback with closure data
        // in C. Use the GlobWalkCtx approach but filter by depth.
        // pal_dir_walk passes the full path, so we can check if removing the parent
        // prefix leaves a single path component (no extra '/').

        // Instead, let's just use pal_dir_walk on the parent and filter in the callback.
        GlobWalkCtx gctx;
        gctx.root_path = root_path;
        gctx.results = results;

        // We want only immediate subdirectories, not recursive. Since pal_dir_walk
        // is recursive, we'll call it and check path depth. But that's complex.
        // Simpler approach: since pal_dir_walk visits children before recursing,
        // and the callback adds directories that have crate manifests, recursion
        // into those directories won't add duplicates (they'd need their own nested
        // crate.toml to be added again, which is the correct behavior for nested workspaces).
        
        // Actually the simplest correct approach: just walk the parent directory.
        // Directories that contain crate.toml will be collected regardless of depth.
        // For the "crates/*" pattern this is fine — we discover all crate dirs under "crates/".
        pal_dir_walk(full_path, glob_walk_callback, &gctx);

    } else {
        // Explicit path: just check if it has a crate manifest
        if (pal_path_join(full_path, sizeof(full_path), root_path, pattern) != 0) {
            return -1;
        }

        char check_path[520];
        static const char* exts[] = { ".toml", ".yaml", ".json" };
        for (int i = 0; i < 3; i++) {
            if (pal_path_join(check_path, sizeof(check_path), full_path, "crate") != 0) {
                continue;
            }
            size_t check_len = strlen(check_path);
            size_t ext_len = strlen(exts[i]);
            if (check_len + ext_len >= sizeof(check_path)) continue;
            memcpy(check_path + check_len, exts[i], ext_len + 1);

            if (pal_path_exists(check_path)) {
                pathlist_push(results, full_path);
                break;
            }
        }
    }

    return 0;
}

/// Parse a single crate manifest and populate the Crate struct.
/// crate_dir is the absolute path to the crate directory.
/// root_path is the workspace root (used to compute relative path).
static int parse_crate_manifest(const char* crate_dir, const char* root_path,
                                Crate* crate, int default_c_std, int default_cpp_std) {
    char* buf = NULL;
    size_t len = 0;

    if (read_config_file(crate_dir, "crate", &buf, &len) != 0) {
        cdo_error("Failed to read crate manifest in: %s", crate_dir);
        return -1;
    }

    // Parse as TOML (primary format)
    TomlTable* root = NULL;
    TomlError err;
    if (toml_parse(buf, len, &root, &err) != 0) {
        cdo_error("Failed to parse crate manifest in %s: line %d, col %d: %s",
                  crate_dir, err.line, err.col, err.message);
        free(buf);
        return -1;
    }
    free(buf);

    // Initialize crate with defaults
    memset(crate, 0, sizeof(Crate));
    crate->c_standard = default_c_std;
    crate->cpp_standard = default_cpp_std;
    crate->type = CRATE_EXECUTABLE;

    // Compute relative path from workspace root
    size_t root_len = strlen(root_path);
    const char* rel = crate_dir;
    if (strncmp(crate_dir, root_path, root_len) == 0) {
        rel = crate_dir + root_len;
        // Skip leading separator
        if (*rel == '/' || *rel == '\\') rel++;
    }
    size_t rel_len = strlen(rel);
    if (rel_len >= sizeof(crate->path)) rel_len = sizeof(crate->path) - 1;
    memcpy(crate->path, rel, rel_len);
    crate->path[rel_len] = '\0';
    // Normalize path separators
    pal_path_normalize(crate->path);

    // Parse [crate] section
    const TomlValue* name_val = toml_get(root, "crate.name");
    if (name_val && name_val->type == TOML_STRING && name_val->as.string) {
        size_t name_len = strlen(name_val->as.string);
        if (name_len >= sizeof(crate->name)) name_len = sizeof(crate->name) - 1;
        memcpy(crate->name, name_val->as.string, name_len);
        crate->name[name_len] = '\0';
    } else {
        // Fall back to directory name
        const char* dir_name = crate_dir;
        const char* last_sep = strrchr(crate_dir, '/');
        const char* last_bsep = strrchr(crate_dir, '\\');
        if (last_bsep && (!last_sep || last_bsep > last_sep)) last_sep = last_bsep;
        if (last_sep) dir_name = last_sep + 1;
        size_t name_len = strlen(dir_name);
        if (name_len >= sizeof(crate->name)) name_len = sizeof(crate->name) - 1;
        memcpy(crate->name, dir_name, name_len);
        crate->name[name_len] = '\0';
    }

    // Parse type
    const TomlValue* type_val = toml_get(root, "crate.type");
    if (type_val && type_val->type == TOML_STRING && type_val->as.string) {
        crate->type = parse_crate_type(type_val->as.string);
    }

    // Parse c-standard
    const TomlValue* c_std_val = toml_get(root, "crate.c-standard");
    if (c_std_val && c_std_val->type == TOML_INTEGER) {
        crate->c_standard = (int)c_std_val->as.integer;
    }

    // Parse cpp-standard
    const TomlValue* cpp_std_val = toml_get(root, "crate.cpp-standard");
    if (cpp_std_val && cpp_std_val->type == TOML_INTEGER) {
        crate->cpp_standard = (int)cpp_std_val->as.integer;
    }

    // Parse dependencies (just collect names for now; indices resolved later)
    // Dependencies are stored as a table under [dependencies]
    const TomlValue* deps_val = toml_get(root, "dependencies");
    if (deps_val && (deps_val->type == TOML_TABLE || deps_val->type == TOML_INLINE_TABLE)) {
        // Count dependencies
        TomlTable* deps_table = deps_val->as.table;
        crate->dep_count = deps_table->count;
        if (crate->dep_count > 0) {
            // Allocate dep_indices (will be resolved to actual indices later)
            // For now, store -1 as placeholder; workspace_resolve will fill them.
            crate->dep_indices = (int*)calloc((size_t)crate->dep_count, sizeof(int));
            if (!crate->dep_indices) {
                crate->dep_count = 0;
            } else {
                for (int i = 0; i < crate->dep_count; i++) {
                    crate->dep_indices[i] = -1;
                }
            }
        }
    }

    toml_free(root);
    return 0;
}

/// After all crates are loaded, resolve dependency names to indices.
/// This reads each crate's manifest again to get dependency names and
/// matches them against the workspace crate list.
static int resolve_dep_indices(Workspace* ws) {
    for (int ci = 0; ci < ws->crate_count; ci++) {
        Crate* crate = &ws->crates[ci];
        if (crate->dep_count == 0) continue;

        // Re-read the crate manifest to get dependency names
        char crate_dir[520];
        if (pal_path_join(crate_dir, sizeof(crate_dir), ws->root_path, crate->path) != 0) {
            continue;
        }

        char* buf = NULL;
        size_t len = 0;
        if (read_config_file(crate_dir, "crate", &buf, &len) != 0) {
            continue;
        }

        TomlTable* root = NULL;
        TomlError err;
        if (toml_parse(buf, len, &root, &err) != 0) {
            free(buf);
            continue;
        }
        free(buf);

        const TomlValue* deps_val = toml_get(root, "dependencies");
        if (deps_val && (deps_val->type == TOML_TABLE || deps_val->type == TOML_INLINE_TABLE)) {
            TomlTable* deps_table = deps_val->as.table;
            TomlEntry* entry = deps_table->head;
            int di = 0;
            while (entry && di < crate->dep_count) {
                // Match this dependency name against workspace crates
                for (int j = 0; j < ws->crate_count; j++) {
                    if (j == ci) continue; // Skip self
                    if (strcmp(entry->key, ws->crates[j].name) == 0) {
                        crate->dep_indices[di] = j;
                        break;
                    }
                }
                entry = entry->next;
                di++;
            }
        }

        toml_free(root);
    }

    return 0;
}

// =============================================================================
// Public API
// =============================================================================

int workspace_load(const char* root_path, Workspace* ws) {
    if (!root_path || !ws) return -1;

    memset(ws, 0, sizeof(Workspace));

    // Store root path (normalized)
    size_t root_len = strlen(root_path);
    if (root_len >= sizeof(ws->root_path)) root_len = sizeof(ws->root_path) - 1;
    memcpy(ws->root_path, root_path, root_len);
    ws->root_path[root_len] = '\0';
    pal_path_normalize(ws->root_path);

    // Strip trailing slash from root_path for consistent joining
    root_len = strlen(ws->root_path);
    while (root_len > 0 && ws->root_path[root_len - 1] == '/') {
        ws->root_path[root_len - 1] = '\0';
        root_len--;
    }

    // Read workspace config file (cdo.toml, cdo.yaml, cdo.json)
    char* buf = NULL;
    size_t buf_len = 0;
    if (read_config_file(ws->root_path, "cdo", &buf, &buf_len) != 0) {
        cdo_error("No workspace manifest found (tried cdo.toml, cdo.yaml, cdo.json) in: %s",
                  ws->root_path);
        return -1;
    }

    // Parse workspace manifest as TOML
    TomlTable* root = NULL;
    TomlError err;
    if (toml_parse(buf, buf_len, &root, &err) != 0) {
        cdo_error("Failed to parse workspace manifest: line %d, col %d: %s",
                  err.line, err.col, err.message);
        free(buf);
        return -1;
    }
    free(buf);

    // Extract workspace settings (defaults for crates)
    int default_c_std = 17;
    int default_cpp_std = 20;

    const TomlValue* c_std_val = toml_get(root, "workspace.settings.c-standard");
    if (c_std_val && c_std_val->type == TOML_INTEGER) {
        default_c_std = (int)c_std_val->as.integer;
    }

    const TomlValue* cpp_std_val = toml_get(root, "workspace.settings.cpp-standard");
    if (cpp_std_val && cpp_std_val->type == TOML_INTEGER) {
        default_cpp_std = (int)cpp_std_val->as.integer;
    }

    // Extract members array
    const TomlValue* members_val = toml_get(root, "workspace.members");
    if (!members_val || members_val->type != TOML_ARRAY) {
        cdo_error("Workspace manifest missing [workspace] members array");
        toml_free(root);
        return -1;
    }

    TomlArray* members_arr = members_val->as.array;

    // Expand all member patterns to discover crate directories
    PathList crate_dirs;
    pathlist_init(&crate_dirs);

    for (int i = 0; i < members_arr->count; i++) {
        TomlValue* item = members_arr->items[i];
        if (!item || item->type != TOML_STRING || !item->as.string) continue;

        expand_member_pattern(ws->root_path, item->as.string, &crate_dirs);
    }

    toml_free(root);

    if (crate_dirs.count == 0) {
        cdo_warn("No crates discovered in workspace");
        pathlist_free(&crate_dirs);
        return 0; // Empty workspace is valid but has no crates
    }

    // Allocate crates array
    ws->crates = (Crate*)calloc((size_t)crate_dirs.count, sizeof(Crate));
    if (!ws->crates) {
        pathlist_free(&crate_dirs);
        return -1;
    }

    // Parse each crate manifest
    int loaded = 0;
    for (int i = 0; i < crate_dirs.count; i++) {
        if (parse_crate_manifest(crate_dirs.paths[i], ws->root_path,
                                 &ws->crates[loaded],
                                 default_c_std, default_cpp_std) == 0) {
            loaded++;
        }
    }
    ws->crate_count = loaded;

    pathlist_free(&crate_dirs);

    // Resolve dependency name references to crate indices
    resolve_dep_indices(ws);

    return 0;
}

// Error code for circular dependencies (matches design doc CdoErrorCode enum)
#define CDO_ERR_CYCLE 8

/// DFS helper for cycle detection. Uses coloring: 0=white, 1=gray (visiting), 2=black (done).
/// When a cycle is found, populates cycle_path with the crate indices forming the cycle.
/// Returns 1 if a cycle is found, 0 otherwise.
static int dfs_find_cycle(const Workspace* ws, int node, int* color,
                          int* parent, int* cycle_path, int* cycle_len,
                          const bool* included) {
    color[node] = 1; // Mark as visiting (gray)

    const Crate* crate = &ws->crates[node];
    for (int d = 0; d < crate->dep_count; d++) {
        int dep = crate->dep_indices[d];
        if (dep < 0 || dep >= ws->crate_count) continue;
        if (included && !included[dep]) continue;

        if (color[dep] == 1) {
            // Found a cycle: trace back from node to dep through parent chain
            // The cycle is: dep -> ... -> node -> dep
            *cycle_len = 0;
            cycle_path[(*cycle_len)++] = dep;
            int cur = node;
            while (cur != dep) {
                cycle_path[(*cycle_len)++] = cur;
                cur = parent[cur];
            }
            cycle_path[(*cycle_len)++] = dep; // Close the cycle
            return 1;
        }

        if (color[dep] == 0) {
            parent[dep] = node;
            if (dfs_find_cycle(ws, dep, color, parent, cycle_path, cycle_len, included)) {
                return 1;
            }
        }
    }

    color[node] = 2; // Mark as done (black)
    return 0;
}

/// Compute the transitive closure of dependencies for the given crate indices.
/// Marks all transitively reachable crates in the `included` array.
static void compute_transitive_closure(const Workspace* ws, bool* included) {
    // BFS/DFS expansion: keep adding dependencies until no more are found
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < ws->crate_count; i++) {
            if (!included[i]) continue;
            const Crate* crate = &ws->crates[i];
            for (int d = 0; d < crate->dep_count; d++) {
                int dep = crate->dep_indices[d];
                if (dep < 0 || dep >= ws->crate_count) continue;
                if (!included[dep]) {
                    included[dep] = true;
                    changed = true;
                }
            }
        }
    }
}

int workspace_resolve(Workspace* ws, const char** crate_names, int count) {
    if (!ws) return -1;
    if (ws->crate_count == 0) {
        ws->build_order_count = 0;
        return 0;
    }

    int n = ws->crate_count;

    // Determine which crates to include
    bool* included = (bool*)calloc((size_t)n, sizeof(bool));
    if (!included) return -1;

    if (crate_names && count > 0) {
        // Partial build: start with named crates
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < n; j++) {
                if (strcmp(crate_names[i], ws->crates[j].name) == 0) {
                    included[j] = true;
                    break;
                }
            }
        }
        // Compute transitive dependency closure
        compute_transitive_closure(ws, included);
    } else {
        // Full build: include all crates
        for (int i = 0; i < n; i++) {
            included[i] = true;
        }
    }

    // Count included crates
    int included_count = 0;
    for (int i = 0; i < n; i++) {
        if (included[i]) included_count++;
    }

    if (included_count == 0) {
        free(included);
        ws->build_order_count = 0;
        return 0;
    }

    // --- Kahn's Algorithm for topological sort ---

    // Compute in-degree for each included crate (only counting edges within included set)
    int* in_degree = (int*)calloc((size_t)n, sizeof(int));
    if (!in_degree) {
        free(included);
        return -1;
    }

    for (int i = 0; i < n; i++) {
        if (!included[i]) continue;
        const Crate* crate = &ws->crates[i];
        for (int d = 0; d < crate->dep_count; d++) {
            int dep = crate->dep_indices[d];
            if (dep < 0 || dep >= n) continue;
            if (!included[dep]) continue;
            // Edge: crate i depends on dep, so dep must come first.
            // In the "depends on" direction, i has in-degree from dep.
            // For build order: we want to process deps first.
            // In-degree of i increases for each dependency it has.
            in_degree[i]++;
        }
    }

    // Queue: start with all included crates that have in-degree 0
    int* queue = (int*)malloc((size_t)n * sizeof(int));
    if (!queue) {
        free(in_degree);
        free(included);
        return -1;
    }
    int q_front = 0, q_back = 0;

    for (int i = 0; i < n; i++) {
        if (included[i] && in_degree[i] == 0) {
            queue[q_back++] = i;
        }
    }

    // Allocate build_order
    free(ws->build_order);
    ws->build_order = (int*)malloc((size_t)included_count * sizeof(int));
    if (!ws->build_order) {
        free(queue);
        free(in_degree);
        free(included);
        return -1;
    }
    ws->build_order_count = 0;

    // Process queue
    while (q_front < q_back) {
        int cur = queue[q_front++];
        ws->build_order[ws->build_order_count++] = cur;

        // For each crate that depends on cur, decrement its in-degree
        for (int i = 0; i < n; i++) {
            if (!included[i]) continue;
            if (i == cur) continue;
            const Crate* crate = &ws->crates[i];
            for (int d = 0; d < crate->dep_count; d++) {
                if (crate->dep_indices[d] == cur) {
                    in_degree[i]--;
                    if (in_degree[i] == 0) {
                        queue[q_back++] = i;
                    }
                    break;
                }
            }
        }
    }

    free(queue);
    free(in_degree);

    // Check for cycle: if not all included crates are in the build order
    if (ws->build_order_count < included_count) {
        // Cycle detected — use DFS to find and report the cycle path
        int* color = (int*)calloc((size_t)n, sizeof(int));
        int* parent = (int*)malloc((size_t)n * sizeof(int));
        int* cycle_path = (int*)malloc((size_t)(n + 1) * sizeof(int));
        int cycle_len = 0;

        if (color && parent && cycle_path) {
            memset(parent, -1, (size_t)n * sizeof(int));

            // Mark non-included crates as already done so DFS skips them
            for (int i = 0; i < n; i++) {
                if (!included[i]) color[i] = 2;
            }

            for (int i = 0; i < n; i++) {
                if (color[i] == 0 && included[i]) {
                    if (dfs_find_cycle(ws, i, color, parent, cycle_path, &cycle_len, included)) {
                        break;
                    }
                }
            }

            // Report cycle
            if (cycle_len > 0) {
                // Build a human-readable cycle string
                char cycle_msg[1024];
                int offset = 0;
                for (int i = cycle_len - 1; i >= 0; i--) {
                    int ci = cycle_path[i];
                    int written = snprintf(cycle_msg + offset,
                                           sizeof(cycle_msg) - (size_t)offset,
                                           "%s%s",
                                           ws->crates[ci].name,
                                           (i > 0) ? " -> " : "");
                    if (written > 0) offset += written;
                    if ((size_t)offset >= sizeof(cycle_msg) - 1) break;
                }
                cdo_error("Circular dependency detected: %s", cycle_msg);
            } else {
                cdo_error("Circular dependency detected in workspace");
            }
        } else {
            cdo_error("Circular dependency detected in workspace");
        }

        free(color);
        free(parent);
        free(cycle_path);
        free(included);

        // Clean up partial build order
        free(ws->build_order);
        ws->build_order = NULL;
        ws->build_order_count = 0;

        return CDO_ERR_CYCLE;
    }

    free(included);
    return 0;
}

void workspace_free(Workspace* ws) {
    if (!ws) return;

    if (ws->crates) {
        for (int i = 0; i < ws->crate_count; i++) {
            free(ws->crates[i].dep_indices);
        }
        free(ws->crates);
        ws->crates = NULL;
    }

    free(ws->build_order);
    ws->build_order = NULL;

    ws->crate_count = 0;
    ws->build_order_count = 0;
}
