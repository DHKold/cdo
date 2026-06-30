#ifndef CDO_CORE_MTIME_INDEX_H
#define CDO_CORE_MTIME_INDEX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// A single entry in the mtime index.
typedef struct {
    char        path[260];      // Absolute path to source or header file
    uint64_t    mtime_ns;       // Last known mtime (nanoseconds, from PAL)
    int64_t     file_size;      // Last known file size in bytes
    char        cache_key[65];  // Associated cache key (64 hex + null)
} MtimeEntry;

/// Opaque handle to a loaded mtime index.
typedef struct MtimeIndex MtimeIndex;

/// Load the mtime index for a given profile from disk.
/// If the file doesn't exist or is corrupted, returns an empty index (not an error).
/// Returns 0 on success (including empty index), non-zero only on allocation failure.
int mtime_index_load(const char* cache_dir, const char* profile, MtimeIndex** out);

/// Look up an entry by absolute file path.
/// Returns pointer to entry if found, NULL if not found.
/// The returned pointer is valid until the next mutating operation on the index.
const MtimeEntry* mtime_index_lookup(const MtimeIndex* idx, const char* abs_path);

/// Insert or update an entry in the index.
/// Returns 0 on success.
int mtime_index_upsert(MtimeIndex* idx, const MtimeEntry* entry);

/// Remove an entry by path. No-op if not found.
void mtime_index_remove(MtimeIndex* idx, const char* abs_path);

/// Persist the index to disk using atomic write (write tmp + rename).
/// Returns 0 on success.
int mtime_index_save(const MtimeIndex* idx, const char* cache_dir, const char* profile);

/// Free all memory associated with the index.
void mtime_index_free(MtimeIndex* idx);

/// Delete the index file from disk (used by `cdo cache clear`).
int mtime_index_delete(const char* cache_dir, const char* profile);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_MTIME_INDEX_H
