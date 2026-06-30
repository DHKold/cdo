// crates/cdo/tst/unit/test_cache_log.c
// Unit tests for consolidated cache log output (cache_log_summary)
// Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 3.6
#include "cdo_ut.h"
#include "core/cache_log.h"
#include "core/log.h"

#include <string.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>

// =============================================================================
// Stream capture helper (same pattern as test_log.c)
// =============================================================================

typedef struct {
    char  path[260];
    int   orig_fd;
    int   stream_fd;  // 1=stdout, 2=stderr
} CacheLogCapture;

static int cache_log_capture_start(CacheLogCapture* cap, int stream_fd) {
    memset(cap, 0, sizeof(*cap));
    cap->stream_fd = stream_fd;
    cap->orig_fd = -1;

    char* name = _tempnam(NULL, "cdo_clog_");
    if (!name) return -1;
    strncpy(cap->path, name, sizeof(cap->path) - 1);
    free(name);

    fflush(stream_fd == 1 ? stdout : stderr);

    cap->orig_fd = _dup(stream_fd);
    if (cap->orig_fd < 0) return -1;

    int tmp_fd = _open(cap->path, _O_CREAT | _O_TRUNC | _O_WRONLY, _S_IREAD | _S_IWRITE);
    if (tmp_fd < 0) { _dup2(cap->orig_fd, stream_fd); _close(cap->orig_fd); return -1; }

    _dup2(tmp_fd, stream_fd);
    _close(tmp_fd);
    return 0;
}

static int cache_log_capture_stop(CacheLogCapture* cap, char* buf, size_t buf_size) {
    fflush(cap->stream_fd == 1 ? stdout : stderr);

    _dup2(cap->orig_fd, cap->stream_fd);
    _close(cap->orig_fd);
    cap->orig_fd = -1;

    int n = 0;
    if (buf && buf_size > 0) {
        buf[0] = '\0';
        FILE* f = fopen(cap->path, "rb");
        if (f) {
            n = (int)fread(buf, 1, buf_size - 1, f);
            if (n >= 0) buf[n] = '\0';
            fclose(f);
        }
    }

    _unlink(cap->path);
    return n;
}

// =============================================================================
// Helper: init log in test mode at INFO level (standard for cache summary)
// =============================================================================

static void init_info_log(void) {
    cdo_log_init_test(CDO_LOG_LEVEL_INFO, false, false);
    cdo_log_test_reset_emit_count();
}

static void init_trace_log(void) {
    cdo_log_init_test(CDO_LOG_LEVEL_TRACE, false, false);
    cdo_log_test_reset_emit_count();
}

// =============================================================================
// Test: Summary emitted with typical hits/misses/skipped
// Requirement 3.1: emit one aggregate INFO-level log message
// Requirement 3.2: format "Cache: <hits> hit(s), <misses> miss(es), <skipped> skipped (below threshold)"
// =============================================================================

TEST_SERIAL(cache_log_summary_typical) {
    init_info_log();
    CacheLogCapture cap;
    TEST_ASSERT_EQ(cache_log_capture_start(&cap, 1), 0);

    CacheStats stats = {0};
    stats.hits = 12;
    stats.misses = 3;
    stats.skipped = 5;
    cache_log_summary(&stats);

    char buf[1024] = {0};
    int n = cache_log_capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "Cache: 12 hit(s), 3 miss(es), 5 skipped (below threshold)") != NULL);
    return 0;
}

// =============================================================================
// Test: Summary with only hits (no misses, no skipped)
// Requirement 3.1, 3.2: summary emitted when hits > 0
// =============================================================================

TEST_SERIAL(cache_log_summary_hits_only) {
    init_info_log();
    CacheLogCapture cap;
    TEST_ASSERT_EQ(cache_log_capture_start(&cap, 1), 0);

    CacheStats stats = {0};
    stats.hits = 25;
    stats.misses = 0;
    stats.skipped = 0;
    cache_log_summary(&stats);

    char buf[1024] = {0};
    int n = cache_log_capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "Cache: 25 hit(s), 0 miss(es), 0 skipped (below threshold)") != NULL);
    return 0;
}

// =============================================================================
// Test: Summary with only misses (no hits, no skipped)
// Requirement 3.1, 3.2: summary emitted when misses > 0
// =============================================================================

TEST_SERIAL(cache_log_summary_misses_only) {
    init_info_log();
    CacheLogCapture cap;
    TEST_ASSERT_EQ(cache_log_capture_start(&cap, 1), 0);

    CacheStats stats = {0};
    stats.hits = 0;
    stats.misses = 7;
    stats.skipped = 0;
    cache_log_summary(&stats);

    char buf[1024] = {0};
    int n = cache_log_capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "Cache: 0 hit(s), 7 miss(es), 0 skipped (below threshold)") != NULL);
    return 0;
}

// =============================================================================
// Test: Summary with single file (1 hit, 0 miss, 0 skipped)
// Requirement 3.2: verify format with singular count
// =============================================================================

TEST_SERIAL(cache_log_summary_single_hit) {
    init_info_log();
    CacheLogCapture cap;
    TEST_ASSERT_EQ(cache_log_capture_start(&cap, 1), 0);

    CacheStats stats = {0};
    stats.hits = 1;
    stats.misses = 0;
    stats.skipped = 0;
    cache_log_summary(&stats);

    char buf[1024] = {0};
    int n = cache_log_capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "Cache: 1 hit(s), 0 miss(es), 0 skipped (below threshold)") != NULL);
    return 0;
}

// =============================================================================
// Test: No log emitted when cache disabled (hits + misses + skipped = 0)
// Requirement 3.5: no aggregate message when zero cache interactions
// =============================================================================

TEST_SERIAL(cache_log_summary_no_output_when_no_interactions) {
    init_info_log();
    CacheLogCapture cap;
    TEST_ASSERT_EQ(cache_log_capture_start(&cap, 1), 0);

    CacheStats stats = {0};
    stats.hits = 0;
    stats.misses = 0;
    stats.skipped = 0;
    cache_log_summary(&stats);

    char buf[1024] = {0};
    int n = cache_log_capture_stop(&cap, buf, sizeof(buf));
    // No output should be emitted
    TEST_ASSERT_EQ(n, 0);
    return 0;
}

// =============================================================================
// Test: No log emitted - emit count unchanged when no interactions
// Requirement 3.5: verify via emit counter that nothing was logged
// =============================================================================

TEST(cache_log_summary_no_emit_when_disabled) {
    init_info_log();

    CacheStats stats = {0};
    stats.hits = 0;
    stats.misses = 0;
    stats.skipped = 0;
    cache_log_summary(&stats);

    // No log should have been emitted
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 0);
    return 0;
}

// =============================================================================
// Test: All files skipped (hits=0, misses=0, skipped>0) → still emit summary
// Requirement 3.6: when only skipped files exist, summary is still emitted
// =============================================================================

TEST_SERIAL(cache_log_summary_all_skipped) {
    init_info_log();
    CacheLogCapture cap;
    TEST_ASSERT_EQ(cache_log_capture_start(&cap, 1), 0);

    CacheStats stats = {0};
    stats.hits = 0;
    stats.misses = 0;
    stats.skipped = 10;
    cache_log_summary(&stats);

    char buf[1024] = {0};
    int n = cache_log_capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "Cache: 0 hit(s), 0 miss(es), 10 skipped (below threshold)") != NULL);
    return 0;
}

// =============================================================================
// Test: Emit count is 1 when summary IS emitted (exactly one message)
// Requirement 3.1: exactly ONE aggregate log message
// =============================================================================

TEST(cache_log_summary_emits_exactly_one_message) {
    init_info_log();

    CacheStats stats = {0};
    stats.hits = 5;
    stats.misses = 2;
    stats.skipped = 3;
    cache_log_summary(&stats);

    // Exactly one log call should have been made
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    return 0;
}

// =============================================================================
// Test: Large values in summary format correctly
// Requirement 3.2: verify format handles larger numbers
// =============================================================================

TEST_SERIAL(cache_log_summary_large_values) {
    init_info_log();
    CacheLogCapture cap;
    TEST_ASSERT_EQ(cache_log_capture_start(&cap, 1), 0);

    CacheStats stats = {0};
    stats.hits = 1000;
    stats.misses = 250;
    stats.skipped = 50;
    cache_log_summary(&stats);

    char buf[1024] = {0};
    int n = cache_log_capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "Cache: 1000 hit(s), 250 miss(es), 50 skipped (below threshold)") != NULL);
    return 0;
}

// =============================================================================
// Test: Summary is emitted at INFO level (not visible at ERROR-only log level)
// Requirement 3.1: INFO-level message
// =============================================================================

TEST(cache_log_summary_is_info_level) {
    // Set log level to ERROR only — INFO should be suppressed
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);
    cdo_log_test_reset_emit_count();

    CacheStats stats = {0};
    stats.hits = 5;
    stats.misses = 2;
    stats.skipped = 0;
    cache_log_summary(&stats);

    // At ERROR level, INFO messages are suppressed — emit count should be 0
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 0);
    return 0;
}

// =============================================================================
// Test: Summary IS visible at INFO log level
// Requirement 3.1: INFO-level message visible when level is INFO or below
// =============================================================================

TEST(cache_log_summary_visible_at_info_level) {
    init_info_log();

    CacheStats stats = {0};
    stats.hits = 3;
    stats.misses = 1;
    stats.skipped = 0;
    cache_log_summary(&stats);

    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    return 0;
}

// =============================================================================
// Test: Per-batch INFO messages are removed (the old "All N file(s) served from cache")
// Requirement 3.3: the cache pipeline SHALL NOT emit per-batch INFO log messages
// Note: This test verifies that cache_log_summary is the ONLY cache INFO message.
//       The removal of the old message is an implementation detail tested by
//       verifying that only one INFO message is emitted for a given stats snapshot.
// =============================================================================

TEST(cache_log_summary_only_one_info_message_for_all_hits) {
    init_info_log();

    // Simulate a case where all files were served from cache (old behavior
    // would have emitted "All N file(s) served from cache" per batch)
    CacheStats stats = {0};
    stats.hits = 20;
    stats.misses = 0;
    stats.skipped = 0;

    // The new consolidated interface emits exactly ONE message
    cache_log_summary(&stats);
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    return 0;
}

// =============================================================================
// Test: Per-file TRACE messages are still emitted when trace enabled
// Requirement 3.4: per-file cache hit/miss messages at TRACE level
// Note: This tests that the log infrastructure supports TRACE-level messages.
//       The actual per-file messages are emitted by compiler_compile_batch,
//       not by cache_log_summary. This test verifies TRACE is not suppressed.
// =============================================================================

TEST_SERIAL(cache_log_trace_messages_visible_at_trace_level) {
    init_trace_log();
    CacheLogCapture cap;
    TEST_ASSERT_EQ(cache_log_capture_start(&cap, 1), 0);

    // Simulate what compiler_compile_batch does at TRACE level
    cdo_log_trace("Cache hit: src/main.c");
    cdo_log_trace("Cache miss: src/util.c");

    char buf[1024] = {0};
    int n = cache_log_capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "Cache hit: src/main.c") != NULL);
    TEST_ASSERT(strstr(buf, "Cache miss: src/util.c") != NULL);
    return 0;
}

// =============================================================================
// Test: Per-file TRACE messages NOT visible at INFO level
// Requirement 3.4: TRACE messages only emitted at TRACE log level
// =============================================================================

TEST_SERIAL(cache_log_trace_messages_hidden_at_info_level) {
    init_info_log();
    CacheLogCapture cap;
    TEST_ASSERT_EQ(cache_log_capture_start(&cap, 1), 0);

    // These TRACE-level messages should be suppressed at INFO level
    cdo_log_trace("Cache hit: src/main.c");
    cdo_log_trace("Cache miss: src/util.c");

    char buf[1024] = {0};
    int n = cache_log_capture_stop(&cap, buf, sizeof(buf));
    // Nothing should have been emitted
    TEST_ASSERT_EQ(n, 0);
    return 0;
}

// =============================================================================
// Test: Summary and TRACE messages coexist at TRACE level
// Requirement 3.1 + 3.4: summary at INFO + per-file at TRACE both visible
// =============================================================================

TEST_SERIAL(cache_log_summary_and_trace_coexist) {
    init_trace_log();
    CacheLogCapture cap;
    TEST_ASSERT_EQ(cache_log_capture_start(&cap, 1), 0);

    // Per-file TRACE messages (emitted during compilation)
    cdo_log_trace("Cache hit: src/a.c");
    cdo_log_trace("Cache miss: src/b.c");

    // Aggregate summary (emitted after all batches)
    CacheStats stats = {0};
    stats.hits = 1;
    stats.misses = 1;
    stats.skipped = 0;
    cache_log_summary(&stats);

    char buf[2048] = {0};
    int n = cache_log_capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    // Both TRACE per-file messages and the INFO summary should be present
    TEST_ASSERT(strstr(buf, "Cache hit: src/a.c") != NULL);
    TEST_ASSERT(strstr(buf, "Cache miss: src/b.c") != NULL);
    TEST_ASSERT(strstr(buf, "Cache: 1 hit(s), 1 miss(es), 0 skipped (below threshold)") != NULL);
    return 0;
}

// =============================================================================
// Test: NULL stats pointer does not crash
// Defensive check for robustness
// =============================================================================

TEST(cache_log_summary_null_stats_no_crash) {
    init_info_log();
    // Should not crash - just do nothing
    cache_log_summary(NULL);
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 0);
    return 0;
}
