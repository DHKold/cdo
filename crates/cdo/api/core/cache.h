#ifndef CDO_CORE_CACHE_H
#define CDO_CORE_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CacheConfig and CacheStats are pure data types living in model/ layer.
#include "model/cache_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Inputs for computing a cache key for a single compilation unit.
typedef struct {
    const char*     source_path;        // For reading source content
    const char*     compiler_path;      // Compiler executable
    const char*     compiler_version;   // e.g., "13.2.0"
    const char*     language_standard;  // e.g., "c17", "c++20"
    bool            optimize;
    bool            debug_info;
    const char**    defines;            // Sorted preprocessor defines
    int             define_count;
    const char**    include_paths;      // Sorted include directories
    int             include_path_count;
    const char*     dep_file_path;      // Path to .d file (NULL if none)
} CacheKeyInputs;

/// Initialize the cache subsystem. Loads config from workspace settings.
/// Creates cache directory if it doesn't exist.
/// Returns 0 on success.
int cache_init(CacheConfig* config, const char* ws_root);

/// Compute a cache key for a compilation unit.
/// Reads source content, parses dep file for header deps, hashes all inputs.
/// Writes hex key into out_key (must be at least CACHE_KEY_HEX_LEN + 1 bytes).
/// Returns 0 on success, non-zero if inputs cannot be read (treat as miss).
int cache_compute_key(const CacheKeyInputs* inputs, char* out_key);

/// Look up a cached object file by key.
/// If found, copies it to dest_path and returns 0.
/// If not found, returns non-zero (cache miss).
int cache_lookup(const CacheConfig* config, const char* key, const char* dest_path);

/// Store a compiled object file in the cache.
/// Atomically copies obj_path to the cache indexed by key.
/// Returns 0 on success.
int cache_store(const CacheConfig* config, const char* key, const char* obj_path);

/// Run eviction if cache exceeds max size.
/// Removes least-recently-accessed entries until under limit.
/// Called at end of build (non-blocking during compilation).
int cache_evict(const CacheConfig* config);

/// Get current cache statistics (size, entry count, oldest entry).
/// For the `cdo cache stats` command.
int cache_get_stats(const CacheConfig* config, int64_t* total_size, int* entry_count, uint64_t* oldest_mtime);

/// Clear all cache entries.
int cache_clear(const CacheConfig* config);

/// Parse a dependency file and return the list of header paths.
/// Supports GCC/Clang .d format (Makefile-style) and MSVC /showIncludes format.
/// Auto-detects format based on content.
/// Returns 0 on success, non-zero on error.
/// On success, *headers is a heap-allocated array of strings (each heap-allocated).
/// Caller must free each string and the array itself.
int depfile_parse(const char* dep_path, char*** headers, int* count);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_CACHE_H
