// lib/core/compiler_internal.h — Internal header for compiler_*.c split files
// NOT a public API header (not in api/).
#ifndef CDO_CORE_COMPILER_INTERNAL_H
#define CDO_CORE_COMPILER_INTERNAL_H

#include "core/compiler.h"  // public types (CompilerInfo, CompileJob, etc.)
#include "pal/pal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal helpers shared between compiler_*.c files
// ---------------------------------------------------------------------------

/// Check if a file extension indicates a compilable source (not a header).
static inline bool is_compilable_source(const char* path) {
    const char* ext = pal_path_ext(path);
    if (!ext) return false;
    return (strcmp(ext, ".c") == 0 ||
            strcmp(ext, ".cpp") == 0 ||
            strcmp(ext, ".cxx") == 0 ||
            strcmp(ext, ".cc") == 0);
}

/// Derive a relative stem from a source file path relative to the crate's src/ dir.
/// E.g., if crate_src_prefix="/proj/mycrate/src" and source="/proj/mycrate/src/foo/bar.c",
/// then stem_out = "foo/bar" (no extension).
/// Returns 0 on success, -1 on failure.
static inline int derive_stem(const char* source_path, const char* crate_src_prefix,
                              size_t prefix_len, char* stem_out, size_t stem_size) {
    // Source path should start with crate_src_prefix
    if (strncmp(source_path, crate_src_prefix, prefix_len) != 0) {
        return -1;
    }

    const char* rel = source_path + prefix_len;
    // Skip leading separator
    if (*rel == '/' || *rel == '\\') rel++;

    // Copy without extension
    const char* ext = pal_path_ext(rel);
    size_t ext_len = ext ? strlen(ext) : 0;
    size_t rel_len = strlen(rel);
    size_t stem_len = rel_len - ext_len;

    if (stem_len >= stem_size) return -1;
    memcpy(stem_out, rel, stem_len);
    stem_out[stem_len] = '\0';
    return 0;
}

#endif // CDO_CORE_COMPILER_INTERNAL_H
