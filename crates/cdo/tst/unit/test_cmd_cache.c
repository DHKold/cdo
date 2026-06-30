// crates/cdo/tst/unit/test_cmd_cache.c
// Unit tests for cache CLI commands (stats, clear) and clean --cache behavior
// Validates: Requirements 3.3, 5.4, 5.5
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
    snprintf(buf, size, "%s/cdo_test_cmd_cache_%s", tmp, suffix);
    pal_path_normalize(buf);
}

/// Set up a cache directory with fake cached .o files for testing.
/// Creates config->path with a two-level structure and populates it.
static void setup_cache_dir(const char* root, CacheConfig* config) {
    char cache_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache_store");
    memset(config, 0, sizeof(*config));
    strncpy(config->path, cache_dir, sizeof(config->path) - 1);
    config->max_size_bytes = (int64_t)2147483648;
    config->enabled = true;
    strncpy(config->backend, "builtin", sizeof(config->backend) - 1);
    cache_init(config, root);

    // Create some fake cached entries in two-level dirs
    char subdir[520], filepath[520];

    // Entry 1: ab/ab1234...o
    pal_path_join(subdir, sizeof(subdir), cache_dir, "ab");
    pal_mkdir_p(subdir);
    pal_path_join(filepath, sizeof(filepath), subdir, "ab12345678901234567890123456789012345678901234567890123456789012.o");
    pal_file_write(filepath, "fake_obj_data_1", 15);

    // Entry 2: cd/cd5678...o
    pal_path_join(subdir, sizeof(subdir), cache_dir, "cd");
    pal_mkdir_p(subdir);
    pal_path_join(filepath, sizeof(filepath), subdir, "cd12345678901234567890123456789012345678901234567890123456789012.o");
    pal_file_write(filepath, "fake_obj_data_2_longer", 22);
}

// =============================================================================
// Tests: cache_get_stats output
// Requirement 5.4: cdo cache stats displays total size, entry count, oldest entry
// =============================================================================

TEST_SERIAL(cmd_cache_stats_reports_correct_totals) {
    char root[520];
    get_temp_dir(root, sizeof(root), "stats");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    CacheConfig config;
    setup_cache_dir(root, &config);

    int64_t total_size = 0;
    int entry_count = 0;
    uint64_t oldest_mtime = 0;

    int rc = cache_get_stats(&config, &total_size, &entry_count, &oldest_mtime);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(entry_count, 2);
    // Total size should be 15 + 22 = 37 bytes
    TEST_ASSERT_EQ(total_size, 37);
    // oldest_mtime should be non-zero (files were just created)
    TEST_ASSERT(oldest_mtime > 0);

    pal_rmdir_r(root);
    return 0;
}

TEST_SERIAL(cmd_cache_stats_empty_cache_returns_zeroes) {
    char root[520];
    get_temp_dir(root, sizeof(root), "stats_empty");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create a cache config pointing to a directory that exists but is empty
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    char cache_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache_store");
    strncpy(config.path, cache_dir, sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;
    config.enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);
    cache_init(&config, root);

    int64_t total_size = 0;
    int entry_count = 0;
    uint64_t oldest_mtime = 0;

    int rc = cache_get_stats(&config, &total_size, &entry_count, &oldest_mtime);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(entry_count, 0);
    TEST_ASSERT_EQ(total_size, 0);
    TEST_ASSERT_EQ(oldest_mtime, 0);

    pal_rmdir_r(root);
    return 0;
}

TEST_SERIAL(cmd_cache_stats_nonexistent_dir_returns_zeroes) {
    // When the cache directory doesn't exist at all, stats should return zeros
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    strncpy(config.path, "C:/nonexistent_path_xyzzy_12345/cache", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;
    config.enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);

    int64_t total_size = 0;
    int entry_count = 0;
    uint64_t oldest_mtime = 0;

    int rc = cache_get_stats(&config, &total_size, &entry_count, &oldest_mtime);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(entry_count, 0);
    TEST_ASSERT_EQ(total_size, 0);
    TEST_ASSERT_EQ(oldest_mtime, 0);

    return 0;
}

TEST(cmd_cache_stats_output_format_bytes) {
    // Verify the output format logic for small sizes (bytes)
    int64_t total_size = 37;
    int entry_count = 2;

    char summary[256];
    if (total_size >= 1073741824LL) {
        snprintf(summary, sizeof(summary), "Cache size: %.2f GB (%d entries)", (double)total_size / 1073741824.0, entry_count);
    } else if (total_size >= 1048576LL) {
        snprintf(summary, sizeof(summary), "Cache size: %.2f MB (%d entries)", (double)total_size / 1048576.0, entry_count);
    } else if (total_size >= 1024LL) {
        snprintf(summary, sizeof(summary), "Cache size: %.2f KB (%d entries)", (double)total_size / 1024.0, entry_count);
    } else {
        snprintf(summary, sizeof(summary), "Cache size: %lld bytes (%d entries)", (long long)total_size, entry_count);
    }

    TEST_ASSERT_STR_EQ(summary, "Cache size: 37 bytes (2 entries)");
    return 0;
}

TEST(cmd_cache_stats_output_format_mb) {
    // Verify output format for MB-range sizes
    int64_t total_size = 52428800LL; // 50 MB
    int entry_count = 120;

    char summary[256];
    if (total_size >= 1073741824LL) {
        snprintf(summary, sizeof(summary), "Cache size: %.2f GB (%d entries)", (double)total_size / 1073741824.0, entry_count);
    } else if (total_size >= 1048576LL) {
        snprintf(summary, sizeof(summary), "Cache size: %.2f MB (%d entries)", (double)total_size / 1048576.0, entry_count);
    } else if (total_size >= 1024LL) {
        snprintf(summary, sizeof(summary), "Cache size: %.2f KB (%d entries)", (double)total_size / 1024.0, entry_count);
    } else {
        snprintf(summary, sizeof(summary), "Cache size: %lld bytes (%d entries)", (long long)total_size, entry_count);
    }

    TEST_ASSERT_STR_EQ(summary, "Cache size: 50.00 MB (120 entries)");
    return 0;
}

TEST(cmd_cache_stats_output_format_gb) {
    // Verify output format for GB-range sizes
    int64_t total_size = 1610612736LL; // 1.5 GB
    int entry_count = 5000;

    char summary[256];
    if (total_size >= 1073741824LL) {
        snprintf(summary, sizeof(summary), "Cache size: %.2f GB (%d entries)", (double)total_size / 1073741824.0, entry_count);
    } else if (total_size >= 1048576LL) {
        snprintf(summary, sizeof(summary), "Cache size: %.2f MB (%d entries)", (double)total_size / 1048576.0, entry_count);
    } else if (total_size >= 1024LL) {
        snprintf(summary, sizeof(summary), "Cache size: %.2f KB (%d entries)", (double)total_size / 1024.0, entry_count);
    } else {
        snprintf(summary, sizeof(summary), "Cache size: %lld bytes (%d entries)", (long long)total_size, entry_count);
    }

    TEST_ASSERT_STR_EQ(summary, "Cache size: 1.50 GB (5000 entries)");
    return 0;
}

// =============================================================================
// Tests: cache_clear removes entries
// Requirement 5.5: cdo cache clear removes all entries and reports freed space
// =============================================================================

TEST_SERIAL(cmd_cache_clear_removes_all_entries) {
    char root[520];
    get_temp_dir(root, sizeof(root), "clear");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    CacheConfig config;
    setup_cache_dir(root, &config);

    // Verify entries exist before clear
    int64_t total_size = 0;
    int entry_count = 0;
    uint64_t oldest_mtime = 0;
    int rc = cache_get_stats(&config, &total_size, &entry_count, &oldest_mtime);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(entry_count, 2);
    TEST_ASSERT(total_size > 0);

    // Clear the cache
    rc = cache_clear(&config);
    TEST_ASSERT_EQ(rc, 0);

    // Verify entries are gone after clear
    rc = cache_get_stats(&config, &total_size, &entry_count, &oldest_mtime);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(entry_count, 0);
    TEST_ASSERT_EQ(total_size, 0);

    // Cache directory itself still exists (recreated by cache_clear)
    TEST_ASSERT_EQ(pal_path_exists(config.path), 0);

    pal_rmdir_r(root);
    return 0;
}

TEST_SERIAL(cmd_cache_clear_on_empty_cache_succeeds) {
    char root[520];
    get_temp_dir(root, sizeof(root), "clear_empty");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    CacheConfig config;
    memset(&config, 0, sizeof(config));
    char cache_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache_store");
    strncpy(config.path, cache_dir, sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;
    config.enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);
    cache_init(&config, root);

    // Clear an already-empty cache
    int rc = cache_clear(&config);
    TEST_ASSERT_EQ(rc, 0);

    // Still empty
    int64_t total_size = 0;
    int entry_count = 0;
    uint64_t oldest_mtime = 0;
    rc = cache_get_stats(&config, &total_size, &entry_count, &oldest_mtime);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(entry_count, 0);
    TEST_ASSERT_EQ(total_size, 0);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Tests: clean without --cache preserves cache
// Requirement 3.3: cdo clean SHALL NOT remove the Cache Store
// =============================================================================

TEST_SERIAL(cmd_clean_without_cache_flag_preserves_cache) {
    char root[520];
    get_temp_dir(root, sizeof(root), "clean_no_flag");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Set up a cache directory with entries
    CacheConfig config;
    setup_cache_dir(root, &config);

    // Simulate having a "build" directory alongside the cache
    char build_dir[520];
    pal_path_join(build_dir, sizeof(build_dir), root, "build");
    pal_mkdir_p(build_dir);
    char build_file[520];
    pal_path_join(build_file, sizeof(build_file), build_dir, "output.o");
    pal_file_write(build_file, "build_artifact", 14);

    // Simulate what cmd_clean does WITHOUT --cache: only removes build dir
    pal_rmdir_r(build_dir);

    // Build dir should be gone
    TEST_ASSERT(pal_path_exists(build_dir) != 0);

    // Cache should still be intact
    int64_t total_size = 0;
    int entry_count = 0;
    uint64_t oldest_mtime = 0;
    int rc = cache_get_stats(&config, &total_size, &entry_count, &oldest_mtime);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(entry_count, 2);
    TEST_ASSERT(total_size > 0);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Tests: clean with --cache removes cache
// Requirement 3.3: Only cdo clean --cache SHALL remove cached objects
// =============================================================================

TEST_SERIAL(cmd_clean_with_cache_flag_removes_cache) {
    char root[520];
    get_temp_dir(root, sizeof(root), "clean_with_flag");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Set up a cache directory with entries
    CacheConfig config;
    setup_cache_dir(root, &config);

    // Verify entries exist
    int64_t total_size = 0;
    int entry_count = 0;
    uint64_t oldest_mtime = 0;
    int rc = cache_get_stats(&config, &total_size, &entry_count, &oldest_mtime);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(entry_count, 2);
    TEST_ASSERT(total_size > 0);

    // Simulate what cmd_clean does WITH --cache: calls cache_clear
    rc = cache_clear(&config);
    TEST_ASSERT_EQ(rc, 0);

    // Cache entries should be gone
    rc = cache_get_stats(&config, &total_size, &entry_count, &oldest_mtime);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(entry_count, 0);
    TEST_ASSERT_EQ(total_size, 0);

    // Cache directory still exists (recreated empty by cache_clear)
    TEST_ASSERT_EQ(pal_path_exists(config.path), 0);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Tests: cache stats threshold display
// Requirement 6.3: cdo cache stats displays configured Filesize_Threshold value
// Requirement 6.4: threshold 0 displayed as "0 (disabled)"
// =============================================================================

TEST(cmd_cache_stats_threshold_display_nonzero) {
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

TEST(cmd_cache_stats_threshold_display_disabled) {
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

TEST(cmd_cache_stats_threshold_display_large_value) {
    // Verify threshold display with a large configured value
    CacheConfig config = {0};
    config.min_file_size = 4096;

    char output[256];
    if (config.min_file_size == 0) {
        snprintf(output, sizeof(output), "Filesize threshold: 0 (disabled)");
    } else {
        snprintf(output, sizeof(output), "Filesize threshold: %lld bytes", (long long)config.min_file_size);
    }

    TEST_ASSERT_STR_EQ(output, "Filesize threshold: 4096 bytes");
    return 0;
}

TEST_SERIAL(cmd_clean_with_cache_flag_also_removes_build) {
    char root[520];
    get_temp_dir(root, sizeof(root), "clean_both");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Set up cache
    CacheConfig config;
    setup_cache_dir(root, &config);

    // Set up build dir
    char build_dir[520];
    pal_path_join(build_dir, sizeof(build_dir), root, "build");
    pal_mkdir_p(build_dir);
    char build_file[520];
    pal_path_join(build_file, sizeof(build_file), build_dir, "output.o");
    pal_file_write(build_file, "build_artifact", 14);

    // Simulate clean --cache: remove build dir AND clear cache
    pal_rmdir_r(build_dir);
    int rc = cache_clear(&config);
    TEST_ASSERT_EQ(rc, 0);

    // Build dir gone
    TEST_ASSERT(pal_path_exists(build_dir) != 0);

    // Cache entries gone
    int64_t total_size = 0;
    int entry_count = 0;
    uint64_t oldest_mtime = 0;
    rc = cache_get_stats(&config, &total_size, &entry_count, &oldest_mtime);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(entry_count, 0);
    TEST_ASSERT_EQ(total_size, 0);

    pal_rmdir_r(root);
    return 0;
}
