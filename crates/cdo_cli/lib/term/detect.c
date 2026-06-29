/**
 * detect.c - Terminal capability probing.
 *
 * Detects TTY state, color level, terminal dimensions, and Unicode support.
 * Platform-conditional: uses Win32 APIs on Windows, POSIX on others.
 *
 * Color level detection flowchart:
 *   1. NO_COLOR env set (any value) -> CLI_COLOR_NONE
 *   2. stdout is NOT a TTY         -> CLI_COLOR_NONE
 *   3. COLORTERM = "truecolor" or "24bit" -> CLI_COLOR_TRUECOLOR
 *   4. TERM contains "256color"    -> CLI_COLOR_EXTENDED_256
 *   5. Otherwise                   -> CLI_COLOR_BASIC_16
 */
#include "../../api/term/cli_term.h"
#include "../../api/cli_errors.h"

#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
/* =========================================================================
 * Windows implementation
 * ========================================================================= */
#include <windows.h>

/**
 * Detect whether stdout is a TTY (console handle) on Windows.
 */
static bool detect_stdout_tty(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == NULL) return false;
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) != 0;
}

/**
 * Detect whether stderr is a TTY (console handle) on Windows.
 */
static bool detect_stderr_tty(void) {
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == NULL) return false;
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) != 0;
}

/**
 * Query terminal dimensions from the console screen buffer.
 * Sets columns and rows. Returns true on success.
 */
static bool detect_dimensions(int* columns, int* rows) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == NULL) return false;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(h, &csbi)) return false;

    *columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return true;
}

/**
 * Detect Unicode support on Windows by checking the console output code page.
 * CP 65001 = UTF-8.
 */
static bool detect_unicode(void) {
    return GetConsoleOutputCP() == 65001;
}

#else
/* =========================================================================
 * POSIX implementation (Linux, macOS)
 * ========================================================================= */
#include <unistd.h>
#include <sys/ioctl.h>

/**
 * Detect whether stdout is a TTY on POSIX.
 */
static bool detect_stdout_tty(void) {
    return isatty(STDOUT_FILENO) != 0;
}

/**
 * Detect whether stderr is a TTY on POSIX.
 */
static bool detect_stderr_tty(void) {
    return isatty(STDERR_FILENO) != 0;
}

/**
 * Query terminal dimensions using ioctl TIOCGWINSZ.
 * Sets columns and rows. Returns true on success.
 */
static bool detect_dimensions(int* columns, int* rows) {
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) return false;
    if (ws.ws_col == 0) return false;

    *columns = (int)ws.ws_col;
    *rows = (int)ws.ws_row;
    return true;
}

/**
 * Detect Unicode support on POSIX by inspecting locale environment variables.
 * Checks LANG, LC_ALL, and LC_CTYPE for "UTF-8" or "utf-8".
 */
static bool detect_unicode(void) {
    const char* vars[] = { "LC_ALL", "LC_CTYPE", "LANG" };
    for (int i = 0; i < 3; i++) {
        const char* val = getenv(vars[i]);
        if (val != NULL) {
            if (strstr(val, "UTF-8") != NULL || strstr(val, "utf-8") != NULL) {
                return true;
            }
        }
    }
    return false;
}

#endif /* _WIN32 */

/* =========================================================================
 * Color level detection (platform-independent logic)
 * ========================================================================= */

/**
 * Detect color level following the flowchart:
 *   1. NO_COLOR set -> NONE
 *   2. Not a TTY   -> NONE
 *   3. COLORTERM = "truecolor" or "24bit" -> TRUECOLOR
 *   4. TERM contains "256color" -> EXTENDED_256
 *   5. Otherwise -> BASIC_16
 */
static CliColorLevel detect_color_level(bool is_tty) {
    /* Step 1: NO_COLOR env variable takes absolute precedence */
    const char* no_color = getenv("NO_COLOR");
    if (no_color != NULL) {
        return CLI_COLOR_NONE;
    }

    /* Step 2: Not a TTY -> no color */
    if (!is_tty) {
        return CLI_COLOR_NONE;
    }

    /* Step 3: COLORTERM = "truecolor" or "24bit" */
    const char* colorterm = getenv("COLORTERM");
    if (colorterm != NULL) {
        if (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0) {
            return CLI_COLOR_TRUECOLOR;
        }
    }

    /* Step 4: TERM contains "256color" */
    const char* term = getenv("TERM");
    if (term != NULL) {
        if (strstr(term, "256color") != NULL) {
            return CLI_COLOR_EXTENDED_256;
        }
    }

    /* Step 5: Fallback to basic 16-color */
    return CLI_COLOR_BASIC_16;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

int cli_term_detect(CliTermInfo* info) {
    if (!info) return CLI_ERR_PLATFORM;

    /* Zero the struct to ensure clean state */
    memset(info, 0, sizeof(*info));

    /* TTY detection */
    info->stdout_tty = detect_stdout_tty();
    info->stderr_tty = detect_stderr_tty();

    /* Color level detection */
    info->color_level = detect_color_level(info->stdout_tty);

    /* Terminal dimensions */
    int cols = 0;
    int rows = 0;
    if (detect_dimensions(&cols, &rows)) {
        info->columns = cols;
        info->rows = rows;
    }

    /* Default columns to 80 when detection fails or returns 0 */
    if (info->columns <= 0) {
        info->columns = 80;
    }

    /* Unicode detection */
    info->unicode = detect_unicode();

    return CLI_OK;
}
