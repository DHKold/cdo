#include "core/output.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// --- File-scope state ---
static CdoLogLevel s_log_level  = CDO_LOG_INFO;
static bool        s_use_color  = false;
static bool        s_is_tty     = false;
static bool        s_quiet      = false;
static ProgressBar* s_active_bar = NULL;

// --- Forward declarations for progress bar helpers ---
static void progress_render(const ProgressBar* bar, int completed);
static void progress_erase(void);
static void progress_rerender(void);

// --- Test instrumentation ---
static int s_output_emit_count = 0;

int output_test_get_emit_count(void) {
    return s_output_emit_count;
}

void output_test_reset_emit_count(void) {
    s_output_emit_count = 0;
}

// --- ANSI escape sequences ---
#define ANSI_RED     "\033[31m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_GREY    "\033[90m"
#define ANSI_RESET   "\033[0m"

// --- Timestamp formatting ---
#define TIMESTAMP_BUF_SIZE 24

// Internal helper - writes "[YYYY-MM-DD HH:MM:SS] " into buf (at least TIMESTAMP_BUF_SIZE bytes)
static void format_timestamp(char* buf, size_t buf_size) {
    static const char placeholder[] = "[0000-00-00 00:00:00] ";

    if (buf_size < TIMESTAMP_BUF_SIZE) {
        if (buf_size > 0) {
            buf[0] = '\0';
        }
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

void output_init(CdoColorMode mode, CdoLogLevel level, bool is_tty) {
    s_log_level = level;
    s_is_tty    = is_tty;
    s_quiet     = (level == CDO_LOG_ERROR);

    switch (mode) {
        case CDO_COLOR_ALWAYS:
            s_use_color = true;
            break;
        case CDO_COLOR_NEVER:
            s_use_color = false;
            break;
        case CDO_COLOR_AUTO:
        default:
            // When stdout is not a TTY, default to no colors
            s_use_color = is_tty;
            break;
    }

#ifdef _WIN32
    // Enable ANSI escape sequences on Windows console
    if (s_use_color) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD dwMode = 0;
            if (GetConsoleMode(hOut, &dwMode)) {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hOut, dwMode);
            }
        }
        HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
        if (hErr != INVALID_HANDLE_VALUE) {
            DWORD dwMode = 0;
            if (GetConsoleMode(hErr, &dwMode)) {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hErr, dwMode);
            }
        }
    }
#endif
}

void output_log(CdoLogLevel level, const char* fmt, ...) {
    // Filter: only emit messages at or above the configured level
    if (level > s_log_level) {
        return;
    }

    s_output_emit_count++;

    // Choose output stream: ERROR and WARN go to stderr, others to stdout
    FILE* stream = (level <= CDO_LOG_WARN) ? stderr : stdout;

    // If progress bar is active on a TTY, erase it before logging
    if (s_active_bar && s_is_tty) {
        progress_erase();
    }

    // Print timestamp
    char ts_buf[TIMESTAMP_BUF_SIZE];
    format_timestamp(ts_buf, sizeof(ts_buf));
    fputs(ts_buf, stream);

    // Emit color prefix if colors are active (wraps level label only)
    if (s_use_color) {
        switch (level) {
            case CDO_LOG_ERROR:
                fputs(ANSI_RED, stream);
                break;
            case CDO_LOG_WARN:
                fputs(ANSI_YELLOW, stream);
                break;
            case CDO_LOG_DEBUG:
            case CDO_LOG_TRACE:
                fputs(ANSI_GREY, stream);
                break;
            case CDO_LOG_INFO:
            default:
                // No color prefix for INFO (default terminal color)
                break;
        }
    }

    // Print level label (5-char padded)
    fputs(level_labels[level], stream);

    // Reset color after level label
    if (s_use_color) {
        if (level != CDO_LOG_INFO) {
            fputs(ANSI_RESET, stream);
        }
    }

    // Space separator between level label and message
    fputc(' ', stream);

    // Print the actual message
    va_list args;
    va_start(args, fmt);
    vfprintf(stream, fmt, args);
    va_end(args);

    // Add newline
    fputc('\n', stream);

    // Re-render progress bar after log message
    if (s_active_bar && s_is_tty) {
        progress_rerender();
    }
}

bool output_use_color(void) {
    return s_use_color;
}

bool output_is_tty(void) {
    return s_is_tty;
}

// --- Progress Bar Implementation ---

#define PROGRESS_BAR_WIDTH 30
#define PROGRESS_LABEL_MAX 64

// Milestone thresholds for non-TTY output (percentages)
static const int s_milestones[] = { 25, 50, 75, 100 };
#define MILESTONE_COUNT 4

struct ProgressBar {
    char    label[PROGRESS_LABEL_MAX];
    int     total;
    int     completed;
    bool    is_tty;
    int     last_milestone;  // index of last printed milestone (non-TTY)
    bool    suppressed;      // true when quiet mode suppresses rendering
};

/// Render the progress bar line to stdout (TTY mode).
/// Format: \r[=====>                        ] completed/total label
static void progress_render(const ProgressBar* bar, int completed) {
    // Clamp completed to [0, total]
    int clamped = completed;
    if (clamped < 0) clamped = 0;
    if (clamped > bar->total) clamped = bar->total;

    // Calculate filled portion
    int filled = (clamped * PROGRESS_BAR_WIDTH) / bar->total;
    int empty  = PROGRESS_BAR_WIDTH - filled;

    // Format: \r[=====>    ] completed/total label
    fprintf(stdout, "\r[");

    for (int i = 0; i < filled; i++) {
        // Last filled char is '>' as the arrow head, rest are '='
        if (i == filled - 1 && clamped < bar->total) {
            fputc('>', stdout);
        } else {
            fputc('=', stdout);
        }
    }
    for (int i = 0; i < empty; i++) {
        fputc(' ', stdout);
    }

    fprintf(stdout, "] %d/%d %s", clamped, bar->total, bar->label);
    fflush(stdout);
}

/// Erase the progress bar line (for log interleaving)
static void progress_erase(void) {
    fputs("\r\033[K", stdout);
    fflush(stdout);
}

/// Re-render the active progress bar after a log line
static void progress_rerender(void) {
    if (s_active_bar) {
        progress_render(s_active_bar, s_active_bar->completed);
    }
}

ProgressBar* progress_create(const char* label, int total) {
    ProgressBar* bar = (ProgressBar*)malloc(sizeof(ProgressBar));
    if (!bar) {
        return NULL;
    }

    // Copy label, truncating if necessary
    if (label) {
        strncpy(bar->label, label, PROGRESS_LABEL_MAX - 1);
        bar->label[PROGRESS_LABEL_MAX - 1] = '\0';
    } else {
        bar->label[0] = '\0';
    }

    bar->total          = (total > 0) ? total : 1;
    bar->completed      = 0;
    bar->is_tty         = s_is_tty;
    bar->last_milestone = -1;
    bar->suppressed     = s_quiet;

    s_active_bar = bar;

    // Print the initial progress line (skip rendering when suppressed)
    if (!bar->suppressed) {
        if (bar->is_tty) {
            progress_render(bar, 0);
        } else {
            // Non-TTY: print a start line
            fprintf(stdout, "%s: 0/%d\n", bar->label, bar->total);
            fflush(stdout);
        }
    }

    return bar;
}

void progress_update(ProgressBar* bar, int completed) {
    if (!bar) {
        return;
    }

    bar->completed = completed;

    if (bar->suppressed) {
        return;
    }

    if (bar->is_tty) {
        // TTY: overwrite line with \r
        progress_render(bar, completed);
    } else {
        // Non-TTY: print milestone percentages (25%, 50%, 75%, 100%)
        int pct = (completed * 100) / bar->total;
        for (int i = bar->last_milestone + 1; i < MILESTONE_COUNT; i++) {
            if (pct >= s_milestones[i]) {
                fprintf(stdout, "%s: %d%% (%d/%d)\n",
                        bar->label, s_milestones[i], completed, bar->total);
                fflush(stdout);
                bar->last_milestone = i;
            } else {
                break;
            }
        }
    }
}

void progress_finish(ProgressBar* bar) {
    if (!bar) {
        return;
    }

    s_active_bar = NULL;

    if (bar->suppressed) {
        free(bar);
        return;
    }

    if (bar->is_tty) {
        // Print final 100% line and newline
        progress_render(bar, bar->total);
        fprintf(stdout, "\n");
        fflush(stdout);
    } else {
        // Non-TTY: print final line if not already printed at 100% milestone
        if (bar->last_milestone < MILESTONE_COUNT - 1) {
            fprintf(stdout, "%s: 100%% (%d/%d)\n",
                    bar->label, bar->total, bar->total);
            fflush(stdout);
        }
    }

    free(bar);
}
