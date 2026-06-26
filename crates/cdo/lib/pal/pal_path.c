#include "pal/pal.h"
#include <string.h>

void pal_path_normalize(char* path) {
    if (!path) return;

    // Convert all backslashes to forward slashes
    for (char* p = path; *p; ++p) {
        if (*p == '\\') {
            *p = '/';
        }
    }

    // Collapse multiple consecutive slashes into one
    char* dst = path;
    char* src = path;
    bool prev_slash = false;

    while (*src) {
        if (*src == '/') {
            if (!prev_slash) {
                *dst++ = '/';
                prev_slash = true;
            }
            // else: skip consecutive slash
        } else {
            *dst++ = *src;
            prev_slash = false;
        }
        src++;
    }
    *dst = '\0';
}

int pal_path_join(char* dest, size_t dest_size, const char* base, const char* rel) {
    if (!dest || dest_size == 0) return -1;
    if (!base) base = "";
    if (!rel) rel = "";

    size_t base_len = strlen(base);
    size_t rel_len = strlen(rel);

    // Strip trailing slashes from base
    while (base_len > 0 && (base[base_len - 1] == '/' || base[base_len - 1] == '\\')) {
        base_len--;
    }

    // Skip leading slashes from rel
    while (*rel == '/' || *rel == '\\') {
        rel++;
        rel_len--;
    }

    // Calculate required size
    // base + '/' + rel + '\0'
    bool need_sep = (base_len > 0 && rel_len > 0);
    size_t total = base_len + (need_sep ? 1 : 0) + rel_len + 1;

    if (total > dest_size) return -1;

    // Copy base (without trailing slashes)
    memcpy(dest, base, base_len);
    size_t pos = base_len;

    // Add separator if needed
    if (need_sep) {
        dest[pos++] = '/';
    }

    // Copy rel (without leading slashes)
    memcpy(dest + pos, rel, rel_len);
    pos += rel_len;

    dest[pos] = '\0';
    return 0;
}

const char* pal_path_ext(const char* path) {
    static const char empty[] = "";
    if (!path) return empty;

    // Find the last path separator
    const char* last_sep = NULL;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            last_sep = p;
        }
    }

    // Search for the last dot after the last separator
    const char* filename = last_sep ? (last_sep + 1) : path;
    const char* last_dot = NULL;
    for (const char* p = filename; *p; ++p) {
        if (*p == '.') {
            last_dot = p;
        }
    }

    // No dot found, or dot is the first character of filename (hidden files like .gitignore)
    if (!last_dot || last_dot == filename) {
        return empty;
    }

    return last_dot;
}
