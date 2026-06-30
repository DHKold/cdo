/**
 * cli_out.h - Rich output public API.
 *
 * Provides styled text output primitives and dynamic visual zones.
 * No logging policy imposed.
 */
#ifndef CDO_CLI_OUT_H
#define CDO_CLI_OUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "../term/cli_term.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Colors --- */
typedef enum {
    CLI_FG_DEFAULT = 0,
    CLI_FG_BLACK, CLI_FG_RED, CLI_FG_GREEN, CLI_FG_YELLOW,
    CLI_FG_BLUE, CLI_FG_MAGENTA, CLI_FG_CYAN, CLI_FG_WHITE,
    CLI_FG_BRIGHT_BLACK, CLI_FG_BRIGHT_RED, CLI_FG_BRIGHT_GREEN, CLI_FG_BRIGHT_YELLOW,
    CLI_FG_BRIGHT_BLUE, CLI_FG_BRIGHT_MAGENTA, CLI_FG_BRIGHT_CYAN, CLI_FG_BRIGHT_WHITE,
} CliFgColor;

typedef enum {
    CLI_BG_DEFAULT = 0,
    CLI_BG_BLACK, CLI_BG_RED, CLI_BG_GREEN, CLI_BG_YELLOW,
    CLI_BG_BLUE, CLI_BG_MAGENTA, CLI_BG_CYAN, CLI_BG_WHITE,
} CliBgColor;

/* --- Style --- */
typedef struct {
    CliFgColor  fg;
    CliBgColor  bg;
    bool        bold;
    bool        dim;
    bool        underline;
} CliStyle;

/// Sentinel style: no formatting applied.
#define CLI_STYLE_NONE ((CliStyle){0})

/* --- Output Context --- */
typedef struct CliOutCtx CliOutCtx;

/// Initialize the output context from terminal info.
/// Returns NULL on allocation failure.
CliOutCtx* cli_out_init(const CliTermInfo* term);

/// Destroy the output context and free resources.
void cli_out_destroy(CliOutCtx* ctx);

/* --- Styled Text --- */

/// Write a styled text span to the given stream (no newline appended).
/// If color_level is NONE, style is ignored and plain text is written.
void cli_out_styled(CliOutCtx* ctx, FILE* stream, CliStyle style, const char* text);

/// Write a styled line (text + newline) to the given stream.
void cli_out_line(CliOutCtx* ctx, FILE* stream, CliStyle style, const char* text);

/// Printf-style formatted output with inline style markers.
/// Markers: {bold}, {dim}, {underline}, {red}, {green}, {yellow}, {blue}, {cyan}, {reset}.
/// Example: cli_out_fmt(ctx, stdout, "{green}Success:{reset} built %d files", count);
void cli_out_fmt(CliOutCtx* ctx, FILE* stream, const char* fmt, ...);

/* --- Context Queries --- */

/// Query the color level of the output context.
CliColorLevel cli_out_get_color_level(const CliOutCtx* ctx);

/// Query whether stdout is a TTY from the output context.
bool cli_out_get_stdout_tty(const CliOutCtx* ctx);

/* --- Emoji / Icon Tokens --- */

/// Write an icon token. If unicode is supported, emits the emoji; otherwise ASCII fallback.
/// token_id identifies the icon (e.g. "check", "cross", "arrow", "warning", "info").
void cli_out_icon(CliOutCtx* ctx, FILE* stream, const char* token_id);

/* --- Dynamic Zones --- */
typedef struct CliSpinner CliSpinner;
typedef struct CliProgressBar CliProgressBar;
typedef struct CliTable CliTable;

/// Create and start a spinner with the given label.
/// Returns NULL on allocation failure.
CliSpinner* cli_out_spinner_start(CliOutCtx* ctx, const char* label, int interval_ms);

/// Update the spinner label text.
void cli_out_spinner_set_label(CliSpinner* spinner, const char* label);

/// Stop and destroy the spinner, optionally printing a final message.
void cli_out_spinner_stop(CliSpinner* spinner, const char* final_msg);

/// Create a progress bar. Returns NULL on allocation failure.
CliProgressBar* cli_out_progress_create(CliOutCtx* ctx, const char* label, int total);

/// Update the progress bar to a new completed count.
void cli_out_progress_update(CliProgressBar* bar, int completed);

/// Finish and destroy the progress bar.
void cli_out_progress_finish(CliProgressBar* bar);

/// Create a table with the given column headers.
/// `headers` is an array of `col_count` strings.
CliTable* cli_out_table_create(CliOutCtx* ctx, const char** headers, int col_count);

/// Add a row to the table. `cells` must have `col_count` entries.
void cli_out_table_add_row(CliTable* table, const char** cells);

/// Render the table to the given stream.
void cli_out_table_render(CliTable* table, FILE* stream);

/// Destroy the table and free resources.
void cli_out_table_destroy(CliTable* table);

#ifdef __cplusplus
}
#endif

#endif /* CDO_CLI_OUT_H */
