/**
 * test_out_styled.c - Unit tests for styled output (cli_out_styled, cli_out_line).
 *
 * Tests ANSI SGR emission with color enabled and disabled, verifying correct
 * escape sequences for style combinations and plain text passthrough.
 */

#include "cdo_ut.h"
#include "../api/out/cli_out.h"
#include "../api/term/cli_term.h"
#include "../api/cli_errors.h"

#include <string.h>
#include <stdio.h>

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

static CliTermInfo make_term(CliColorLevel color, bool unicode) {
    CliTermInfo t;
    memset(&t, 0, sizeof(t));
    t.color_level = color;
    t.unicode = unicode;
    t.stdout_tty = true;
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
    return fopen(path, "w+bTD"); /* T=temp, D=delete-on-close */
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
/* Test: cli_out_init returns non-null with valid CliTermInfo.               */
/* ========================================================================= */

TEST(out_init_returns_non_null) {
    CliTermInfo term = make_term(CLI_COLOR_BASIC_16, true);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: cli_out_destroy(NULL) does not crash.                               */
/* ========================================================================= */

TEST(out_destroy_null_safe) {
    cli_out_destroy(NULL);
    /* If we reach here, it didn't crash */
    TEST_ASSERT(1);
    return 0;
}

/* ========================================================================= */
/* Test: Styled output emits ANSI codes for bold+red when color is BASIC_16. */
/* ========================================================================= */

TEST(out_styled_emits_ansi_for_bold_red) {
    CliTermInfo term = make_term(CLI_COLOR_BASIC_16, true);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    FILE* f = open_tmp();
    TEST_ASSERT(f != NULL);

    CliStyle style = { .fg = CLI_FG_RED, .bold = true };
    cli_out_styled(ctx, f, style, "error");

    long len = 0;
    char* output = read_tmpfile_contents(f, &len);
    fclose(f);

    /* Should contain bold+red SGR code */
    TEST_ASSERT(output != NULL);
    TEST_ASSERT(strstr(output, "\033[1;31m") != NULL);
    TEST_ASSERT(strstr(output, "\033[0m") != NULL);
    TEST_ASSERT(strstr(output, "error") != NULL);

    free(output);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: Styled output emits no escape codes when color level is NONE.       */
/* ========================================================================= */

TEST(out_styled_emits_no_escapes_when_none) {
    CliTermInfo term = make_term(CLI_COLOR_NONE, false);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    FILE* f = open_tmp();
    TEST_ASSERT(f != NULL);

    CliStyle style = { .fg = CLI_FG_RED, .bold = true };
    cli_out_styled(ctx, f, style, "plain text");

    long len = 0;
    char* output = read_tmpfile_contents(f, &len);
    fclose(f);

    /* No ANSI escape sequences */
    TEST_ASSERT(output != NULL);
    TEST_ASSERT(strstr(output, "\033[") == NULL);
    /* Plain text IS present */
    TEST_ASSERT(strstr(output, "plain text") != NULL);

    free(output);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: cli_out_line appends a newline to the output.                       */
/* ========================================================================= */

TEST(out_line_appends_newline) {
    CliTermInfo term = make_term(CLI_COLOR_BASIC_16, true);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    FILE* f = open_tmp();
    TEST_ASSERT(f != NULL);

    CliStyle style = { .fg = CLI_FG_GREEN };
    cli_out_line(ctx, f, style, "done");

    long len = 0;
    char* output = read_tmpfile_contents(f, &len);
    fclose(f);

    TEST_ASSERT(output != NULL);
    TEST_ASSERT(len > 0);
    /* Last character should be newline */
    TEST_ASSERT(output[len - 1] == '\n');

    free(output);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: cli_out_fmt with inline markers emits corresponding ANSI codes.     */
/* ========================================================================= */

TEST(out_fmt_with_markers) {
    CliTermInfo term = make_term(CLI_COLOR_BASIC_16, true);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    FILE* f = open_tmp();
    TEST_ASSERT(f != NULL);

    cli_out_fmt(ctx, f, "{green}hello{reset} world");

    long len = 0;
    char* output = read_tmpfile_contents(f, &len);
    fclose(f);

    TEST_ASSERT(output != NULL);
    /* Green ANSI code should be present */
    TEST_ASSERT(strstr(output, "\033[32m") != NULL);
    /* Reset should be present */
    TEST_ASSERT(strstr(output, "\033[0m") != NULL);
    /* Text should be present */
    TEST_ASSERT(strstr(output, "hello") != NULL);
    TEST_ASSERT(strstr(output, " world") != NULL);

    free(output);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: cli_out_fmt with no markers, plain printf specifiers work.          */
/* ========================================================================= */

TEST(out_fmt_no_markers_plain) {
    CliTermInfo term = make_term(CLI_COLOR_BASIC_16, true);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    FILE* f = open_tmp();
    TEST_ASSERT(f != NULL);

    cli_out_fmt(ctx, f, "hello %d", 42);

    long len = 0;
    char* output = read_tmpfile_contents(f, &len);
    fclose(f);

    TEST_ASSERT(output != NULL);
    TEST_ASSERT_STR_EQ(output, "hello 42");

    free(output);
    cli_out_destroy(ctx);
    return 0;
}
