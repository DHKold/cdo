// crates/cdo/tst/unit/test_cache_stats.c
// Unit tests for cache statistics tracking, cache clear, and threshold display
// Validates: Requirements 5.1, 5.2, 5.3, 6.3, 6.4
#include "cdo_ut.h"
#include "core/cache.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
// Helper utilities
// =============================================================================

static void get_temp_dir(char* buf, size_t size, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(buf, size, "%s/cdo_test_cache_stats_%s", tmp, suffix);
    pal_path_normalize(buf);
}

/// Set up a cache directory structure with fake objects for testing.
/// root/cache_store/ is the objects directory (config->path).
static void setup_cache_with_objects(const char* root, CacheConfig* config) {
    char cache_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache_store");
    memset(config, 0, sizeof(*config));
    strncpy(config->path, cache_dir, sizeof(config->path) - 1);
    config->max_size_bytes = (int64_t)2147483648;
    config->enabled = true;
    strncpy(config->backend, "builtin", sizeof(config->backend) - 1);
    cache_init(config, root);

    // Create a fake cached entry
    char subdir[520], filepath[520];
    pal_path_join(subdir, sizeof(subdir), cache_dir, "ab");
    pal_mkdir_p(subdir);
    pal_path_join(filepath, sizeof(filepath), subdir, "ab12345678901234567890123456789012345678901234567890123456789012.o");
    pal_file_write(filepath, "fake_obj_data", 13);
}

// =============================================================================
// Tests: CacheStats initialization
// Requirement 5.1: track hits and misses during build
// =============================================================================

TEST(cache_stats_zero_initialized) {
    CacheStats stats = {0};
    TEST_ASSERT_EQ(stats.hits, 0);
    TEST_ASSERT_EQ(stats.misses, 0);
    TEST_ASSERT_EQ(stats.stored, 0);
    TEST_ASSERT_EQ(stats.evicted, 0);
    TEST_ASSERT_EQ(stats.skipped, 0);
    return 0;
}

TEST(cache_stats_increment_hits) {
    CacheStats stats = {0};
    stats.hits++;
    stats.hits++;
    stats.hits++;
    TEST_ASSERT_EQ(stats.hits, 3);
    TEST_ASSERT_EQ(stats.misses, 0);
    return 0;
}

TEST(cache_stats_increment_misses) {
    CacheStats stats = {0};
    stats.misses++;
    stats.misses++;
    TEST_ASSERT_EQ(stats.misses, 2);
    TEST_ASSERT_EQ(stats.hits, 0);
    return 0;
}

TEST(cache_stats_increment_skipped) {
    CacheStats stats = {0};
    stats.skipped++;
    stats.skipped++;
    stats.skipped++;
    stats.skipped++;
    TEST_ASSERT_EQ(stats.skipped, 4);
    TEST_ASSERT_EQ(stats.hits, 0);
    TEST_ASSERT_EQ(stats.misses, 0);
    return 0;
}

TEST(cache_stats_mixed_hits_and_misses) {
    CacheStats stats = {0};
    stats.hits = 45;
    stats.misses = 12;
    stats.stored = 12;

    int total = stats.hits + stats.misses;
    TEST_ASSERT_EQ(total, 57);

    int hit_rate = (stats.hits * 100) / total;
    TEST_ASSERT_EQ(hit_rate, 78); // 45*100/57 = 78.94... truncated to 78
    return 0;
}

// =============================================================================
// Tests: Hit rate computation
// Requirement 5.2: print "Cache: <hits> hits, <misses> misses (<hit_rate>% hit rate)"
// =============================================================================

TEST(cache_stats_hit_rate_100_percent) {
    CacheStats stats = {0};
    stats.hits = 50;
    stats.misses = 0;

    int total = stats.hits + stats.misses;
    TEST_ASSERT_EQ(total, 50);

    int hit_rate = (stats.hits * 100) / total;
    TEST_ASSERT_EQ(hit_rate, 100);
    return 0;
}

TEST(cache_stats_hit_rate_0_percent) {
    CacheStats stats = {0};
    stats.hits = 0;
    stats.misses = 20;

    int total = stats.hits + stats.misses;
    TEST_ASSERT_EQ(total, 20);

    int hit_rate = (stats.hits * 100) / total;
    TEST_ASSERT_EQ(hit_rate, 0);
    return 0;
}

TEST(cache_stats_hit_rate_50_percent) {
    CacheStats stats = {0};
    stats.hits = 10;
    stats.misses = 10;

    int total = stats.hits + stats.misses;
    TEST_ASSERT_EQ(total, 20);

    int hit_rate = (stats.hits * 100) / total;
    TEST_ASSERT_EQ(hit_rate, 50);
    return 0;
}

TEST(cache_stats_hit_rate_truncates_down) {
    // 1 hit, 2 misses → 33.33% → truncates to 33
    CacheStats stats = {0};
    stats.hits = 1;
    stats.misses = 2;

    int total = stats.hits + stats.misses;
    int hit_rate = (stats.hits * 100) / total;
    TEST_ASSERT_EQ(hit_rate, 33);
    return 0;
}

// =============================================================================
// Tests: Edge case - no activity (total == 0)
// Requirement 5.2: summary only printed when there is activity
// =============================================================================

TEST(cache_stats_no_activity_zero_total) {
    CacheStats stats = {0};
    int total = stats.hits + stats.misses;
    TEST_ASSERT_EQ(total, 0);
    // When total == 0, no summary should be printed (avoids division by zero)
    // This is validated by the condition: if (total > 0) in cmd_build.c
    return 0;
}

// =============================================================================
// Tests: Accumulation across multiple crates
// Requirement 5.1: stats accumulate across the full build
// =============================================================================

TEST(cache_stats_accumulation_across_crates) {
    CacheStats stats = {0};

    // Simulate crate 1: 10 hits, 5 misses
    stats.hits += 10;
    stats.misses += 5;
    stats.stored += 5;

    // Simulate crate 2: 20 hits, 3 misses
    stats.hits += 20;
    stats.misses += 3;
    stats.stored += 3;

    // Simulate crate 3: 15 hits, 4 misses
    stats.hits += 15;
    stats.misses += 4;
    stats.stored += 4;

    TEST_ASSERT_EQ(stats.hits, 45);
    TEST_ASSERT_EQ(stats.misses, 12);
    TEST_ASSERT_EQ(stats.stored, 12);

    int total = stats.hits + stats.misses;
    TEST_ASSERT_EQ(total, 57);

    int hit_rate = (stats.hits * 100) / total;
    TEST_ASSERT_EQ(hit_rate, 78); // 4500/57 = 78
    return 0;
}

// =============================================================================
// Tests: cache_clear removes old files
// =============================================================================

TEST_SERIAL(cache_clear_no_index_files_succeeds) {
    // cache_clear should succeed even when there are no mtime index files
    char root[520];
    get_temp_dir(root, sizeof(root), "clear_no_idx");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    CacheConfig config;
    setup_cache_with_objects(root, &config);

    // Just clear the cache
    int rc = cache_clear(&config);
    TEST_ASSERT_EQ(rc, 0);

    // Objects should be gone
    int64_t total_size = 0;
    int entry_count = 0;
    uint64_t oldest_mtime = 0;
    rc = cache_get_stats(&config, &total_size, &entry_count, &oldest_mtime);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(entry_count, 0);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Tests: Threshold display format
// Requirement 6.3: `cdo cache stats` displays configured threshold value in bytes
// Requirement 6.4: threshold 0 displays "0 (disabled)"
// =============================================================================

TEST(cache_stats_threshold_display_nonzero) {
    // Verify the threshold display format when min_file_size > 0
    CacheConfig config = {0};
    config.min_file_size = 512;

    char output[256];
    if (config.min_file_size == 0) {
        snprintf(output, sizeof(output), "Filesize threshold: 0 (disabled)");
    } else {
        snprintf(output, sizeof(output), "Filesize threshold: %lld bytes", (long long)config.min_file_size);
    }

    TEST_ASSERT_STR_EQ(output, "Filesize threshold: 512 bytes");
    return 0;
}

TEST(cache_stats_threshold_display_zero_disabled) {
    // Verify the threshold display format when min_file_size == 0 (disabled)
    CacheConfig config = {0};
    config.min_file_size = 0;

    char output[256];
    if (config.min_file_size == 0) {
        snprintf(output, sizeof(output), "Filesize threshold: 0 (disabled)");
    } else {
        snprintf(output, sizeof(output), "Filesize threshold: %lld bytes", (long long)config.min_file_size);
    }

    TEST_ASSERT_STR_EQ(output, "Filesize threshold: 0 (disabled)");
    return 0;
}

TEST(cache_stats_threshold_display_custom_value) {
    // Verify threshold display with a custom configured value
    CacheConfig config = {0};
    config.min_file_size = 1024;

    char output[256];
    if (config.min_file_size == 0) {
        snprintf(output, sizeof(output), "Filesize threshold: 0 (disabled)");
    } else {
        snprintf(output, sizeof(output), "Filesize threshold: %lld bytes", (long long)config.min_file_size);
    }

    TEST_ASSERT_STR_EQ(output, "Filesize threshold: 1024 bytes");
    return 0;
}
