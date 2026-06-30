#include "cmd_build_internal.h"

#include "core/log.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Shader Module build (incremental .hlsl -> .dxil compilation via DXC)
// ---------------------------------------------------------------------------

/// Dynamic list for collecting file paths during directory walk.
typedef struct {
    char**  paths;
    int     count;
    int     capacity;
} ShdPathList;

static int shd_pathlist_add(ShdPathList* pl, const char* path) {
    if (pl->count >= pl->capacity) {
        int new_cap = pl->capacity == 0 ? 32 : pl->capacity * 2;
        char** new_paths = (char**)realloc(pl->paths, sizeof(char*) * (size_t)new_cap);
        if (!new_paths) return 1;
        pl->paths = new_paths;
        pl->capacity = new_cap;
    }
    pl->paths[pl->count] = strdup(path);
    if (!pl->paths[pl->count]) return 1;
    pl->count++;
    return 0;
}

static void shd_pathlist_free(ShdPathList* pl) {
    for (int i = 0; i < pl->count; i++) free(pl->paths[i]);
    free(pl->paths);
    pl->paths = NULL;
    pl->count = 0;
    pl->capacity = 0;
}

// ---------------------------------------------------------------------------
// Walk callback: collects .hlsl files only
// ---------------------------------------------------------------------------

typedef struct {
    ShdPathList*  list;
    int           error;
} ShdWalkCtx;

static void collect_hlsl_cb(const char* entry_path, bool is_dir, void* ctx) {
    ShdWalkCtx* wc = (ShdWalkCtx*)ctx;
    if (wc->error) return;
    if (is_dir) return;

    // Only collect .hlsl files
    const char* ext = pal_path_ext(entry_path);
    if (!ext || strcmp(ext, ".hlsl") != 0) return;

    // Normalize the path
    size_t len = strlen(entry_path);
    char* normalized = (char*)malloc(len + 1);
    if (!normalized) { wc->error = 1; return; }
    memcpy(normalized, entry_path, len + 1);
    pal_path_normalize(normalized);

    if (shd_pathlist_add(wc->list, normalized) != 0) {
        wc->error = 1;
    }
    free(normalized);
}

// ---------------------------------------------------------------------------
// Compute relative path from base directory
// ---------------------------------------------------------------------------

static const char* shd_relative_to(const char* base, const char* full) {
    size_t base_len = strlen(base);
    while (base_len > 0 && base[base_len - 1] == '/') base_len--;
    if (strncmp(base, full, base_len) != 0) return NULL;
    const char* rel = full + base_len;
    if (*rel == '/') rel++;
    return rel;
}

// ---------------------------------------------------------------------------
// build_shader_module
// ---------------------------------------------------------------------------

/// Build (compile) the Shader_Module for a crate using DXC.
/// Performs incremental compilation of .hlsl â†’ .dxil.
///
/// Requirements: 4.3, 4.4, 4.5, 4.6, 4.7, 4.8, 4.9, 4.10
int build_shader_module(const Workspace* ws, Crate* crate,
                        const char* profile,
                        const BuildProfile* build_prof,
                        bool force,
                        CliProgressBar* progress,
                        int* completed_units) {
    (void)build_prof; // reserved for future DXC flags from profile

    if (!crate->has_shd) return 0;

    // Req 4.7: Check DXC exists at .cdo/tools/dxc/bin/dxc.exe
    char dxc_path[520];
    if (pal_path_join(dxc_path, sizeof(dxc_path), ws->root_path, ".cdo/tools/dxc/bin/dxc.exe") != 0) {
        cdo_log_error("shd module: failed to compute DXC path");
        return 1;
    }

    if (pal_path_exists(dxc_path) != 0) {
        cdo_log_error("DXC not found at '%s'. Install with: cdo tool install dxc", dxc_path);
        return 1;
    }

    Module* shd_mod = &crate->modules[MODULE_SHD];
    const char* src_dir = shd_mod->dir_path;

    // Normalize source dir for consistent path comparison
    char norm_src[260];
    strncpy(norm_src, src_dir, sizeof(norm_src) - 1);
    norm_src[sizeof(norm_src) - 1] = '\0';
    pal_path_normalize(norm_src);

    // Build destination directory: build/<profile>/<crate_name>/shd/
    char build_dir[260];
    build_dir_for_crate(ws, crate, profile, build_dir, sizeof(build_dir));

    char dest_dir[520];
    if (pal_path_join(dest_dir, sizeof(dest_dir), build_dir, "shd") != 0) {
        cdo_log_error("shd module: failed to compute destination path for crate '%s'", crate->name);
        return 1;
    }

    // Check if source directory exists
    if (pal_path_exists(norm_src) != 0) {
        // Req 4.10: empty/missing shd/ -> zero compiled, zero skipped, return 0
        cdo_log_info("shd module '%s': 0 compiled, 0 skipped", crate->name);
        if (progress && completed_units) {
            *completed_units += 1;
            cli_out_progress_update(progress, *completed_units);
        }
        return 0;
    }

    // Walk shd/ directory recursively for .hlsl files
    ShdPathList hlsl_files = {0};
    ShdWalkCtx walk_ctx = { .list = &hlsl_files, .error = 0 };

    int rc = pal_dir_walk(norm_src, collect_hlsl_cb, &walk_ctx);
    if (rc != 0 || walk_ctx.error) {
        cdo_log_error("shd module: failed to walk source directory '%s' for crate '%s'", norm_src, crate->name);
        shd_pathlist_free(&hlsl_files);
        return 1;
    }

    // Req 4.10: If shd/ has zero .hlsl files, report and return success
    if (hlsl_files.count == 0) {
        cdo_log_info("shd module '%s': 0 compiled, 0 skipped", crate->name);
        if (progress && completed_units) {
            *completed_units += 1;
            cli_out_progress_update(progress, *completed_units);
        }
        shd_pathlist_free(&hlsl_files);
        return 0;
    }

    // Ensure destination directory exists
    if (pal_mkdir_p(dest_dir) != 0) {
        cdo_log_error("shd module: failed to create output directory '%s' for crate '%s'", dest_dir, crate->name);
        shd_pathlist_free(&hlsl_files);
        return 1;
    }

    // Process each .hlsl file
    int compiled = 0;
    int skipped = 0;
    int errors = 0;

    for (int i = 0; i < hlsl_files.count; i++) {
        const char* src_path = hlsl_files.paths[i];

        // Compute relative path from source dir
        const char* rel = shd_relative_to(norm_src, src_path);
        if (!rel || *rel == '\0') {
            cdo_log_error("shd module: failed to compute relative path for '%s'", src_path);
            errors++;
            continue;
        }

        // Req 4.4: Output is same relative path with .dxil appended
        // e.g., vertex.hlsl -> vertex.hlsl.dxil, subdir/pixel.hlsl -> subdir/pixel.hlsl.dxil
        char output_rel[520];
        snprintf(output_rel, sizeof(output_rel), "%s.dxil", rel);

        char output_path[520];
        if (pal_path_join(output_path, sizeof(output_path), dest_dir, output_rel) != 0) {
            cdo_log_error("shd module: output path too long for '%s'", rel);
            errors++;
            continue;
        }

        // Req 4.5: Incremental â€” skip if output mtime >= source mtime (unless force)
        if (!force) {
            uint64_t src_mtime = 0, out_mtime = 0;
            if (pal_file_mtime(src_path, &src_mtime) == 0 && pal_file_mtime(output_path, &out_mtime) == 0) {
                if (out_mtime >= src_mtime) {
                    cdo_log_debug("shd: up-to-date, skipping: %s", rel);
                    skipped++;
                    continue;
                }
            }
        }

        // Ensure parent directory of output exists (for subdirectories)
        char parent_dir[520];
        strncpy(parent_dir, output_path, sizeof(parent_dir) - 1);
        parent_dir[sizeof(parent_dir) - 1] = '\0';
        char* last_slash = strrchr(parent_dir, '/');
        if (last_slash) {
            *last_slash = '\0';
            if (pal_mkdir_p(parent_dir) != 0) {
                cdo_log_error("shd module: failed to create directory '%s' for crate '%s'", parent_dir, crate->name);
                errors++;
                continue;
            }
        }

        // Invoke DXC via pal_spawn
        const char* args[] = {
            "-T", "lib_6_3",
            "-Fo", output_path,
            src_path
        };

        PalSpawnOpts spawn_opts;
        memset(&spawn_opts, 0, sizeof(spawn_opts));
        spawn_opts.program = dxc_path;
        spawn_opts.args = args;
        spawn_opts.arg_count = 5;
        spawn_opts.capture_output = true;

        PalSpawnResult result;
        memset(&result, 0, sizeof(result));

        cdo_log_debug("shd: DXC %s -> %s", src_path, output_path);

        int spawn_rc = pal_spawn(&spawn_opts, &result);
        if (spawn_rc != PAL_OK) {
            cdo_log_error("shd module: failed to spawn DXC for '%s'", rel);
            pal_spawn_result_free(&result);
            errors++;
            continue;
        }

        // Req 4.8: On failure, log DXC stderr, continue remaining shaders
        if (result.exit_code != 0) {
            if (result.stderr_buf && result.stderr_buf[0] != '\0') {
                cdo_log_error("DXC error [%s]: %s", rel, result.stderr_buf);
            }
            if (result.stdout_buf && result.stdout_buf[0] != '\0') {
                cdo_log_error("DXC output [%s]: %s", rel, result.stdout_buf);
            }
            cdo_log_error("shd module: shader compilation failed: %s (exit code %d)", rel, result.exit_code);
            pal_spawn_result_free(&result);
            errors++;
            continue;
        }

        pal_spawn_result_free(&result);
        compiled++;
    }

    // Req 4.9: Report compiled/skipped counts at info verbosity
    cdo_log_info("shd module '%s': %d compiled, %d skipped", crate->name, compiled, skipped);

    // Update progress
    if (progress && completed_units) {
        *completed_units += 1;
        cli_out_progress_update(progress, *completed_units);
    }

    shd_pathlist_free(&hlsl_files);

    // Req 4.8: Return non-zero if any shader failed
    if (errors > 0) {
        cdo_log_error("shd module '%s': %d shader(s) failed", crate->name, errors);
        return 1;
    }

    return 0;
}
