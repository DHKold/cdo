#ifndef CDO_MODEL_CACHE_CONFIG_H
#define CDO_MODEL_CACHE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CACHE_KEY_HEX_LEN 64  // SHA256 = 32 bytes = 64 hex chars
#define CACHE_FORMAT_VERSION "v1"

/// Cache configuration (parsed from [workspace.settings.cache]).
typedef struct {
    char    path[260];          // Cache store directory (default: .cdo/cache/objects)
    int64_t max_size_bytes;     // Max cache size in bytes (default: 2GB)
    bool    enabled;            // Whether caching is active (default: true)
    char    backend[32];        // "builtin", "ccache", or "sccache"
    bool    fast_path_enabled;  // Enable mtime fast-path (default: true)
    int64_t min_file_size;      // Filesize threshold in bytes (default: 512)
} CacheConfig;

/// Cache statistics for a single build run.
typedef struct {
    int     hits;
    int     misses;
    int     stored;
    int     evicted;
    int     skipped;    // Files bypassed due to filesize threshold
} CacheStats;

#ifdef __cplusplus
}
#endif

#endif // CDO_MODEL_CACHE_CONFIG_H
