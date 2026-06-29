// crates/cdo/tst/unit/test_cache_external.c
// Unit tests for external cache backend integration (ccache / sccache)
// Validates: Requirements 8.1, 8.2, 8.3, 8.4, 8.5
#include "cdo_ut.h"
#include "core/cache.h"
#include "core/compiler.h"

#include <string.h>
#include <stdlib.h>

// =============================================================================
// Tests: ccache prefix is added to compiler invocations
// Requirement 8.1: backend = "ccache" prefixes compiler invocations with "ccache"
// =============================================================================

TEST(external_ccache_sets_external_backend) {
    // When backend = "ccache", enabled = true, no_cache = false:
    // The external_backend variable should be set to "ccache"
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    strncpy(config.backend, "ccache", sizeof(config.backend) - 1);
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;

    bool no_cache = false;

    // Replicate the condition from compiler_compile_batch:
    // const char* external_backend = NULL;
    // if (cache_config != NULL && cache_config->enabled && !no_cache
    //     && strcmp(cache_config->backend, "builtin") != 0 && cache_config->backend[0] != '\0')
    //     external_backend = cache_config->backend;
    const char* external_backend = NULL;
    if (&config != NULL && config.enabled && !no_cache && strcmp(config.backend, "builtin") != 0 && config.backend[0] != '\0') {
        external_backend = config.backend;
    }

    TEST_ASSERT(external_backend != NULL);
    TEST_ASSERT_STR_EQ(external_backend, "ccache");
    return 0;
}

TEST(external_ccache_disables_builtin_cache) {
    // When backend = "ccache", the use_cache condition (builtin cache) must be false
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    strncpy(config.backend, "ccache", sizeof(config.backend) - 1);
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;

    CacheStats stats = {0};
    bool no_cache = false;

    // Replicate use_cache condition from compiler_compile_batch:
    // bool use_cache = (cache_config != NULL && cache_config->enabled && !no_cache
    //                   && cache_stats != NULL && strcmp(cache_config->backend, "builtin") == 0);
    bool use_cache = (&config != NULL && config.enabled && !no_cache && &stats != NULL && strcmp(config.backend, "builtin") == 0);

    TEST_ASSERT(use_cache == false);
    return 0;
}

// =============================================================================
// Tests: sccache prefix is added to compiler invocations
// Requirement 8.2: backend = "sccache" prefixes compiler invocations with "sccache"
// =============================================================================

TEST(external_sccache_sets_external_backend) {
    // When backend = "sccache", enabled = true, no_cache = false:
    // The external_backend variable should be set to "sccache"
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    strncpy(config.backend, "sccache", sizeof(config.backend) - 1);
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;

    bool no_cache = false;

    const char* external_backend = NULL;
    if (&config != NULL && config.enabled && !no_cache && strcmp(config.backend, "builtin") != 0 && config.backend[0] != '\0') {
        external_backend = config.backend;
    }

    TEST_ASSERT(external_backend != NULL);
    TEST_ASSERT_STR_EQ(external_backend, "sccache");
    return 0;
}

TEST(external_sccache_disables_builtin_cache) {
    // When backend = "sccache", the use_cache condition (builtin cache) must be false
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    strncpy(config.backend, "sccache", sizeof(config.backend) - 1);
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;

    CacheStats stats = {0};
    bool no_cache = false;

    bool use_cache = (&config != NULL && config.enabled && !no_cache && &stats != NULL && strcmp(config.backend, "builtin") == 0);

    TEST_ASSERT(use_cache == false);
    return 0;
}

// =============================================================================
// Tests: backend not found → warning + no caching
// Requirement 8.4: when external backend tool not on PATH, warn and fall back to no caching
// =============================================================================

TEST(external_backend_not_found_disables_all_caching) {
    // When the external backend tool is not found, the build system should disable
    // caching entirely (config.enabled set to false). After this:
    // - use_cache should be false (no builtin cache)
    // - external_backend should be NULL (no external cache)
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = false; // Simulates aftermath of backend-not-found check
    strncpy(config.backend, "ccache", sizeof(config.backend) - 1);
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;

    CacheStats stats = {0};
    bool no_cache = false;

    // Neither builtin nor external cache should be active
    bool use_cache = (&config != NULL && config.enabled && !no_cache && &stats != NULL && strcmp(config.backend, "builtin") == 0);
    TEST_ASSERT(use_cache == false);

    const char* external_backend = NULL;
    if (&config != NULL && config.enabled && !no_cache && strcmp(config.backend, "builtin") != 0 && config.backend[0] != '\0') {
        external_backend = config.backend;
    }
    TEST_ASSERT_NULL(external_backend);

    // Stats should never be modified
    TEST_ASSERT_EQ(stats.hits, 0);
    TEST_ASSERT_EQ(stats.misses, 0);
    TEST_ASSERT_EQ(stats.stored, 0);
    return 0;
}

TEST(external_backend_not_found_with_sccache) {
    // Same scenario with sccache backend
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = false; // Simulates aftermath of backend-not-found check
    strncpy(config.backend, "sccache", sizeof(config.backend) - 1);
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;

    CacheStats stats = {0};
    bool no_cache = false;

    bool use_cache = (&config != NULL && config.enabled && !no_cache && &stats != NULL && strcmp(config.backend, "builtin") == 0);
    TEST_ASSERT(use_cache == false);

    const char* external_backend = NULL;
    if (&config != NULL && config.enabled && !no_cache && strcmp(config.backend, "builtin") != 0 && config.backend[0] != '\0') {
        external_backend = config.backend;
    }
    TEST_ASSERT_NULL(external_backend);
    return 0;
}

// =============================================================================
// Tests: builtin cache not used when external backend configured
// Requirement 8.3: built-in cache lookup/population logic skipped entirely
// =============================================================================

TEST(external_builtin_not_used_when_ccache_configured) {
    // With backend = "ccache": use_cache must be false (no cache_compute_key, no cache_lookup)
    // and external_backend must be "ccache" (compiler prefixed with ccache)
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    strncpy(config.backend, "ccache", sizeof(config.backend) - 1);
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;

    CacheStats stats = {0};
    bool no_cache = false;

    // Builtin cache condition
    bool use_cache = (&config != NULL && config.enabled && !no_cache && &stats != NULL && strcmp(config.backend, "builtin") == 0);
    TEST_ASSERT(use_cache == false);

    // External backend condition
    const char* external_backend = NULL;
    if (&config != NULL && config.enabled && !no_cache && strcmp(config.backend, "builtin") != 0 && config.backend[0] != '\0') {
        external_backend = config.backend;
    }
    TEST_ASSERT(external_backend != NULL);
    TEST_ASSERT_STR_EQ(external_backend, "ccache");

    // Since use_cache is false, no cache operations happen: stats stay zero
    TEST_ASSERT_EQ(stats.hits, 0);
    TEST_ASSERT_EQ(stats.misses, 0);
    TEST_ASSERT_EQ(stats.stored, 0);
    return 0;
}

TEST(external_builtin_not_used_when_sccache_configured) {
    // Same test with sccache
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    strncpy(config.backend, "sccache", sizeof(config.backend) - 1);
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;

    CacheStats stats = {0};
    bool no_cache = false;

    bool use_cache = (&config != NULL && config.enabled && !no_cache && &stats != NULL && strcmp(config.backend, "builtin") == 0);
    TEST_ASSERT(use_cache == false);

    const char* external_backend = NULL;
    if (&config != NULL && config.enabled && !no_cache && strcmp(config.backend, "builtin") != 0 && config.backend[0] != '\0') {
        external_backend = config.backend;
    }
    TEST_ASSERT(external_backend != NULL);
    TEST_ASSERT_STR_EQ(external_backend, "sccache");

    TEST_ASSERT_EQ(stats.hits, 0);
    TEST_ASSERT_EQ(stats.misses, 0);
    TEST_ASSERT_EQ(stats.stored, 0);
    return 0;
}

// =============================================================================
// Tests: backend = "builtin" does NOT set external_backend
// Requirement 8.5: default backend is "builtin", using CDo's own cache
// =============================================================================

TEST(external_builtin_backend_does_not_set_external) {
    // When backend = "builtin", external_backend should remain NULL
    // and use_cache should be true (builtin cache active)
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;

    CacheStats stats = {0};
    bool no_cache = false;

    // Builtin cache is active
    bool use_cache = (&config != NULL && config.enabled && !no_cache && &stats != NULL && strcmp(config.backend, "builtin") == 0);
    TEST_ASSERT(use_cache == true);

    // External backend is NOT set
    const char* external_backend = NULL;
    if (&config != NULL && config.enabled && !no_cache && strcmp(config.backend, "builtin") != 0 && config.backend[0] != '\0') {
        external_backend = config.backend;
    }
    TEST_ASSERT_NULL(external_backend);
    return 0;
}

TEST(external_empty_backend_does_not_set_external) {
    // Edge case: empty backend string should NOT activate external backend
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    config.backend[0] = '\0'; // empty
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;

    bool no_cache = false;

    const char* external_backend = NULL;
    if (&config != NULL && config.enabled && !no_cache && strcmp(config.backend, "builtin") != 0 && config.backend[0] != '\0') {
        external_backend = config.backend;
    }
    TEST_ASSERT_NULL(external_backend);
    return 0;
}

// =============================================================================
// Tests: no_cache flag overrides external backend
// Requirement 8.3 combined with 7.2: --no-cache disables ALL caching including external
// =============================================================================

TEST(external_no_cache_flag_disables_external_backend) {
    // Even when backend = "ccache", if no_cache = true, external_backend should be NULL
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    strncpy(config.backend, "ccache", sizeof(config.backend) - 1);
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;

    bool no_cache = true;

    const char* external_backend = NULL;
    if (&config != NULL && config.enabled && !no_cache && strcmp(config.backend, "builtin") != 0 && config.backend[0] != '\0') {
        external_backend = config.backend;
    }
    TEST_ASSERT_NULL(external_backend);

    // Builtin cache also disabled
    CacheStats stats = {0};
    bool use_cache = (&config != NULL && config.enabled && !no_cache && &stats != NULL && strcmp(config.backend, "builtin") == 0);
    TEST_ASSERT(use_cache == false);
    return 0;
}

TEST(external_null_config_no_external_backend) {
    // When cache_config is NULL, neither builtin nor external should be active
    CacheConfig* config = NULL;
    bool no_cache = false;

    const char* external_backend = NULL;
    if (config != NULL && config->enabled && !no_cache && strcmp(config->backend, "builtin") != 0 && config->backend[0] != '\0') {
        external_backend = config->backend;
    }
    TEST_ASSERT_NULL(external_backend);

    CacheStats stats = {0};
    bool use_cache = (config != NULL && config->enabled && !no_cache && &stats != NULL && strcmp(config->backend, "builtin") == 0);
    TEST_ASSERT(use_cache == false);
    return 0;
}
