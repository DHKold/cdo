#include "compiler_internal.h"
#include "core/compiler.h"
#include "model/scanner.h"
#include "pal/pal.h"
#include "core/output.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// compiler_compute_dirty_set
// ---------------------------------------------------------------------------

int compiler_compute_dirty_set(const BuildUnit* units, int unit_count,
                               int* dirty_out) {
    if (!units || unit_count < 0 || !dirty_out) return -1;
    if (unit_count == 0) return 0;

    int dirty_count = 0;

    for (int i = 0; i < unit_count; i++) {
        const BuildUnit* u = &units[i];
        bool dirty = false;

        // Rule (c): object file does not exist
        if (!u->object_exists) {
            dirty = true;
        }
        // Rule (a): source mtime is newer than object mtime
        else if (u->source_mtime > u->object_mtime) {
            dirty = true;
        }
        // Rule (b): any header dependency mtime is newer than object mtime
        else if (u->header_mtimes && u->header_dep_count > 0) {
            for (int h = 0; h < u->header_dep_count; h++) {
                if (u->header_mtimes[h] > u->object_mtime) {
                    dirty = true;
                    break;
                }
            }
        }

        if (dirty) {
            dirty_out[dirty_count++] = i;
        }
    }

    return dirty_count;
}

// ---------------------------------------------------------------------------
// compiler_compute_dirty — filesystem-aware incremental compilation
// ---------------------------------------------------------------------------

/// Parse a GCC/Clang-style .d dependency file to extract header dependencies.
/// The format is: target: dep1 dep2 dep3 ...
/// Lines may use backslash-newline continuation.
/// Returns 0 on success, non-zero on failure. Caller frees *deps and each entry.
static int parse_depfile(const char* content, size_t len,
                         char*** deps, int* dep_count) {
    *deps = NULL;
    *dep_count = 0;

    if (!content || len == 0) return 0;

    // Skip past the first ':' (the target portion)
    const char* p = content;
    const char* end = content + len;
    while (p < end && *p != ':') p++;
    if (p >= end) return 0; // no colon found, skip
    p++; // skip ':'

    // Now parse space-separated dependency paths.
    // Handle backslash-newline continuation and backslash-space escapes.
    int capacity = 16;
    char** result = (char**)malloc((size_t)capacity * sizeof(char*));
    if (!result) return -1;

    int count = 0;
    char path_buf[1024];
    int path_len = 0;

    while (p < end) {
        char c = *p;

        // Backslash handling
        if (c == '\\') {
            if (p + 1 < end && p[1] == '\n') {
                // continuation line, skip both characters
                p += 2;
                continue;
            }
            if (p + 1 < end && p[1] == '\r' && p + 2 < end && p[2] == '\n') {
                // Windows continuation: \<CR><LF>
                p += 3;
                continue;
            }
            if (p + 1 < end && p[1] == ' ') {
                // escaped space — part of the filename
                if (path_len < (int)sizeof(path_buf) - 1) {
                    path_buf[path_len++] = ' ';
                }
                p += 2;
                continue;
            }
            // Regular backslash (e.g., Windows path separator)
            if (path_len < (int)sizeof(path_buf) - 1) {
                path_buf[path_len++] = c;
            }
            p++;
            continue;
        }

        // Whitespace separates dependencies
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (path_len > 0) {
                // Terminate and store the path
                path_buf[path_len] = '\0';

                // Skip the first entry (the source file itself that also
                // appears as a dependency in some .d files)
                // We include all entries — the caller will handle dedup if needed.
                if (count >= capacity) {
                    capacity *= 2;
                    char** tmp = (char**)realloc(result, (size_t)capacity * sizeof(char*));
                    if (!tmp) { goto fail; }
                    result = tmp;
                }
                result[count] = (char*)malloc((size_t)(path_len + 1));
                if (!result[count]) { goto fail; }
                memcpy(result[count], path_buf, (size_t)(path_len + 1));
                count++;
                path_len = 0;
            }
            p++;
            continue;
        }

        // Regular character
        if (path_len < (int)sizeof(path_buf) - 1) {
            path_buf[path_len++] = c;
        }
        p++;
    }

    // Handle last token if any
    if (path_len > 0) {
        path_buf[path_len] = '\0';
        if (count >= capacity) {
            capacity *= 2;
            char** tmp = (char**)realloc(result, (size_t)capacity * sizeof(char*));
            if (!tmp) { goto fail; }
            result = tmp;
        }
        result[count] = (char*)malloc((size_t)(path_len + 1));
        if (!result[count]) { goto fail; }
        memcpy(result[count], path_buf, (size_t)(path_len + 1));
        count++;
    }

    *deps = result;
    *dep_count = count;
    return 0;

fail:
    for (int i = 0; i < count; i++) free(result[i]);
    free(result);
    *deps = NULL;
    *dep_count = 0;
    return -1;
}

int compiler_compute_dirty(const Crate* crate, const char* build_dir,
                           int** dirty_indices, int* dirty_count) {
    if (!crate || !build_dir || !dirty_indices || !dirty_count) return -1;

    *dirty_indices = NULL;
    *dirty_count = 0;

    // Resolve crate path: if relative, join with workspace root (we receive
    // build_dir already as an absolute path like "build/<crate_name>").
    // The crate->path is relative to the workspace root. We need the absolute
    // crate path for scanning. The build_dir is the output directory for this crate.

    // Scan source files in the crate
    FileList sources;
    memset(&sources, 0, sizeof(sources));
    int rc = scanner_scan_sources(crate->path, NULL, 0, &sources);
    if (rc != 0) {
        cdo_warn("Failed to scan sources for crate '%s', falling back to full rebuild",
                 crate->name);
        // Fall back: return empty dirty set (caller should do full rebuild)
        // Actually for fallback, we signal that ALL files are dirty.
        // But we can't know how many there are. Return error to signal full rebuild.
        return -1;
    }

    if (sources.count == 0) {
        filelist_free(&sources);
        return 0; // nothing to build
    }

    // Filter to only compilable sources (exclude .h, .hpp)
    int source_count = 0;
    for (int i = 0; i < sources.count; i++) {
        if (is_compilable_source(sources.paths[i])) {
            source_count++;
        }
    }

    if (source_count == 0) {
        filelist_free(&sources);
        return 0;
    }

    // Compute the crate src/ prefix for deriving relative stems
    char crate_src[1024];
    if (pal_path_join(crate_src, sizeof(crate_src), crate->path, "src") != 0) {
        filelist_free(&sources);
        return -1;
    }
    pal_path_normalize(crate_src);
    size_t crate_src_len = strlen(crate_src);

    // Allocate BuildUnit array
    BuildUnit* units = (BuildUnit*)calloc((size_t)source_count, sizeof(BuildUnit));
    if (!units) {
        filelist_free(&sources);
        return -1;
    }

    int unit_idx = 0;
    bool fallback_full_rebuild = false;

    for (int i = 0; i < sources.count && !fallback_full_rebuild; i++) {
        if (!is_compilable_source(sources.paths[i])) continue;

        BuildUnit* u = &units[unit_idx];
        const char* src = sources.paths[i];

        // Store source path
        size_t src_len = strlen(src);
        if (src_len >= sizeof(u->source_path)) src_len = sizeof(u->source_path) - 1;
        memcpy(u->source_path, src, src_len);
        u->source_path[src_len] = '\0';

        // Get source mtime
        if (pal_file_mtime(src, &u->source_mtime) != PAL_OK) {
            cdo_warn("Cannot stat source file '%s', triggering full rebuild", src);
            fallback_full_rebuild = true;
            break;
        }

        // Derive stem for object/dep paths
        char stem[512];
        if (derive_stem(src, crate_src, crate_src_len, stem, sizeof(stem)) != 0) {
            // If we can't derive, just use the filename without extension
            const char* filename = strrchr(src, '/');
            if (!filename) filename = strrchr(src, '\\');
            if (!filename) filename = src;
            else filename++;
            const char* ext = pal_path_ext(filename);
            size_t fname_len = ext ? (size_t)(ext - filename) : strlen(filename);
            if (fname_len >= sizeof(stem)) fname_len = sizeof(stem) - 1;
            memcpy(stem, filename, fname_len);
            stem[fname_len] = '\0';
        }

        // Build object path: build_dir/<stem>.o (or .obj on MSVC, but .o is standard for MinGW/GCC)
        char obj_path[1024];
        char obj_rel[560];
        snprintf(obj_rel, sizeof(obj_rel), "%s.o", stem);
        if (pal_path_join(obj_path, sizeof(obj_path), build_dir, obj_rel) != 0) {
            fallback_full_rebuild = true;
            break;
        }
        pal_path_normalize(obj_path);

        // Check if object exists and get its mtime
        if (pal_path_exists(obj_path) == 0) {
            u->object_exists = true;
            if (pal_file_mtime(obj_path, &u->object_mtime) != PAL_OK) {
                cdo_warn("Cannot stat object file '%s', triggering full rebuild", obj_path);
                fallback_full_rebuild = true;
                break;
            }
        } else {
            u->object_exists = false;
            u->object_mtime = 0;
        }

        // Parse dependency file for header deps
        char dep_path[1024];
        char dep_rel[560];
        snprintf(dep_rel, sizeof(dep_rel), "%s.d", stem);
        if (pal_path_join(dep_path, sizeof(dep_path), build_dir, dep_rel) != 0) {
            fallback_full_rebuild = true;
            break;
        }
        pal_path_normalize(dep_path);

        u->header_mtimes = NULL;
        u->header_dep_count = 0;

        if (pal_path_exists(dep_path) == 0) {
            char* dep_content = NULL;
            size_t dep_len = 0;
            if (pal_file_read(dep_path, &dep_content, &dep_len) == PAL_OK && dep_content) {
                char** header_paths = NULL;
                int header_count = 0;
                if (parse_depfile(dep_content, dep_len, &header_paths, &header_count) == 0 &&
                    header_count > 0) {
                    // Gather mtimes for all header dependencies
                    u->header_mtimes = (uint64_t*)calloc((size_t)header_count, sizeof(uint64_t));
                    if (u->header_mtimes) {
                        int valid_headers = 0;
                        for (int h = 0; h < header_count; h++) {
                            // Normalize header path for consistent lookup
                            pal_path_normalize(header_paths[h]);
                            uint64_t hdr_mtime = 0;
                            if (pal_file_mtime(header_paths[h], &hdr_mtime) == PAL_OK) {
                                u->header_mtimes[valid_headers++] = hdr_mtime;
                            }
                            // If a header no longer exists, the file was deleted —
                            // this makes the dep file stale, so treat as needing rebuild.
                            // We just won't add it, meaning the source mtime vs object
                            // mtime comparison will still apply.
                            // Actually, a deleted header means the dep info is invalid.
                            // Mark as needing rebuild by setting mtime to max.
                            else {
                                u->header_mtimes[valid_headers++] = UINT64_MAX;
                            }
                        }
                        u->header_dep_count = valid_headers;
                    }
                    // Free header path strings
                    for (int h = 0; h < header_count; h++) free(header_paths[h]);
                    free(header_paths);
                }
                free(dep_content);
            }
            // If reading/parsing the dep file fails, we just proceed without header
            // deps — the source mtime check will still catch changes to the source itself.
        }

        unit_idx++;
    }

    // If we hit a fallback condition, mark everything dirty
    if (fallback_full_rebuild) {
        // Free any allocated header_mtimes
        for (int i = 0; i < unit_idx; i++) {
            free(units[i].header_mtimes);
        }
        free(units);

        // Return all indices as dirty
        *dirty_indices = (int*)malloc((size_t)source_count * sizeof(int));
        if (!*dirty_indices) {
            filelist_free(&sources);
            return -1;
        }
        for (int i = 0; i < source_count; i++) {
            (*dirty_indices)[i] = i;
        }
        *dirty_count = source_count;
        filelist_free(&sources);
        cdo_debug("Full rebuild triggered for crate '%s' (%d files)", crate->name, source_count);
        return 0;
    }

    // Compute dirty set
    int* dirty_out = (int*)malloc((size_t)source_count * sizeof(int));
    if (!dirty_out) {
        for (int i = 0; i < source_count; i++) free(units[i].header_mtimes);
        free(units);
        filelist_free(&sources);
        return -1;
    }

    int n_dirty = compiler_compute_dirty_set(units, source_count, dirty_out);
    if (n_dirty < 0) {
        // Error in dirty set computation — fallback to full rebuild
        free(dirty_out);
        for (int i = 0; i < source_count; i++) free(units[i].header_mtimes);
        free(units);
        filelist_free(&sources);
        return -1;
    }

    // Clean up BuildUnit header_mtimes
    for (int i = 0; i < source_count; i++) {
        free(units[i].header_mtimes);
    }
    free(units);
    filelist_free(&sources);

    if (n_dirty == 0) {
        free(dirty_out);
        *dirty_indices = NULL;
        *dirty_count = 0;
    } else {
        // Allocate exact-size array for caller
        *dirty_indices = (int*)malloc((size_t)n_dirty * sizeof(int));
        if (!*dirty_indices) {
            free(dirty_out);
            return -1;
        }
        memcpy(*dirty_indices, dirty_out, (size_t)n_dirty * sizeof(int));
        *dirty_count = n_dirty;
        free(dirty_out);
    }

    cdo_debug("Dirty set for crate '%s': %d of %d files need rebuild",
              crate->name, *dirty_count, source_count);
    return 0;
}
