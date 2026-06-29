/**
 * cli_term.h - Terminal detection and capability probing.
 *
 * Responsible for probing the terminal environment once at startup
 * and caching the results. Call cli_term_detect() once at startup.
 */
#ifndef CDO_CLI_TERM_H
#define CDO_CLI_TERM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Color capability levels detected from the terminal.
typedef enum {
    CLI_COLOR_NONE,         /* No color support or NO_COLOR set */
    CLI_COLOR_BASIC_16,     /* Standard 16-color ANSI */
    CLI_COLOR_EXTENDED_256, /* 256-color xterm */
    CLI_COLOR_TRUECOLOR,    /* 24-bit RGB */
} CliColorLevel;

/// Cached terminal capabilities, populated by cli_term_detect().
typedef struct {
    bool            stdout_tty;     /* Is stdout a TTY? */
    bool            stderr_tty;     /* Is stderr a TTY? */
    CliColorLevel   color_level;    /* Detected color capability */
    int             columns;        /* Terminal width (0 = unknown, default 80) */
    int             rows;           /* Terminal height (0 = unknown) */
    bool            unicode;        /* Terminal supports Unicode output */
} CliTermInfo;

/// Detect terminal capabilities. Populates `info`.
/// Inspects: isatty, TERM, COLORTERM, NO_COLOR, locale/codepage, dimensions.
/// Call once at startup. Thread-safe after initialization.
/// Returns 0 on success, non-zero on failure.
int cli_term_detect(CliTermInfo* info);

/// Enable ANSI virtual terminal processing (Windows only, no-op elsewhere).
/// Must be called after cli_term_detect() confirms TTY.
/// Returns 0 on success, non-zero if enablement failed.
int cli_term_enable_ansi(void);

#ifdef __cplusplus
}
#endif

#endif /* CDO_CLI_TERM_H */
