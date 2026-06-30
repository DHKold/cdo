/// @file e2e_fixture.c
/// @brief Implementation of e2e fixture copy functionality.
/// Recursively copies a named fixture directory into the test environment.
/// Requirements: 7.1-7.5, 7.8

#include "cdo_e2e.h"
#include <ctype.h>

#define FIXTURE_NAME_MAX_LEN 64

/// Context passed to the pal_dir_walk callback during fixture copy.
typedef struct {
    const char* src_root;      // fixture source root path
    int         src_root_len;  // length of source root path (for stripping prefix)
    const char* dst_root;      // env->root_path (destination)
    int         error_count;   // number of errors encountered
} FixtureCopyCtx;

/// Validate that fixture_name contains only alphanumeric, hyphens, underscores and is 1-64 chars.
static bool fixture_name_is_valid(const char* name) {
    if (name == NULL || name[0] == '\0') return false;

    size_t len = 0;
    for (const char* p = name; *p != '\0'; p++) {
        len++;
        if (len > FIXTURE_NAME_MAX_LEN) return false;
        char c = *p;
        if (!isalnum((unsigned char)c) && c != '-' && c != '_') return false;
    }
    return true;
}

/// Callback for pal_dir_walk: copies each file/directory from fixture source to destination.
static void fixture_copy_walk_cb(const char* path, bool is_dir, void* ctx) {
    FixtureCopyCtx* fctx = (FixtureCopyCtx*)ctx;

    // Compute relative path by stripping the source root prefix
    const char* rel = path + fctx->src_root_len;
    // Skip leading separator if present
    if (*rel == '/' || *rel == '\\') rel++;

    // If rel is empty, this is the root directory itself — skip it
    if (*rel == '\0') return;

    // Build destination path
    char dst_path[512];
    if (pal_path_join(dst_path, sizeof(dst_path), fctx->dst_root, rel) != 0) {
        fprintf(stderr, "[e2e_fixture] ERROR: path join failed for rel='%s'\n", rel);
        fctx->error_count++;
        return;
    }

    if (is_dir) {
        // Create directory in destination
        if (pal_mkdir_p(dst_path) != 0) {
            fprintf(stderr, "[e2e_fixture] ERROR: failed to create directory '%s'\n", dst_path);
            fctx->error_count++;
        }
    } else {
        // Ensure parent directory exists
        char parent[512];
        snprintf(parent, sizeof(parent), "%s", dst_path);
        char* last_sep = strrchr(parent, '/');
        if (!last_sep) last_sep = strrchr(parent, '\\');
        if (last_sep) {
            *last_sep = '\0';
            if (pal_mkdir_p(parent) != 0) {
                fprintf(stderr, "[e2e_fixture] ERROR: failed to create parent dir '%s'\n", parent);
                fctx->error_count++;
                return;
            }
        }

        // Copy the file
        if (pal_file_copy(path, dst_path) != 0) {
            fprintf(stderr, "[e2e_fixture] ERROR: failed to copy '%s' -> '%s'\n", path, dst_path);
            fctx->error_count++;
        }
    }
}

int e2e_fixture_copy(E2eEnv* env, const char* fixture_name) {
    // Validate inputs
    if (env == NULL || fixture_name == NULL) return E2E_ERR_INVALID;
    if (env->crate_path[0] == '\0') return E2E_ERR_INVALID;
    if (!fixture_name_is_valid(fixture_name)) return E2E_ERR_INVALID;

    // Build fixture source path: <crate_path>/e2e/fixtures/<fixture_name>
    char fixtures_base[512];
    if (pal_path_join(fixtures_base, sizeof(fixtures_base), env->crate_path, "e2e/fixtures") != 0) {
        return E2E_ERR_INVALID;
    }

    char fixture_path[512];
    if (pal_path_join(fixture_path, sizeof(fixture_path), fixtures_base, fixture_name) != 0) {
        return E2E_ERR_INVALID;
    }

    // Check fixture directory exists (pal_path_exists returns 0 on success = exists)
    if (pal_path_exists(fixture_path) != 0) {
        return E2E_ERR_NOT_FOUND;
    }

    // Recursively copy using pal_dir_walk
    FixtureCopyCtx ctx;
    ctx.src_root = fixture_path;
    ctx.src_root_len = (int)strlen(fixture_path);
    ctx.dst_root = env->root_path;
    ctx.error_count = 0;

    int walk_rc = pal_dir_walk(fixture_path, fixture_copy_walk_cb, &ctx);
    if (walk_rc != 0) {
        fprintf(stderr, "[e2e_fixture] ERROR: pal_dir_walk failed for '%s' (rc=%d)\n", fixture_path, walk_rc);
        return E2E_ERR_IO;
    }

    if (ctx.error_count > 0) {
        fprintf(stderr, "[e2e_fixture] WARNING: %d error(s) during fixture copy of '%s'\n", ctx.error_count, fixture_name);
        return E2E_ERR_IO;
    }

    return E2E_OK;
}
