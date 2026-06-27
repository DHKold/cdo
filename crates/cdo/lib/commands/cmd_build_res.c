#include "cmd_build_internal.h"

#include "core/output.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Resource Module build (incremental file copy)
// ---------------------------------------------------------------------------

/// Dynamic list for collecting file paths during directory walk.
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

// ---------------------------------------------------------------------------
// Walk callback: collects all file paths (skips directories)
// ---------------------------------------------------------------------------

typedef struct {
    PathList*   list;
    int         error;
} WalkCtx;

static void collect_files_cb(const char* entry_path, bool is_dir, void* ctx) {
    WalkCtx* wc = (WalkCtx*)ctx;
    if (wc->error) return;
    if (is_dir) return;

    // Normalize the path
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
    // Skip trailing slash in base if present
    while (base_len > 0 && base[base_len - 1] == '/') base_len--;
    // full should start with base
    if (strncmp(base, full, base_len) != 0) return NULL;
    const char* rel = full + base_len;
    if (*rel == '/') rel++;
    return rel;
}

// ---------------------------------------------------------------------------
// build_resource_module
// ---------------------------------------------------------------------------

/// Build (copy) the Resource_Module for a crate.
/// Performs incremental copy from res/ to build/<profile>/<crate>/res/.
/// Removes stale files not present in source.
///
/// Requirements: 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9
int build_resource_module(const Workspace* ws, Crate* crate,
                          const char* profile,
                          ProgressBar* progress,
                          int* completed_units) {
    if (!crate->has_res) return 0;

    Module* res_mod = &crate->modules[MODULE_RES];
    const char* src_dir = res_mod->dir_path;

    // Build destination directory: build/<profile>/<crate_name>/res/
    char build_dir[260];
    build_dir_for_crate(ws, crate, profile, build_dir, sizeof(build_dir));

    char dest_dir[260];
    if (pal_path_join(dest_dir, sizeof(dest_dir), build_dir, "res") != 0) {
        cdo_error("res module: failed to compute destination path for crate '%s'", crate->name);
        return 1;
    }

    // Normalize source dir for consistent path comparison
    char norm_src[260];
    strncpy(norm_src, src_dir, sizeof(norm_src) - 1);
    norm_src[sizeof(norm_src) - 1] = '\0';
    pal_path_normalize(norm_src);

    // --- Collect source files ---
    PathList src_files = {0};
    WalkCtx src_ctx = { .list = &src_files, .error = 0 };

    // Req 1.7: Empty res/ is fine — return 0
    if (pal_path_exists(norm_src) != 0) {
        cdo_debug("res module '%s': source dir does not exist, nothing to do", crate->name);
        if (progress && completed_units) {
            *completed_units += 1;
            progress_update(progress, *completed_units);
        }
        return 0;
    }

    int rc = pal_dir_walk(norm_src, collect_files_cb, &src_ctx);
    if (rc != 0 || src_ctx.error) {
        cdo_error("res module: failed to walk source directory '%s' for crate '%s'", norm_src, crate->name);
        pathlist_free(&src_files);
        return 1;
    }

    // Req 1.7: If res/ has zero files, report and return success
    if (src_files.count == 0) {
        cdo_debug("res module '%s': 0 copied, 0 skipped (empty)", crate->name);
        if (progress && completed_units) {
            *completed_units += 1;
            progress_update(progress, *completed_units);
        }
        pathlist_free(&src_files);
        return 0;
    }

    // --- Ensure destination directory exists ---
    if (pal_mkdir_p(dest_dir) != 0) {
        cdo_error("res module: failed to create destination directory '%s' for crate '%s'", dest_dir, crate->name);
        pathlist_free(&src_files);
        return 1;
    }

    // --- Incremental copy ---
    int copied = 0;
    int skipped = 0;

    for (int i = 0; i < src_files.count; i++) {
        const char* src_path = src_files.paths[i];
        const char* rel = relative_to(norm_src, src_path);
        if (!rel || *rel == '\0') {
            cdo_error("res module: failed to compute relative path for '%s'", src_path);
            pathlist_free(&src_files);
            return 1;
        }

        // Compute destination path
        char dst_path[260];
        if (pal_path_join(dst_path, sizeof(dst_path), dest_dir, rel) != 0) {
            cdo_error("res module: destination path too long for '%s'", rel);
            pathlist_free(&src_files);
            return 1;
        }

        // Req 1.5: Incremental — skip if dest mtime >= source mtime
        uint64_t src_mtime = 0, dst_mtime = 0;
        bool needs_copy = true;

        if (pal_file_mtime(src_path, &src_mtime) == 0 && pal_file_mtime(dst_path, &dst_mtime) == 0) {
            if (dst_mtime >= src_mtime) {
                needs_copy = false;
            }
        }

        if (!needs_copy) {
            skipped++;
            continue;
        }

        // Ensure parent directory of destination exists
        // Extract parent dir from dst_path
        char parent_dir[260];
        strncpy(parent_dir, dst_path, sizeof(parent_dir) - 1);
        parent_dir[sizeof(parent_dir) - 1] = '\0';
        // Find last slash
        char* last_slash = strrchr(parent_dir, '/');
        if (last_slash) {
            *last_slash = '\0';
            if (pal_mkdir_p(parent_dir) != 0) {
                cdo_error("res module: failed to create directory '%s' for crate '%s'", parent_dir, crate->name);
                pathlist_free(&src_files);
                return 1;
            }
        }

        // Req 1.8: Copy with error reporting
        if (pal_file_copy(src_path, dst_path) != 0) {
            cdo_error("res module: failed to copy '%s' -> '%s' for crate '%s'", src_path, dst_path, crate->name);
            pathlist_free(&src_files);
            return 1;
        }

        copied++;
    }

    // --- Req 1.9: Remove stale files from destination ---
    // Walk dest dir, check each file against source
    char norm_dest[260];
    strncpy(norm_dest, dest_dir, sizeof(norm_dest) - 1);
    norm_dest[sizeof(norm_dest) - 1] = '\0';
    pal_path_normalize(norm_dest);

    PathList dest_files = {0};
    WalkCtx dest_ctx = { .list = &dest_files, .error = 0 };

    if (pal_path_exists(norm_dest) == 0) {
        rc = pal_dir_walk(norm_dest, collect_files_cb, &dest_ctx);
        if (rc != 0 || dest_ctx.error) {
            cdo_error("res module: failed to walk destination directory '%s' for stale removal", norm_dest);
            pathlist_free(&src_files);
            pathlist_free(&dest_files);
            return 1;
        }

        for (int d = 0; d < dest_files.count; d++) {
            const char* dest_path = dest_files.paths[d];
            const char* rel = relative_to(norm_dest, dest_path);
            if (!rel) continue;

            // Check if this relative path exists in source
            char check_src[260];
            if (pal_path_join(check_src, sizeof(check_src), norm_src, rel) != 0) continue;

            if (pal_path_exists(check_src) != 0) {
                // Stale: source no longer exists, remove destination
                remove(dest_path);
                cdo_debug("res module '%s': removed stale file '%s'", crate->name, rel);
            }
        }
    }

    // Req 1.6: Log counts at debug level
    cdo_debug("res module '%s': %d copied, %d skipped", crate->name, copied, skipped);

    // Update progress
    if (progress && completed_units) {
        *completed_units += 1;
        progress_update(progress, *completed_units);
    }

    pathlist_free(&src_files);
    pathlist_free(&dest_files);
    return 0;
}
