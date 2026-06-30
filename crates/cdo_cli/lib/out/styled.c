/**
 * styled.c - Styled text span emission.
 *
 * Maps CliStyle to ANSI SGR sequences. Skips styling when color level is NONE.
 * Manages the CliOutCtx lifecycle.
 */
#include "out_internal.h"
#include "../../api/cli_errors.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================= */
/* Internal: Map CliFgColor enum to ANSI SGR code number.                    */
/* ========================================================================= */

static int fg_to_sgr(CliFgColor fg) {
    switch (fg) {
        case CLI_FG_BLACK:          return 30;
        case CLI_FG_RED:            return 31;
        case CLI_FG_GREEN:          return 32;
        case CLI_FG_YELLOW:         return 33;
        case CLI_FG_BLUE:           return 34;
        case CLI_FG_MAGENTA:        return 35;
        case CLI_FG_CYAN:           return 36;
        case CLI_FG_WHITE:          return 37;
        case CLI_FG_BRIGHT_BLACK:   return 90;
        case CLI_FG_BRIGHT_RED:     return 91;
        case CLI_FG_BRIGHT_GREEN:   return 92;
        case CLI_FG_BRIGHT_YELLOW:  return 93;
        case CLI_FG_BRIGHT_BLUE:    return 94;
        case CLI_FG_BRIGHT_MAGENTA: return 95;
        case CLI_FG_BRIGHT_CYAN:    return 96;
        case CLI_FG_BRIGHT_WHITE:   return 97;
        default:                    return -1;
    }
}

/* ========================================================================= */
/* Internal: Map CliBgColor enum to ANSI SGR code number.                    */
/* ========================================================================= */

static int bg_to_sgr(CliBgColor bg) {
    switch (bg) {
        case CLI_BG_BLACK:   return 40;
        case CLI_BG_RED:     return 41;
        case CLI_BG_GREEN:   return 42;
        case CLI_BG_YELLOW:  return 43;
        case CLI_BG_BLUE:    return 44;
        case CLI_BG_MAGENTA: return 45;
        case CLI_BG_CYAN:    return 46;
        case CLI_BG_WHITE:   return 47;
        default:             return -1;
    }
}

/* ========================================================================= */
/* Internal: Emit ANSI SGR opening sequence for a given style.               */
/* ========================================================================= */

static void emit_sgr_open(FILE* stream, CliStyle style) {
    /* Collect codes to combine into a single escape sequence */
    int codes[8];
    int count = 0;

    if (style.bold)      codes[count++] = 1;
    if (style.dim)       codes[count++] = 2;
    if (style.underline) codes[count++] = 4;

    int fg_code = fg_to_sgr(style.fg);
    if (fg_code >= 0) codes[count++] = fg_code;

    int bg_code = bg_to_sgr(style.bg);
    if (bg_code >= 0) codes[count++] = bg_code;

    if (count == 0) return;

    fputs("\033[", stream);
    for (int i = 0; i < count; i++) {
        if (i > 0) fputc(';', stream);
        fprintf(stream, "%d", codes[i]);
    }
    fputc('m', stream);
}

/* ========================================================================= */
/* Internal: Check if a style has any formatting attributes set.             */
/* ========================================================================= */

static bool style_has_attrs(CliStyle style) {
    return style.bold || style.dim || style.underline || style.fg != CLI_FG_DEFAULT || style.bg != CLI_BG_DEFAULT;
}

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

CliOutCtx* cli_out_init(const CliTermInfo* term) {
    if (!term) return NULL;

    CliOutCtx* ctx = (CliOutCtx*)malloc(sizeof(CliOutCtx));
    if (!ctx) return NULL;

    ctx->color_level = term->color_level;
    ctx->unicode     = term->unicode;
    ctx->stdout_tty  = term->stdout_tty;
    ctx->columns     = term->columns;
    return ctx;
}

void cli_out_destroy(CliOutCtx* ctx) {
    if (ctx) {
        free(ctx);
    }
}

void cli_out_styled(CliOutCtx* ctx, FILE* stream, CliStyle style, const char* text) {
    if (!ctx || !stream || !text) return;

    if (ctx->color_level != CLI_COLOR_NONE && style_has_attrs(style)) {
        emit_sgr_open(stream, style);
        fputs(text, stream);
        fputs("\033[0m", stream);
    } else {
        fputs(text, stream);
    }
}

void cli_out_line(CliOutCtx* ctx, FILE* stream, CliStyle style, const char* text) {
    if (!ctx || !stream || !text) return;

    cli_out_styled(ctx, stream, style, text);
    fputc('\n', stream);
}

CliColorLevel cli_out_get_color_level(const CliOutCtx* ctx) {
    if (!ctx) return CLI_COLOR_NONE;
    return ctx->color_level;
}

bool cli_out_get_stdout_tty(const CliOutCtx* ctx) {
    if (!ctx) return false;
    return ctx->stdout_tty;
}
