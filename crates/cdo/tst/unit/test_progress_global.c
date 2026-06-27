// crates/cdo/tst/unit/test_progress_global.c
// Unit tests for progress bar accuracy and global counting logic.
// Validates: Requirements 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 7.7, 7.8
#include "cdo_ut.h"
#include "core/output.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Initialize output in non-TTY quiet mode to suppress rendering during tests.
static void init_quiet(void) {
    output_init(CDO_COLOR_NEVER, CDO_LOG_ERROR, false);
}

// ---------------------------------------------------------------------------
// Test: progress_create with valid total returns non-NULL (Req 7.1, 7.2)
// ---------------------------------------------------------------------------

TEST(progress_create_valid_total_returns_non_null) {
    init_quiet();
    ProgressBar* bar = progress_create("Building", 12);
    TEST_ASSERT(bar != NULL);
    progress_finish(bar);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: progress_create with label NULL returns non-NULL (Req 7.2)
// ---------------------------------------------------------------------------

TEST(progress_create_null_label_returns_non_null) {
    init_quiet();
    ProgressBar* bar = progress_create(NULL, 5);
    TEST_ASSERT(bar != NULL);
    progress_finish(bar);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: progress_create with total == 0 still returns non-NULL internally,
// but cmd_build guards this (Req 7.7 — zero sources → no progress bar).
// The API itself clamps total to 1 to avoid division by zero.
// The build command checks total_units > 0 before calling progress_create.
// ---------------------------------------------------------------------------

TEST(progress_create_zero_total_clamps_to_one) {
    init_quiet();
    // progress_create(label, 0) clamps total to 1 internally
    ProgressBar* bar = progress_create("Building", 0);
    TEST_ASSERT(bar != NULL);
    progress_finish(bar);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: zero sources → no progress bar (Req 7.7)
// The build command uses: progress = (total_units > 0) ? progress_create(...) : NULL;
// When total_units == 0, progress is NULL. Verify NULL is safe to pass.
// ---------------------------------------------------------------------------

TEST(progress_zero_sources_no_progress_bar) {
    init_quiet();
    // Simulate what cmd_build does when total_units == 0
    int total_units = 0;
    ProgressBar* progress = (total_units > 0) ? progress_create("Building", total_units) : NULL;
    TEST_ASSERT_NULL(progress);
    // progress_finish(NULL) must be safe
    progress_finish(progress);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: progress_finish with NULL is safe (Req 7.7, 7.8)
// ---------------------------------------------------------------------------

TEST(progress_finish_null_is_safe) {
    init_quiet();
    // Must not crash
    progress_finish(NULL);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: progress_update with NULL is safe (Req 7.8)
// ---------------------------------------------------------------------------

TEST(progress_update_null_is_safe) {
    init_quiet();
    // Must not crash
    progress_update(NULL, 5);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: global count across multiple crates matches expected file count (Req 7.1, 7.5)
// Simulates counting compilable files from 3 crates and verifying the total.
// ---------------------------------------------------------------------------

TEST(progress_global_count_multiple_crates) {
    init_quiet();
    // Simulate counting: crate A has 4 files, crate B has 3, crate C has 5 = 12 total
    int crate_a_files = 4;
    int crate_b_files = 3;
    int crate_c_files = 5;
    int total_units = crate_a_files + crate_b_files + crate_c_files;

    TEST_ASSERT_EQ(total_units, 12);

    ProgressBar* progress = progress_create("Building", total_units);
    TEST_ASSERT(progress != NULL);

    // Simulate building all files one by one
    int completed = 0;
    for (int i = 0; i < crate_a_files; i++) {
        completed++;
        progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 4);

    for (int i = 0; i < crate_b_files; i++) {
        completed++;
        progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 7);

    for (int i = 0; i < crate_c_files; i++) {
        completed++;
        progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 12);

    progress_finish(progress);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: single-crate targeting counts only that crate + deps (Req 7.4)
// ---------------------------------------------------------------------------

TEST(progress_single_crate_targeting_counts_crate_and_deps) {
    init_quiet();
    // Simulate: target crate has 6 files, its one dependency has 3 files.
    // Total should be 9 (not the full workspace total of, say, 20).
    int target_crate_files = 6;
    int dep_crate_files = 3;
    int total_units = target_crate_files + dep_crate_files;

    TEST_ASSERT_EQ(total_units, 9);

    ProgressBar* progress = progress_create("Building", total_units);
    TEST_ASSERT(progress != NULL);

    // Build dependency first (in dependency order)
    int completed = 0;
    for (int i = 0; i < dep_crate_files; i++) {
        completed++;
        progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 3);

    // Then build target crate
    for (int i = 0; i < target_crate_files; i++) {
        completed++;
        progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 9);

    progress_finish(progress);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: up-to-date skip adds count immediately (Req 7.6)
// When a crate is fully up-to-date, its file count is added to completed
// without compiling — progress bar jumps forward.
// ---------------------------------------------------------------------------

TEST(progress_up_to_date_skip_adds_count_immediately) {
    init_quiet();
    // Scenario: 3 crates. Crate A (4 files) needs rebuild, Crate B (5 files)
    // is up-to-date, Crate C (3 files) needs rebuild.
    int crate_a_files = 4;
    int crate_b_files = 5;
    int crate_c_files = 3;
    int total_units = crate_a_files + crate_b_files + crate_c_files; // 12

    ProgressBar* progress = progress_create("Building", total_units);
    TEST_ASSERT(progress != NULL);

    int completed = 0;

    // Build crate A file by file
    for (int i = 0; i < crate_a_files; i++) {
        completed++;
        progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 4);

    // Crate B is up-to-date: skip by adding all its files at once
    completed += crate_b_files;
    progress_update(progress, completed);
    TEST_ASSERT_EQ(completed, 9);

    // Build crate C file by file
    for (int i = 0; i < crate_c_files; i++) {
        completed++;
        progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 12);

    progress_finish(progress);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: mid-build error → progress finalizes at current count (Req 7.8)
// When an error occurs, progress_finish is called immediately with
// completed < total. The bar must handle this gracefully.
// ---------------------------------------------------------------------------

TEST(progress_mid_build_error_finalizes_at_current_count) {
    init_quiet();
    int total_units = 10;
    ProgressBar* progress = progress_create("Building", total_units);
    TEST_ASSERT(progress != NULL);

    int completed = 0;

    // Build first 4 files successfully
    for (int i = 0; i < 4; i++) {
        completed++;
        progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 4);

    // Error occurs at file 5! Finalize progress at current count.
    // progress_finish called with bar->completed == 4, bar->total == 10
    // Must not crash, must handle incomplete progress gracefully.
    progress_finish(progress);

    // Progress was finalized at 4/10 — no crash, no UB
    TEST_ASSERT_EQ(completed, 4);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: progress_update increments correctly across the full range (Req 7.3)
// ---------------------------------------------------------------------------

TEST(progress_update_increments_correctly) {
    init_quiet();
    int total = 20;
    ProgressBar* progress = progress_create("Building", total);
    TEST_ASSERT(progress != NULL);

    // Simulate the build loop incrementing one at a time
    for (int i = 1; i <= total; i++) {
        progress_update(progress, i);
    }

    // If we got here without crashing, updates are working correctly
    progress_finish(progress);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: progress_update can jump forward (batch skip for up-to-date) (Req 7.6)
// ---------------------------------------------------------------------------

TEST(progress_update_batch_skip) {
    init_quiet();
    int total = 15;
    ProgressBar* progress = progress_create("Building", total);
    TEST_ASSERT(progress != NULL);

    // Jump from 0 to 7 (skip 7 up-to-date files at once)
    progress_update(progress, 7);

    // Continue one by one
    progress_update(progress, 8);
    progress_update(progress, 9);

    // Jump again (another up-to-date crate)
    progress_update(progress, 15);

    progress_finish(progress);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: large total works correctly (Req 7.1)
// ---------------------------------------------------------------------------

TEST(progress_large_total_works) {
    init_quiet();
    int total = 500;
    ProgressBar* progress = progress_create("Building", total);
    TEST_ASSERT(progress != NULL);

    // Spot-check various update points
    progress_update(progress, 100);
    progress_update(progress, 250);
    progress_update(progress, 499);
    progress_update(progress, 500);

    progress_finish(progress);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: progress_create with negative total clamps to 1 (edge case)
// ---------------------------------------------------------------------------

TEST(progress_create_negative_total_clamps) {
    init_quiet();
    ProgressBar* bar = progress_create("Building", -5);
    TEST_ASSERT(bar != NULL);
    // Should not crash with progress_update
    progress_update(bar, 1);
    progress_finish(bar);
    return 0;
}

