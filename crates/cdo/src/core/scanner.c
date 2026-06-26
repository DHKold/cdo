#include "scanner.h"
#include "pal/pal.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// --- FileList helpers ---

#define FILELIST_INITIAL_CAPACITY 16

static int filelist_init(FileList* fl) {
    fl->paths = (char**)malloc(FILELIST_INITIAL_CAPACITY * sizeof(char*));
    if (!fl->paths) return 1;
    fl->count = 0;
    fl->capacity = FILELIST_INITIAL_CAPACITY;
    return 0;
}

static int filelist_add(FileList* fl, const char* path) {
    if (fl->count >= fl->capacity) {
        int new_cap = fl->capacity * 2;
        char** new_paths = (char**)realloc(fl->paths, (size_t)new_cap * sizeof(char*));
        if (!new_paths) return 1;
        fl->paths = new_paths;
        fl->capacity = new_cap;
    }
    size_t len = strlen(path);
    char* copy = (char*)malloc(len + 1);
    if (!copy) return 1;
    memcpy(copy, path, len + 1);
    fl->paths[fl->count++] = copy;
    return 0;
}

void filelist_free(FileList* fl) {
    if (!fl) return;
    if (fl->paths) {
        for (int i = 0; i < fl->count; i++) {
            free(fl->paths[i]);
        }
        free(fl->paths);
    }
    fl->paths = NULL;
    fl->count = 0;
    fl->capacity = 0;
}

// --- Glob pattern matching ---

/**
 * Match a path against a glob pattern.
 * Supports:
 *   *  - matches any sequence of characters except '/'
 *   ** - matches any sequence of characters including '/'
 *   ?  - matches exactly one character (not '/')
 *
 * Both pattern and path should use '/' as separator.
 */
static bool glob_match(const char* pattern, const char* path) {
    const char* p = pattern;
    const char* s = path;
    const char* star_p = NULL;
    const char* star_s = NULL;

    while (*s) {
        if (*p == '*') {
            // Check for **
            if (*(p + 1) == '*') {
                // ** matches any characters including '/'
                p += 2;
                // Skip any trailing '/' after **
                if (*p == '/') p++;
                // If pattern is exhausted, everything matches
                if (*p == '\0') return true;
                // Try matching the rest of the pattern at every position
                for (const char* try_s = s; *try_s; try_s++) {
                    if (glob_match(p, try_s)) return true;
                }
                // Also try matching at the end
                return glob_match(p, s + strlen(s));
            } else {
                // * matches any chars except '/'
                star_p = p;
                star_s = s;
                p++;
            }
        } else if (*p == '?' && *s != '/') {
            p++;
            s++;
        } else if (*p == *s) {
            p++;
            s++;
        } else if (star_p) {
            // Backtrack: advance star_s by one (but not past '/')
            star_s++;
            if (*star_s == '/' || *(star_s - 1) == '/') {
                // '*' cannot match across '/'
                // Reset to after the '*' and check if we hit '/'
                if (*(star_s - 1) == '/') {
                    // We consumed a '/', '*' can't do that
                    return false;
                }
            }
            if (*(star_s - 1) == '\0') return false;
            s = star_s;
            p = star_p + 1;
        } else {
            return false;
        }
    }

    // Consume trailing stars in pattern
    while (*p == '*') p++;
    return *p == '\0';
}

/**
 * Check if a relative path matches any of the exclude patterns.
 */
static bool matches_any_exclude(const char* rel_path,
                                const char** exclude_patterns,
                                int exclude_count) {
    for (int i = 0; i < exclude_count; i++) {
        if (glob_match(exclude_patterns[i], rel_path)) {
            return true;
        }
    }
    return false;
}

// --- Extension checking ---

static bool is_source_ext(const char* ext) {
    return (strcmp(ext, ".c") == 0 ||
            strcmp(ext, ".cpp") == 0 ||
            strcmp(ext, ".h") == 0 ||
            strcmp(ext, ".hpp") == 0);
}

static bool is_header_ext(const char* ext) {
    return (strcmp(ext, ".h") == 0 ||
            strcmp(ext, ".hpp") == 0);
}

// --- Walk context ---

typedef struct {
    FileList*       out;
    const char*     base_path;      // The scanned directory (e.g., <crate>/src/)
    size_t          base_len;       // Length of base_path for computing relative paths
    const char*     crate_path;     // The crate root path
    size_t          crate_len;      // Length of crate_path
    const char**    exclude_patterns;
    int             exclude_count;
    bool            check_sources;  // true = source extensions, false = header extensions
    int             error;          // non-zero if an allocation failed
} ScanContext;

static void scan_callback(const char* entry_path, bool is_dir, void* ctx) {
    ScanContext* sc = (ScanContext*)ctx;
    if (sc->error) return;
    if (is_dir) return;

    // Normalize the entry path for consistent matching
    size_t entry_len = strlen(entry_path);
    char* normalized = (char*)malloc(entry_len + 1);
    if (!normalized) { sc->error = 1; return; }
    memcpy(normalized, entry_path, entry_len + 1);
    pal_path_normalize(normalized);

    // Check extension
    const char* ext = pal_path_ext(normalized);
    bool ext_match = sc->check_sources ? is_source_ext(ext) : is_header_ext(ext);
    if (!ext_match) {
        free(normalized);
        return;
    }

    // Compute relative path from crate root for exclude pattern matching
    if (sc->exclude_count > 0 && sc->crate_len > 0) {
        const char* rel = normalized;
        // Skip past crate_path prefix
        if (strlen(normalized) > sc->crate_len &&
            (normalized[sc->crate_len] == '/' || normalized[sc->crate_len] == '\\')) {
            rel = normalized + sc->crate_len + 1;
        }
        if (matches_any_exclude(rel, sc->exclude_patterns, sc->exclude_count)) {
            free(normalized);
            return;
        }
    }

    // Add to the file list
    if (filelist_add(sc->out, normalized) != 0) {
        sc->error = 1;
    }
    free(normalized);
}

// --- Public API ---

int scanner_scan_sources(const char* crate_path, const char** exclude_patterns,
                         int exclude_count, FileList* out) {
    if (!crate_path || !out) return 1;

    if (filelist_init(out) != 0) return 1;

    // Build the src/ path
    char src_path[1024];
    if (pal_path_join(src_path, sizeof(src_path), crate_path, "src") != 0) {
        filelist_free(out);
        return 1;
    }
    pal_path_normalize(src_path);

    // Check if src/ exists
    if (pal_path_exists(src_path) != 0) {
        // No src/ directory — return empty list (not an error)
        return 0;
    }

    // Normalize crate_path for consistent prefix stripping
    size_t crate_path_len = strlen(crate_path);
    char* norm_crate = (char*)malloc(crate_path_len + 1);
    if (!norm_crate) { filelist_free(out); return 1; }
    memcpy(norm_crate, crate_path, crate_path_len + 1);
    pal_path_normalize(norm_crate);
    // Strip trailing slash if present
    size_t norm_crate_len = strlen(norm_crate);
    while (norm_crate_len > 0 && norm_crate[norm_crate_len - 1] == '/') {
        norm_crate[--norm_crate_len] = '\0';
    }

    ScanContext ctx = {
        .out = out,
        .base_path = src_path,
        .base_len = strlen(src_path),
        .crate_path = norm_crate,
        .crate_len = norm_crate_len,
        .exclude_patterns = exclude_patterns,
        .exclude_count = exclude_count,
        .check_sources = true,
        .error = 0,
    };

    int walk_result = pal_dir_walk(src_path, scan_callback, &ctx);
    free(norm_crate);

    if (walk_result != PAL_OK || ctx.error) {
        filelist_free(out);
        return 1;
    }

    return 0;
}

int scanner_scan_headers(const char* crate_path, FileList* out) {
    if (!crate_path || !out) return 1;

    if (filelist_init(out) != 0) return 1;

    // Build the include/ path
    char inc_path[1024];
    if (pal_path_join(inc_path, sizeof(inc_path), crate_path, "include") != 0) {
        filelist_free(out);
        return 1;
    }
    pal_path_normalize(inc_path);

    // Check if include/ exists
    if (pal_path_exists(inc_path) != 0) {
        // No include/ directory — return empty list (not an error)
        return 0;
    }

    // Normalize crate_path for consistent prefix stripping
    size_t crate_path_len = strlen(crate_path);
    char* norm_crate = (char*)malloc(crate_path_len + 1);
    if (!norm_crate) { filelist_free(out); return 1; }
    memcpy(norm_crate, crate_path, crate_path_len + 1);
    pal_path_normalize(norm_crate);
    size_t norm_crate_len = strlen(norm_crate);
    while (norm_crate_len > 0 && norm_crate[norm_crate_len - 1] == '/') {
        norm_crate[--norm_crate_len] = '\0';
    }

    ScanContext ctx = {
        .out = out,
        .base_path = inc_path,
        .base_len = strlen(inc_path),
        .crate_path = norm_crate,
        .crate_len = norm_crate_len,
        .exclude_patterns = NULL,
        .exclude_count = 0,
        .check_sources = false,
        .error = 0,
    };

    int walk_result = pal_dir_walk(inc_path, scan_callback, &ctx);
    free(norm_crate);

    if (walk_result != PAL_OK || ctx.error) {
        filelist_free(out);
        return 1;
    }

    return 0;
}
