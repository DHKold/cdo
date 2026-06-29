/**
 * test_out_dynamic.c - Unit tests for dynamic zones (spinner, progress, table).
 *
 * Tests handle creation, null-safety, and basic rendering for the dynamic
 * output components.
 */

#include "cdo_ut.h"
#include "../api/out/cli_out.h"
#include "../api/term/cli_term.h"

#include <string.h>
#include <stdio.h>

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

static CliTermInfo make_term_tty(bool is_tty) {
    CliTermInfo t;
    memset(&t, 0, sizeof(t));
    t.color_level = CLI_COLOR_BASIC_16;
    t.unicode = true;
    t.stdout_tty = is_tty;
    t.columns = 80;
    return t;
}

#ifdef _WIN32
#include <windows.h>
static FILE* open_tmp(void) {
    char path[MAX_PATH];
    char tmp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp_dir);
    GetTempFileNameA(tmp_dir, "cdo", 0, path);
    return fopen(path, "w+bTD");
}
#else
static FILE* open_tmp(void) { return tmpfile(); }
#endif

static char* read_tmpfile_contents(FILE* f, long* out_len) {
    fflush(f);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) return NULL;
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

/* ========================================================================= */
/* Test: Spinner creation with TTY returns a valid handle.                   */
/* ========================================================================= */

TEST(spinner_create_returns_handle) {
    CliTermInfo term = make_term_tty(true);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    CliSpinner* spinner = cli_out_spinner_start(ctx, "Loading...", 100);
    TEST_ASSERT(spinner != NULL);

    cli_out_spinner_stop(spinner, NULL);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: Spinner creation with non-TTY still returns a valid handle.         */
/* ========================================================================= */

TEST(spinner_non_tty_returns_handle) {
    CliTermInfo term = make_term_tty(false);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    CliSpinner* spinner = cli_out_spinner_start(ctx, "Working...", 80);
    TEST_ASSERT(spinner != NULL);

    cli_out_spinner_stop(spinner, NULL);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: Progress bar creation returns a valid handle.                       */
/* ========================================================================= */

TEST(progress_create_returns_handle) {
    CliTermInfo term = make_term_tty(true);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    CliProgressBar* bar = cli_out_progress_create(ctx, "Building", 100);
    TEST_ASSERT(bar != NULL);

    cli_out_progress_finish(bar);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: Progress update to 50% does not crash.                              */
/* ========================================================================= */

TEST(progress_update_no_crash) {
    CliTermInfo term = make_term_tty(false);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    CliProgressBar* bar = cli_out_progress_create(ctx, "Compiling", 200);
    TEST_ASSERT(bar != NULL);

    cli_out_progress_update(bar, 100);
    cli_out_progress_finish(bar);

    /* If we reach here, no crash occurred */
    TEST_ASSERT(1);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: Table creation returns a valid handle.                              */
/* ========================================================================= */

TEST(table_create_returns_handle) {
    CliTermInfo term = make_term_tty(true);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    const char* headers[] = { "Name", "Status", "Time" };
    CliTable* table = cli_out_table_create(ctx, headers, 3);
    TEST_ASSERT(table != NULL);

    cli_out_table_destroy(table);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: Table add row and render produces output.                           */
/* ========================================================================= */

TEST(table_add_row_and_render) {
    CliTermInfo term = make_term_tty(true);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    const char* headers[] = { "Crate", "Result" };
    CliTable* table = cli_out_table_create(ctx, headers, 2);
    TEST_ASSERT(table != NULL);

    const char* row1[] = { "cdo_core", "PASS" };
    const char* row2[] = { "cdo_cli", "FAIL" };
    cli_out_table_add_row(table, row1);
    cli_out_table_add_row(table, row2);

    FILE* f = open_tmp();
    TEST_ASSERT(f != NULL);

    cli_out_table_render(table, f);

    long len = 0;
    char* output = read_tmpfile_contents(f, &len);
    fclose(f);

    TEST_ASSERT(output != NULL);
    TEST_ASSERT(len > 0);
    /* Output should contain the table data */
    TEST_ASSERT(strstr(output, "Crate") != NULL);
    TEST_ASSERT(strstr(output, "cdo_core") != NULL);
    TEST_ASSERT(strstr(output, "PASS") != NULL);

    free(output);
    cli_out_table_destroy(table);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: cli_out_table_destroy(NULL) does not crash.                         */
/* ========================================================================= */

TEST(table_destroy_null_safe) {
    cli_out_table_destroy(NULL);
    /* If we reach here, it didn't crash */
    TEST_ASSERT(1);
    return 0;
}
