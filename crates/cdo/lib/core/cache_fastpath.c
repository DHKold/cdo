// crates/cdo/lib/core/cache_fastpath.c
// Mtime-based fast-path cache validation.
// Skips SHA-256 hash computation when filesystem timestamps indicate no changes.
// Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.8, 1.9, 4.4, 5.3

#include "core/cache_fastpath.h"
#include "core/mtime_index.h"
#include "model/cache_config.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>

FastPathResult cache_fastpath_check(const char* source_path, const char** dep_headers, int dep_count, MtimeIndex* idx, const CacheConfig* cfg, char* out_key) {
    // Step 1: If fast-path is disabled, immediately return MISS
    if (!cfg->fast_path_enabled) {
        return FASTPATH_MISS;
    }

    // Step 2: Get current mtime and file size of the source file
    uint64_t current_mtime = 0;
    int64_t current_size = 0;
    if (pal_file_info(source_path, &current_mtime, &current_size) != 0) {
        // PAL failure (file doesn't exist or can't be stat'd)
        // Remove stale entry if present, return MISS
        mtime_index_remove(idx, source_path);
        return FASTPATH_MISS;
    }

    // Step 3: Look up source in MtimeIndex — if missing, return MISS
    const MtimeEntry* src_entry = mtime_index_lookup(idx, source_path);
    if (!src_entry) {
        return FASTPATH_MISS;
    }

    // Step 4: Compare mtime_ns and file_size — if mismatch, return MISS
    if (src_entry->mtime_ns != current_mtime || src_entry->file_size != current_size) {
        return FASTPATH_MISS;
    }

    // Step 5: For each dep_header: look up in index, get real mtime/size, compare
    for (int i = 0; i < dep_count; i++) {
        const char* hdr_path = dep_headers[i];

        // Get current mtime and size for this header
        uint64_t hdr_mtime = 0;
        int64_t hdr_size = 0;
        if (pal_file_info(hdr_path, &hdr_mtime, &hdr_size) != 0) {
            // Header file no longer exists or can't be read → miss
            mtime_index_remove(idx, hdr_path);
            return FASTPATH_MISS;
        }

        // Look up header in MtimeIndex
        const MtimeEntry* hdr_entry = mtime_index_lookup(idx, hdr_path);
        if (!hdr_entry) {
            // No entry for this header → miss
            return FASTPATH_MISS;
        }

        // Compare stored vs current mtime and size
        if (hdr_entry->mtime_ns != hdr_mtime || hdr_entry->file_size != hdr_size) {
            return FASTPATH_MISS;
        }
    }

    // Step 6: Verify cache object exists at <cfg->path>/<key[0:2]>/<key>.o
    const char* cache_key = src_entry->cache_key;
    char subdir[4] = {cache_key[0], cache_key[1], '\0'};
    char dir_path[520];
    if (pal_path_join(dir_path, sizeof(dir_path), cfg->path, subdir) != 0) {
        return FASTPATH_MISS;
    }

    char filename[128];
    snprintf(filename, sizeof(filename), "%s.o", cache_key);
    char obj_path[520];
    if (pal_path_join(obj_path, sizeof(obj_path), dir_path, filename) != 0) {
        return FASTPATH_MISS;
    }

    if (pal_path_exists(obj_path) != 0) {
        // Step 7: Object missing — remove stale entry from index, return MISS
        mtime_index_remove(idx, source_path);
        return FASTPATH_MISS;
    }

    // Step 8: Object exists — copy cache_key to out_key, return FASTPATH_HIT
    memcpy(out_key, cache_key, 64);
    out_key[64] = '\0';
    return FASTPATH_HIT;
}
