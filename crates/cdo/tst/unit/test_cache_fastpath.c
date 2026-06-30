// crates/cdo/tst/unit/test_cache_fastpath.c
// Unit tests for fast-path cache validation logic
// Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.8, 1.9, 4.4, 5.3
#include "cdo_ut.h"
#include "core/cache_fastpath.h"
#include "core/mtime_index.h"
#include "model/cache_config.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
// Helpers
// =============================================================================

static void get_temp_dir(char* buf, size_t size, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(buf, size, "%s/cdo_test_fastpath_%s", tmp, suffix);
    pal_path_normalize(buf);
}

static void cleanup_temp_dir(const char* dir) {
    pal_rmdir_r(dir);
}

/// Create a CacheConfig pointing to a temp cache directory.
static CacheConfig make_config(const char* cache_path, bool fast_path_enabled) {
    CacheConfig cfg = {0};
    strncpy(cfg.path, cache_path, sizeof(cfg.path) - 1);
    cfg.max_size_bytes = (int64_t)2147483648;
    cfg.enabled = true;
    strncpy(cfg.backend, "builtin", sizeof(cfg.backend) - 1);
    cfg.fast_path_enabled = fast_path_enabled;
    cfg.min_file_size = 512;
    return cfg;
}

/// Create a fake source file with known content and return its actual mtime/size.
static int create_source_file(const char* path, const char* content, uint64_t* out_mtime, int64_t* out_size) {
    size_t len = strlen(content);
    if (pal_file_write(path, content, len) != 0) return -1;
    if (out_size) *out_size = (int64_t)len;
    if (out_mtime) {
        if (pal_file_mtime(path, out_mtime) != 0) return -1;
    }
    return 0;
}

/// Place a fake cache object at the expected path: <cache_dir>/<key[0:2]>/<key>.o
static int create_cache_object(const char* cache_dir, const char* key) {
    char subdir[4] = {key[0], key[1], '\0'};
    char dir_path[520];
    if (pal_path_join(dir_path, sizeof(dir_path), cache_dir, subdir) != 0) return -1;
    if (pal_mkdir_p(dir_path) != 0) return -1;

    char filename[128];
    snprintf(filename, sizeof(filename), "%s.o", key);
    char obj_path[520];
    if (pal_path_join(obj_path, sizeof(obj_path), dir_path, filename) != 0) return -1;

    const char dummy[] = "FAKE_OBJ";
    return pal_file_write(obj_path, dummy, sizeof(dummy) - 1);
}

// Well-known test cache key (64 hex chars)
#define TEST_KEY_A "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
#define TEST_KEY_B "1111111111111111111111111111111111111111111111111111111111111111"

// =============================================================================
// Test: Matching mtime+size returns stored cache key (fast-path hit)
// Requirement 1.1: mtime+size match → skip SHA-256, serve cached object
// Requirement 1.4: fast-path hit recorded as cache hit
// =============================================================================

TEST_SERIAL(fastpath_matching_mtime_size_returns_hit) {
    char root[520];
    get_temp_dir(root, sizeof(root), "hit");
    cleanup_temp_dir(root);
    pal_mkdir_p(root);

    // Set up cache directory and source file directory
    char cache_dir[520], src_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache");
    pal_path_join(src_dir, sizeof(src_dir), root, "src");
    pal_mkdir_p(cache_dir);
    pal_mkdir_p(src_dir);

    // Create a source file
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), src_dir, "main.c");
    uint64_t real_mtime = 0;
    int64_t real_size = 0;
    int rc = create_source_file(src_path, "int main() { return 0; }", &real_mtime, &real_size);
    TEST_ASSERT_EQ(rc, 0);

    // Create MtimeIndex with matching entry
    MtimeIndex* idx = NULL;
    rc = mtime_index_load(root, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, src_path, sizeof(entry.path) - 1);
    entry.mtime_ns = real_mtime;
    entry.file_size = real_size;
    strncpy(entry.cache_key, TEST_KEY_A, sizeof(entry.cache_key) - 1);
    rc = mtime_index_upsert(idx, &entry);
    TEST_ASSERT_EQ(rc, 0);

    // Place matching cache object on disk
    rc = create_cache_object(cache_dir, TEST_KEY_A);
    TEST_ASSERT_EQ(rc, 0);

    // Configure with fast-path enabled
    CacheConfig cfg = make_config(cache_dir, true);

    // Perform fast-path check
    char out_key[65] = {0};
    FastPathResult result = cache_fastpath_check(src_path, NULL, 0, idx, &cfg, out_key);

    TEST_ASSERT_EQ(result, FASTPATH_HIT);
    TEST_ASSERT_STR_EQ(out_key, TEST_KEY_A);

    mtime_index_free(idx);
    cleanup_temp_dir(root);
    return 0;
}

// =============================================================================
// Test: Mismatched mtime falls through to hash (returns MISS)
// Requirement 1.2: mtime differs → fall through to SHA-256
// =============================================================================

TEST_SERIAL(fastpath_mismatched_mtime_returns_miss) {
    char root[520];
    get_temp_dir(root, sizeof(root), "mtime_miss");
    cleanup_temp_dir(root);
    pal_mkdir_p(root);

    char cache_dir[520], src_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache");
    pal_path_join(src_dir, sizeof(src_dir), root, "src");
    pal_mkdir_p(cache_dir);
    pal_mkdir_p(src_dir);

    // Create source file and get its real mtime
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), src_dir, "util.c");
    uint64_t real_mtime = 0;
    int64_t real_size = 0;
    int rc = create_source_file(src_path, "void util() {}", &real_mtime, &real_size);
    TEST_ASSERT_EQ(rc, 0);

    // Insert entry with a DIFFERENT mtime (stale value)
    MtimeIndex* idx = NULL;
    rc = mtime_index_load(root, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, src_path, sizeof(entry.path) - 1);
    entry.mtime_ns = real_mtime - 1000000;  // Stored mtime is older than actual
    entry.file_size = real_size;             // Size matches
    strncpy(entry.cache_key, TEST_KEY_A, sizeof(entry.cache_key) - 1);
    mtime_index_upsert(idx, &entry);

    // Place cache object (would be a hit if mtime matched)
    create_cache_object(cache_dir, TEST_KEY_A);

    CacheConfig cfg = make_config(cache_dir, true);
    char out_key[65] = {0};
    FastPathResult result = cache_fastpath_check(src_path, NULL, 0, idx, &cfg, out_key);

    TEST_ASSERT_EQ(result, FASTPATH_MISS);

    mtime_index_free(idx);
    cleanup_temp_dir(root);
    return 0;
}

// =============================================================================
// Test: Mismatched file size falls through to hash (returns MISS)
// Requirement 1.2: file size differs → fall through to SHA-256
// =============================================================================

TEST_SERIAL(fastpath_mismatched_size_returns_miss) {
    char root[520];
    get_temp_dir(root, sizeof(root), "size_miss");
    cleanup_temp_dir(root);
    pal_mkdir_p(root);

    char cache_dir[520], src_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache");
    pal_path_join(src_dir, sizeof(src_dir), root, "src");
    pal_mkdir_p(cache_dir);
    pal_mkdir_p(src_dir);

    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), src_dir, "types.c");
    uint64_t real_mtime = 0;
    int64_t real_size = 0;
    int rc = create_source_file(src_path, "typedef int MyInt;", &real_mtime, &real_size);
    TEST_ASSERT_EQ(rc, 0);

    MtimeIndex* idx = NULL;
    rc = mtime_index_load(root, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, src_path, sizeof(entry.path) - 1);
    entry.mtime_ns = real_mtime;           // Mtime matches
    entry.file_size = real_size + 100;     // Size is WRONG (stored bigger than actual)
    strncpy(entry.cache_key, TEST_KEY_A, sizeof(entry.cache_key) - 1);
    mtime_index_upsert(idx, &entry);

    create_cache_object(cache_dir, TEST_KEY_A);

    CacheConfig cfg = make_config(cache_dir, true);
    char out_key[65] = {0};
    FastPathResult result = cache_fastpath_check(src_path, NULL, 0, idx, &cfg, out_key);

    TEST_ASSERT_EQ(result, FASTPATH_MISS);

    mtime_index_free(idx);
    cleanup_temp_dir(root);
    return 0;
}

// =============================================================================
// Test: Missing MtimeIndex entry falls through to hash
// Requirement 1.3: no entry in index → fall through to SHA-256
// =============================================================================

TEST_SERIAL(fastpath_missing_entry_returns_miss) {
    char root[520];
    get_temp_dir(root, sizeof(root), "no_entry");
    cleanup_temp_dir(root);
    pal_mkdir_p(root);

    char cache_dir[520], src_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache");
    pal_path_join(src_dir, sizeof(src_dir), root, "src");
    pal_mkdir_p(cache_dir);
    pal_mkdir_p(src_dir);

    // Create source file but do NOT add to index
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), src_dir, "new_file.c");
    int rc = create_source_file(src_path, "void new_func() {}", NULL, NULL);
    TEST_ASSERT_EQ(rc, 0);

    // Load empty index (no entries)
    MtimeIndex* idx = NULL;
    rc = mtime_index_load(root, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    CacheConfig cfg = make_config(cache_dir, true);
    char out_key[65] = {0};
    FastPathResult result = cache_fastpath_check(src_path, NULL, 0, idx, &cfg, out_key);

    TEST_ASSERT_EQ(result, FASTPATH_MISS);

    mtime_index_free(idx);
    cleanup_temp_dir(root);
    return 0;
}

// =============================================================================
// Test: Stale cache key (object not in Build_Cache) removes entry, returns MISS
// Requirement 1.9: object missing from cache → remove stale entry, fall through
// =============================================================================

TEST_SERIAL(fastpath_stale_key_removes_entry_returns_miss) {
    char root[520];
    get_temp_dir(root, sizeof(root), "stale_key");
    cleanup_temp_dir(root);
    pal_mkdir_p(root);

    char cache_dir[520], src_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache");
    pal_path_join(src_dir, sizeof(src_dir), root, "src");
    pal_mkdir_p(cache_dir);
    pal_mkdir_p(src_dir);

    // Create source file
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), src_dir, "stale.c");
    uint64_t real_mtime = 0;
    int64_t real_size = 0;
    int rc = create_source_file(src_path, "void stale() {}", &real_mtime, &real_size);
    TEST_ASSERT_EQ(rc, 0);

    // Insert entry with matching mtime+size but do NOT create the cache object
    MtimeIndex* idx = NULL;
    rc = mtime_index_load(root, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, src_path, sizeof(entry.path) - 1);
    entry.mtime_ns = real_mtime;
    entry.file_size = real_size;
    strncpy(entry.cache_key, TEST_KEY_B, sizeof(entry.cache_key) - 1);
    mtime_index_upsert(idx, &entry);

    // Do NOT create cache object → stale reference
    CacheConfig cfg = make_config(cache_dir, true);
    char out_key[65] = {0};
    FastPathResult result = cache_fastpath_check(src_path, NULL, 0, idx, &cfg, out_key);

    TEST_ASSERT_EQ(result, FASTPATH_MISS);

    // Verify the stale entry was removed from the index
    const MtimeEntry* lookup = mtime_index_lookup(idx, src_path);
    TEST_ASSERT_NULL(lookup);

    mtime_index_free(idx);
    cleanup_temp_dir(root);
    return 0;
}

// =============================================================================
// Test: PAL mtime failure treats file as changed, falls through to hash
// Requirement 1.8: PAL mtime failure → treat as changed, fall through
// =============================================================================

TEST_SERIAL(fastpath_pal_mtime_failure_returns_miss) {
    char root[520];
    get_temp_dir(root, sizeof(root), "mtime_fail");
    cleanup_temp_dir(root);
    pal_mkdir_p(root);

    char cache_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache");
    pal_mkdir_p(cache_dir);

    // Reference a non-existent source file (pal_file_mtime will fail)
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "nonexistent_file.c");

    // Insert an entry for this path in the index (pretend it existed before)
    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(root, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, src_path, sizeof(entry.path) - 1);
    entry.mtime_ns = 9999999;
    entry.file_size = 1024;
    strncpy(entry.cache_key, TEST_KEY_A, sizeof(entry.cache_key) - 1);
    mtime_index_upsert(idx, &entry);

    // Even if cache object exists, mtime failure should return MISS
    create_cache_object(cache_dir, TEST_KEY_A);

    CacheConfig cfg = make_config(cache_dir, true);
    char out_key[65] = {0};
    FastPathResult result = cache_fastpath_check(src_path, NULL, 0, idx, &cfg, out_key);

    TEST_ASSERT_EQ(result, FASTPATH_MISS);

    mtime_index_free(idx);
    cleanup_temp_dir(root);
    return 0;
}

// =============================================================================
// Test: Deleted file tracked in index → treats as miss, removes stale entry
// Requirement 4.4: file no longer exists on filesystem → miss, remove stale entry
// =============================================================================

TEST_SERIAL(fastpath_deleted_file_removes_stale_entry) {
    char root[520];
    get_temp_dir(root, sizeof(root), "deleted");
    cleanup_temp_dir(root);
    pal_mkdir_p(root);

    char cache_dir[520], src_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache");
    pal_path_join(src_dir, sizeof(src_dir), root, "src");
    pal_mkdir_p(cache_dir);
    pal_mkdir_p(src_dir);

    // Build a path for a file that does NOT exist on disk
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), src_dir, "deleted_file.c");

    // Insert entry in index as if this file was previously tracked
    MtimeIndex* idx = NULL;
    int rc = mtime_index_load(root, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, src_path, sizeof(entry.path) - 1);
    entry.mtime_ns = 5000000;
    entry.file_size = 256;
    strncpy(entry.cache_key, TEST_KEY_B, sizeof(entry.cache_key) - 1);
    mtime_index_upsert(idx, &entry);

    create_cache_object(cache_dir, TEST_KEY_B);

    CacheConfig cfg = make_config(cache_dir, true);
    char out_key[65] = {0};
    FastPathResult result = cache_fastpath_check(src_path, NULL, 0, idx, &cfg, out_key);

    TEST_ASSERT_EQ(result, FASTPATH_MISS);

    // Entry should be removed from index since file no longer exists
    const MtimeEntry* lookup = mtime_index_lookup(idx, src_path);
    TEST_ASSERT_NULL(lookup);

    mtime_index_free(idx);
    cleanup_temp_dir(root);
    return 0;
}

// =============================================================================
// Test: Fast-path disabled (fast_path_enabled = false) always falls through
// Requirement 5.3: fast-path disabled → skip mtime check, always full hash
// =============================================================================

TEST_SERIAL(fastpath_disabled_always_returns_miss) {
    char root[520];
    get_temp_dir(root, sizeof(root), "disabled");
    cleanup_temp_dir(root);
    pal_mkdir_p(root);

    char cache_dir[520], src_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache");
    pal_path_join(src_dir, sizeof(src_dir), root, "src");
    pal_mkdir_p(cache_dir);
    pal_mkdir_p(src_dir);

    // Create a source file with perfectly matching index entry
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), src_dir, "perfect.c");
    uint64_t real_mtime = 0;
    int64_t real_size = 0;
    int rc = create_source_file(src_path, "int perfect() { return 42; }", &real_mtime, &real_size);
    TEST_ASSERT_EQ(rc, 0);

    MtimeIndex* idx = NULL;
    rc = mtime_index_load(root, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry entry = {0};
    strncpy(entry.path, src_path, sizeof(entry.path) - 1);
    entry.mtime_ns = real_mtime;
    entry.file_size = real_size;
    strncpy(entry.cache_key, TEST_KEY_A, sizeof(entry.cache_key) - 1);
    mtime_index_upsert(idx, &entry);

    create_cache_object(cache_dir, TEST_KEY_A);

    // Disable fast-path
    CacheConfig cfg = make_config(cache_dir, false);
    char out_key[65] = {0};
    FastPathResult result = cache_fastpath_check(src_path, NULL, 0, idx, &cfg, out_key);

    // Even though everything matches, fast-path is disabled → MISS
    TEST_ASSERT_EQ(result, FASTPATH_MISS);

    mtime_index_free(idx);
    cleanup_temp_dir(root);
    return 0;
}

// =============================================================================
// Test: Fast-path hit with dependency headers all matching
// Requirement 1.1: source + ALL dep headers must match for fast-path hit
// =============================================================================

TEST_SERIAL(fastpath_hit_with_matching_dep_headers) {
    char root[520];
    get_temp_dir(root, sizeof(root), "deps_hit");
    cleanup_temp_dir(root);
    pal_mkdir_p(root);

    char cache_dir[520], src_dir[520], inc_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache");
    pal_path_join(src_dir, sizeof(src_dir), root, "src");
    pal_path_join(inc_dir, sizeof(inc_dir), root, "include");
    pal_mkdir_p(cache_dir);
    pal_mkdir_p(src_dir);
    pal_mkdir_p(inc_dir);

    // Create source file
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), src_dir, "app.c");
    uint64_t src_mtime = 0;
    int64_t src_size = 0;
    int rc = create_source_file(src_path, "#include \"app.h\"\nint main() {}", &src_mtime, &src_size);
    TEST_ASSERT_EQ(rc, 0);

    // Create header file
    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), inc_dir, "app.h");
    uint64_t hdr_mtime = 0;
    int64_t hdr_size = 0;
    rc = create_source_file(hdr_path, "#ifndef APP_H\n#define APP_H\n#endif", &hdr_mtime, &hdr_size);
    TEST_ASSERT_EQ(rc, 0);

    // Insert entries for both source and header
    MtimeIndex* idx = NULL;
    rc = mtime_index_load(root, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    MtimeEntry src_entry = {0};
    strncpy(src_entry.path, src_path, sizeof(src_entry.path) - 1);
    src_entry.mtime_ns = src_mtime;
    src_entry.file_size = src_size;
    strncpy(src_entry.cache_key, TEST_KEY_A, sizeof(src_entry.cache_key) - 1);
    mtime_index_upsert(idx, &src_entry);

    MtimeEntry hdr_entry = {0};
    strncpy(hdr_entry.path, hdr_path, sizeof(hdr_entry.path) - 1);
    hdr_entry.mtime_ns = hdr_mtime;
    hdr_entry.file_size = hdr_size;
    strncpy(hdr_entry.cache_key, TEST_KEY_A, sizeof(hdr_entry.cache_key) - 1);
    mtime_index_upsert(idx, &hdr_entry);

    // Place cache object
    create_cache_object(cache_dir, TEST_KEY_A);

    CacheConfig cfg = make_config(cache_dir, true);
    const char* deps[] = {hdr_path};
    char out_key[65] = {0};
    FastPathResult result = cache_fastpath_check(src_path, deps, 1, idx, &cfg, out_key);

    TEST_ASSERT_EQ(result, FASTPATH_HIT);
    TEST_ASSERT_STR_EQ(out_key, TEST_KEY_A);

    mtime_index_free(idx);
    cleanup_temp_dir(root);
    return 0;
}

// =============================================================================
// Test: Dep header mtime mismatch causes fast-path miss
// Requirement 1.2: any dep header mismatch → fall through to SHA-256
// =============================================================================

TEST_SERIAL(fastpath_dep_header_mismatch_returns_miss) {
    char root[520];
    get_temp_dir(root, sizeof(root), "deps_miss");
    cleanup_temp_dir(root);
    pal_mkdir_p(root);

    char cache_dir[520], src_dir[520], inc_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache");
    pal_path_join(src_dir, sizeof(src_dir), root, "src");
    pal_path_join(inc_dir, sizeof(inc_dir), root, "include");
    pal_mkdir_p(cache_dir);
    pal_mkdir_p(src_dir);
    pal_mkdir_p(inc_dir);

    // Create source file
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), src_dir, "lib.c");
    uint64_t src_mtime = 0;
    int64_t src_size = 0;
    int rc = create_source_file(src_path, "void lib_fn() {}", &src_mtime, &src_size);
    TEST_ASSERT_EQ(rc, 0);

    // Create header file
    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), inc_dir, "lib.h");
    uint64_t hdr_mtime = 0;
    int64_t hdr_size = 0;
    rc = create_source_file(hdr_path, "void lib_fn();", &hdr_mtime, &hdr_size);
    TEST_ASSERT_EQ(rc, 0);

    MtimeIndex* idx = NULL;
    rc = mtime_index_load(root, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    // Source entry matches perfectly
    MtimeEntry src_entry = {0};
    strncpy(src_entry.path, src_path, sizeof(src_entry.path) - 1);
    src_entry.mtime_ns = src_mtime;
    src_entry.file_size = src_size;
    strncpy(src_entry.cache_key, TEST_KEY_A, sizeof(src_entry.cache_key) - 1);
    mtime_index_upsert(idx, &src_entry);

    // Header entry has WRONG mtime (simulates header was modified)
    MtimeEntry hdr_entry = {0};
    strncpy(hdr_entry.path, hdr_path, sizeof(hdr_entry.path) - 1);
    hdr_entry.mtime_ns = hdr_mtime - 5000000;  // Stored is older than actual
    hdr_entry.file_size = hdr_size;
    strncpy(hdr_entry.cache_key, TEST_KEY_A, sizeof(hdr_entry.cache_key) - 1);
    mtime_index_upsert(idx, &hdr_entry);

    create_cache_object(cache_dir, TEST_KEY_A);

    CacheConfig cfg = make_config(cache_dir, true);
    const char* deps[] = {hdr_path};
    char out_key[65] = {0};
    FastPathResult result = cache_fastpath_check(src_path, deps, 1, idx, &cfg, out_key);

    // Header mismatch → fall through
    TEST_ASSERT_EQ(result, FASTPATH_MISS);

    mtime_index_free(idx);
    cleanup_temp_dir(root);
    return 0;
}

// =============================================================================
// Test: Dep header missing from index causes fast-path miss
// Requirement 1.3: missing entry for dep header → fall through
// =============================================================================

TEST_SERIAL(fastpath_dep_header_missing_entry_returns_miss) {
    char root[520];
    get_temp_dir(root, sizeof(root), "deps_noentry");
    cleanup_temp_dir(root);
    pal_mkdir_p(root);

    char cache_dir[520], src_dir[520], inc_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache");
    pal_path_join(src_dir, sizeof(src_dir), root, "src");
    pal_path_join(inc_dir, sizeof(inc_dir), root, "include");
    pal_mkdir_p(cache_dir);
    pal_mkdir_p(src_dir);
    pal_mkdir_p(inc_dir);

    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), src_dir, "mod.c");
    uint64_t src_mtime = 0;
    int64_t src_size = 0;
    int rc = create_source_file(src_path, "void mod() {}", &src_mtime, &src_size);
    TEST_ASSERT_EQ(rc, 0);

    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), inc_dir, "mod.h");
    rc = create_source_file(hdr_path, "void mod();", NULL, NULL);
    TEST_ASSERT_EQ(rc, 0);

    MtimeIndex* idx = NULL;
    rc = mtime_index_load(root, "debug", &idx);
    TEST_ASSERT_EQ(rc, 0);

    // Only insert source entry, NOT the header entry
    MtimeEntry src_entry = {0};
    strncpy(src_entry.path, src_path, sizeof(src_entry.path) - 1);
    src_entry.mtime_ns = src_mtime;
    src_entry.file_size = src_size;
    strncpy(src_entry.cache_key, TEST_KEY_A, sizeof(src_entry.cache_key) - 1);
    mtime_index_upsert(idx, &src_entry);

    create_cache_object(cache_dir, TEST_KEY_A);

    CacheConfig cfg = make_config(cache_dir, true);
    const char* deps[] = {hdr_path};
    char out_key[65] = {0};
    FastPathResult result = cache_fastpath_check(src_path, deps, 1, idx, &cfg, out_key);

    // Header not in index → miss
    TEST_ASSERT_EQ(result, FASTPATH_MISS);

    mtime_index_free(idx);
    cleanup_temp_dir(root);
    return 0;
}

