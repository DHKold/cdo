#include "cmd_build_internal.h"

#include "core/module.h"
#include "core/output.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Inter-crate dependency module propagation
// ---------------------------------------------------------------------------

/// Tracking entry for conflict detection: maps a relative path to the crate
/// that first provided it.
typedef struct {
    char*   rel_path;
    int     source_crate_idx;   // index into ws->crates
} PathOrigin;

typedef struct {
    PathOrigin* entries;
    int         count;
    int         capacity;
} PathOriginList;

static int path_origin_add(PathOriginList* list, const char* rel, int crate_idx) {
    if (list->count >= list->capacity) {
        int new_cap = list->capacity == 0 ? 64 : list->capacity * 2;
        PathOrigin* new_entries = (PathOrigin*)realloc(list->entries, sizeof(PathOrigin) * (size_t)new_cap);
        if (!new_entries) return 1;
        list->entries = new_entries;
        list->capacity = new_cap;
    }
    list->entries[list->count].rel_path = strdup(rel);
    if (!list->entries[list->count].rel_path) return 1;
    list->entries[list->count].source_crate_idx = crate_idx;
    list->count++;
    return 0;
}

/// Find the crate index that originally provided a given relative path.
/// Returns -1 if not found.
static int path_origin_find(const PathOriginList* list, const char* rel) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->entries[i].rel_path, rel) == 0) {
            return list->entries[i].source_crate_idx;
        }
    }
    return -1;
}

static void path_origin_free(PathOriginList* list) {
    for (int i = 0; i < list->count; i++) free(list->entries[i].rel_path);
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

// ---------------------------------------------------------------------------
// Walk callback: collects all file paths (skips directories)
// ---------------------------------------------------------------------------

typedef struct {
    char**  paths;
    int     count;
    int     capacity;
} PathList;

static int pathlist_add(PathList* pl, const char* path) {
    if (pl->count >= pl->capacity) {
        int new_cap = pl->capacity == 0 ? 32 : pl->capacity * 2;
        char** new_paths = (char**)realloc(pl->paths, sizeof(char*) * (size_t)new_cap);
        if (!new_paths) return 1;
        pl->paths = new_paths;
        pl->capacity = new_cap;
    }
    pl->paths[pl->count] = strdup(path);
    if (!pl->paths[pl->count]) return 1;
    pl->count++;
    return 0;
}

static void pathlist_free(PathList* pl) {
    for (int i = 0; i < pl->count; i++) free(pl->paths[i]);
    free(pl->paths);
    pl->paths = NULL;
    pl->count = 0;
    pl->capacity = 0;
}

typedef struct {
    PathList*   list;
    int         error;
} WalkCtx;

static void collect_files_cb(const char* entry_path, bool is_dir, void* ctx) {
    WalkCtx* wc = (WalkCtx*)ctx;
    if (wc->error) return;
    if (is_dir) return;

    size_t len = strlen(entry_path);
    char* normalized = (char*)malloc(len + 1);
    if (!normalized) { wc->error = 1; return; }
    memcpy(normalized, entry_path, len + 1);
    pal_path_normalize(normalized);

    if (pathlist_add(wc->list, normalized) != 0) {
        wc->error = 1;
    }
    free(normalized);
}

// ---------------------------------------------------------------------------
// Compute relative path: given a base directory and a full path, return the
// portion of `full` after `base`. Both should be normalized (forward slashes).
// Returns pointer into `full` (no allocation).
// ---------------------------------------------------------------------------

static const char* relative_to(const char* base, const char* full) {
    size_t base_len = strlen(base);
    while (base_len > 0 && base[base_len - 1] == '/') base_len--;
    if (strncmp(base, full, base_len) != 0) return NULL;
    const char* rel = full + base_len;
    if (*rel == '/') rel++;
    return rel;
}

// ---------------------------------------------------------------------------
// Incremental copy of a directory tree from src_dir to dst_dir.
// Checks for conflicts against `seen` list; if a relative path was already
// provided by another crate, reports error.
//
// Returns 0 on success, 1 on error.
// ---------------------------------------------------------------------------

static int propagate_dir(const char* src_dir, const char* dst_dir, const char* kind_label, int dep_crate_idx, const Workspace* ws, PathOriginList* seen) {
    // Check if source directory exists
    char norm_src[260];
    strncpy(norm_src, src_dir, sizeof(norm_src) - 1);
    norm_src[sizeof(norm_src) - 1] = '\0';
    pal_path_normalize(norm_src);

    if (pal_path_exists(norm_src) != 0) {
        return 0; // Source dir doesn't exist, nothing to propagate
    }

    // Walk source directory
    PathList files = {0};
    WalkCtx walk_ctx = { .list = &files, .error = 0 };

    int rc = pal_dir_walk(norm_src, collect_files_cb, &walk_ctx);
    if (rc != 0 || walk_ctx.error) {
        cdo_error("dep propagation: failed to walk %s directory '%s'", kind_label, norm_src);
        pathlist_free(&files);
        return 1;
    }

    if (files.count == 0) {
        pathlist_free(&files);
        return 0;
    }

    // Ensure destination directory exists
    if (pal_mkdir_p(dst_dir) != 0) {
        cdo_error("dep propagation: failed to create directory '%s'", dst_dir);
        pathlist_free(&files);
        return 1;
    }

    for (int i = 0; i < files.count; i++) {
        const char* src_path = files.paths[i];
        const char* rel = relative_to(norm_src, src_path);
        if (!rel || *rel == '\0') continue;

        // Conflict detection: check if another dep already provided this relative path
        int prior_idx = path_origin_find(seen, rel);
        if (prior_idx >= 0 && prior_idx != dep_crate_idx) {
            cdo_error("dep propagation: conflict in %s/ — file '%s' provided by both crate '%s' and crate '%s'", kind_label, rel, ws->crates[prior_idx].name, ws->crates[dep_crate_idx].name);
            pathlist_free(&files);
            return 1;
        }

        // Record this path as seen from this dep
        if (prior_idx < 0) {
            if (path_origin_add(seen, rel, dep_crate_idx) != 0) {
                cdo_error("dep propagation: out of memory tracking %s paths", kind_label);
                pathlist_free(&files);
                return 1;
            }
        }

        // Compute destination path
        char dst_path[260];
        if (pal_path_join(dst_path, sizeof(dst_path), dst_dir, rel) != 0) {
            cdo_error("dep propagation: destination path too long for '%s'", rel);
            pathlist_free(&files);
            return 1;
        }

        // Incremental: skip if dest mtime >= source mtime
        uint64_t src_mtime = 0, dst_mtime = 0;
        bool needs_copy = true;
        if (pal_file_mtime(src_path, &src_mtime) == 0 && pal_file_mtime(dst_path, &dst_mtime) == 0) {
            if (dst_mtime >= src_mtime) {
                needs_copy = false;
            }
        }

        if (!needs_copy) continue;

        // Ensure parent directory exists
        char parent_dir[260];
        strncpy(parent_dir, dst_path, sizeof(parent_dir) - 1);
        parent_dir[sizeof(parent_dir) - 1] = '\0';
        char* last_slash = strrchr(parent_dir, '/');
        if (last_slash) {
            *last_slash = '\0';
            if (pal_mkdir_p(parent_dir) != 0) {
                cdo_error("dep propagation: failed to create directory '%s'", parent_dir);
                pathlist_free(&files);
                return 1;
            }
        }

        // Copy file
        if (pal_file_copy(src_path, dst_path) != 0) {
            cdo_error("dep propagation: failed to copy '%s' -> '%s'", src_path, dst_path);
            pathlist_free(&files);
            return 1;
        }

        cdo_debug("dep propagation: copied %s '%s' from crate '%s'", kind_label, rel, ws->crates[dep_crate_idx].name);
    }

    pathlist_free(&files);
    return 0;
}

// ---------------------------------------------------------------------------
// propagate_dep_modules
// ---------------------------------------------------------------------------

/// Propagate dependency module outputs (res, shd, dyn) into the
/// dependent crate's build directory. Detects conflicts.
///
/// For each resolved dependency (from dep_indices, which already includes
/// transitive deps resolved by workspace_resolve BFS order):
///   - If dep has res: copy build/<profile>/<dep>/res/ → build/<profile>/<crate>/res/ (incremental)
///   - If dep has shd: copy build/<profile>/<dep>/shd/ → build/<profile>/<crate>/shd/ (incremental)
///   - If dep has dyn: copy DLL/SO to build/<profile>/<crate>/ adjacent to exe
///
/// Excludes exe and tst modules from dependency resolution.
///
/// Requirements: 2.3, 2.4, 2.5, 2.6, 2.7, 2.8
int propagate_dep_modules(const Workspace* ws, Crate* crate, const char* profile) {
    if (!ws || !crate || !profile) return 1;
    if (crate->dep_count == 0) return 0;

    // Get the build directory for the dependent crate
    char crate_build_dir[260];
    build_dir_for_crate(ws, crate, profile, crate_build_dir, sizeof(crate_build_dir));

    // Destination dirs for res/ and shd/ within the crate's build dir
    char crate_res_dir[260];
    char crate_shd_dir[260];
    pal_path_join(crate_res_dir, sizeof(crate_res_dir), crate_build_dir, "res");
    pal_path_join(crate_shd_dir, sizeof(crate_shd_dir), crate_build_dir, "shd");

    // Conflict detection lists for res and shd
    PathOriginList res_seen = {0};
    PathOriginList shd_seen = {0};

    int rc = 0;

    for (int d = 0; d < crate->dep_count; d++) {
        int dep_idx = crate->dep_indices[d];
        if (dep_idx < 0 || dep_idx >= ws->crate_count) continue;

        Crate* dep = &ws->crates[dep_idx];

        // Req 2.8: Exclude exe and tst modules from dependency resolution.
        // We only propagate res, shd, and dyn. The exe/tst check means we skip
        // propagation from those module types, but dep_indices already only
        // points to crates with lib/api/dyn/res/shd (workspace_resolve handles this).
        // The exclusion here is about not copying exe/tst artifacts — which we
        // simply don't do (we only handle res, shd, dyn below).

        // Req 2.3: Propagate resource module output
        if (dep->has_res) {
            char dep_build_dir[260];
            build_dir_for_crate(ws, dep, profile, dep_build_dir, sizeof(dep_build_dir));

            char dep_res_dir[260];
            pal_path_join(dep_res_dir, sizeof(dep_res_dir), dep_build_dir, "res");

            rc = propagate_dir(dep_res_dir, crate_res_dir, "res", dep_idx, ws, &res_seen);
            if (rc != 0) goto cleanup;
        }

        // Req 2.4: Propagate shader module output
        if (dep->has_shd) {
            char dep_build_dir[260];
            build_dir_for_crate(ws, dep, profile, dep_build_dir, sizeof(dep_build_dir));

            char dep_shd_dir[260];
            pal_path_join(dep_shd_dir, sizeof(dep_shd_dir), dep_build_dir, "shd");

            rc = propagate_dir(dep_shd_dir, crate_shd_dir, "shd", dep_idx, ws, &shd_seen);
            if (rc != 0) goto cleanup;
        }

        // Req 2.5: Propagate dynamic library (DLL/SO) adjacent to exe
        if (dep->modules[MODULE_DYN].present) {
            char dep_build_dir[260];
            build_dir_for_crate(ws, dep, profile, dep_build_dir, sizeof(dep_build_dir));

            // Compute DLL/SO artifact name for the dep
            char artifact_name[128];
            if (module_artifact_name(dep->name, MODULE_DYN, artifact_name, sizeof(artifact_name)) != 0) {
                cdo_error("dep propagation: failed to compute DLL artifact name for dep '%s'", dep->name);
                rc = 1;
                goto cleanup;
            }

            // Source: build/<profile>/<dep>/<artifact>
            char src_dll[260];
            pal_path_join(src_dll, sizeof(src_dll), dep_build_dir, artifact_name);

            // Destination: build/<profile>/<crate>/<artifact> (adjacent to exe)
            char dst_dll[260];
            pal_path_join(dst_dll, sizeof(dst_dll), crate_build_dir, artifact_name);

            // Check source exists
            if (pal_path_exists(src_dll) != 0) {
                cdo_debug("dep propagation: DLL '%s' from dep '%s' not found (not yet built?), skipping", artifact_name, dep->name);
                continue;
            }

            // Incremental: skip if dest mtime >= source mtime
            uint64_t src_mtime = 0, dst_mtime = 0;
            bool needs_copy = true;
            if (pal_file_mtime(src_dll, &src_mtime) == 0 && pal_file_mtime(dst_dll, &dst_mtime) == 0) {
                if (dst_mtime >= src_mtime) {
                    needs_copy = false;
                }
            }

            if (needs_copy) {
                if (pal_file_copy(src_dll, dst_dll) != 0) {
                    cdo_error("dep propagation: failed to copy DLL '%s' -> '%s'", src_dll, dst_dll);
                    rc = 1;
                    goto cleanup;
                }
                cdo_debug("dep propagation: copied DLL '%s' from crate '%s'", artifact_name, dep->name);
            }
        }
    }

cleanup:
    path_origin_free(&res_seen);
    path_origin_free(&shd_seen);
    return rc;
}
