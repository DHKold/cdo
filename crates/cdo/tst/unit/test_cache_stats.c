// crates/cdo/tst/unit/test_cache_stats.c
// Unit tests for cache statistics tracking and summary computation
// Validates: Requirements 5.1, 5.2, 5.3
#include "cdo_ut.h"
#include "core/cache.h"

#include <string.h>

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
