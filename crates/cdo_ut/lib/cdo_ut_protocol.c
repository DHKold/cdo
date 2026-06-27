/**
 * cdo_ut_protocol.c — Structured protocol emission (JSON Lines to stdout).
 *
 * Each function emits exactly one JSON object per line followed by a newline,
 * then flushes stdout to ensure immediate output for piped reading.
 *
 * All string values are ASCII-safe: double quotes, backslashes, and control
 * characters are escaped. Non-ASCII bytes are escaped as \uXXXX sequences.
 */

#include "cdo_ut_protocol.h"

#include <stdio.h>
#include <string.h>

// =============================================================================
// Internal: JSON string escaping
// =============================================================================

/**
 * Write a JSON-escaped string value (without surrounding quotes) to stdout.
 * Escapes: double quote, backslash, control characters (0x00-0x1F),
 * and non-ASCII bytes (0x80-0xFF) as \uXXXX.
 */
static void emit_json_string(const char *s)
{
    if (s == NULL) {
        return;
    }

    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;

        if (c == '"') {
            fputs("\\\"", stdout);
        } else if (c == '\\') {
            fputs("\\\\", stdout);
        } else if (c == '\n') {
            fputs("\\n", stdout);
        } else if (c == '\r') {
            fputs("\\r", stdout);
        } else if (c == '\t') {
            fputs("\\t", stdout);
        } else if (c < 0x20) {
            /* Other control characters */
            fprintf(stdout, "\\u%04x", c);
        } else if (c >= 0x80) {
            /* Non-ASCII byte — escape as \uXXXX */
            fprintf(stdout, "\\u%04x", c);
        } else {
            fputc(c, stdout);
        }
    }
}

// =============================================================================
// cdo_ut_emit_suite_start
// =============================================================================

void cdo_ut_emit_suite_start(int total)
{
    fprintf(stdout, "{\"type\":\"suite_start\",\"total\":%d}\n", total);
    fflush(stdout);
}

// =============================================================================
// cdo_ut_emit_test_start
// =============================================================================

void cdo_ut_emit_test_start(const char *name, int id)
{
    fputs("{\"type\":\"test_start\",\"name\":\"", stdout);
    emit_json_string(name);
    fprintf(stdout, "\",\"id\":%d}\n", id);
    fflush(stdout);
}

// =============================================================================
// cdo_ut_emit_test_end
// =============================================================================

void cdo_ut_emit_test_end(const char *name, int id, const char *status,
                          double duration_ms,
                          const char *file, int line, const char *expr,
                          const char *actual, const char *expected)
{
    fputs("{\"type\":\"test_end\",\"name\":\"", stdout);
    emit_json_string(name);
    fprintf(stdout, "\",\"id\":%d,\"status\":\"", id);
    emit_json_string(status);
    fprintf(stdout, "\",\"duration_ms\":%.2f", duration_ms);

    /* Append failure details if status is "fail" and file is provided */
    if (file != NULL) {
        fputs(",\"failure\":{\"file\":\"", stdout);
        emit_json_string(file);
        fprintf(stdout, "\",\"line\":%d,\"expr\":\"", line);
        emit_json_string(expr);
        fputs("\",\"actual\":\"", stdout);
        emit_json_string(actual);
        fputs("\",\"expected\":\"", stdout);
        emit_json_string(expected);
        fputs("\"}", stdout);
    }

    fputs("}\n", stdout);
    fflush(stdout);
}

// =============================================================================
// cdo_ut_emit_suite_end
// =============================================================================

void cdo_ut_emit_suite_end(int total, int passed, int failed, int skipped,
                           double duration_ms)
{
    fprintf(stdout,
        "{\"type\":\"suite_end\",\"total\":%d,\"passed\":%d,"
        "\"failed\":%d,\"skipped\":%d,\"duration_ms\":%.2f}\n",
        total, passed, failed, skipped, duration_ms);
    fflush(stdout);
}

// =============================================================================
// cdo_ut_emit_error
// =============================================================================

void cdo_ut_emit_error(const char *message)
{
    fputs("{\"type\":\"error\",\"message\":\"", stdout);
    emit_json_string(message);
    fputs("\"}\n", stdout);
    fflush(stdout);
}
