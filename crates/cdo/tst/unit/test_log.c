/**
 * test_log.c - Unit tests for the cdo logging module (core/log.h).
 *
 * Validates: Requirements 10.2, 10.3, 10.4, 10.5, 12.3, 12.4
 *
 * Phase 4: The logging module outputs directly via cli_out_fmt() when a
 * CliOutCtx is available, or via plain fprintf in test/fallback mode.
 * Tests use cdo_log_init_test() to configure color and TTY state without
 * requiring a CliOutCtx, and cdo_log_test_get_emit_count() for level filtering.
 *
 * Additional tests exercise the CliOutCtx-backed path (non-NULL ctx)
 * and the progress bar in non-TTY mode.
 */
#include "cdo_ut.h"
#include "core/log.h"
#include "out/cli_out.h"
#include "term/cli_term.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

/// Initialize the log module in test mode (no CliOutCtx) with color disabled.
static void init_log_at_level(CdoLogLevel level) {
    cdo_log_init_test(level, false, false);
    cdo_log_test_reset_emit_count();
}

/// Initialize the log module in test mode with color enabled and TTY=true.
static void init_log_with_color(CdoLogLevel level) {
    cdo_log_init_test(level, true, true);
    cdo_log_test_reset_emit_count();
}

/* --------------------------------------------------------------------------
 * Stream capture helper.
 *
 * On Windows, tmpfile() often fails due to permission issues (tries to write
 * to C:\). Instead we use a named temp file via _tempnam + open + _dup2.
 * -------------------------------------------------------------------------- */

typedef struct {
    char  path[260];
    int   orig_fd;
    int   stream_fd;  // 1=stdout, 2=stderr
} StreamCapture;

static int capture_start(StreamCapture* cap, int stream_fd) {
    memset(cap, 0, sizeof(*cap));
    cap->stream_fd = stream_fd;
    cap->orig_fd = -1;

    // Create a unique temp file name
    char* name = _tempnam(NULL, "cdo_log_test_");
    if (!name) return -1;
    strncpy(cap->path, name, sizeof(cap->path) - 1);
    free(name);

    // Flush the target stream before redirecting
    fflush(stream_fd == 1 ? stdout : stderr);

    // Save original fd
    cap->orig_fd = _dup(stream_fd);
    if (cap->orig_fd < 0) return -1;

    // Open temp file for writing
    int tmp_fd = _open(cap->path, _O_CREAT | _O_TRUNC | _O_WRONLY, _S_IREAD | _S_IWRITE);
    if (tmp_fd < 0) { _dup2(cap->orig_fd, stream_fd); _close(cap->orig_fd); return -1; }

    // Redirect stream to temp file
    _dup2(tmp_fd, stream_fd);
    _close(tmp_fd);
    return 0;
}

static int capture_stop(StreamCapture* cap, char* buf, size_t buf_size) {
    // Flush the redirected stream
    fflush(cap->stream_fd == 1 ? stdout : stderr);

    // Restore original fd
    _dup2(cap->orig_fd, cap->stream_fd);
    _close(cap->orig_fd);
    cap->orig_fd = -1;

    // Read back the captured content
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

    // Clean up temp file
    _unlink(cap->path);
    return n;
}

/* --------------------------------------------------------------------------
 * Level Filtering Tests (Req 10.3)
 * Messages below the configured level are suppressed.
 * -------------------------------------------------------------------------- */

TEST(log_level_error_suppresses_all_below) {
    init_log_at_level(CDO_LOG_LEVEL_ERROR);
    cdo_log(CDO_LOG_LEVEL_ERROR, "error msg");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    cdo_log(CDO_LOG_LEVEL_WARN, "warn msg");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    cdo_log(CDO_LOG_LEVEL_INFO, "info msg");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    cdo_log(CDO_LOG_LEVEL_DEBUG, "debug msg");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    cdo_log(CDO_LOG_LEVEL_TRACE, "trace msg");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    return 0;
}

TEST(log_level_warn_allows_error_and_warn) {
    init_log_at_level(CDO_LOG_LEVEL_WARN);
    cdo_log(CDO_LOG_LEVEL_ERROR, "error msg");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    cdo_log(CDO_LOG_LEVEL_WARN, "warn msg");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 2);
    cdo_log(CDO_LOG_LEVEL_INFO, "info msg");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 2);
    cdo_log(CDO_LOG_LEVEL_DEBUG, "debug msg");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 2);
    cdo_log(CDO_LOG_LEVEL_TRACE, "trace msg");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 2);
    return 0;
}

TEST(log_level_info_allows_error_warn_info) {
    init_log_at_level(CDO_LOG_LEVEL_INFO);
    cdo_log(CDO_LOG_LEVEL_ERROR, "error");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    cdo_log(CDO_LOG_LEVEL_WARN, "warn");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 2);
    cdo_log(CDO_LOG_LEVEL_INFO, "info");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 3);
    cdo_log(CDO_LOG_LEVEL_DEBUG, "debug");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 3);
    cdo_log(CDO_LOG_LEVEL_TRACE, "trace");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 3);
    return 0;
}

TEST(log_level_debug_allows_error_warn_info_debug) {
    init_log_at_level(CDO_LOG_LEVEL_DEBUG);
    cdo_log(CDO_LOG_LEVEL_ERROR, "e");
    cdo_log(CDO_LOG_LEVEL_WARN, "w");
    cdo_log(CDO_LOG_LEVEL_INFO, "i");
    cdo_log(CDO_LOG_LEVEL_DEBUG, "d");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 4);
    cdo_log(CDO_LOG_LEVEL_TRACE, "t");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 4);
    return 0;
}

TEST(log_level_trace_allows_all) {
    init_log_at_level(CDO_LOG_LEVEL_TRACE);
    cdo_log(CDO_LOG_LEVEL_ERROR, "e");
    cdo_log(CDO_LOG_LEVEL_WARN, "w");
    cdo_log(CDO_LOG_LEVEL_INFO, "i");
    cdo_log(CDO_LOG_LEVEL_DEBUG, "d");
    cdo_log(CDO_LOG_LEVEL_TRACE, "t");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 5);
    return 0;
}

/* --------------------------------------------------------------------------
 * Convenience Macro Tests (Req 10.3)
 * Macros expand to cdo_log() with correct level.
 * -------------------------------------------------------------------------- */

TEST(log_macros_respect_level_filtering) {
    init_log_at_level(CDO_LOG_LEVEL_WARN);
    cdo_log_error("error via macro");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    cdo_log_warn("warn via macro");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 2);
    cdo_log_info("info via macro - should be suppressed");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 2);
    cdo_log_debug("debug via macro - should be suppressed");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 2);
    cdo_log_trace("trace via macro - should be suppressed");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 2);
    return 0;
}

/* --------------------------------------------------------------------------
 * Stream Routing Tests (Req 10.4)
 * ERROR/WARN route to stderr; INFO/DEBUG/TRACE route to stdout.
 * -------------------------------------------------------------------------- */

TEST_SERIAL(log_error_routes_to_stderr) {
    init_log_at_level(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 2), 0);

    cdo_log(CDO_LOG_LEVEL_ERROR, "error message");

    char buf[512] = {0};
    int n = capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "error message") != NULL);
    return 0;
}

TEST_SERIAL(log_warn_routes_to_stderr) {
    init_log_at_level(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 2), 0);

    cdo_log(CDO_LOG_LEVEL_WARN, "warn message");

    char buf[512] = {0};
    int n = capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "warn message") != NULL);
    return 0;
}

TEST_SERIAL(log_info_routes_to_stdout) {
    init_log_at_level(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    cdo_log(CDO_LOG_LEVEL_INFO, "info message");

    char buf[512] = {0};
    int n = capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "info message") != NULL);
    return 0;
}

TEST_SERIAL(log_debug_routes_to_stdout) {
    init_log_at_level(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    cdo_log(CDO_LOG_LEVEL_DEBUG, "debug message");

    char buf[512] = {0};
    int n = capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "debug message") != NULL);
    return 0;
}

TEST_SERIAL(log_trace_routes_to_stdout) {
    init_log_at_level(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    cdo_log(CDO_LOG_LEVEL_TRACE, "trace message");

    char buf[512] = {0};
    int n = capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "trace message") != NULL);
    return 0;
}

/* --------------------------------------------------------------------------
 * Timestamp Format Tests (Req 10.5)
 * Log lines are prepended with [YYYY-MM-DD HH:MM:SS] format.
 * -------------------------------------------------------------------------- */

TEST_SERIAL(log_output_contains_timestamp_format) {
    init_log_at_level(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    cdo_log(CDO_LOG_LEVEL_INFO, "timestamp test");

    char buf[512] = {0};
    capture_stop(&cap, buf, sizeof(buf));

    // Verify timestamp pattern: [YYYY-MM-DD HH:MM:SS]
    TEST_ASSERT(buf[0] == '[');
    TEST_ASSERT(buf[5] == '-');   // YYYY-
    TEST_ASSERT(buf[8] == '-');   // MM-
    TEST_ASSERT(buf[11] == ' '); // DD space
    TEST_ASSERT(buf[14] == ':'); // HH:
    TEST_ASSERT(buf[17] == ':'); // MM:
    TEST_ASSERT(buf[20] == ']'); // SS]
    return 0;
}

TEST_SERIAL(log_timestamp_uses_current_date) {
    init_log_at_level(CDO_LOG_LEVEL_TRACE);

    // Capture current date before logging
    time_t now = time(NULL);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    char expected_date[16];
    snprintf(expected_date, sizeof(expected_date), "[%04d-%02d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);

    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    cdo_log(CDO_LOG_LEVEL_INFO, "time check");

    char buf[512] = {0};
    capture_stop(&cap, buf, sizeof(buf));

    // The date portion should match today's date
    TEST_ASSERT(strncmp(buf, expected_date, strlen(expected_date)) == 0);
    return 0;
}

/* --------------------------------------------------------------------------
 * Color Code Tests (Req 10.2)
 * Color codes applied per level when color enabled:
 *   red (\033[31m) = ERROR, yellow (\033[33m) = WARN, dim/grey (\033[90m) = DEBUG/TRACE
 * No color for INFO (uses default terminal color).
 * -------------------------------------------------------------------------- */

TEST_SERIAL(log_error_has_red_ansi_when_color_enabled) {
    init_log_with_color(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 2), 0);  // ERROR goes to stderr

    cdo_log(CDO_LOG_LEVEL_ERROR, "red error");

    char buf[512] = {0};
    capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "\033[31m") != NULL);  // red
    TEST_ASSERT(strstr(buf, "\033[0m") != NULL);   // reset
    return 0;
}

TEST_SERIAL(log_warn_has_yellow_ansi_when_color_enabled) {
    init_log_with_color(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 2), 0);  // WARN goes to stderr

    cdo_log(CDO_LOG_LEVEL_WARN, "yellow warn");

    char buf[512] = {0};
    capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "\033[33m") != NULL);  // yellow
    TEST_ASSERT(strstr(buf, "\033[0m") != NULL);   // reset
    return 0;
}

TEST_SERIAL(log_debug_has_dim_ansi_when_color_enabled) {
    init_log_with_color(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);  // DEBUG goes to stdout

    cdo_log(CDO_LOG_LEVEL_DEBUG, "dim debug");

    char buf[512] = {0};
    capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "\033[90m") != NULL);  // grey/dim
    TEST_ASSERT(strstr(buf, "\033[0m") != NULL);   // reset
    return 0;
}

TEST_SERIAL(log_trace_has_dim_ansi_when_color_enabled) {
    init_log_with_color(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);  // TRACE goes to stdout

    cdo_log(CDO_LOG_LEVEL_TRACE, "dim trace");

    char buf[512] = {0};
    capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "\033[90m") != NULL);  // grey/dim
    TEST_ASSERT(strstr(buf, "\033[0m") != NULL);   // reset
    return 0;
}

TEST_SERIAL(log_info_has_no_color_prefix_when_color_enabled) {
    init_log_with_color(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);  // INFO goes to stdout

    cdo_log(CDO_LOG_LEVEL_INFO, "plain info");

    char buf[512] = {0};
    capture_stop(&cap, buf, sizeof(buf));
    // INFO should NOT have any ANSI color escape sequences
    TEST_ASSERT(strstr(buf, "\033[31m") == NULL);  // no red
    TEST_ASSERT(strstr(buf, "\033[33m") == NULL);  // no yellow
    TEST_ASSERT(strstr(buf, "\033[90m") == NULL);  // no grey
    TEST_ASSERT(strstr(buf, "\033[0m") == NULL);   // no reset
    return 0;
}

/* --------------------------------------------------------------------------
 * No ANSI Codes When Color Disabled (Req 10.2)
 * -------------------------------------------------------------------------- */

TEST_SERIAL(log_no_ansi_codes_when_color_disabled_stderr) {
    init_log_at_level(CDO_LOG_LEVEL_TRACE);  // color=false, tty=false
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 2), 0);

    cdo_log(CDO_LOG_LEVEL_ERROR, "no color error");
    cdo_log(CDO_LOG_LEVEL_WARN, "no color warn");

    char buf[1024] = {0};
    capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "\033[") == NULL);
    return 0;
}

TEST_SERIAL(log_no_ansi_codes_when_color_disabled_stdout) {
    init_log_at_level(CDO_LOG_LEVEL_TRACE);  // color=false, tty=false
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    cdo_log(CDO_LOG_LEVEL_INFO, "no color info");
    cdo_log(CDO_LOG_LEVEL_DEBUG, "no color debug");
    cdo_log(CDO_LOG_LEVEL_TRACE, "no color trace");

    char buf[1024] = {0};
    capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "\033[") == NULL);
    return 0;
}

/* --------------------------------------------------------------------------
 * cdo_log_use_color / cdo_log_is_tty Query Tests
 * -------------------------------------------------------------------------- */

TEST(log_use_color_returns_false_when_color_disabled) {
    cdo_log_init_test(CDO_LOG_LEVEL_INFO, false, false);
    TEST_ASSERT(cdo_log_use_color() == false);
    return 0;
}

TEST(log_use_color_returns_true_when_color_enabled) {
    cdo_log_init_test(CDO_LOG_LEVEL_INFO, true, true);
    TEST_ASSERT(cdo_log_use_color() == true);
    return 0;
}

TEST(log_is_tty_returns_false_when_not_tty) {
    cdo_log_init_test(CDO_LOG_LEVEL_INFO, false, false);
    TEST_ASSERT(cdo_log_is_tty() == false);
    return 0;
}

TEST(log_is_tty_returns_true_when_tty) {
    cdo_log_init_test(CDO_LOG_LEVEL_INFO, false, true);
    TEST_ASSERT(cdo_log_is_tty() == true);
    return 0;
}

/* --------------------------------------------------------------------------
 * cdo_log_init Tests - Verify re-initialization changes effective level.
 * -------------------------------------------------------------------------- */

TEST(log_init_changes_level) {
    // Start at INFO level
    init_log_at_level(CDO_LOG_LEVEL_INFO);
    cdo_log(CDO_LOG_LEVEL_DEBUG, "should be suppressed");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 0);

    // Re-init at DEBUG level
    cdo_log_init_test(CDO_LOG_LEVEL_DEBUG, false, false);
    cdo_log_test_reset_emit_count();
    cdo_log(CDO_LOG_LEVEL_DEBUG, "should now pass");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    return 0;
}

/* --------------------------------------------------------------------------
 * Format String Tests - printf-style formatting works through cdo_log().
 * -------------------------------------------------------------------------- */

TEST_SERIAL(log_format_string_interpolation) {
    init_log_at_level(CDO_LOG_LEVEL_TRACE);
    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    cdo_log(CDO_LOG_LEVEL_INFO, "built %d files in %s", 42, "crate_a");

    char buf[512] = {0};
    capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(strstr(buf, "built 42 files in crate_a") != NULL);
    return 0;
}

/* ==========================================================================
 * CliOutCtx-Backed Tests (Req 10.2, 10.3)
 *
 * These tests exercise the cli_out_fmt() path by creating a real CliOutCtx
 * with specific terminal settings (non-TTY, no color) and calling
 * cdo_log_init(ctx, level).
 * ========================================================================== */

/// Create a non-TTY, no-color CliOutCtx for testing purposes.
static CliOutCtx* create_non_tty_ctx(void) {
    CliTermInfo term = {0};
    term.stdout_tty  = false;
    term.stderr_tty  = false;
    term.color_level = CLI_COLOR_NONE;
    term.columns     = 80;
    term.unicode     = false;
    return cli_out_init(&term);
}

/// Create a TTY CliOutCtx with basic color support for testing.
static CliOutCtx* create_tty_color_ctx(void) {
    CliTermInfo term = {0};
    term.stdout_tty  = true;
    term.stderr_tty  = true;
    term.color_level = CLI_COLOR_BASIC_16;
    term.columns     = 80;
    term.unicode     = false;
    return cli_out_init(&term);
}

/* --------------------------------------------------------------------------
 * Level filtering through CliOutCtx path (Req 10.3)
 * -------------------------------------------------------------------------- */

TEST(log_ctx_level_error_suppresses_below) {
    CliOutCtx* ctx = create_non_tty_ctx();
    TEST_ASSERT(ctx != NULL);

    cdo_log_init(ctx, CDO_LOG_LEVEL_ERROR);
    cdo_log_test_reset_emit_count();

    cdo_log(CDO_LOG_LEVEL_ERROR, "error via ctx");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    cdo_log(CDO_LOG_LEVEL_WARN, "warn via ctx");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    cdo_log(CDO_LOG_LEVEL_INFO, "info via ctx");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    cdo_log(CDO_LOG_LEVEL_DEBUG, "debug via ctx");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    cdo_log(CDO_LOG_LEVEL_TRACE, "trace via ctx");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);

    cli_out_destroy(ctx);
    return 0;
}

TEST(log_ctx_level_info_allows_error_warn_info) {
    CliOutCtx* ctx = create_non_tty_ctx();
    TEST_ASSERT(ctx != NULL);

    cdo_log_init(ctx, CDO_LOG_LEVEL_INFO);
    cdo_log_test_reset_emit_count();

    cdo_log(CDO_LOG_LEVEL_ERROR, "e");
    cdo_log(CDO_LOG_LEVEL_WARN, "w");
    cdo_log(CDO_LOG_LEVEL_INFO, "i");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 3);
    cdo_log(CDO_LOG_LEVEL_DEBUG, "d");
    cdo_log(CDO_LOG_LEVEL_TRACE, "t");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 3);

    cli_out_destroy(ctx);
    return 0;
}

TEST(log_ctx_level_trace_allows_all) {
    CliOutCtx* ctx = create_non_tty_ctx();
    TEST_ASSERT(ctx != NULL);

    cdo_log_init(ctx, CDO_LOG_LEVEL_TRACE);
    cdo_log_test_reset_emit_count();

    cdo_log(CDO_LOG_LEVEL_ERROR, "e");
    cdo_log(CDO_LOG_LEVEL_WARN, "w");
    cdo_log(CDO_LOG_LEVEL_INFO, "i");
    cdo_log(CDO_LOG_LEVEL_DEBUG, "d");
    cdo_log(CDO_LOG_LEVEL_TRACE, "t");
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 5);

    cli_out_destroy(ctx);
    return 0;
}

/* --------------------------------------------------------------------------
 * No ANSI sequences via CliOutCtx with CLI_COLOR_NONE (Req 10.2)
 * -------------------------------------------------------------------------- */

TEST_SERIAL(log_ctx_no_ansi_when_color_none_stdout) {
    CliOutCtx* ctx = create_non_tty_ctx();
    TEST_ASSERT(ctx != NULL);

    cdo_log_init(ctx, CDO_LOG_LEVEL_TRACE);
    cdo_log_test_reset_emit_count();

    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    cdo_log(CDO_LOG_LEVEL_INFO, "plain info ctx");
    cdo_log(CDO_LOG_LEVEL_DEBUG, "plain debug ctx");
    cdo_log(CDO_LOG_LEVEL_TRACE, "plain trace ctx");

    char buf[2048] = {0};
    capture_stop(&cap, buf, sizeof(buf));

    // No ANSI escape sequences when color_level is CLI_COLOR_NONE
    TEST_ASSERT(strstr(buf, "\033[") == NULL);

    cli_out_destroy(ctx);
    return 0;
}

TEST_SERIAL(log_ctx_no_ansi_when_color_none_stderr) {
    CliOutCtx* ctx = create_non_tty_ctx();
    TEST_ASSERT(ctx != NULL);

    cdo_log_init(ctx, CDO_LOG_LEVEL_TRACE);
    cdo_log_test_reset_emit_count();

    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 2), 0);

    cdo_log(CDO_LOG_LEVEL_ERROR, "plain error ctx");
    cdo_log(CDO_LOG_LEVEL_WARN, "plain warn ctx");

    char buf[2048] = {0};
    capture_stop(&cap, buf, sizeof(buf));

    // No ANSI escape sequences when color_level is CLI_COLOR_NONE
    TEST_ASSERT(strstr(buf, "\033[") == NULL);

    cli_out_destroy(ctx);
    return 0;
}

/* --------------------------------------------------------------------------
 * CliOutCtx with color enabled DOES emit ANSI via cli_out_fmt (Req 10.2)
 * -------------------------------------------------------------------------- */

TEST_SERIAL(log_ctx_emits_ansi_when_color_enabled) {
    CliOutCtx* ctx = create_tty_color_ctx();
    TEST_ASSERT(ctx != NULL);

    cdo_log_init(ctx, CDO_LOG_LEVEL_TRACE);
    cdo_log_test_reset_emit_count();

    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 2), 0);

    cdo_log(CDO_LOG_LEVEL_ERROR, "colored error");

    char buf[2048] = {0};
    capture_stop(&cap, buf, sizeof(buf));

    // cli_out_fmt with color enabled should emit ANSI codes for {red}
    TEST_ASSERT(strstr(buf, "\033[31m") != NULL);
    TEST_ASSERT(strstr(buf, "\033[0m") != NULL);

    cli_out_destroy(ctx);
    return 0;
}

/* --------------------------------------------------------------------------
 * Stream routing via CliOutCtx path (Req 10.4)
 * ERROR/WARN -> stderr, INFO/DEBUG/TRACE -> stdout
 * -------------------------------------------------------------------------- */

TEST_SERIAL(log_ctx_error_routes_to_stderr) {
    CliOutCtx* ctx = create_non_tty_ctx();
    TEST_ASSERT(ctx != NULL);

    cdo_log_init(ctx, CDO_LOG_LEVEL_TRACE);
    cdo_log_test_reset_emit_count();

    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 2), 0);

    cdo_log(CDO_LOG_LEVEL_ERROR, "ctx error stream");

    char buf[512] = {0};
    int n = capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "ctx error stream") != NULL);

    cli_out_destroy(ctx);
    return 0;
}

TEST_SERIAL(log_ctx_info_routes_to_stdout) {
    CliOutCtx* ctx = create_non_tty_ctx();
    TEST_ASSERT(ctx != NULL);

    cdo_log_init(ctx, CDO_LOG_LEVEL_TRACE);
    cdo_log_test_reset_emit_count();

    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    cdo_log(CDO_LOG_LEVEL_INFO, "ctx info stream");

    char buf[512] = {0};
    int n = capture_stop(&cap, buf, sizeof(buf));
    TEST_ASSERT(n > 0);
    TEST_ASSERT(strstr(buf, "ctx info stream") != NULL);

    cli_out_destroy(ctx);
    return 0;
}

/* ==========================================================================
 * Progress Bar Tests in Non-TTY Mode (Req 12.3, 12.4)
 *
 * Verifies that cli_out_progress_* with a non-TTY CliOutCtx emits milestone
 * percentage lines (25%, 50%, 75%, 100%) without ANSI sequences.
 * ========================================================================== */

TEST_SERIAL(progress_non_tty_emits_milestones) {
    CliOutCtx* ctx = create_non_tty_ctx();
    TEST_ASSERT(ctx != NULL);

    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    CliProgressBar* bar = cli_out_progress_create(ctx, "Building", 100);
    TEST_ASSERT(bar != NULL);

    // Update through all milestone thresholds
    cli_out_progress_update(bar, 25);   // 25%
    cli_out_progress_update(bar, 50);   // 50%
    cli_out_progress_update(bar, 75);   // 75%
    cli_out_progress_finish(bar);       // 100%

    char buf[2048] = {0};
    capture_stop(&cap, buf, sizeof(buf));

    // Verify milestone percentages are present
    TEST_ASSERT(strstr(buf, "25%") != NULL);
    TEST_ASSERT(strstr(buf, "50%") != NULL);
    TEST_ASSERT(strstr(buf, "75%") != NULL);
    TEST_ASSERT(strstr(buf, "100%") != NULL);

    cli_out_destroy(ctx);
    return 0;
}

TEST_SERIAL(progress_non_tty_no_ansi_sequences) {
    CliOutCtx* ctx = create_non_tty_ctx();
    TEST_ASSERT(ctx != NULL);

    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    CliProgressBar* bar = cli_out_progress_create(ctx, "Testing", 40);
    TEST_ASSERT(bar != NULL);

    // Update incrementally so each milestone fires
    cli_out_progress_update(bar, 10);   // 25%
    cli_out_progress_update(bar, 20);   // 50%
    cli_out_progress_update(bar, 30);   // 75%
    cli_out_progress_update(bar, 40);   // 100%
    cli_out_progress_finish(bar);

    char buf[2048] = {0};
    capture_stop(&cap, buf, sizeof(buf));

    // No ANSI escape sequences in non-TTY progress output
    TEST_ASSERT(strstr(buf, "\033[") == NULL);
    // No standalone carriage return (TTY in-place update uses \r without \n).
    // On Windows, \r\n is normal line ending so only check for \r NOT followed by \n.
    const char* p = buf;
    while ((p = strchr(p, '\r')) != NULL) {
        TEST_ASSERT(*(p + 1) == '\n');  // must be part of \r\n line ending
        p++;
    }

    cli_out_destroy(ctx);
    return 0;
}

TEST_SERIAL(progress_non_tty_includes_label) {
    CliOutCtx* ctx = create_non_tty_ctx();
    TEST_ASSERT(ctx != NULL);

    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    CliProgressBar* bar = cli_out_progress_create(ctx, "Compiling", 4);
    TEST_ASSERT(bar != NULL);

    cli_out_progress_update(bar, 1);   // 25%
    cli_out_progress_finish(bar);

    char buf[2048] = {0};
    capture_stop(&cap, buf, sizeof(buf));

    // Label should be included in milestone output
    TEST_ASSERT(strstr(buf, "Compiling") != NULL);

    cli_out_destroy(ctx);
    return 0;
}

TEST_SERIAL(progress_non_tty_skips_repeated_milestones) {
    CliOutCtx* ctx = create_non_tty_ctx();
    TEST_ASSERT(ctx != NULL);

    StreamCapture cap;
    TEST_ASSERT_EQ(capture_start(&cap, 1), 0);

    CliProgressBar* bar = cli_out_progress_create(ctx, "Build", 100);
    TEST_ASSERT(bar != NULL);

    // Multiple updates within same milestone range should not duplicate
    cli_out_progress_update(bar, 25);
    cli_out_progress_update(bar, 26);  // still in 25% bracket
    cli_out_progress_update(bar, 30);  // still in 25% bracket
    cli_out_progress_finish(bar);

    char buf[2048] = {0};
    capture_stop(&cap, buf, sizeof(buf));

    // "25%" should appear exactly once - count occurrences
    int count_25 = 0;
    const char* p = buf;
    while ((p = strstr(p, "25%")) != NULL) {
        count_25++;
        p += 3;
    }
    TEST_ASSERT_EQ(count_25, 1);

    cli_out_destroy(ctx);
    return 0;
}
