/**
 * bundle.c - Shared runtime bundling utility.
 *
 * Extracts the staging logic originally in cmd_run.c so that both `cdo run`
 * and `cdo install` can produce a self-contained runtime bundle for an exe crate.
 */
#include "commands/bundle.h"
#include "core/log.h"
#include "model/module.h"
#include "pal/pal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define BUILD_DIR "build"

// ---------------------------------------------------------------------------
// Directory copy (recursive)
// ---------------------------------------------------------------------------

typedef struct {
    const char* src_base;
    size_t      src_base_len;
    const char* dst_base;
    int         error;
} DirCopyCtx;

static void copy_dir_callback(const char* entry_path, bool is_dir, void* ctx) {
    DirCopyCtx* dc = (DirCopyCtx*)ctx;
    if (dc->error) return;
    if (is_dir) return;

    size_t len = strlen(entry_path);
    char* norm_entry = (char*)malloc(len + 1);
    if (!norm_entry) { dc->error = 1; return; }
    memcpy(norm_entry, entry_path, len + 1);
    pal_path_normalize(norm_entry);

    const char* rel = norm_entry + dc->src_base_len;
    if (*rel == '/') rel++;

    char dst_path[1024];
    if (pal_path_join(dst_path, sizeof(dst_path), dc->dst_base, rel) != 0) {
        cdo_log_error("bundle: destination path too long for '%s'", rel);
        free(norm_entry);
        dc->error = 1;
        return;
    }

    char parent_dir[1024];
    strncpy(parent_dir, dst_path, sizeof(parent_dir) - 1);
    parent_dir[sizeof(parent_dir) - 1] = '\0';
    char* last_slash = strrchr(parent_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (pal_mkdir_p(parent_dir) != 0) {
            cdo_log_error("bundle: failed to create directory '%s'", parent_dir);
            free(norm_entry);
            dc->error = 1;
            return;
        }
    }

    if (pal_file_copy(norm_entry, dst_path) != 0) {
        cdo_log_error("bundle: failed to copy '%s' -> '%s'", norm_entry, dst_path);
        free(norm_entry);
        dc->error = 1;
        return;
    }

    free(norm_entry);
}

int bundle_copy_dir_recursive(const char* src_dir, const char* dst_dir) {
    char norm_src[1024];
    strncpy(norm_src, src_dir, sizeof(norm_src) - 1);
    norm_src[sizeof(norm_src) - 1] = '\0';
    pal_path_normalize(norm_src);

    size_t src_len = strlen(norm_src);
    while (src_len > 0 && norm_src[src_len - 1] == '/') {
        norm_src[--src_len] = '\0';
    }

    DirCopyCtx ctx;
    ctx.src_base = norm_src;
    ctx.src_base_len = src_len;
    ctx.dst_base = dst_dir;
    ctx.error = 0;

    int rc = pal_dir_walk(norm_src, copy_dir_callback, &ctx);
    if (rc != 0) return 1;
    if (ctx.error) return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// DLL copy
// ---------------------------------------------------------------------------

typedef struct {
    const char* dst_dir;
    int         error;
} DllCopyCtx;

static void copy_dll_callback(const char* entry_path, bool is_dir, void* ctx) {
    DllCopyCtx* dc = (DllCopyCtx*)ctx;
    if (dc->error) return;
    if (is_dir) return;

    const char* ext = pal_path_ext(entry_path);
#ifdef _WIN32
    if (strcmp(ext, ".dll") != 0) return;
#else
    if (strcmp(ext, ".so") != 0) return;
#endif

    const char* filename = strrchr(entry_path, '/');
    if (!filename) filename = strrchr(entry_path, '\\');
    if (filename) filename++;
    else filename = entry_path;

    char dst_path[1024];
    if (pal_path_join(dst_path, sizeof(dst_path), dc->dst_dir, filename) != 0) {
        cdo_log_error("bundle: DLL path too long for '%s'", filename);
        dc->error = 1;
        return;
    }

    if (pal_file_copy(entry_path, dst_path) != 0) {
        cdo_log_error("bundle: failed to copy DLL '%s' -> '%s'", entry_path, dst_path);
        dc->error = 1;
        return;
    }

    cdo_log_debug("bundle: copied DLL '%s'", filename);
}

// ---------------------------------------------------------------------------
// Crate selection
// ---------------------------------------------------------------------------

static const Crate* find_crate_by_name(const Workspace* ws, const char* name) {
    for (int i = 0; i < ws->crate_count; i++) {
        if (strcmp(ws->crates[i].name, name) == 0) {
            return &ws->crates[i];
        }
    }
    return NULL;
}

const Crate* bundle_select_exe_crate(const Workspace* ws, const char* const* positional_values, int positional_count) {
    if (positional_count > 0) {
        const char* name = positional_values[0];
        const Crate* crate = find_crate_by_name(ws, name);
        if (!crate) {
            cdo_log_error("Crate '%s' not found in workspace", name);
            return NULL;
        }
        if (!crate->modules[MODULE_EXE].present) {
            cdo_log_error("Crate '%s' has no executable target", name);
            return NULL;
        }
        return crate;
    }

    const Crate* exec_crates[64];
    int exec_count = 0;

    for (int i = 0; i < ws->crate_count; i++) {
        if (ws->crates[i].modules[MODULE_EXE].present) {
            if (exec_count < 64) {
                exec_crates[exec_count++] = &ws->crates[i];
            }
        }
    }

    if (exec_count == 0) {
        cdo_log_error("No executable crate found in workspace");
        return NULL;
    }

    if (exec_count == 1) {
        return exec_crates[0];
    }

    cdo_log_error("Multiple executable crates found; specify one:");
    for (int i = 0; i < exec_count; i++) {
        cdo_log_error("  %s", exec_crates[i]->name);
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Bundle preparation
// ---------------------------------------------------------------------------

int bundle_prepare(const Workspace* ws, const Crate* crate, const char* profile, const char* staging_dir, const char* exe_path, const BundleOpts* opts) {
    // Resolve effective base paths
    const char* res_base = (opts && opts->resource_base && opts->resource_base[0] != '\0') ? opts->resource_base : ".";
    const char* shd_base = (opts && opts->shader_base && opts->shader_base[0] != '\0') ? opts->shader_base : ".";

    // Clear prior staging contents
    if (pal_path_exists(staging_dir) == 0) {
        cdo_log_debug("bundle: clearing prior contents at '%s'", staging_dir);
        if (pal_rmdir_r(staging_dir) != 0) {
            cdo_log_error("bundle: failed to clear prior staging folder '%s'", staging_dir);
            return 1;
        }
    }

    if (pal_mkdir_p(staging_dir) != 0) {
        cdo_log_error("bundle: failed to create staging folder '%s'", staging_dir);
        return 1;
    }

    // Copy executable
    char exe_name[128];
#ifdef _WIN32
    snprintf(exe_name, sizeof(exe_name), "%s.exe", crate->name);
#else
    snprintf(exe_name, sizeof(exe_name), "%s", crate->name);
#endif

    char staging_exe[1024];
    if (pal_path_join(staging_exe, sizeof(staging_exe), staging_dir, exe_name) != 0) {
        cdo_log_error("bundle: exe path too long");
        return 1;
    }

    if (pal_file_copy(exe_path, staging_exe) != 0) {
        cdo_log_error("bundle: failed to copy executable '%s' -> '%s'", exe_path, staging_exe);
        return 1;
    }
    cdo_log_debug("bundle: copied executable '%s'", exe_name);

    // Copy DLLs from crate build directory
    char crate_build_dir[1024];
    {
        char profile_dir[512];
        char build_base[512];
        pal_path_join(build_base, sizeof(build_base), ws->root_path, BUILD_DIR);
        pal_path_join(profile_dir, sizeof(profile_dir), build_base, profile);
        pal_path_join(crate_build_dir, sizeof(crate_build_dir), profile_dir, crate->name);
    }

    DllCopyCtx dll_ctx = { .dst_dir = staging_dir, .error = 0 };
    pal_dir_walk(crate_build_dir, copy_dll_callback, &dll_ctx);
    if (dll_ctx.error) {
        cdo_log_error("bundle: failed to copy shared libraries");
        return 1;
    }

    // Copy res/ contents to <staging_dir>/<resource_base>/
    char res_src[1024];
    if (pal_path_join(res_src, sizeof(res_src), crate_build_dir, "res") == 0) {
        if (pal_path_exists(res_src) == 0) {
            char res_dst[1024];
            if (strcmp(res_base, ".") == 0) {
                strncpy(res_dst, staging_dir, sizeof(res_dst) - 1);
                res_dst[sizeof(res_dst) - 1] = '\0';
            } else {
                pal_path_join(res_dst, sizeof(res_dst), staging_dir, res_base);
                if (pal_mkdir_p(res_dst) != 0) {
                    cdo_log_error("bundle: failed to create resource base directory '%s'", res_dst);
                    return 1;
                }
            }
            if (bundle_copy_dir_recursive(res_src, res_dst) != 0) {
                cdo_log_error("bundle: failed to copy resources");
                return 1;
            }
            cdo_log_debug("bundle: copied resources to '%s'", res_base);
        }
    }

    // Copy shd/ contents to <staging_dir>/<shader_base>/
    char shd_src[1024];
    if (pal_path_join(shd_src, sizeof(shd_src), crate_build_dir, "shd") == 0) {
        if (pal_path_exists(shd_src) == 0) {
            char shd_dst[1024];
            if (strcmp(shd_base, ".") == 0) {
                strncpy(shd_dst, staging_dir, sizeof(shd_dst) - 1);
                shd_dst[sizeof(shd_dst) - 1] = '\0';
            } else {
                pal_path_join(shd_dst, sizeof(shd_dst), staging_dir, shd_base);
                if (pal_mkdir_p(shd_dst) != 0) {
                    cdo_log_error("bundle: failed to create shader base directory '%s'", shd_dst);
                    return 1;
                }
            }
            if (bundle_copy_dir_recursive(shd_src, shd_dst) != 0) {
                cdo_log_error("bundle: failed to copy shaders");
                return 1;
            }
            cdo_log_debug("bundle: copied shaders to '%s'", shd_base);
        }
    }

    return 0;
}
