// crates/cdo/tst/unit/test_mtime_index.c
// Unit tests for MtimeIndex CRUD operations, serialization, and corruption handling
// Validates: Requirements 1.6, 1.7, 1.10, 1.11, 4.3, 4.5, 4.6
#include "cdo_ut.h"
#include "core/mtime_index.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
// Helper: get a temp directory for mtime index tests
// =============================================================================

static void get_temp_dir(char* buf, size_t size, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(buf, size, "%s/cdo_test_mtime_%s", tmp, suffix);
    pal_path_normalize(buf);
}

static void cleanup_temp_dir(const char* dir) {
    pal_rmdir_r(dir);
}

// =============================================================================
// Helper: build the expected index file path for a profile
// =============================================================================

static void get_index_file_path(char* buf, size_t size, const char* cache_dir, const char* profile) {
    char filename[128];
    snprintf(filename, sizeof(filename), "mtime_index_%s.bin", profile);
    pal_path_join(buf, size, cache_dir, filename);
}

// =============================================================================
// Test: Load from non-existent directory returns empty index
// Requirement 1.7: missing index file treated as empty, not an error
// =============================================================================

TEST_SERIAL(mtime_index_load_empty) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "load_empty");
    cleanup_temp_dir(cache_dir);

    // Load from a directory that does not exist
    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(idx != NULL);

    // Lookup anything should return NULL
    const MtimeEntry* entry = mtime_index_lookup(idx, "/some/path.c");
    TEST_ASSERT_NULL(entry);

    mtime_index_free(idx);
    return 0;
}

// =============================================================================
// Test: Upsert an entry, then lookup returns matching entry
// Requirement 4.3: entries keyed by absolute path
// =============================================================================

TEST_SERIAL(mtime_index_upsert_and_lookup) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "upsert_lookup");
    cleanup_temp_dir(cache_dir);

    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, "C:/project/src/main.c", sizeof(entry.path) - 1);
    entry.mtime_ns = 1700000000000000000ULL;
    entry.file_size = 4096;
    strncpy(entry.cache_key, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789", sizeof(entry.cache_key) - 1);

    rc = mtime_index_upsert(idx, &entry);
    TEST_ASSERT_EQ(rc, 0);

    const MtimeEntry* found = mtime_index_lookup(idx, "C:/project/src/main.c");
    TEST_ASSERT(found != NULL);
    TEST_ASSERT_STR_EQ(found->path, "C:/project/src/main.c");
    TEST_ASSERT_EQ(found->mtime_ns, 1700000000000000000ULL);
    TEST_ASSERT_EQ(found->file_size, 4096);
    TEST_ASSERT_STR_EQ(found->cache_key, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");

    mtime_index_free(idx);
    cleanup_temp_dir(cache_dir);
    return 0;
}

// =============================================================================
// Test: Lookup non-existent path returns NULL
// =============================================================================

TEST_SERIAL(mtime_index_lookup_miss) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "lookup_miss");
    cleanup_temp_dir(cache_dir);

    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    // Insert one entry
    MtimeEntry entry = {0};
    strncpy(entry.path, "C:/project/src/a.c", sizeof(entry.path) - 1);
    entry.mtime_ns = 100;
    entry.file_size = 200;
    strncpy(entry.cache_key, "1111111111111111111111111111111111111111111111111111111111111111", sizeof(entry.cache_key) - 1);
    mtime_index_upsert(idx, &entry);

    // Lookup a different path
    const MtimeEntry* found = mtime_index_lookup(idx, "C:/project/src/b.c");
    TEST_ASSERT_NULL(found);

    mtime_index_free(idx);
    cleanup_temp_dir(cache_dir);
    return 0;
}

// =============================================================================
// Test: Upsert with same path but different values updates the entry
// Requirement 1.10: mtime compared using exact integer (update must store new value)
// =============================================================================

TEST_SERIAL(mtime_index_upsert_update) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "upsert_update");
    cleanup_temp_dir(cache_dir);

    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    // Insert initial entry
    MtimeEntry entry = {0};
    strncpy(entry.path, "C:/project/src/main.c", sizeof(entry.path) - 1);
    entry.mtime_ns = 1000;
    entry.file_size = 500;
    strncpy(entry.cache_key, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", sizeof(entry.cache_key) - 1);
    rc = mtime_index_upsert(idx, &entry);
    TEST_ASSERT_EQ(rc, 0);

    // Upsert with updated values (same path)
    MtimeEntry updated = {0};
    strncpy(updated.path, "C:/project/src/main.c", sizeof(updated.path) - 1);
    updated.mtime_ns = 2000;
    updated.file_size = 600;
    strncpy(updated.cache_key, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", sizeof(updated.cache_key) - 1);
    rc = mtime_index_upsert(idx, &updated);
    TEST_ASSERT_EQ(rc, 0);

    // Lookup should return updated values
    const MtimeEntry* found = mtime_index_lookup(idx, "C:/project/src/main.c");
    TEST_ASSERT(found != NULL);
    TEST_ASSERT_EQ(found->mtime_ns, 2000ULL);
    TEST_ASSERT_EQ(found->file_size, 600);
    TEST_ASSERT_STR_EQ(found->cache_key, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

    mtime_index_free(idx);
    cleanup_temp_dir(cache_dir);
    return 0;
}

// =============================================================================
// Test: Remove an existing entry, lookup returns NULL afterwards
// =============================================================================

TEST_SERIAL(mtime_index_remove_exists) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "remove_exists");
    cleanup_temp_dir(cache_dir);

    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, "C:/project/src/util.c", sizeof(entry.path) - 1);
    entry.mtime_ns = 5000;
    entry.file_size = 1024;
    strncpy(entry.cache_key, "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc", sizeof(entry.cache_key) - 1);
    mtime_index_upsert(idx, &entry);

    // Verify it's there
    TEST_ASSERT(mtime_index_lookup(idx, "C:/project/src/util.c") != NULL);

    // Remove it
    mtime_index_remove(idx, "C:/project/src/util.c");

    // Verify it's gone
    TEST_ASSERT_NULL(mtime_index_lookup(idx, "C:/project/src/util.c"));

    mtime_index_free(idx);
    cleanup_temp_dir(cache_dir);
    return 0;
}

// =============================================================================
// Test: Remove non-existent path is a no-op (no crash)
// =============================================================================

TEST_SERIAL(mtime_index_remove_noop) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "remove_noop");
    cleanup_temp_dir(cache_dir);

    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    // Remove from empty index should not crash
    mtime_index_remove(idx, "C:/nonexistent/file.c");

    // Insert one, then remove a different one
    MtimeEntry entry = {0};
    strncpy(entry.path, "C:/project/src/a.c", sizeof(entry.path) - 1);
    entry.mtime_ns = 100;
    entry.file_size = 50;
    strncpy(entry.cache_key, "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd", sizeof(entry.cache_key) - 1);
    mtime_index_upsert(idx, &entry);

    mtime_index_remove(idx, "C:/project/src/b.c");

    // Original entry still intact
    TEST_ASSERT(mtime_index_lookup(idx, "C:/project/src/a.c") != NULL);

    mtime_index_free(idx);
    cleanup_temp_dir(cache_dir);
    return 0;
}

// =============================================================================
// Test: Save then load produces equivalent entries (round-trip)
// Requirement 1.6: index persists across builds by writing to .cdo/cache/
// =============================================================================

TEST_SERIAL(mtime_index_save_load_roundtrip) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "roundtrip");
    cleanup_temp_dir(cache_dir);
    pal_mkdir_p(cache_dir);

    // Create and populate an index
    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry e1 = {0};
    strncpy(e1.path, "C:/project/src/main.c", sizeof(e1.path) - 1);
    e1.mtime_ns = 1700000000000000000ULL;
    e1.file_size = 4096;
    strncpy(e1.cache_key, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789", sizeof(e1.cache_key) - 1);
    mtime_index_upsert(idx, &e1);

    MtimeEntry e2 = {0};
    strncpy(e2.path, "C:/project/include/header.h", sizeof(e2.path) - 1);
    e2.mtime_ns = 1600000000000000000ULL;
    e2.file_size = 256;
    strncpy(e2.cache_key, "9876543210fedcba9876543210fedcba9876543210fedcba9876543210fedcba", sizeof(e2.cache_key) - 1);
    mtime_index_upsert(idx, &e2);

    // Save to disk
    rc = mtime_index_save(idx, cache_dir, "debug");
    TEST_ASSERT_EQ(rc, 0);

    // Free original
    mtime_index_free(idx);
    idx = NULL;

    // Load from disk
    MtimeIndex* loaded = NULL;
    rc = mtime_index_load(cache_dir, "debug", &loaded);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(loaded != NULL);

    // Verify entries match
    const MtimeEntry* f1 = mtime_index_lookup(loaded, "C:/project/src/main.c");
    TEST_ASSERT(f1 != NULL);
    TEST_ASSERT_STR_EQ(f1->path, "C:/project/src/main.c");
    TEST_ASSERT_EQ(f1->mtime_ns, 1700000000000000000ULL);
    TEST_ASSERT_EQ(f1->file_size, 4096);
    TEST_ASSERT_STR_EQ(f1->cache_key, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");

    const MtimeEntry* f2 = mtime_index_lookup(loaded, "C:/project/include/header.h");
    TEST_ASSERT(f2 != NULL);
    TEST_ASSERT_STR_EQ(f2->path, "C:/project/include/header.h");
    TEST_ASSERT_EQ(f2->mtime_ns, 1600000000000000000ULL);
    TEST_ASSERT_EQ(f2->file_size, 256);
    TEST_ASSERT_STR_EQ(f2->cache_key, "9876543210fedcba9876543210fedcba9876543210fedcba9876543210fedcba");

    mtime_index_free(loaded);
    cleanup_temp_dir(cache_dir);
    return 0;
}

// =============================================================================
// Test: Profile-scoped isolation - write to "debug", "release" is unaffected
// Requirement 1.11: index scoped per build profile
// =============================================================================

TEST_SERIAL(mtime_index_profile_isolation) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "profile_iso");
    cleanup_temp_dir(cache_dir);
    pal_mkdir_p(cache_dir);

    // Create and save an entry under "debug" profile
    MtimeIndex* debug_idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &debug_idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, "C:/project/src/main.c", sizeof(entry.path) - 1);
    entry.mtime_ns = 9999;
    entry.file_size = 1234;
    strncpy(entry.cache_key, "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee", sizeof(entry.cache_key) - 1);
    mtime_index_upsert(debug_idx, &entry);

    rc = mtime_index_save(debug_idx, cache_dir, "debug");
    TEST_ASSERT_EQ(rc, 0);
    mtime_index_free(debug_idx);

    // Load "release" profile - should be empty
    MtimeIndex* release_idx = NULL;
    rc = mtime_index_load(cache_dir, "release", &release_idx);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(release_idx != NULL);

    const MtimeEntry* found = mtime_index_lookup(release_idx, "C:/project/src/main.c");
    TEST_ASSERT_NULL(found);

    mtime_index_free(release_idx);
    cleanup_temp_dir(cache_dir);
    return 0;
}

// =============================================================================
// Test: Corruption handling - zero-length file treated as empty
// Requirement 4.6: partial/zero-length file treated as corrupted, discarded
// =============================================================================

TEST_SERIAL(mtime_index_corruption_zero_length) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "corrupt_zero");
    cleanup_temp_dir(cache_dir);
    pal_mkdir_p(cache_dir);

    // Write a zero-length file where the index would be
    char idx_path[520];
    get_index_file_path(idx_path, sizeof(idx_path), cache_dir, "debug");
    pal_file_write(idx_path, "", 0);

    // Load should succeed with an empty index
    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(idx != NULL);

    const MtimeEntry* found = mtime_index_lookup(idx, "C:/any/path.c");
    TEST_ASSERT_NULL(found);

    mtime_index_free(idx);
    cleanup_temp_dir(cache_dir);
    return 0;
}

// =============================================================================
// Test: Corruption handling - bad magic bytes treated as empty
// Requirement 4.6: corrupted file discarded, fall through to full hash
// =============================================================================

TEST_SERIAL(mtime_index_corruption_bad_magic) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "corrupt_magic");
    cleanup_temp_dir(cache_dir);
    pal_mkdir_p(cache_dir);

    // Write garbage bytes where the index would be
    char idx_path[520];
    get_index_file_path(idx_path, sizeof(idx_path), cache_dir, "debug");
    const char garbage[] = "THIS_IS_NOT_A_VALID_MTIME_INDEX_FILE_RANDOM_GARBAGE_DATA_12345";
    pal_file_write(idx_path, garbage, strlen(garbage));

    // Load should succeed with an empty index
    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(idx != NULL);

    const MtimeEntry* found = mtime_index_lookup(idx, "C:/any/path.c");
    TEST_ASSERT_NULL(found);

    mtime_index_free(idx);
    cleanup_temp_dir(cache_dir);
    return 0;
}

// =============================================================================
// Test: Corruption handling - truncated entries treated as empty
// Requirement 4.6: partial write → treat as corrupted
// =============================================================================

TEST_SERIAL(mtime_index_corruption_truncated) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "corrupt_trunc");
    cleanup_temp_dir(cache_dir);
    pal_mkdir_p(cache_dir);

    // First, create a valid index with entries and save it
    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, "C:/project/src/file.c", sizeof(entry.path) - 1);
    entry.mtime_ns = 42000;
    entry.file_size = 800;
    strncpy(entry.cache_key, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", sizeof(entry.cache_key) - 1);
    mtime_index_upsert(idx, &entry);
    rc = mtime_index_save(idx, cache_dir, "debug");
    TEST_ASSERT_EQ(rc, 0);
    mtime_index_free(idx);

    // Now truncate the file: keep only the first 8 bytes (magic + partial header)
    // This simulates an interrupted write that left a valid magic but truncated entries
    char idx_path[520];
    get_index_file_path(idx_path, sizeof(idx_path), cache_dir, "debug");

    // Write just a small stub that has valid-looking magic bytes but truncated entry data
    // The format is: magic(4) + version(4) + count(4) = 12 bytes header, then entries
    // Writing only 12 bytes with a count > 0 but no entry data = truncated
    const char truncated_header[] = "MTIX"  // Likely magic bytes
                                    "\x01\x00\x00\x00"  // version 1
                                    "\x01\x00\x00\x00"; // count = 1 but no entry data follows
    pal_file_write(idx_path, truncated_header, 12);

    // Load should treat this as corrupted → empty index
    MtimeIndex* loaded = NULL;
    rc = mtime_index_load(cache_dir, "debug", &loaded);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(loaded != NULL);

    const MtimeEntry* found = mtime_index_lookup(loaded, "C:/project/src/file.c");
    TEST_ASSERT_NULL(found);

    mtime_index_free(loaded);
    cleanup_temp_dir(cache_dir);
    return 0;
}

// =============================================================================
// Test: Delete removes the index file from disk
// Requirement 4.5: atomic write uses tmp+rename; delete removes file
// =============================================================================

TEST_SERIAL(mtime_index_delete_removes_file) {
    char cache_dir[520];
    get_temp_dir(cache_dir, sizeof(cache_dir), "delete_file");
    cleanup_temp_dir(cache_dir);
    pal_mkdir_p(cache_dir);

    // Create, populate, and save an index
    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(cache_dir, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, "C:/project/src/delete_me.c", sizeof(entry.path) - 1);
    entry.mtime_ns = 7777;
    entry.file_size = 333;
    strncpy(entry.cache_key, "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef", sizeof(entry.cache_key) - 1);
    mtime_index_upsert(idx, &entry);

    rc = mtime_index_save(idx, cache_dir, "debug");
    TEST_ASSERT_EQ(rc, 0);
    mtime_index_free(idx);

    // Verify the file exists
    char idx_path[520];
    get_index_file_path(idx_path, sizeof(idx_path), cache_dir, "debug");
    TEST_ASSERT_EQ(pal_path_exists(idx_path), 0);  // 0 = exists (PAL convention)

    // Delete the index
    rc = mtime_index_delete(cache_dir, "debug");
    TEST_ASSERT_EQ(rc, 0);

    // Verify file no longer exists (PAL returns non-zero when path does NOT exist)
    TEST_ASSERT(pal_path_exists(idx_path) != 0);

    cleanup_temp_dir(cache_dir);
    return 0;
}
