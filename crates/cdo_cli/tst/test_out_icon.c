/**
 * test_out_icon.c - Unit tests for icon token resolution.
 *
 * Tests Unicode and ASCII fallback paths for known icon tokens,
 * as well as passthrough behavior for unknown tokens.
 */

#include "cdo_ut.h"
#include "../api/out/cli_out.h"
#include "../api/term/cli_term.h"

#include <string.h>
#include <stdio.h>

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

static CliTermInfo make_term_icon(bool unicode) {
    CliTermInfo t;
    memset(&t, 0, sizeof(t));
    t.color_level = CLI_COLOR_BASIC_16;
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
/* Test: "check" icon with unicode=true emits UTF-8 check mark.             */
/* ========================================================================= */

TEST(icon_check_unicode) {
    CliTermInfo term = make_term_icon(true);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    FILE* f = open_tmp();
    TEST_ASSERT(f != NULL);

    cli_out_icon(ctx, f, "check");

    long len = 0;
    char* output = read_tmpfile_contents(f, &len);
    fclose(f);

    TEST_ASSERT(output != NULL);
    TEST_ASSERT(len > 0);
    /* UTF-8 check mark: E2 9C 93 */
    TEST_ASSERT(strstr(output, "\xe2\x9c\x93") != NULL);

    free(output);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: "check" icon with unicode=false emits ASCII fallback "v".           */
/* ========================================================================= */

TEST(icon_check_ascii) {
    CliTermInfo term = make_term_icon(false);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    FILE* f = open_tmp();
    TEST_ASSERT(f != NULL);

    cli_out_icon(ctx, f, "check");

    long len = 0;
    char* output = read_tmpfile_contents(f, &len);
    fclose(f);

    TEST_ASSERT(output != NULL);
    TEST_ASSERT_STR_EQ(output, "v");

    free(output);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: Unknown token "zzz" emits "zzz" as-is.                             */
/* ========================================================================= */

TEST(icon_unknown_token) {
    CliTermInfo term = make_term_icon(true);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    FILE* f = open_tmp();
    TEST_ASSERT(f != NULL);

    cli_out_icon(ctx, f, "zzz");

    long len = 0;
    char* output = read_tmpfile_contents(f, &len);
    fclose(f);

    TEST_ASSERT(output != NULL);
    TEST_ASSERT_STR_EQ(output, "zzz");

    free(output);
    cli_out_destroy(ctx);
    return 0;
}

/* ========================================================================= */
/* Test: "cross" icon with unicode=false emits ASCII "x".                    */
/* ========================================================================= */

TEST(icon_cross_ascii) {
    CliTermInfo term = make_term_icon(false);
    CliOutCtx* ctx = cli_out_init(&term);
    TEST_ASSERT(ctx != NULL);

    FILE* f = open_tmp();
    TEST_ASSERT(f != NULL);

    cli_out_icon(ctx, f, "cross");

    long len = 0;
    char* output = read_tmpfile_contents(f, &len);
    fclose(f);

    TEST_ASSERT(output != NULL);
    TEST_ASSERT_STR_EQ(output, "x");

    free(output);
    cli_out_destroy(ctx);
    return 0;
}
