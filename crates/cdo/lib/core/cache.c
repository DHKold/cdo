#include "core/cache.h"
#include "core/log.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/time.h>
#endif

// =============================================================================
// Helper: check if a path is absolute
// =============================================================================

/// Returns true if path is absolute (starts with '/' or a drive letter like 'C:').
static bool path_is_absolute(const char* path) {
    if (!path || path[0] == '\0') return false;
    if (path[0] == '/' || path[0] == '\\') return true;
    // Windows drive letter: e.g. "C:\" or "C:/"
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':') return true;
    return false;
}

/// Returns true if backend is one of the valid values.
static bool is_valid_backend(const char* backend) {
    if (!backend || backend[0] == '\0') return false;
    if (strcmp(backend, "builtin") == 0) return true;
    if (strcmp(backend, "ccache") == 0) return true;
    if (strcmp(backend, "sccache") == 0) return true;
    return false;
}

// =============================================================================
// Public API
// =============================================================================

int cache_init(CacheConfig* config, const char* ws_root) {
    if (!config || !ws_root) return -1;

    // Validate max_size_bytes > 0
    if (config->max_size_bytes <= 0) {
        cdo_log_error("Cache: invalid max_size_bytes (%lld), must be > 0", (long long)config->max_size_bytes);
        return -1;
    }

    // Validate backend
    if (!is_valid_backend(config->backend)) {
        cdo_log_error("Cache: invalid backend '%s' (expected 'builtin', 'ccache', or 'sccache')", config->backend);
        return -1;
    }

    // Resolve cache path relative to workspace root if not absolute
    if (!path_is_absolute(config->path)) {
        char resolved[260];
        if (pal_path_join(resolved, sizeof(resolved), ws_root, config->path) != 0) {
            cdo_log_error("Cache: failed to resolve path '%s' relative to '%s'", config->path, ws_root);
            return -1;
        }
        memcpy(config->path, resolved, sizeof(config->path));
        config->path[sizeof(config->path) - 1] = '\0';
    }

    // Normalize the resolved path
    pal_path_normalize(config->path);

    // Create cache directory if it doesn't exist
    if (pal_path_exists(config->path) != 0) {
        if (pal_mkdir_p(config->path) != 0) {
            cdo_log_warn("Cache: failed to create cache directory '%s', disabling cache", config->path);
            config->enabled = false;
            return -1;
        }
        cdo_log_debug("Cache: created directory '%s'", config->path);
    }

    cdo_log_debug("Cache: initialized (backend=%s, path=%s, max_size=%lldMB)", config->backend, config->path, (long long)(config->max_size_bytes / (1024 * 1024)));
    return 0;
}

// cache_compute_key is implemented in cache_key.c

int cache_lookup(const CacheConfig* config, const char* key, const char* dest_path) {
    if (!config || !key || !dest_path) return -1;
    if (key[0] == '\0') return -1;

    // Build cache file path: <cache_path>/<key[0:2]>/<key>.o
    char subdir[4];
    subdir[0] = key[0];
    subdir[1] = key[1];
    subdir[2] = '\0';

    char dir_path[260];
    if (pal_path_join(dir_path, sizeof(dir_path), config->path, subdir) != 0) return -1;

    char filename[CACHE_KEY_HEX_LEN + 3]; // key + ".o" + null
    size_t key_len = strlen(key);
    if (key_len + 2 >= sizeof(filename)) return -1;
    memcpy(filename, key, key_len);
    filename[key_len] = '.';
    filename[key_len + 1] = 'o';
    filename[key_len + 2] = '\0';

    char cache_file[260];
    if (pal_path_join(cache_file, sizeof(cache_file), dir_path, filename) != 0) return -1;

    // Check if cached file exists
    if (pal_path_exists(cache_file) != 0) {
        return -1; // Cache miss
    }

    // If the destination already exists, skip the copy — same cache key means
    // identical content, and rewriting would needlessly update the file's mtime
    // which triggers unnecessary re-linking downstream.
    if (pal_path_exists(dest_path) == 0) {
        return 0;
    }

    // Copy cached object to destination
    if (pal_file_copy(cache_file, dest_path) != 0) {
        // Copy failed (corrupted/truncated): delete the cache entry and report miss
        cdo_log_debug("Cache: corrupted entry '%s', removing", cache_file);
        remove(cache_file);
        return -1;
    }

    // Update access time to mark this entry as recently used (for LRU eviction)
#ifdef _WIN32
    {
        HANDLE hFile = CreateFileA(cache_file, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            SetFileTime(hFile, NULL, &ft, &ft);
            CloseHandle(hFile);
        }
    }
#else
    {
        // Set access and modification time to current time
        utimes(cache_file, NULL);
    }
#endif

    cdo_log_debug("Cache: hit for key %.8s... -> %s", key, dest_path);
    return 0;
}

int cache_store(const CacheConfig* config, const char* key, const char* obj_path) {
    if (!config || !key || !obj_path) return -1;
    if (strlen(key) < 3) return -1;

    // Build subdirectory: <cache_path>/<key[0:2]>/
    char prefix[3] = { key[0], key[1], '\0' };
    char subdir[260];
    if (pal_path_join(subdir, sizeof(subdir), config->path, prefix) != 0) return -1;

    // Create subdirectory if it doesn't exist
    if (pal_path_exists(subdir) != 0) {
        if (pal_mkdir_p(subdir) != 0) {
            cdo_log_warn("Cache: failed to create subdir '%s'", subdir);
            return -1;
        }
    }

    // Build temp file path: <cache_path>/<key[0:2]>/<key>.tmp
    char tmp_name[CACHE_KEY_HEX_LEN + 5]; // <key>.tmp\0
    snprintf(tmp_name, sizeof(tmp_name), "%s.tmp", key);
    char tmp_path[260];
    if (pal_path_join(tmp_path, sizeof(tmp_path), subdir, tmp_name) != 0) return -1;

    // Build final path: <cache_path>/<key[0:2]>/<key>.o
    char final_name[CACHE_KEY_HEX_LEN + 3]; // <key>.o\0
    snprintf(final_name, sizeof(final_name), "%s.o", key);
    char final_path[260];
    if (pal_path_join(final_path, sizeof(final_path), subdir, final_name) != 0) return -1;

    // Copy object file to temp location
    if (pal_file_copy(obj_path, tmp_path) != 0) {
        cdo_log_warn("Cache: failed to copy '%s' to temp '%s'", obj_path, tmp_path);
        return -1;
    }

    // Atomic rename temp -> final
    if (rename(tmp_path, final_path) != 0) {
        // Rename failed â€” likely target already exists from a parallel store. That's fine.
        remove(tmp_path);
        cdo_log_debug("Cache: rename to '%s' failed (parallel store?), cleaned up temp", final_path);
    }

    return 0;
}

// =============================================================================
// Eviction: LRU-based cache size management
// =============================================================================

/// Entry collected during cache directory walk for eviction.
typedef struct {
    char    path[260];
    int64_t size;
    uint64_t access_time; // nanoseconds (or mtime as fallback)
} CacheEntry;

/// Context for the eviction walk callback.
typedef struct {
    CacheEntry* entries;
    int         count;
    int         capacity;
    int64_t     total_size;
} EvictWalkCtx;

/// Get file size and last access time (falls back to mtime).
/// Returns 0 on success.
static int get_file_info(const char* path, int64_t* out_size, uint64_t* out_atime) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attr)) return -1;
    *out_size = ((int64_t)attr.nFileSizeHigh << 32) | (int64_t)attr.nFileSizeLow;
    // Use last access time; fall back to last write time if access time is zero
    ULARGE_INTEGER at;
    at.LowPart = attr.ftLastAccessTime.dwLowDateTime;
    at.HighPart = attr.ftLastAccessTime.dwHighDateTime;
    if (at.QuadPart == 0) {
        at.LowPart = attr.ftLastWriteTime.dwLowDateTime;
        at.HighPart = attr.ftLastWriteTime.dwHighDateTime;
    }
    // Convert Windows FILETIME (100ns intervals since 1601) to a comparable uint64
    *out_atime = at.QuadPart;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    *out_size = (int64_t)st.st_size;
    // Use atime, fall back to mtime if atime equals 0
    if (st.st_atime != 0) {
        *out_atime = (uint64_t)st.st_atime * 1000000000ULL;
    } else {
        *out_atime = (uint64_t)st.st_mtime * 1000000000ULL;
    }
#endif
    return 0;
}

/// Callback for pal_dir_walk during eviction: collects .o file entries.
static void evict_walk_cb(const char* entry_path, bool is_dir, void* ctx) {
    if (is_dir) return;
    EvictWalkCtx* wctx = (EvictWalkCtx*)ctx;

    // Only consider .o files
    const char* ext = pal_path_ext(entry_path);
    if (!ext || strcmp(ext, ".o") != 0) return;

    int64_t fsize = 0;
    uint64_t atime = 0;
    if (get_file_info(entry_path, &fsize, &atime) != 0) return;

    // Grow array if needed
    if (wctx->count >= wctx->capacity) {
        int new_cap = wctx->capacity == 0 ? 256 : wctx->capacity * 2;
        CacheEntry* new_entries = (CacheEntry*)realloc(wctx->entries, (size_t)new_cap * sizeof(CacheEntry));
        if (!new_entries) return;
        wctx->entries = new_entries;
        wctx->capacity = new_cap;
    }

    CacheEntry* e = &wctx->entries[wctx->count];
    strncpy(e->path, entry_path, sizeof(e->path) - 1);
    e->path[sizeof(e->path) - 1] = '\0';
    e->size = fsize;
    e->access_time = atime;
    wctx->total_size += fsize;
    wctx->count++;
}

/// qsort comparator: oldest access time first.
static int compare_entries_by_atime(const void* a, const void* b) {
    const CacheEntry* ea = (const CacheEntry*)a;
    const CacheEntry* eb = (const CacheEntry*)b;
    if (ea->access_time < eb->access_time) return -1;
    if (ea->access_time > eb->access_time) return 1;
    return 0;
}

int cache_evict(const CacheConfig* config) {
    if (!config) return -1;

    EvictWalkCtx wctx = {0};

    // Walk cache directory and collect all .o entries
    if (pal_dir_walk(config->path, evict_walk_cb, &wctx) != 0) {
        free(wctx.entries);
        return -1;
    }

    // If total size is within limit, nothing to do
    if (wctx.total_size <= config->max_size_bytes) {
        free(wctx.entries);
        return 0;
    }

    // Sort by access time (oldest first)
    qsort(wctx.entries, (size_t)wctx.count, sizeof(CacheEntry), compare_entries_by_atime);

    // Delete entries from oldest until we're under the limit
    int evicted = 0;
    int64_t freed = 0;
    for (int i = 0; i < wctx.count && wctx.total_size > config->max_size_bytes; i++) {
        if (remove(wctx.entries[i].path) == 0) {
            wctx.total_size -= wctx.entries[i].size;
            freed += wctx.entries[i].size;
            evicted++;
        }
        // If remove fails (file in use, etc.), skip and continue
    }

    cdo_log_debug("Cache: evicted %d entries, freed %lld bytes", evicted, (long long)freed);

    free(wctx.entries);
    return 0;
}

// --- Helper: get file size and mtime for a single path ---

#ifdef _WIN32
static int file_stat_info(const char* path, int64_t* size_out, uint64_t* mtime_ns_out) {
    if (!path) return -1;
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attr)) return -1;
    if (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1;
    if (size_out) {
        *size_out = ((int64_t)attr.nFileSizeHigh << 32) | (int64_t)attr.nFileSizeLow;
    }
    if (mtime_ns_out) {
        static const uint64_t EPOCH_DIFF = 116444736000000000ULL;
        uint64_t ft = ((uint64_t)attr.ftLastWriteTime.dwHighDateTime << 32) | (uint64_t)attr.ftLastWriteTime.dwLowDateTime;
        *mtime_ns_out = (ft >= EPOCH_DIFF) ? (ft - EPOCH_DIFF) * 100 : 0;
    }
    return 0;
}
#else
static int file_stat_info(const char* path, int64_t* size_out, uint64_t* mtime_ns_out) {
    if (!path) return -1;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) return -1;
    if (size_out) *size_out = (int64_t)st.st_size;
    if (mtime_ns_out) {
#if defined(__APPLE__)
        *mtime_ns_out = (uint64_t)st.st_mtimespec.tv_sec * 1000000000ULL + (uint64_t)st.st_mtimespec.tv_nsec;
#elif defined(__linux__)
        *mtime_ns_out = (uint64_t)st.st_mtim.tv_sec * 1000000000ULL + (uint64_t)st.st_mtim.tv_nsec;
#else
        *mtime_ns_out = (uint64_t)st.st_mtime * 1000000000ULL;
#endif
    }
    return 0;
}
#endif

// --- Context for cache directory walk ---

typedef struct {
    int64_t  total_size;
    int      entry_count;
    uint64_t oldest_mtime_ns; // nanoseconds (0 = unset)
} CacheWalkCtx;

static void cache_stats_walk_cb(const char* path, bool is_dir, void* ctx) {
    if (is_dir) return;

    // Only count .o files
    const char* ext = pal_path_ext(path);
    if (strcmp(ext, ".o") != 0) return;

    CacheWalkCtx* wctx = (CacheWalkCtx*)ctx;

    int64_t  fsize = 0;
    uint64_t mtime_ns = 0;
    if (file_stat_info(path, &fsize, &mtime_ns) != 0) return;

    wctx->total_size += fsize;
    wctx->entry_count++;

    if (wctx->oldest_mtime_ns == 0 || mtime_ns < wctx->oldest_mtime_ns) {
        wctx->oldest_mtime_ns = mtime_ns;
    }
}

int cache_get_stats(const CacheConfig* config, int64_t* total_size, int* entry_count, uint64_t* oldest_mtime) {
    if (!config || !total_size || !entry_count || !oldest_mtime) return -1;

    // If the cache directory doesn't exist, report empty stats
    if (pal_path_exists(config->path) != 0) {
        *total_size = 0;
        *entry_count = 0;
        *oldest_mtime = 0;
        return 0;
    }

    CacheWalkCtx ctx = { .total_size = 0, .entry_count = 0, .oldest_mtime_ns = 0 };
    int rc = pal_dir_walk(config->path, cache_stats_walk_cb, &ctx);
    if (rc != 0) return -1;

    *total_size = ctx.total_size;
    *entry_count = ctx.entry_count;
    // Convert nanoseconds to seconds (Unix timestamp)
    *oldest_mtime = ctx.oldest_mtime_ns / 1000000000ULL;
    return 0;
}

// --- cache_clear helpers ---

typedef struct {
    int64_t total_size;
    int     entry_count;
} CacheClearCtx;

/// Get the size of a file in bytes. Returns 0 on failure.
static int64_t get_file_size(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    fclose(f);
    return (sz > 0) ? (int64_t)sz : 0;
}

/// pal_dir_walk callback: accumulate file sizes and count.
static void cache_clear_walk_cb(const char* path, bool is_dir, void* ctx) {
    if (is_dir) return;
    CacheClearCtx* cc = (CacheClearCtx*)ctx;
    cc->total_size += get_file_size(path);
    cc->entry_count++;
}

/// Derive the parent directory from a path by stripping the last path component.
/// Writes the parent into `dest`. Returns 0 on success, -1 on failure.
static int get_parent_dir(char* dest, size_t dest_size, const char* path) {
    if (!path || !dest || dest_size == 0) return -1;

    size_t len = strlen(path);
    if (len == 0 || len >= dest_size) return -1;

    memcpy(dest, path, len + 1);

    // Strip trailing separators
    while (len > 0 && (dest[len - 1] == '/' || dest[len - 1] == '\\')) {
        dest[--len] = '\0';
    }

    // Find last separator
    char* last_sep = NULL;
    for (size_t i = len; i > 0; i--) {
        if (dest[i - 1] == '/' || dest[i - 1] == '\\') {
            last_sep = &dest[i - 1];
            break;
        }
    }

    if (!last_sep) return -1; // No parent found
    *last_sep = '\0';
    return 0;
}

/// pal_dir_walk callback: delete mtime_index_*.bin files found in the cache directory.
static void delete_mtime_index_files_cb(const char* path, bool is_dir, void* ctx) {
    (void)ctx;
    if (is_dir) return;

    // Extract filename from path
    const char* filename = path;
    const char* p = path;
    while (*p) {
        if (*p == '/' || *p == '\\') filename = p + 1;
        p++;
    }

    // Check if filename matches mtime_index_*.bin pattern
    const char* prefix = "mtime_index_";
    const char* suffix = ".bin";
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t filename_len = strlen(filename);

    if (filename_len <= prefix_len + suffix_len) return;
    if (strncmp(filename, prefix, prefix_len) != 0) return;
    if (strcmp(filename + filename_len - suffix_len, suffix) != 0) return;

    // Also skip .bin.tmp files (partial writes)
    if (filename_len > 4 && strcmp(filename + filename_len - 4, ".tmp") == 0) return;

    if (remove(path) == 0) {
        cdo_log_debug("Cache: deleted mtime index file '%s'", path);
    } else {
        cdo_log_warn("Cache: failed to delete mtime index file '%s'", path);
    }
}

int cache_clear(const CacheConfig* config) {
    if (!config) return -1;

    // Derive the parent cache directory (e.g., .cdo/cache/) from config->path (.cdo/cache/objects/)
    char cache_dir[260];
    int has_cache_dir = (get_parent_dir(cache_dir, sizeof(cache_dir), config->path) == 0);

    // Check if cache directory exists
    if (pal_path_exists(config->path) != 0) {
        // Even if objects dir doesn't exist, try to clean up mtime index files
        if (has_cache_dir && pal_path_exists(cache_dir) == 0) {
            pal_dir_walk(cache_dir, delete_mtime_index_files_cb, NULL);
        }
        cdo_log_info("Cache cleared: freed 0 bytes (0 entries)");
        return 0;
    }

    // Walk the cache directory to sum file sizes and count entries
    CacheClearCtx ctx = { .total_size = 0, .entry_count = 0 };
    pal_dir_walk(config->path, cache_clear_walk_cb, &ctx);

    // Remove the entire cache directory tree
    if (pal_rmdir_r(config->path) != 0) {
        cdo_log_warn("Cache: failed to remove cache directory '%s'", config->path);
        return -1;
    }

    // Recreate the empty cache directory
    if (pal_mkdir_p(config->path) != 0) {
        cdo_log_warn("Cache: failed to recreate cache directory '%s'", config->path);
        return -1;
    }

    // Delete all mtime index files in the parent cache directory (Requirement 4.1)
    if (has_cache_dir && pal_path_exists(cache_dir) == 0) {
        pal_dir_walk(cache_dir, delete_mtime_index_files_cb, NULL);
    }

    cdo_log_info("Cache cleared: freed %lld bytes (%d entries)", (long long)ctx.total_size, ctx.entry_count);
    return 0;
}
