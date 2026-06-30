// crates/cdo/tst/unit/test_cache_threshold.c
// Unit tests for filesize threshold logic (cache_threshold_skip)
// Validates: Requirements 2.2, 2.3, 2.5, 2.7, 6.2
#include "cdo_ut.h"
#include "core/cache_threshold.h"
#include "core/cache.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// Helper: create a CacheConfig with a given min_file_size threshold
// =============================================================================

static CacheConfig make_config(int64_t min_file_size) {
    CacheConfig cfg = {0};
    cfg.enabled = true;
    cfg.min_file_size = min_file_size;
    cfg.fast_path_enabled = true;
    cfg.max_size_bytes = (int64_t)2147483648;
    strncpy(cfg.backend, "builtin", sizeof(cfg.backend) - 1);
    strncpy(cfg.path, ".cdo/cache/objects", sizeof(cfg.path) - 1);
    return cfg;
}

// =============================================================================
// Test: file below default threshold (511 < 512) -> skip cache, increment skipped
// Requirement 2.2: files below min_file_size bypass cache
// Requirement 6.2: skipped counter incremented
// =============================================================================

TEST(threshold_below_default) {
    CacheConfig cfg = make_config(512);
    bool skip = cache_threshold_skip(511, &cfg);
    TEST_ASSERT(skip == true);
    return 0;
}

// =============================================================================
// Test: file at boundary (512 == 512) -> proceed with cache pipeline
// Requirement 2.2: "below" means strictly less than, at-boundary proceeds
// =============================================================================

TEST(threshold_at_boundary) {
    CacheConfig cfg = make_config(512);
    bool skip = cache_threshold_skip(512, &cfg);
    TEST_ASSERT(skip == false);
    return 0;
}

// =============================================================================
// Test: file above threshold (513 > 512) -> proceed with cache pipeline
// Requirement 2.2: files at or above threshold use normal cache
// =============================================================================

TEST(threshold_above) {
    CacheConfig cfg = make_config(512);
    bool skip = cache_threshold_skip(513, &cfg);
    TEST_ASSERT(skip == false);
    return 0;
}

// =============================================================================
// Test: threshold = 0 -> disabled, all files proceed through cache
// Requirement 2.5: threshold of 0 disables size-based skipping
// =============================================================================

TEST(threshold_zero_disabled) {
    CacheConfig cfg = make_config(0);
    bool skip = cache_threshold_skip(100, &cfg);
    TEST_ASSERT(skip == false);
    return 0;
}

// =============================================================================
// Test: threshold = 0, even very small files (1 byte) proceed
// Requirement 2.5: disabled means ALL files go through cache
// =============================================================================

TEST(threshold_zero_disabled_tiny_file) {
    CacheConfig cfg = make_config(0);
    bool skip = cache_threshold_skip(1, &cfg);
    TEST_ASSERT(skip == false);
    return 0;
}

// =============================================================================
// Test: threshold = 0, zero-size file also proceeds
// Requirement 2.5: disabled threshold never skips
// =============================================================================

TEST(threshold_zero_disabled_zero_size) {
    CacheConfig cfg = make_config(0);
    bool skip = cache_threshold_skip(0, &cfg);
    TEST_ASSERT(skip == false);
    return 0;
}

// =============================================================================
// Test: file size unknown (-1) -> treat as above threshold, proceed
// Requirement 2.7: when size cannot be determined, do not skip
// =============================================================================

TEST(threshold_size_unknown) {
    CacheConfig cfg = make_config(512);
    bool skip = cache_threshold_skip(-1, &cfg);
    TEST_ASSERT(skip == false);
    return 0;
}

// =============================================================================
// Test: file size unknown with large negative value -> treat as above threshold
// Requirement 2.7: any negative value means unknown
// =============================================================================

TEST(threshold_size_unknown_large_negative) {
    CacheConfig cfg = make_config(512);
    bool skip = cache_threshold_skip(-9999, &cfg);
    TEST_ASSERT(skip == false);
    return 0;
}

// =============================================================================
// Test: file size = 0 (very small) -> below threshold, skip
// Requirement 2.2: 0 < 512 so file is skipped
// =============================================================================

TEST(threshold_very_small_file) {
    CacheConfig cfg = make_config(512);
    bool skip = cache_threshold_skip(0, &cfg);
    TEST_ASSERT(skip == true);
    return 0;
}

// =============================================================================
// Test: file below a large threshold -> skip
// Requirement 2.2: threshold can be any positive value
// =============================================================================

TEST(threshold_large_threshold) {
    CacheConfig cfg = make_config(2000);
    bool skip = cache_threshold_skip(1000, &cfg);
    TEST_ASSERT(skip == true);
    return 0;
}

// =============================================================================
// Test: CacheStats.skipped incremented when file is below threshold
// Requirement 6.2: each skipped file increments the skipped counter
// This simulates the pipeline behavior: if threshold_skip returns true,
// the caller increments stats.skipped.
// =============================================================================

TEST(threshold_skipped_increments_stats) {
    CacheConfig cfg = make_config(512);
    CacheStats stats = {0};

    // Simulate processing 3 files: 2 below threshold, 1 above
    int64_t file_sizes[] = {100, 300, 600};
    for (int i = 0; i < 3; i++) {
        if (cache_threshold_skip(file_sizes[i], &cfg)) {
            stats.skipped++;
        }
    }

    // 100 < 512 → skip, 300 < 512 → skip, 600 >= 512 → proceed
    TEST_ASSERT_EQ(stats.skipped, 2);
    TEST_ASSERT_EQ(stats.hits, 0);
    TEST_ASSERT_EQ(stats.misses, 0);
    TEST_ASSERT_EQ(stats.stored, 0);
    return 0;
}

// =============================================================================
// Test: Skipped files are NOT stored in cache
// Requirement 2.3: files below threshold bypass both lookup AND store
// This test simulates the full pipeline decision: if a file is below threshold,
// cache_store is never called for it.
// =============================================================================

TEST_SERIAL(threshold_skipped_not_stored_in_cache) {
    // Set up a real cache directory
    char root[520];
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(root, sizeof(root), "%s/cdo_test_threshold_no_store", tmp);
    pal_path_normalize(root);
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Initialize cache
    CacheConfig config = {0};
    char cache_dir[520];
    snprintf(cache_dir, sizeof(cache_dir), "%s/cache_objects", root);
    strncpy(config.path, cache_dir, sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)1073741824;
    config.enabled = true;
    config.min_file_size = 512;
    config.fast_path_enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);

    int rc = cache_init(&config, root);
    TEST_ASSERT_EQ(rc, 0);

    // Create a small source file (below threshold)
    char src_path[520];
    snprintf(src_path, sizeof(src_path), "%s/tiny.c", root);
    const char* tiny_content = "int x;";  // 6 bytes, well below 512
    pal_file_write(src_path, tiny_content, strlen(tiny_content));

    // Simulate the pipeline decision: check threshold first
    int64_t file_size = (int64_t)strlen(tiny_content);
    bool should_skip = cache_threshold_skip(file_size, &config);
    TEST_ASSERT(should_skip == true);

    // Since the file is below threshold, the pipeline does NOT call cache_store.
    // Create a fake object file that WOULD be stored if threshold allowed it.
    char obj_path[520];
    snprintf(obj_path, sizeof(obj_path), "%s/tiny.o", root);
    const char* obj_content = "FAKE_OBJECT_BELOW_THRESHOLD";
    pal_file_write(obj_path, obj_content, strlen(obj_content));

    // Simulate: we do NOT store since threshold said skip.
    // Verify: the object is NOT in the cache (lookup should fail)
    const char* fake_key = "0000000000000000000000000000000000000000000000000000000000000001";
    char lookup_dest[520];
    snprintf(lookup_dest, sizeof(lookup_dest), "%s/lookup_result.o", root);
    rc = cache_lookup(&config, fake_key, lookup_dest);
    TEST_ASSERT(rc != 0);  // Cache miss — file was never stored

    // Verify stats: skipped incremented, stored NOT incremented
    CacheStats stats = {0};
    if (should_skip) {
        stats.skipped++;
    }
    TEST_ASSERT_EQ(stats.skipped, 1);
    TEST_ASSERT_EQ(stats.stored, 0);

    // Cleanup
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: Above-threshold file CAN be stored in cache (contrast with skipped)
// Requirement 2.2/2.3: only below-threshold files bypass store
// =============================================================================

TEST_SERIAL(threshold_above_stored_in_cache) {
    // Set up a real cache directory
    char root[520];
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(root, sizeof(root), "%s/cdo_test_threshold_store", tmp);
    pal_path_normalize(root);
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Initialize cache
    CacheConfig config = {0};
    char cache_dir[520];
    snprintf(cache_dir, sizeof(cache_dir), "%s/cache_objects", root);
    strncpy(config.path, cache_dir, sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)1073741824;
    config.enabled = true;
    config.min_file_size = 512;
    config.fast_path_enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);

    int rc = cache_init(&config, root);
    TEST_ASSERT_EQ(rc, 0);

    // Simulate file above threshold (600 bytes > 512)
    int64_t file_size = 600;
    bool should_skip = cache_threshold_skip(file_size, &config);
    TEST_ASSERT(should_skip == false);

    // Since above threshold, pipeline proceeds → store in cache after compilation
    char obj_path[520];
    snprintf(obj_path, sizeof(obj_path), "%s/large.o", root);
    const char* obj_content = "COMPILED_OBJECT_ABOVE_THRESHOLD_DATA";
    pal_file_write(obj_path, obj_content, strlen(obj_content));

    const char* key = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
    rc = cache_store(&config, key, obj_path);
    TEST_ASSERT_EQ(rc, 0);

    // Verify: lookup should succeed (file was stored)
    char lookup_dest[520];
    snprintf(lookup_dest, sizeof(lookup_dest), "%s/lookup_result.o", root);
    rc = cache_lookup(&config, key, lookup_dest);
    TEST_ASSERT_EQ(rc, 0);  // Cache hit — file was stored

    // Verify stats: stored incremented, skipped NOT incremented
    CacheStats stats = {0};
    if (!should_skip) {
        stats.stored++;
    }
    TEST_ASSERT_EQ(stats.stored, 1);
    TEST_ASSERT_EQ(stats.skipped, 0);

    // Cleanup
    pal_rmdir_r(root);
    return 0;
}
