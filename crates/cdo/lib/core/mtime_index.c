// core/mtime_index.c - Mtime index for fast-path cache validation
// Stores file modification timestamps, sizes, and cache keys in a hash map.
// Persists to disk as a binary file with atomic write (tmp + rename).
// Validates: Requirements 1.6, 1.7, 1.10, 1.11, 4.3, 4.5, 4.6

#include "core/mtime_index.h"
#include "core/log.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// Constants
// =============================================================================

#define MTIME_INDEX_MAGIC       0x4D544958  // "MTIX" in little-endian
#define MTIME_INDEX_VERSION     1
#define MTIME_INDEX_HEADER_SIZE 12          // magic(4) + version(4) + count(4)
#define MTIME_INDEX_MIN_ENTRY   82          // path_len(2) + path(0) + mtime(8) + size(8) + key(64) minimum with 0-len path is 82, but path must be at least 1
#define INITIAL_CAPACITY        64
#define LOAD_FACTOR_THRESHOLD   70          // Resize when 70% full

// =============================================================================
// Internal hash map structures
// =============================================================================

typedef struct MtimeNode {
    MtimeEntry          entry;
    struct MtimeNode*   next;   // Chaining for collision resolution
} MtimeNode;

struct MtimeIndex {
    MtimeNode** buckets;
    int         capacity;
    int         count;
};

// =============================================================================
// Hash function (FNV-1a)
// =============================================================================

static unsigned int fnv1a_hash(const char* str) {
    unsigned int hash = 2166136261u;
    while (*str) {
        hash ^= (unsigned char)*str++;
        hash *= 16777619u;
    }
    return hash;
}

// =============================================================================
// Internal helpers
// =============================================================================

static int mtime_index_alloc(MtimeIndex** out, int capacity) {
    MtimeIndex* idx = (MtimeIndex*)calloc(1, sizeof(MtimeIndex));
    if (!idx) return -1;

    idx->buckets = (MtimeNode**)calloc((size_t)capacity, sizeof(MtimeNode*));
    if (!idx->buckets) {
        free(idx);
        return -1;
    }
    idx->capacity = capacity;
    idx->count = 0;
    *out = idx;
    return 0;
}

static void mtime_index_resize(MtimeIndex* idx) {
    int new_cap = idx->capacity * 2;
    MtimeNode** new_buckets = (MtimeNode**)calloc((size_t)new_cap, sizeof(MtimeNode*));
    if (!new_buckets) return; // Resize failure is non-fatal, just slower lookups

    // Rehash all entries
    for (int i = 0; i < idx->capacity; i++) {
        MtimeNode* node = idx->buckets[i];
        while (node) {
            MtimeNode* next = node->next;
            unsigned int slot = fnv1a_hash(node->entry.path) % (unsigned int)new_cap;
            node->next = new_buckets[slot];
            new_buckets[slot] = node;
            node = next;
        }
    }

    free(idx->buckets);
    idx->buckets = new_buckets;
    idx->capacity = new_cap;
}

/// Build the index file path: <cache_dir>/mtime_index_<profile>.bin
static int build_index_path(char* dest, size_t dest_size, const char* cache_dir, const char* profile) {
    char filename[128];
    snprintf(filename, sizeof(filename), "mtime_index_%s.bin", profile);
    return pal_path_join(dest, dest_size, cache_dir, filename);
}

// =============================================================================
// Serialization helpers (little-endian)
// =============================================================================

static void write_u16_le(unsigned char* buf, uint16_t val) {
    buf[0] = (unsigned char)(val & 0xFF);
    buf[1] = (unsigned char)((val >> 8) & 0xFF);
}

static void write_u32_le(unsigned char* buf, uint32_t val) {
    buf[0] = (unsigned char)(val & 0xFF);
    buf[1] = (unsigned char)((val >> 8) & 0xFF);
    buf[2] = (unsigned char)((val >> 16) & 0xFF);
    buf[3] = (unsigned char)((val >> 24) & 0xFF);
}

static void write_u64_le(unsigned char* buf, uint64_t val) {
    for (int i = 0; i < 8; i++) {
        buf[i] = (unsigned char)((val >> (i * 8)) & 0xFF);
    }
}

static uint16_t read_u16_le(const unsigned char* buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t read_u32_le(const unsigned char* buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static uint64_t read_u64_le(const unsigned char* buf) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
        val |= (uint64_t)buf[i] << (i * 8);
    }
    return val;
}

// =============================================================================
// Public API: Load
// =============================================================================

int mtime_index_load(const char* cache_dir, const char* profile, MtimeIndex** out) {
    if (!cache_dir || !profile || !out) return -1;

    // Allocate an empty index first
    if (mtime_index_alloc(out, INITIAL_CAPACITY) != 0) return -1;

    // Build path to the index file
    char idx_path[520];
    if (build_index_path(idx_path, sizeof(idx_path), cache_dir, profile) != 0) {
        cdo_log_debug("mtime_index: path too long for cache_dir='%s' profile='%s'", cache_dir, profile);
        return 0; // Return empty index
    }

    // If file doesn't exist, return empty index (not an error)
    if (pal_path_exists(idx_path) != 0) {
        cdo_log_debug("mtime_index: no index file at '%s', starting empty", idx_path);
        return 0;
    }

    // Read the entire file
    char* buf = NULL;
    size_t len = 0;
    if (pal_file_read(idx_path, &buf, &len) != 0) {
        cdo_log_debug("mtime_index: failed to read '%s', treating as empty", idx_path);
        return 0;
    }

    // Validate minimum header size
    if (len < MTIME_INDEX_HEADER_SIZE) {
        cdo_log_debug("mtime_index: file too short (%zu bytes), discarding", len);
        free(buf);
        return 0;
    }

    const unsigned char* data = (const unsigned char*)buf;

    // Validate magic
    uint32_t magic = read_u32_le(data);
    if (magic != MTIME_INDEX_MAGIC) {
        cdo_log_debug("mtime_index: bad magic 0x%08X (expected 0x%08X), discarding", magic, MTIME_INDEX_MAGIC);
        free(buf);
        return 0;
    }

    // Validate version
    uint32_t version = read_u32_le(data + 4);
    if (version != MTIME_INDEX_VERSION) {
        cdo_log_debug("mtime_index: unsupported version %u (expected %u), discarding", version, MTIME_INDEX_VERSION);
        free(buf);
        return 0;
    }

    // Read entry count
    uint32_t entry_count = read_u32_le(data + 8);

    // Sanity check: minimum entry size is 2 (path_len) + 0 (path) + 8 + 8 + 64 = 82
    // But a valid path must have at least 1 character, so minimum is 83
    size_t data_region = len - MTIME_INDEX_HEADER_SIZE;
    if (entry_count > 0 && data_region < (size_t)entry_count * 82) {
        cdo_log_debug("mtime_index: entry count %u exceeds available data (%zu bytes), discarding", entry_count, data_region);
        free(buf);
        return 0;
    }

    // Parse entries
    size_t offset = MTIME_INDEX_HEADER_SIZE;
    for (uint32_t i = 0; i < entry_count; i++) {
        // Need at least 2 bytes for path_len
        if (offset + 2 > len) {
            cdo_log_debug("mtime_index: truncated at entry %u (path_len), discarding remaining", i);
            break;
        }

        uint16_t path_len = read_u16_le(data + offset);
        offset += 2;

        // Validate we have enough data for this entry
        size_t entry_data_needed = (size_t)path_len + 8 + 8 + 64;
        if (offset + entry_data_needed > len) {
            cdo_log_debug("mtime_index: truncated at entry %u (data), discarding remaining", i);
            break;
        }

        // Skip entries with paths too long for our buffer
        if (path_len >= sizeof(((MtimeEntry*)0)->path)) {
            offset += entry_data_needed;
            cdo_log_debug("mtime_index: path too long (%u bytes) at entry %u, skipping", path_len, i);
            continue;
        }

        MtimeEntry entry = {0};
        memcpy(entry.path, data + offset, path_len);
        entry.path[path_len] = '\0';
        offset += path_len;

        entry.mtime_ns = read_u64_le(data + offset);
        offset += 8;

        entry.file_size = (int64_t)read_u64_le(data + offset);
        offset += 8;

        memcpy(entry.cache_key, data + offset, 64);
        entry.cache_key[64] = '\0';
        offset += 64;

        mtime_index_upsert(*out, &entry);
    }

    free(buf);
    cdo_log_debug("mtime_index: loaded %d entries from '%s'", (*out)->count, idx_path);
    return 0;
}

// =============================================================================
// Public API: Lookup
// =============================================================================

const MtimeEntry* mtime_index_lookup(const MtimeIndex* idx, const char* abs_path) {
    if (!idx || !abs_path) return NULL;

    unsigned int slot = fnv1a_hash(abs_path) % (unsigned int)idx->capacity;
    MtimeNode* node = idx->buckets[slot];
    while (node) {
        if (strcmp(node->entry.path, abs_path) == 0) {
            return &node->entry;
        }
        node = node->next;
    }
    return NULL;
}

// =============================================================================
// Public API: Upsert
// =============================================================================

int mtime_index_upsert(MtimeIndex* idx, const MtimeEntry* entry) {
    if (!idx || !entry) return -1;

    unsigned int slot = fnv1a_hash(entry->path) % (unsigned int)idx->capacity;

    // Check if entry already exists (update in place)
    MtimeNode* node = idx->buckets[slot];
    while (node) {
        if (strcmp(node->entry.path, entry->path) == 0) {
            node->entry.mtime_ns = entry->mtime_ns;
            node->entry.file_size = entry->file_size;
            memcpy(node->entry.cache_key, entry->cache_key, sizeof(node->entry.cache_key));
            return 0;
        }
        node = node->next;
    }

    // Insert new node
    MtimeNode* new_node = (MtimeNode*)calloc(1, sizeof(MtimeNode));
    if (!new_node) return -1;

    memcpy(&new_node->entry, entry, sizeof(MtimeEntry));
    new_node->next = idx->buckets[slot];
    idx->buckets[slot] = new_node;
    idx->count++;

    // Resize if load factor exceeded
    if (idx->count * 100 / idx->capacity > LOAD_FACTOR_THRESHOLD) {
        mtime_index_resize(idx);
    }

    return 0;
}

// =============================================================================
// Public API: Remove
// =============================================================================

void mtime_index_remove(MtimeIndex* idx, const char* abs_path) {
    if (!idx || !abs_path) return;

    unsigned int slot = fnv1a_hash(abs_path) % (unsigned int)idx->capacity;
    MtimeNode* prev = NULL;
    MtimeNode* node = idx->buckets[slot];

    while (node) {
        if (strcmp(node->entry.path, abs_path) == 0) {
            if (prev) {
                prev->next = node->next;
            } else {
                idx->buckets[slot] = node->next;
            }
            free(node);
            idx->count--;
            return;
        }
        prev = node;
        node = node->next;
    }
}

// =============================================================================
// Public API: Save
// =============================================================================

int mtime_index_save(const MtimeIndex* idx, const char* cache_dir, const char* profile) {
    if (!idx || !cache_dir || !profile) return -1;

    // Ensure cache directory exists
    if (pal_path_exists(cache_dir) != 0) {
        if (pal_mkdir_p(cache_dir) != 0) {
            cdo_log_warn("mtime_index: failed to create cache dir '%s'", cache_dir);
            return -1;
        }
    }

    // Calculate total buffer size needed
    size_t total_size = MTIME_INDEX_HEADER_SIZE;
    for (int i = 0; i < idx->capacity; i++) {
        MtimeNode* node = idx->buckets[i];
        while (node) {
            size_t path_len = strlen(node->entry.path);
            total_size += 2 + path_len + 8 + 8 + 64; // path_len(2) + path + mtime(8) + size(8) + key(64)
            node = node->next;
        }
    }

    // Allocate serialization buffer
    unsigned char* buf = (unsigned char*)malloc(total_size);
    if (!buf) {
        cdo_log_warn("mtime_index: allocation failed for save buffer (%zu bytes)", total_size);
        return -1;
    }

    // Write header
    write_u32_le(buf + 0, MTIME_INDEX_MAGIC);
    write_u32_le(buf + 4, MTIME_INDEX_VERSION);
    write_u32_le(buf + 8, (uint32_t)idx->count);

    // Write entries
    size_t offset = MTIME_INDEX_HEADER_SIZE;
    for (int i = 0; i < idx->capacity; i++) {
        MtimeNode* node = idx->buckets[i];
        while (node) {
            uint16_t path_len = (uint16_t)strlen(node->entry.path);
            write_u16_le(buf + offset, path_len);
            offset += 2;

            memcpy(buf + offset, node->entry.path, path_len);
            offset += path_len;

            write_u64_le(buf + offset, node->entry.mtime_ns);
            offset += 8;

            write_u64_le(buf + offset, (uint64_t)node->entry.file_size);
            offset += 8;

            memcpy(buf + offset, node->entry.cache_key, 64);
            offset += 64;

            node = node->next;
        }
    }

    // Write to tmp file, then rename atomically
    char tmp_path[520];
    char final_path[520];
    char tmp_filename[128];
    snprintf(tmp_filename, sizeof(tmp_filename), "mtime_index_%s.bin.tmp", profile);

    if (pal_path_join(tmp_path, sizeof(tmp_path), cache_dir, tmp_filename) != 0) {
        free(buf);
        return -1;
    }
    if (build_index_path(final_path, sizeof(final_path), cache_dir, profile) != 0) {
        free(buf);
        return -1;
    }

    // Write to tmp file
    if (pal_file_write(tmp_path, (const char*)buf, offset) != 0) {
        cdo_log_warn("mtime_index: failed to write tmp file '%s'", tmp_path);
        free(buf);
        return -1;
    }

    free(buf);

    // Atomic rename: tmp -> final
    // On Windows, rename() fails if destination exists, so remove it first
#ifdef _WIN32
    remove(final_path);
#endif
    if (rename(tmp_path, final_path) != 0) {
        cdo_log_warn("mtime_index: rename '%s' -> '%s' failed", tmp_path, final_path);
        remove(tmp_path); // Clean up tmp on failure
        return -1;
    }

    cdo_log_debug("mtime_index: saved %d entries to '%s'", idx->count, final_path);
    return 0;
}

// =============================================================================
// Public API: Free
// =============================================================================

void mtime_index_free(MtimeIndex* idx) {
    if (!idx) return;

    for (int i = 0; i < idx->capacity; i++) {
        MtimeNode* node = idx->buckets[i];
        while (node) {
            MtimeNode* next = node->next;
            free(node);
            node = next;
        }
    }

    free(idx->buckets);
    free(idx);
}

// =============================================================================
// Public API: Delete
// =============================================================================

int mtime_index_delete(const char* cache_dir, const char* profile) {
    if (!cache_dir || !profile) return -1;

    char idx_path[520];
    if (build_index_path(idx_path, sizeof(idx_path), cache_dir, profile) != 0) {
        return -1;
    }

    // If file doesn't exist, that's fine — return success
    if (pal_path_exists(idx_path) != 0) {
        cdo_log_debug("mtime_index: delete - file '%s' does not exist, nothing to do", idx_path);
        return 0;
    }

    if (remove(idx_path) != 0) {
        cdo_log_warn("mtime_index: failed to delete '%s'", idx_path);
        return -1;
    }

    cdo_log_debug("mtime_index: deleted '%s'", idx_path);
    return 0;
}
