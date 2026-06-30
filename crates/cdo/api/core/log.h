/**
 * log.h - Structured logging API for cdo.
 *
 * Provides level-filtered logging with timestamp formatting and stream routing.
 * Phase 4: backed by cli_out_fmt() when a CliOutCtx is available.
 * Falls back to plain fprintf when no context is provided (test mode).
 *
 * Replaces commons/output.h logging functions.
 */
#ifndef CDO_CORE_LOG_H
#define CDO_CORE_LOG_H

#include "out/cli_out.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CDO_LOG_LEVEL_ERROR = 0,
    CDO_LOG_LEVEL_WARN,
    CDO_LOG_LEVEL_INFO,
    CDO_LOG_LEVEL_DEBUG,
    CDO_LOG_LEVEL_TRACE,
} CdoLogLevel;

/// Initialize the logging subsystem with the output context and level.
/// When ctx is non-NULL, output flows through cli_out_fmt() with styled markers.
/// When ctx is NULL, output falls back to plain fprintf (used in tests).
/// Call once at startup before any logging.
void cdo_log_init(CliOutCtx* ctx, CdoLogLevel level);

/// Test-oriented initializer: sets level, color, and TTY state directly.
/// Does NOT require a CliOutCtx. Used by unit tests to control output behavior.
void cdo_log_init_test(CdoLogLevel level, bool use_color, bool is_tty);

/// Log a message at the given level. Filtered by configured threshold.
/// ERROR/WARN go to stderr; INFO/DEBUG/TRACE go to stdout.
/// Prepends [YYYY-MM-DD HH:MM:SS] timestamp.
void cdo_log(CdoLogLevel level, const char* fmt, ...);

/// Query whether colors are currently active (for conditional formatting).
bool cdo_log_use_color(void);

/// Query whether stdout is a TTY (for progress bar decisions).
bool cdo_log_is_tty(void);

/// Test helper: get the number of log messages emitted since last reset.
int cdo_log_test_get_emit_count(void);

/// Test helper: reset the emit counter to zero.
void cdo_log_test_reset_emit_count(void);

// Convenience macros
#define cdo_log_error(fmt, ...) cdo_log(CDO_LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define cdo_log_warn(fmt, ...)  cdo_log(CDO_LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__)
#define cdo_log_info(fmt, ...)  cdo_log(CDO_LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)
#define cdo_log_debug(fmt, ...) cdo_log(CDO_LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define cdo_log_trace(fmt, ...) cdo_log(CDO_LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* CDO_CORE_LOG_H */
