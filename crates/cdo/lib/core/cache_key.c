#include "core/cache.h"
#include "core/log.h"
#include "commons/checksum.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// =============================================================================
// Dep File Parsing
// =============================================================================

#define DEPFILE_INITIAL_CAP 32
#define DEPFILE_MAX_LINE 4096

/// MSVC /showIncludes prefix (English locale).
static const char MSVC_PREFIX[] = "Note: including file:";
static const size_t MSVC_PREFIX_LEN = sizeof(MSVC_PREFIX) - 1;

// -----------------------------------------------------------------------------
// Internal: dynamic string array
// -----------------------------------------------------------------------------

typedef struct {
    char** items;
    int    count;
    int    cap;
} StrArray;

static int strarray_init(StrArray* arr) {
    arr->items = (char**)malloc(DEPFILE_INITIAL_CAP * sizeof(char*));
    if (!arr->items) return -1;
    arr->count = 0;
    arr->cap = DEPFILE_INITIAL_CAP;
    return 0;
}

static int strarray_push(StrArray* arr, const char* str, size_t len) {
    if (arr->count >= arr->cap) {
        int new_cap = arr->cap * 2;
        char** new_items = (char**)realloc(arr->items, (size_t)new_cap * sizeof(char*));
        if (!new_items) return -1;
        arr->items = new_items;
        arr->cap = new_cap;
    }
    char* copy = (char*)malloc(len + 1);
    if (!copy) return -1;
    memcpy(copy, str, len);
    copy[len] = '\0';
    arr->items[arr->count++] = copy;
    return 0;
}

static void strarray_free(StrArray* arr) {
    for (int i = 0; i < arr->count; i++) {
        free(arr->items[i]);
    }
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->cap = 0;
}

// -----------------------------------------------------------------------------
// GCC/Clang .d file parser
// -----------------------------------------------------------------------------

/// Parse a GCC/Clang Makefile-format dependency file.
/// Format: "target.o: dep1.c dep2.h dep3.h ..."
/// Lines can be continued with backslash-newline.
/// Paths with spaces are escaped: "path\ with\ spaces/file.h"
/// Returns all dependencies after the colon (including the source file).
static int parse_gcc_depfile(const char* content, size_t content_len, StrArray* out) {
    const char* p = content;
    const char* end = content + content_len;

    // Skip to past the target separator colon (target.o: deps...)
    // On Windows, paths may start with a drive letter like "C:/..." or "C:\..."
    // so we need to skip drive-letter colons (single alpha + colon + path separator).
    bool found_colon = false;
    while (p < end) {
        if (*p == ':') {
            // Check if this is a drive letter colon (e.g., "C:/")
            // Pattern: single alpha char at start-of-token, followed by colon, followed by / or backslash
            bool is_drive_letter = false;
            if (p + 1 < end && (*(p + 1) == '/' || *(p + 1) == '\\')) {
                if (p > content && ((*(p - 1) >= 'A' && *(p - 1) <= 'Z') || (*(p - 1) >= 'a' && *(p - 1) <= 'z'))) {
                    // Verify the alpha char is at start of content or after whitespace (start of a token)
                    if (p - 1 == content || *(p - 2) == ' ' || *(p - 2) == '\t' || *(p - 2) == '\n' || *(p - 2) == '\r') {
                        is_drive_letter = true;
                    }
                }
            }
            if (!is_drive_letter) {
                p++; // skip the colon
                found_colon = true;
                break;
            }
        }
        p++;
    }

    if (!found_colon) {
        cdo_log_debug("depfile_parse: no colon found in GCC .d file");
        return -1;
    }

    // Now parse tokens (dependencies) separated by whitespace.
    // Handle backslash-newline (line continuation) and backslash-space (escaped space in path).
    char token_buf[DEPFILE_MAX_LINE];
    int token_len = 0;

    while (p < end) {
        // Skip whitespace (but not escaped spaces)
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            // Emit current token if non-empty
            if (token_len > 0) {
                if (strarray_push(out, token_buf, (size_t)token_len) != 0) return -1;
                token_len = 0;
            }
            p++;
            continue;
        }

        if (*p == '\\') {
            // Check what follows the backslash
            if (p + 1 < end) {
                char next = *(p + 1);
                if (next == '\n') {
                    // Line continuation: skip backslash and newline
                    p += 2;
                    // Emit token if non-empty (continuation acts as separator)
                    if (token_len > 0) {
                        if (strarray_push(out, token_buf, (size_t)token_len) != 0) return -1;
                        token_len = 0;
                    }
                    continue;
                } else if (next == '\r' && p + 2 < end && *(p + 2) == '\n') {
                    // Line continuation with CRLF: skip backslash, CR, LF
                    p += 3;
                    if (token_len > 0) {
                        if (strarray_push(out, token_buf, (size_t)token_len) != 0) return -1;
                        token_len = 0;
                    }
                    continue;
                } else if (next == ' ') {
                    // Escaped space: part of the path
                    if (token_len < DEPFILE_MAX_LINE - 1) {
                        token_buf[token_len++] = ' ';
                    }
                    p += 2;
                    continue;
                }
            }
            // Just a regular backslash character in path (e.g., Windows paths)
            if (token_len < DEPFILE_MAX_LINE - 1) {
                token_buf[token_len++] = *p;
            }
            p++;
            continue;
        }

        // Regular character
        if (token_len < DEPFILE_MAX_LINE - 1) {
            token_buf[token_len++] = *p;
        }
        p++;
    }

    // Emit final token if any
    if (token_len > 0) {
        if (strarray_push(out, token_buf, (size_t)token_len) != 0) return -1;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// MSVC /showIncludes parser
// -----------------------------------------------------------------------------

/// Parse MSVC /showIncludes output.
/// Each relevant line starts with "Note: including file:" followed by whitespace and a path.
/// Nesting depth (leading whitespace after prefix) is ignored; we just extract the path.
static int parse_msvc_depfile(const char* content, size_t content_len, StrArray* out) {
    const char* p = content;
    const char* end = content + content_len;

    while (p < end) {
        // Find start of current line
        const char* line_start = p;

        // Find end of current line
        const char* line_end = p;
        while (line_end < end && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }

        size_t line_len = (size_t)(line_end - line_start);

        // Check if line starts with the MSVC prefix
        if (line_len > MSVC_PREFIX_LEN && strncmp(line_start, MSVC_PREFIX, MSVC_PREFIX_LEN) == 0) {
            // Skip prefix and leading whitespace
            const char* path_start = line_start + MSVC_PREFIX_LEN;
            while (path_start < line_end && (*path_start == ' ' || *path_start == '\t')) {
                path_start++;
            }

            // Trim trailing whitespace
            const char* path_end = line_end;
            while (path_end > path_start && (*(path_end - 1) == ' ' || *(path_end - 1) == '\t' || *(path_end - 1) == '\r')) {
                path_end--;
            }

            size_t path_len = (size_t)(path_end - path_start);
            if (path_len > 0) {
                if (strarray_push(out, path_start, path_len) != 0) return -1;
            }
        }

        // Advance past line ending
        p = line_end;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// Auto-detect format and parse
// -----------------------------------------------------------------------------

/// Detect whether the content is MSVC format (first non-empty line starts with "Note: including file:").
static bool is_msvc_format(const char* content, size_t content_len) {
    const char* p = content;
    const char* end = content + content_len;

    // Skip leading whitespace/empty lines
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }

    size_t remaining = (size_t)(end - p);
    if (remaining >= MSVC_PREFIX_LEN && strncmp(p, MSVC_PREFIX, MSVC_PREFIX_LEN) == 0) {
        return true;
    }
    return false;
}

// =============================================================================
// Internal: qsort comparator for C strings
// =============================================================================

static int cmp_str(const void* a, const void* b) {
    const char* sa = *(const char* const*)a;
    const char* sb = *(const char* const*)b;
    return strcmp(sa, sb);
}

// =============================================================================
// Internal: dynamic buffer for building canonical string
// =============================================================================

typedef struct {
    char*   data;
    size_t  len;
    size_t  cap;
} DynBuf;

static int dynbuf_init(DynBuf* buf, size_t initial_cap) {
    buf->data = (char*)malloc(initial_cap);
    if (!buf->data) return -1;
    buf->len = 0;
    buf->cap = initial_cap;
    return 0;
}

static int dynbuf_append(DynBuf* buf, const char* str, size_t str_len) {
    if (buf->len + str_len > buf->cap) {
        size_t new_cap = (buf->len + str_len) * 2;
        char* new_data = (char*)realloc(buf->data, new_cap);
        if (!new_data) return -1;
        buf->data = new_data;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, str, str_len);
    buf->len += str_len;
    return 0;
}

static int dynbuf_append_str(DynBuf* buf, const char* str) {
    return dynbuf_append(buf, str, strlen(str));
}

static void dynbuf_free(DynBuf* buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

// =============================================================================
// Internal: compute SHA256 hex of file content
// =============================================================================

/// Read a file and compute its SHA256 hex digest.
/// out_hex must be at least 65 bytes. Returns 0 on success.
static int sha256_file(const char* path, char* out_hex) {
    char* content = NULL;
    size_t content_len = 0;
    if (pal_file_read(path, &content, &content_len) != 0) {
        return -1;
    }
    int rc = checksum_compute(content, content_len, CHECKSUM_SHA256, out_hex);
    free(content);
    return rc;
}

// =============================================================================
// Public API: cache_compute_key
// =============================================================================

int cache_compute_key(const CacheKeyInputs* inputs, char* out_key) {
    if (!inputs || !out_key) return -1;

    // Requirement 1.2: If dep file is absent, treat as cache miss
    if (!inputs->dep_file_path) {
        cdo_log_debug("cache_compute_key: no dep file path, treating as miss");
        return -1;
    }

    // Hash source file content
    char source_hash[129] = {0};
    if (sha256_file(inputs->source_path, source_hash) != 0) {
        cdo_log_debug("cache_compute_key: failed to hash source file '%s'", inputs->source_path);
        return -1;
    }

    // Parse dep file for headers
    char** headers = NULL;
    int header_count = 0;
    if (depfile_parse(inputs->dep_file_path, &headers, &header_count) != 0) {
        cdo_log_debug("cache_compute_key: failed to parse dep file '%s'", inputs->dep_file_path);
        return -1;
    }

    // Hash each header file. If any header doesn't exist, force miss.
    // Build "path:hash" pairs for the canonical string.
    char** header_pairs = NULL;
    if (header_count > 0) {
        header_pairs = (char**)malloc((size_t)header_count * sizeof(char*));
        if (!header_pairs) {
            for (int i = 0; i < header_count; i++) free(headers[i]);
            free(headers);
            return -1;
        }
    }

    int valid_header_count = 0;
    for (int i = 0; i < header_count; i++) {
        // Check if header exists
        if (pal_path_exists(headers[i]) != 0) {
            cdo_log_debug("cache_compute_key: header '%s' no longer exists, forcing miss", headers[i]);
            // Clean up
            for (int j = 0; j < valid_header_count; j++) free(header_pairs[j]);
            free(header_pairs);
            for (int j = 0; j < header_count; j++) free(headers[j]);
            free(headers);
            return -1;
        }

        char hdr_hash[129] = {0};
        if (sha256_file(headers[i], hdr_hash) != 0) {
            cdo_log_debug("cache_compute_key: failed to hash header '%s'", headers[i]);
            for (int j = 0; j < valid_header_count; j++) free(header_pairs[j]);
            free(header_pairs);
            for (int j = 0; j < header_count; j++) free(headers[j]);
            free(headers);
            return -1;
        }

        // Build "path:hash" pair string
        size_t pair_len = strlen(headers[i]) + 1 + CACHE_KEY_HEX_LEN + 1;
        header_pairs[valid_header_count] = (char*)malloc(pair_len);
        if (!header_pairs[valid_header_count]) {
            for (int j = 0; j < valid_header_count; j++) free(header_pairs[j]);
            free(header_pairs);
            for (int j = 0; j < header_count; j++) free(headers[j]);
            free(headers);
            return -1;
        }
        snprintf(header_pairs[valid_header_count], pair_len, "%s:%s", headers[i], hdr_hash);
        valid_header_count++;
    }

    // Free raw headers (we have the pairs now)
    for (int i = 0; i < header_count; i++) free(headers[i]);
    free(headers);

    // Sort header pairs for determinism
    if (valid_header_count > 1) {
        qsort(header_pairs, (size_t)valid_header_count, sizeof(char*), cmp_str);
    }

    // Sort defines and include paths (work on copies to not modify caller's arrays)
    const char** sorted_defines = NULL;
    if (inputs->define_count > 0) {
        sorted_defines = (const char**)malloc((size_t)inputs->define_count * sizeof(const char*));
        if (!sorted_defines) {
            for (int i = 0; i < valid_header_count; i++) free(header_pairs[i]);
            free(header_pairs);
            return -1;
        }
        memcpy(sorted_defines, inputs->defines, (size_t)inputs->define_count * sizeof(const char*));
        qsort(sorted_defines, (size_t)inputs->define_count, sizeof(const char*), cmp_str);
    }

    const char** sorted_includes = NULL;
    if (inputs->include_path_count > 0) {
        sorted_includes = (const char**)malloc((size_t)inputs->include_path_count * sizeof(const char*));
        if (!sorted_includes) {
            free(sorted_defines);
            for (int i = 0; i < valid_header_count; i++) free(header_pairs[i]);
            free(header_pairs);
            return -1;
        }
        memcpy(sorted_includes, inputs->include_paths, (size_t)inputs->include_path_count * sizeof(const char*));
        qsort(sorted_includes, (size_t)inputs->include_path_count, sizeof(const char*), cmp_str);
    }

    // Build canonical string
    DynBuf canonical;
    if (dynbuf_init(&canonical, 4096) != 0) {
        free(sorted_defines);
        free(sorted_includes);
        for (int i = 0; i < valid_header_count; i++) free(header_pairs[i]);
        free(header_pairs);
        return -1;
    }

    // Format version line
    dynbuf_append_str(&canonical, CACHE_FORMAT_VERSION);
    dynbuf_append_str(&canonical, "\n");

    // compiler:<path>:<version>
    dynbuf_append_str(&canonical, "compiler:");
    dynbuf_append_str(&canonical, inputs->compiler_path ? inputs->compiler_path : "");
    dynbuf_append_str(&canonical, ":");
    dynbuf_append_str(&canonical, inputs->compiler_version ? inputs->compiler_version : "");
    dynbuf_append_str(&canonical, "\n");

    // standard:<c17|c++20>
    dynbuf_append_str(&canonical, "standard:");
    dynbuf_append_str(&canonical, inputs->language_standard ? inputs->language_standard : "");
    dynbuf_append_str(&canonical, "\n");

    // optimize:<true|false>
    dynbuf_append_str(&canonical, "optimize:");
    dynbuf_append_str(&canonical, inputs->optimize ? "true" : "false");
    dynbuf_append_str(&canonical, "\n");

    // debug:<true|false>
    dynbuf_append_str(&canonical, "debug:");
    dynbuf_append_str(&canonical, inputs->debug_info ? "true" : "false");
    dynbuf_append_str(&canonical, "\n");

    // defines:<sorted, semicolon-separated>
    dynbuf_append_str(&canonical, "defines:");
    for (int i = 0; i < inputs->define_count; i++) {
        if (i > 0) dynbuf_append_str(&canonical, ";");
        dynbuf_append_str(&canonical, sorted_defines[i]);
    }
    dynbuf_append_str(&canonical, "\n");

    // includes:<sorted, semicolon-separated>
    dynbuf_append_str(&canonical, "includes:");
    for (int i = 0; i < inputs->include_path_count; i++) {
        if (i > 0) dynbuf_append_str(&canonical, ";");
        dynbuf_append_str(&canonical, sorted_includes[i]);
    }
    dynbuf_append_str(&canonical, "\n");

    // source:<sha256 of source content>
    dynbuf_append_str(&canonical, "source:");
    dynbuf_append_str(&canonical, source_hash);
    dynbuf_append_str(&canonical, "\n");

    // headers:<sorted header_path:sha256 pairs, semicolon-separated>
    dynbuf_append_str(&canonical, "headers:");
    for (int i = 0; i < valid_header_count; i++) {
        if (i > 0) dynbuf_append_str(&canonical, ";");
        dynbuf_append_str(&canonical, header_pairs[i]);
    }
    dynbuf_append_str(&canonical, "\n");

    // Clean up sorted arrays and header pairs
    free(sorted_defines);
    free(sorted_includes);
    for (int i = 0; i < valid_header_count; i++) free(header_pairs[i]);
    free(header_pairs);

    // SHA256 hash the canonical string to produce the final key
    char final_hash[129] = {0};
    int rc = checksum_compute(canonical.data, canonical.len, CHECKSUM_SHA256, final_hash);
    dynbuf_free(&canonical);

    if (rc != 0) {
        cdo_log_debug("cache_compute_key: failed to hash canonical string");
        return -1;
    }

    // Write the 64-char hex key to output (prefixed with format version for future compatibility)
    memcpy(out_key, final_hash, CACHE_KEY_HEX_LEN);
    out_key[CACHE_KEY_HEX_LEN] = '\0';

    return 0;
}

// =============================================================================
// Public API: depfile_parse
// =============================================================================

int depfile_parse(const char* dep_path, char*** headers, int* count) {
    if (!dep_path || !headers || !count) return -1;

    *headers = NULL;
    *count = 0;

    // Read the dep file content
    char* content = NULL;
    size_t content_len = 0;
    int rc = pal_file_read(dep_path, &content, &content_len);
    if (rc != 0) {
        cdo_log_debug("depfile_parse: failed to read '%s'", dep_path);
        return -1;
    }

    if (content_len == 0) {
        free(content);
        return 0; // Empty file, no deps
    }

    StrArray arr;
    if (strarray_init(&arr) != 0) {
        free(content);
        return -1;
    }

    // Auto-detect format and parse
    if (is_msvc_format(content, content_len)) {
        rc = parse_msvc_depfile(content, content_len, &arr);
    } else {
        rc = parse_gcc_depfile(content, content_len, &arr);
    }

    free(content);

    if (rc != 0) {
        strarray_free(&arr);
        return -1;
    }

    // Transfer ownership to caller
    *headers = arr.items;
    *count = arr.count;
    return 0;
}
