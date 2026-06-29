/**
 * fmt.c - Printf-style formatted output with inline style markers.
 *
 * Recognizes markers: {bold}, {dim}, {underline}, {red}, {green},
 * {yellow}, {blue}, {cyan}, {magenta}, {white}, {reset}.
 * Strips markers when color is NONE. Printf format specifiers (%) are
 * passed through to vfprintf for argument substitution.
 */
#include "out_internal.h"
#include "../../api/cli_errors.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* ========================================================================= */
/* Internal: Marker-to-ANSI mapping table.                                   */
/* ========================================================================= */

static const struct {
    const char* marker;
    const char* ansi;
} marker_table[] = {
    { "bold",      "\033[1m" },
    { "dim",       "\033[2m" },
    { "underline", "\033[4m" },
    { "red",       "\033[31m" },
    { "green",     "\033[32m" },
    { "yellow",    "\033[33m" },
    { "blue",      "\033[34m" },
    { "magenta",   "\033[35m" },
    { "cyan",      "\033[36m" },
    { "white",     "\033[37m" },
    { "reset",     "\033[0m" },
    { NULL, NULL }
};

/* ========================================================================= */
/* Internal: Try to match a marker at position. Returns marker length         */
/* (including braces) if matched, 0 otherwise. Sets ansi_out to replacement. */
/* ========================================================================= */

static int try_match_marker(const char* p, const char** ansi_out, bool color_enabled) {
    if (*p != '{') return 0;

    const char* closing = strchr(p + 1, '}');
    if (!closing) return 0;

    int name_len = (int)(closing - (p + 1));
    if (name_len <= 0 || name_len > 16) return 0;

    for (int i = 0; marker_table[i].marker != NULL; i++) {
        int mlen = (int)strlen(marker_table[i].marker);
        if (mlen == name_len && strncmp(p + 1, marker_table[i].marker, (size_t)mlen) == 0) {
            *ansi_out = color_enabled ? marker_table[i].ansi : "";
            return name_len + 2; /* { + name + } */
        }
    }

    return 0;
}

/* ========================================================================= */
/* Internal: Build a new format string with markers replaced by ANSI codes   */
/* (or empty strings if color disabled). The caller must free the result.    */
/* ========================================================================= */

static char* expand_markers(const char* fmt, bool color_enabled) {
    /* Estimate output size: worst case each marker becomes ~6 bytes ANSI */
    size_t fmt_len = strlen(fmt);
    size_t buf_cap = fmt_len * 2 + 128;
    char* buf = (char*)malloc(buf_cap);
    if (!buf) return NULL;

    size_t out_pos = 0;
    const char* p = fmt;

    while (*p) {
        const char* ansi = NULL;
        int marker_len = try_match_marker(p, &ansi, color_enabled);

        if (marker_len > 0) {
            /* Insert the ANSI code (or empty string) */
            size_t ansi_len = strlen(ansi);
            /* Grow buffer if needed */
            if (out_pos + ansi_len + 1 >= buf_cap) {
                buf_cap = buf_cap * 2 + ansi_len;
                char* new_buf = (char*)realloc(buf, buf_cap);
                if (!new_buf) { free(buf); return NULL; }
                buf = new_buf;
            }
            memcpy(buf + out_pos, ansi, ansi_len);
            out_pos += ansi_len;
            p += marker_len;
        } else {
            /* Copy character as-is */
            if (out_pos + 2 >= buf_cap) {
                buf_cap *= 2;
                char* new_buf = (char*)realloc(buf, buf_cap);
                if (!new_buf) { free(buf); return NULL; }
                buf = new_buf;
            }
            buf[out_pos++] = *p++;
        }
    }

    buf[out_pos] = '\0';
    return buf;
}

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

void cli_out_fmt(CliOutCtx* ctx, FILE* stream, const char* fmt, ...) {
    if (!ctx || !stream || !fmt) return;

    bool color_enabled = (ctx->color_level != CLI_COLOR_NONE);

    /* Build expanded format string with markers replaced */
    char* expanded = expand_markers(fmt, color_enabled);
    if (!expanded) {
        /* Fallback: just print the raw format string without marker expansion */
        va_list args;
        va_start(args, fmt);
        vfprintf(stream, fmt, args);
        va_end(args);
        return;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stream, expanded, args);
    va_end(args);

    free(expanded);
}
