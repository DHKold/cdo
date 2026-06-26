#ifndef CDO_CORE_OUTPUT_H
#define CDO_CORE_OUTPUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Color Mode ---
typedef enum {
    CDO_COLOR_AUTO,
    CDO_COLOR_ALWAYS,
    CDO_COLOR_NEVER,
} CdoColorMode;

// --- Log Level ---
typedef enum {
    CDO_LOG_ERROR,
    CDO_LOG_WARN,
    CDO_LOG_INFO,
    CDO_LOG_DEBUG,
    CDO_LOG_TRACE,
} CdoLogLevel;

/// Initialize the output system.
/// Detects TTY status, configures color mode and log level.
/// Call once at startup before any logging.
void output_init(CdoColorMode mode, CdoLogLevel level, bool is_tty);

/// Log a message at the given level. Filtered by configured level.
/// Messages below the configured level are suppressed.
/// ERROR and WARN go to stderr; INFO, DEBUG, TRACE go to stdout.
void output_log(CdoLogLevel level, const char* fmt, ...);

/// Returns whether colors are currently active.
/// Useful for deciding whether to emit progress animations.
bool output_use_color(void);

/// Returns whether the output is a TTY.
/// Useful for deciding whether to emit progress animations.
bool output_is_tty(void);

// --- Progress Bar ---

/// Opaque progress bar handle.
typedef struct ProgressBar ProgressBar;

/// Create a progress bar with a label and total count.
/// If not a TTY, suppresses animations but tracks state.
/// Returns NULL on allocation failure.
ProgressBar* progress_create(const char* label, int total);

/// Update the progress bar to show the given completed count.
/// If TTY, overwrites the current line with a visual bar.
/// If not TTY, does nothing.
void progress_update(ProgressBar* bar, int completed);

/// Finish and clean up the progress bar.
/// If TTY, clears the progress line and prints a completion message.
/// Frees the ProgressBar memory.
void progress_finish(ProgressBar* bar);

// --- Convenience Macros ---
#define cdo_error(fmt, ...) output_log(CDO_LOG_ERROR, fmt, ##__VA_ARGS__)
#define cdo_warn(fmt, ...)  output_log(CDO_LOG_WARN,  fmt, ##__VA_ARGS__)
#define cdo_info(fmt, ...)  output_log(CDO_LOG_INFO,  fmt, ##__VA_ARGS__)
#define cdo_debug(fmt, ...) output_log(CDO_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define cdo_trace(fmt, ...) output_log(CDO_LOG_TRACE, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_OUTPUT_H
