/**
 * log.c - Structured logging implementation for cdo.
 *
 * Phase 4: Routes through cli_out_fmt() when a CliOutCtx is available.
 * Falls back to plain fprintf when s_out_ctx is NULL (test mode / pre-init).
 */
#include "core/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

// --- Module state ---
static CliOutCtx*  s_out_ctx     = NULL;
static CdoLogLevel s_level       = CDO_LOG_LEVEL_INFO;
static bool        s_initialized = false;
static bool        s_use_color   = false;
static bool        s_is_tty      = false;

// --- Test instrumentation ---
static int s_emit_count = 0;

int cdo_log_test_get_emit_count(void) {
    return s_emit_count;
}

void cdo_log_test_reset_emit_count(void) {
    s_emit_count = 0;
}

// --- ANSI escape sequences (used in fallback fprintf path) ---
#define ANSI_RED     "\033[31m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_GREY    "\033[90m"
#define ANSI_RESET   "\033[0m"

// --- Timestamp formatting ---
#define TIMESTAMP_BUF_SIZE 24

static void format_timestamp(char* buf, size_t buf_size) {
    static const char placeholder[] = "[0000-00-00 00:00:00] ";

    if (buf_size < TIMESTAMP_BUF_SIZE) {
        if (buf_size > 0) buf[0] = '\0';
        return;
    }

    time_t now = time(NULL);
    if (now == (time_t)-1) {
        memcpy(buf, placeholder, sizeof(placeholder));
        return;
    }

    struct tm tm_buf;
#ifdef _WIN32
    if (localtime_s(&tm_buf, &now) != 0) {
        memcpy(buf, placeholder, sizeof(placeholder));
        return;
    }
#else
    if (localtime_r(&now, &tm_buf) == NULL) {
        memcpy(buf, placeholder, sizeof(placeholder));
        return;
    }
#endif

    size_t written = strftime(buf, buf_size, "[%Y-%m-%d %H:%M:%S] ", &tm_buf);
    if (written == 0) {
        memcpy(buf, placeholder, sizeof(placeholder));
    }
}

// --- Level labels (5-char padded for aligned output) ---
static const char* level_labels[] = {
    "ERROR", "WARN ", "INFO ", "DEBUG", "TRACE"
};

void cdo_log_init(CliOutCtx* ctx, CdoLogLevel level) {
    s_out_ctx     = ctx;
    s_level       = level;
    s_initialized = true;

    // Derive color and TTY state from the CliOutCtx when available
    if (ctx) {
        s_use_color = (cli_out_get_color_level(ctx) != CLI_COLOR_NONE);
        s_is_tty    = cli_out_get_stdout_tty(ctx);
    }
}

void cdo_log_init_test(CdoLogLevel level, bool use_color, bool is_tty) {
    s_out_ctx     = NULL;
    s_level       = level;
    s_initialized = true;
    s_use_color   = use_color;
    s_is_tty      = is_tty;
}

void cdo_log(CdoLogLevel level, const char* fmt, ...) {
    // Filter: suppress messages below configured threshold
    if (level > s_level) {
        return;
    }

    s_emit_count++;

    // Format the user message
    va_list args;
    va_start(args, fmt);
    char msg_buf[2048];
    int n = vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);
    if (n < 0) return;

    // Choose output stream: ERROR/WARN -> stderr, INFO/DEBUG/TRACE -> stdout
    FILE* stream = (level <= CDO_LOG_LEVEL_WARN) ? stderr : stdout;

    // Format timestamp
    char ts_buf[TIMESTAMP_BUF_SIZE];
    format_timestamp(ts_buf, sizeof(ts_buf));

    if (s_out_ctx) {
        // --- Phase 4 path: use cli_out_fmt() with inline markers ---
        // Markers must be embedded in the format string for cli_out_fmt to process them.
        switch (level) {
            case CDO_LOG_LEVEL_ERROR:
                cli_out_fmt(s_out_ctx, stream, "%s{red}%s{reset} %s\n", ts_buf, level_labels[level], msg_buf);
                break;
            case CDO_LOG_LEVEL_WARN:
                cli_out_fmt(s_out_ctx, stream, "%s{yellow}%s{reset} %s\n", ts_buf, level_labels[level], msg_buf);
                break;
            case CDO_LOG_LEVEL_DEBUG:
            case CDO_LOG_LEVEL_TRACE:
                cli_out_fmt(s_out_ctx, stream, "%s{dim}%s{reset} %s\n", ts_buf, level_labels[level], msg_buf);
                break;
            case CDO_LOG_LEVEL_INFO:
            default:
                cli_out_fmt(s_out_ctx, stream, "%s%s %s\n", ts_buf, level_labels[level], msg_buf);
                break;
        }
    } else {
        // --- Fallback path: plain fprintf (tests / pre-init) ---
        fputs(ts_buf, stream);

        if (s_use_color) {
            switch (level) {
                case CDO_LOG_LEVEL_ERROR:
                    fputs(ANSI_RED, stream);
                    break;
                case CDO_LOG_LEVEL_WARN:
                    fputs(ANSI_YELLOW, stream);
                    break;
                case CDO_LOG_LEVEL_DEBUG:
                case CDO_LOG_LEVEL_TRACE:
                    fputs(ANSI_GREY, stream);
                    break;
                case CDO_LOG_LEVEL_INFO:
                default:
                    break;
            }
        }

        fputs(level_labels[level], stream);

        if (s_use_color && level != CDO_LOG_LEVEL_INFO) {
            fputs(ANSI_RESET, stream);
        }

        fputc(' ', stream);
        fputs(msg_buf, stream);
        fputc('\n', stream);
    }
}

bool cdo_log_use_color(void) {
    if (s_out_ctx) {
        return (cli_out_get_color_level(s_out_ctx) != CLI_COLOR_NONE);
    }
    return s_use_color;
}

bool cdo_log_is_tty(void) {
    if (s_out_ctx) {
        return cli_out_get_stdout_tty(s_out_ctx);
    }
    return s_is_tty;
}
