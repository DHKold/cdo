#include "workspace_internal.h"
#include "core/workspace.h"
#include "commons/toml.h"
#include "core/output.h"
#include "pal/pal.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Error code for circular dependencies (matches design doc CdoErrorCode enum)
#define CDO_ERR_CYCLE 8

// =============================================================================
// Internal helpers (shared via workspace_internal.h)
// =============================================================================

/// DFS helper for cycle detection. Uses coloring: 0=white, 1=gray (visiting), 2=black (done).
int dfs_find_cycle(const Workspace* ws, int node, int* color,
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
void compute_transitive_closure(const Workspace* ws, bool* included) {
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

/// Resolve dependency name references to crate indices.
int resolve_dep_indices(Workspace* ws) {
    for (int ci = 0; ci < ws->crate_count; ci++) {
        Crate* crate = &ws->crates[ci];
        if (crate->dep_count == 0 && crate->dev_dep_count == 0) continue;

        // Re-read the crate manifest to get dependency names
        char crate_dir[520];
        if (pal_path_join(crate_dir, sizeof(crate_dir), ws->root_path, crate->path) != 0) {
            continue;
        }

        char* buf = NULL;
        size_t len = 0;
        if (ws_read_config_file(crate_dir, "crate", &buf, &len) != 0) {
            continue;
        }

        TomlTable* root = NULL;
        TomlError err;
        if (toml_parse(buf, len, &root, &err) != 0) {
            free(buf);
            continue;
        }
        free(buf);

        // Resolve normal dependencies
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

        // Resolve dev-dependencies
        const TomlValue* dev_deps_val = toml_get(root, "dev-dependencies");
        if (dev_deps_val && (dev_deps_val->type == TOML_TABLE || dev_deps_val->type == TOML_INLINE_TABLE)) {
            TomlTable* dev_deps_table = dev_deps_val->as.table;
            TomlEntry* entry = dev_deps_table->head;
            int di = 0;
            while (entry && di < crate->dev_dep_count) {
                // Match this dev-dependency name against workspace crates
                for (int j = 0; j < ws->crate_count; j++) {
                    if (j == ci) continue; // Skip self
                    if (strcmp(entry->key, ws->crates[j].name) == 0) {
                        crate->dev_dep_indices[di] = j;
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

    // Compute in-degree for each included crate
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
        int* parent_arr = (int*)malloc((size_t)n * sizeof(int));
        int* cycle_path = (int*)malloc((size_t)(n + 1) * sizeof(int));
        int cycle_len = 0;

        if (color && parent_arr && cycle_path) {
            memset(parent_arr, -1, (size_t)n * sizeof(int));

            // Mark non-included crates as already done so DFS skips them
            for (int i = 0; i < n; i++) {
                if (!included[i]) color[i] = 2;
            }

            for (int i = 0; i < n; i++) {
                if (color[i] == 0 && included[i]) {
                    if (dfs_find_cycle(ws, i, color, parent_arr, cycle_path, &cycle_len, included)) {
                        break;
                    }
                }
            }

            // Report cycle
            if (cycle_len > 0) {
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
        free(parent_arr);
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

// =============================================================================
// workspace_resolve_module_deps — inter-crate module validation
// =============================================================================

/// BFS-based transitive dependency collector.
static void collect_transitive_deps(const Workspace* ws, int start,
                                    bool* visited, int* queue) {
    int n = ws->crate_count;
    for (int i = 0; i < n; i++) visited[i] = false;

    int q_front = 0, q_back = 0;

    const Crate* root = &ws->crates[start];
    for (int d = 0; d < root->dep_count; d++) {
        int dep = root->dep_indices[d];
        if (dep < 0 || dep >= n) continue;
        if (!visited[dep]) {
            visited[dep] = true;
            queue[q_back++] = dep;
        }
    }

    while (q_front < q_back) {
        int cur = queue[q_front++];
        const Crate* crate = &ws->crates[cur];
        for (int d = 0; d < crate->dep_count; d++) {
            int dep = crate->dep_indices[d];
            if (dep < 0 || dep >= n) continue;
            if (!visited[dep]) {
                visited[dep] = true;
                queue[q_back++] = dep;
            }
        }
    }
}

int workspace_resolve_module_deps(Workspace* ws) {
    if (!ws) return -1;
    if (ws->crate_count == 0) return 0;

    int n = ws->crate_count;
    int errors = 0;

    // --- Phase 1: Cycle detection ---
    int* color = (int*)calloc((size_t)n, sizeof(int));
    int* parent_arr = (int*)malloc((size_t)n * sizeof(int));
    int* cycle_path = (int*)malloc((size_t)(n + 1) * sizeof(int));
    int cycle_len = 0;

    if (!color || !parent_arr || !cycle_path) {
        free(color);
        free(parent_arr);
        free(cycle_path);
        return -1;
    }

    memset(parent_arr, -1, (size_t)n * sizeof(int));

    for (int i = 0; i < n; i++) {
        if (color[i] == 0) {
            if (dfs_find_cycle(ws, i, color, parent_arr, cycle_path, &cycle_len, NULL)) {
                char cycle_msg[1024];
                int offset = 0;
                for (int j = cycle_len - 1; j >= 0; j--) {
                    int ci = cycle_path[j];
                    int written = snprintf(cycle_msg + offset,
                                           sizeof(cycle_msg) - (size_t)offset,
                                           "%s%s",
                                           ws->crates[ci].name,
                                           (j > 0) ? " -> " : "");
                    if (written > 0) offset += written;
                    if ((size_t)offset >= sizeof(cycle_msg) - 1) break;
                }
                cdo_error("Circular dependency detected: %s", cycle_msg);
                errors++;
                break;
            }
        }
    }

    free(color);
    free(parent_arr);
    free(cycle_path);

    if (errors > 0) return -1;

    // --- Phase 2: Validate library module presence for dependencies ---
    for (int i = 0; i < n; i++) {
        const Crate* crate = &ws->crates[i];
        if (crate->dep_count == 0) continue;

        for (int d = 0; d < crate->dep_count; d++) {
            int dep_idx = crate->dep_indices[d];
            if (dep_idx < 0 || dep_idx >= n) continue;

            const Crate* dep_crate = &ws->crates[dep_idx];

            if (dep_crate->module_count > 0 && !dep_crate->has_lib) {
                cdo_error("Crate '%s' depends on '%s' which has no library module (lib/)",
                          crate->name, dep_crate->name);
                errors++;
            }
        }
    }

    if (errors > 0) return -1;

    // --- Phase 3: Resolve transitive dependencies ---
    bool* visited = (bool*)calloc((size_t)n, sizeof(bool));
    int* queue = (int*)malloc((size_t)n * sizeof(int));

    if (!visited || !queue) {
        free(visited);
        free(queue);
        return -1;
    }

    for (int i = 0; i < n; i++) {
        const Crate* crate = &ws->crates[i];
        if (crate->dep_count == 0) continue;

        collect_transitive_deps(ws, i, visited, queue);

        for (int j = 0; j < n; j++) {
            if (!visited[j]) continue;
            const Crate* trans_dep = &ws->crates[j];
            if (trans_dep->module_count > 0 && !trans_dep->has_lib) {
                cdo_error("Crate '%s' transitively depends on '%s' which has no library module (lib/)",
                          crate->name, trans_dep->name);
                errors++;
            }
        }
    }

    free(visited);
    free(queue);

    return (errors > 0) ? -1 : 0;
}

void workspace_free(Workspace* ws) {
    if (!ws) return;

    if (ws->crates) {
        for (int i = 0; i < ws->crate_count; i++) {
            free(ws->crates[i].dep_indices);
            free(ws->crates[i].dev_dep_indices);
            if (ws->crates[i].link_libs) {
                for (int j = 0; j < ws->crates[i].link_lib_count; j++) {
                    free(ws->crates[i].link_libs[j]);
                }
                free(ws->crates[i].link_libs);
            }
            if (ws->crates[i].defines) {
                for (int j = 0; j < ws->crates[i].define_count; j++) {
                    free(ws->crates[i].defines[j]);
                }
                free(ws->crates[i].defines);
            }
        }
        free(ws->crates);
        ws->crates = NULL;
    }

    free(ws->build_order);
    ws->build_order = NULL;

    ws->crate_count = 0;
    ws->build_order_count = 0;
}
