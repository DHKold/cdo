#ifndef CDO_CORE_CACHE_FASTPATH_H
#define CDO_CORE_CACHE_FASTPATH_H

#include "core/mtime_index.h"
#include "model/cache_config.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Result of a fast-path cache validation check.
typedef enum {
    FASTPATH_HIT,       // Mtime match + object exists → use stored cache key
    FASTPATH_MISS,      // Mismatch or missing → fall through to full hash
} FastPathResult;

/// Perform mtime-based fast-path validation for a single source file.
///
/// Checks if the source file and all its dependency headers have mtime and size
/// values matching their MtimeIndex entries. If all match and the cache object
/// exists, returns FASTPATH_HIT with the cache key in out_key.
///
/// @param source_path  Absolute path to the source file
/// @param dep_headers  Array of absolute paths to dependency headers (can be NULL if count is 0)
/// @param dep_count    Number of dependency headers
/// @param idx          The loaded MtimeIndex to check against (may be mutated to remove stale entries)
/// @param config       Cache configuration (for cache path to verify object existence)
/// @param out_key      Buffer for cache key on hit (must be at least 65 bytes)
/// @return FASTPATH_HIT if all conditions met, FASTPATH_MISS otherwise
FastPathResult cache_fastpath_check(const char* source_path,
                                    const char** dep_headers, int dep_count,
                                    MtimeIndex* idx,
                                    const CacheConfig* config,
                                    char* out_key);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_CACHE_FASTPATH_H
