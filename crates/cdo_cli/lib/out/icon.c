/**
 * icon.c - Emoji/icon token resolution and fallback.
 *
 * Resolves icon token IDs (e.g. "check", "cross", "arrow") to
 * Unicode emoji or ASCII fallback based on terminal capabilities.
 */
#include "out_internal.h"
#include "../../api/cli_errors.h"

#include <string.h>
#include <stdio.h>

/* ========================================================================= */
/* Icon lookup table: token -> unicode glyph -> ascii fallback.              */
/* ========================================================================= */

static const struct {
    const char* token;
    const char* unicode;
    const char* ascii;
} icon_table[] = {
    { "check",   "\xe2\x9c\x93", "v" },    /* U+2713 CHECK MARK */
    { "cross",   "\xe2\x9c\x97", "x" },    /* U+2717 BALLOT X */
    { "arrow",   "\xe2\x86\x92", "->" },   /* U+2192 RIGHTWARDS ARROW */
    { "warning", "\xe2\x9a\xa0", "!" },    /* U+26A0 WARNING SIGN */
    { "info",    "\xe2\x84\xb9", "i" },    /* U+2139 INFORMATION SOURCE */
    { NULL, NULL, NULL }
};

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

void cli_out_icon(CliOutCtx* ctx, FILE* stream, const char* token_id) {
    if (!ctx || !stream || !token_id) return;

    /* Search the icon table for a matching token */
    for (int i = 0; icon_table[i].token != NULL; i++) {
        if (strcmp(icon_table[i].token, token_id) == 0) {
            if (ctx->unicode) {
                fputs(icon_table[i].unicode, stream);
            } else {
                fputs(icon_table[i].ascii, stream);
            }
            return;
        }
    }

    /* Unknown token: emit as-is */
    fputs(token_id, stream);
}
