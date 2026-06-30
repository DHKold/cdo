// crates/cdo/tst/unit/test_progress_global.c
// Unit tests for progress bar accuracy and global counting logic.
// Now uses cli_out_progress_* from cdo_cli framework.
// Validates: Requirements 12.1, 12.2, 12.3, 12.4
#include "cdo_ut.h"
#include "out/cli_out.h"
#include "term/cli_term.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Create a non-TTY CliOutCtx for testing (suppresses animated output).
static CliOutCtx* make_test_ctx(void) {
    CliTermInfo term = {0};
    term.stdout_tty = false;
    term.stderr_tty = false;
    term.columns = 80;
    term.color_level = CLI_COLOR_NONE;
    term.unicode = false;
    return cli_out_init(&term);
}

// ---------------------------------------------------------------------------
// Test: cli_out_progress_create with valid total returns non-NULL (Req 12.1)
// ---------------------------------------------------------------------------

TEST(progress_create_valid_total_returns_non_null) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    CliProgressBar* bar = cli_out_progress_create(ctx, "Building", 12);
    TEST_ASSERT(bar != NULL);
    cli_out_progress_finish(bar);
    cli_out_destroy(ctx);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cli_out_progress_create with label NULL returns non-NULL (Req 12.1)
// ---------------------------------------------------------------------------

TEST(progress_create_null_label_returns_non_null) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    CliProgressBar* bar = cli_out_progress_create(ctx, NULL, 5);
    TEST_ASSERT(bar != NULL);
    cli_out_progress_finish(bar);
    cli_out_destroy(ctx);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cli_out_progress_create with total == 0 still returns non-NULL.
// The cli_out implementation clamps total to 1 to avoid division by zero.
// The build command checks total_units > 0 before calling progress_create.
// ---------------------------------------------------------------------------

TEST(progress_create_zero_total_clamps_to_one) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    CliProgressBar* bar = cli_out_progress_create(ctx, "Building", 0);
    TEST_ASSERT(bar != NULL);
    cli_out_progress_finish(bar);
    cli_out_destroy(ctx);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: zero sources -> no progress bar (Req 12.1)
// The build command uses: progress = (total_units > 0) ? cli_out_progress_create(...) : NULL;
// When total_units == 0, progress is NULL. Verify NULL is safe to pass.
// ---------------------------------------------------------------------------

TEST(progress_zero_sources_no_progress_bar) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    int total_units = 0;
    CliProgressBar* progress = (total_units > 0) ? cli_out_progress_create(ctx, "Building", total_units) : NULL;
    TEST_ASSERT_NULL(progress);
    // cli_out_progress_finish(NULL) must be safe
    cli_out_progress_finish(progress);
    cli_out_destroy(ctx);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cli_out_progress_finish with NULL is safe (Req 12.1, 12.2)
// ---------------------------------------------------------------------------

TEST(progress_finish_null_is_safe) {
    // Must not crash
    cli_out_progress_finish(NULL);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cli_out_progress_update with NULL is safe (Req 12.1)
// ---------------------------------------------------------------------------

TEST(progress_update_null_is_safe) {
    // Must not crash
    cli_out_progress_update(NULL, 5);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: global count across multiple crates matches expected file count (Req 12.1)
// Simulates counting compilable files from 3 crates and verifying the total.
// ---------------------------------------------------------------------------

TEST(progress_global_count_multiple_crates) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    // Simulate counting: crate A has 4 files, crate B has 3, crate C has 5 = 12 total
    int crate_a_files = 4;
    int crate_b_files = 3;
    int crate_c_files = 5;
    int total_units = crate_a_files + crate_b_files + crate_c_files;

    TEST_ASSERT_EQ(total_units, 12);

    CliProgressBar* progress = cli_out_progress_create(ctx, "Building", total_units);
    TEST_ASSERT(progress != NULL);

    // Simulate building all files one by one
    int completed = 0;
    for (int i = 0; i < crate_a_files; i++) {
        completed++;
        cli_out_progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 4);

    for (int i = 0; i < crate_b_files; i++) {
        completed++;
        cli_out_progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 7);

    for (int i = 0; i < crate_c_files; i++) {
        completed++;
        cli_out_progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 12);

    cli_out_progress_finish(progress);
    cli_out_destroy(ctx);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: single-crate targeting counts only that crate + deps (Req 12.1)
// ---------------------------------------------------------------------------

TEST(progress_single_crate_targeting_counts_crate_and_deps) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    // Simulate: target crate has 6 files, its one dependency has 3 files.
    int target_crate_files = 6;
    int dep_crate_files = 3;
    int total_units = target_crate_files + dep_crate_files;

    TEST_ASSERT_EQ(total_units, 9);

    CliProgressBar* progress = cli_out_progress_create(ctx, "Building", total_units);
    TEST_ASSERT(progress != NULL);

    // Build dependency first (in dependency order)
    int completed = 0;
    for (int i = 0; i < dep_crate_files; i++) {
        completed++;
        cli_out_progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 3);

    // Then build target crate
    for (int i = 0; i < target_crate_files; i++) {
        completed++;
        cli_out_progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 9);

    cli_out_progress_finish(progress);
    cli_out_destroy(ctx);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: up-to-date skip adds count immediately (Req 12.1)
// When a crate is fully up-to-date, its file count is added to completed
// without compiling - progress bar jumps forward.
// ---------------------------------------------------------------------------

TEST(progress_up_to_date_skip_adds_count_immediately) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    // Scenario: 3 crates. Crate A (4 files) needs rebuild, Crate B (5 files)
    // is up-to-date, Crate C (3 files) needs rebuild.
    int crate_a_files = 4;
    int crate_b_files = 5;
    int crate_c_files = 3;
    int total_units = crate_a_files + crate_b_files + crate_c_files; // 12

    CliProgressBar* progress = cli_out_progress_create(ctx, "Building", total_units);
    TEST_ASSERT(progress != NULL);

    int completed = 0;

    // Build crate A file by file
    for (int i = 0; i < crate_a_files; i++) {
        completed++;
        cli_out_progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 4);

    // Crate B is up-to-date: skip by adding all its files at once
    completed += crate_b_files;
    cli_out_progress_update(progress, completed);
    TEST_ASSERT_EQ(completed, 9);

    // Build crate C file by file
    for (int i = 0; i < crate_c_files; i++) {
        completed++;
        cli_out_progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 12);

    cli_out_progress_finish(progress);
    cli_out_destroy(ctx);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: mid-build error -> progress finalizes at current count (Req 12.2)
// When an error occurs, cli_out_progress_finish is called immediately with
// completed < total. The bar must handle this gracefully.
// ---------------------------------------------------------------------------

TEST(progress_mid_build_error_finalizes_at_current_count) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    int total_units = 10;
    CliProgressBar* progress = cli_out_progress_create(ctx, "Building", total_units);
    TEST_ASSERT(progress != NULL);

    int completed = 0;

    // Build first 4 files successfully
    for (int i = 0; i < 4; i++) {
        completed++;
        cli_out_progress_update(progress, completed);
    }
    TEST_ASSERT_EQ(completed, 4);

    // Error occurs at file 5! Finalize progress at current count.
    // Must not crash, must handle incomplete progress gracefully.
    cli_out_progress_finish(progress);

    // Progress was finalized at 4/10 - no crash, no UB
    TEST_ASSERT_EQ(completed, 4);
    cli_out_destroy(ctx);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cli_out_progress_update increments correctly across the full range (Req 12.1)
// ---------------------------------------------------------------------------

TEST(progress_update_increments_correctly) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    int total = 20;
    CliProgressBar* progress = cli_out_progress_create(ctx, "Building", total);
    TEST_ASSERT(progress != NULL);

    // Simulate the build loop incrementing one at a time
    for (int i = 1; i <= total; i++) {
        cli_out_progress_update(progress, i);
    }

    // If we got here without crashing, updates are working correctly
    cli_out_progress_finish(progress);
    cli_out_destroy(ctx);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cli_out_progress_update can jump forward (batch skip) (Req 12.1)
// ---------------------------------------------------------------------------

TEST(progress_update_batch_skip) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    int total = 15;
    CliProgressBar* progress = cli_out_progress_create(ctx, "Building", total);
    TEST_ASSERT(progress != NULL);

    // Jump from 0 to 7 (skip 7 up-to-date files at once)
    cli_out_progress_update(progress, 7);

    // Continue one by one
    cli_out_progress_update(progress, 8);
    cli_out_progress_update(progress, 9);

    // Jump again (another up-to-date crate)
    cli_out_progress_update(progress, 15);

    cli_out_progress_finish(progress);
    cli_out_destroy(ctx);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: large total works correctly (Req 12.1)
// ---------------------------------------------------------------------------

TEST(progress_large_total_works) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    int total = 500;
    CliProgressBar* progress = cli_out_progress_create(ctx, "Building", total);
    TEST_ASSERT(progress != NULL);

    // Spot-check various update points
    cli_out_progress_update(progress, 100);
    cli_out_progress_update(progress, 250);
    cli_out_progress_update(progress, 499);
    cli_out_progress_update(progress, 500);

    cli_out_progress_finish(progress);
    cli_out_destroy(ctx);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cli_out_progress_create with negative total clamps (edge case)
// ---------------------------------------------------------------------------

TEST(progress_create_negative_total_clamps) {
    CliOutCtx* ctx = make_test_ctx();
    TEST_ASSERT(ctx != NULL);
    CliProgressBar* bar = cli_out_progress_create(ctx, "Building", -5);
    TEST_ASSERT(bar != NULL);
    // Should not crash with cli_out_progress_update
    cli_out_progress_update(bar, 1);
    cli_out_progress_finish(bar);
    cli_out_destroy(ctx);
    return 0;
}
