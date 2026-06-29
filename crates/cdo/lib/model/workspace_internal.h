#ifndef CDO_MODEL_WORKSPACE_INTERNAL_H
#define CDO_MODEL_WORKSPACE_INTERNAL_H

#include "model/workspace.h"
#include <stddef.h>

/// Try to read a config file with fallback order: .toml, .yaml, .json.
/// Returns 0 on success with buf/len populated. Caller frees buf.
int ws_read_config_file(const char* dir, const char* base_name,
                        char** buf, size_t* len);

/// Resolve dependency name references to crate indices.
/// Called by workspace_load after all crate manifests are parsed.
int resolve_dep_indices(Workspace* ws);

/// DFS helper for cycle detection. Uses coloring: 0=white, 1=gray, 2=black.
/// Returns 1 if a cycle is found, 0 otherwise.
int dfs_find_cycle(const Workspace* ws, int node, int* color,
                   int* parent, int* cycle_path, int* cycle_len,
                   const bool* included);

/// Compute transitive closure of dependencies for crates marked in `included`.
void compute_transitive_closure(const Workspace* ws, bool* included);

#endif // CDO_MODEL_WORKSPACE_INTERNAL_H
