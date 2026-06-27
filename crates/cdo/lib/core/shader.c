#include "core/shader.h"
#include "core/output.h"
#include "pal/pal.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

/// Context passed to pal_dir_walk callback for collecting .hlsl files.
typedef struct {
    const char* output_dir;
    const char* dxc_path;
    int         compiled;
    int         skipped;
    int         errors;
} ShaderWalkCtx;

/// Extended context for shader_compile_ex walk callback.
typedef struct {
    const char* output_dir;
    const char* dxc_path;
    const char* target_profile;
    bool        force;
    int         compiled;
    int         skipped;
    int         errors;
} ShaderWalkExCtx;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Extract the filename (without extension) from a path and write it to buf.
/// Returns 0 on success, non-zero if buf is too small.
static int get_basename_no_ext(const char* path, char* buf, size_t buf_size) {
    // Find last separator
    const char* last_sep = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            last_sep = p;
        }
    }

    const char* filename = last_sep ? (last_sep + 1) : path;

    // Find last dot in filename
    const char* dot = NULL;
    for (const char* p = filename; *p; p++) {
        if (*p == '.') {
            dot = p;
        }
    }

    size_t len = dot ? (size_t)(dot - filename) : strlen(filename);
    if (len >= buf_size) return -1;

    memcpy(buf, filename, len);
    buf[len] = '\0';
    return 0;
}

/// Determine whether a shader needs recompilation by comparing mtimes.
/// Returns true if the shader should be compiled, false if it can be skipped.
static bool shader_needs_compile(const char* source_path, const char* output_path) {
    // If output doesn't exist, must compile (Requirement 9.4)
    if (pal_path_exists(output_path) != 0) {
        cdo_debug("Shader output missing, will compile: %s", source_path);
        return true;
    }

    // Compare mtimes (Requirements 9.1, 9.2, 9.3)
    uint64_t source_mtime = 0;
    uint64_t output_mtime = 0;

    if (pal_file_mtime(source_path, &source_mtime) != PAL_OK) {
        // Can't read source mtime — safer to recompile
        cdo_warn("Cannot read mtime for shader source: %s", source_path);
        return true;
    }

    if (pal_file_mtime(output_path, &output_mtime) != PAL_OK) {
        // Can't read output mtime — safer to recompile
        cdo_warn("Cannot read mtime for shader output: %s", output_path);
        return true;
    }

    // Skip if source is older than or equal to output (Requirement 9.2)
    if (source_mtime <= output_mtime) {
        cdo_debug("Shader up-to-date, skipping: %s", source_path);
        return false;
    }

    // Source is newer than output — recompile (Requirement 9.3)
    cdo_debug("Shader source newer than output, will compile: %s", source_path);
    return true;
}

/// Invoke DXC to compile a single shader file.
/// Returns 0 on success, non-zero on failure.
static int invoke_dxc(const char* dxc_path, const char* source_path,
                      const char* output_path) {
    // DXC arguments: -T <target_profile> -E main -Fo <output> <source>
    // Default to vs_6_0 for now; a real implementation would detect the profile
    // from the filename or annotations. For incremental compilation, we use
    // a library target (lib_6_3) which compiles without requiring an entry point.
    const char* args[] = {
        "-T", "lib_6_3",
        "-Fo", output_path,
        source_path
    };

    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.program = dxc_path;
    opts.args = args;
    opts.arg_count = 5;
    opts.capture_output = true;

    PalSpawnResult result;
    memset(&result, 0, sizeof(result));

    cdo_debug("DXC: %s -> %s", source_path, output_path);

    int rc = pal_spawn(&opts, &result);
    if (rc != PAL_OK) {
        cdo_error("Failed to spawn DXC for: %s", source_path);
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        if (result.stderr_buf && result.stderr_buf[0] != '\0') {
            cdo_error("DXC error: %s", result.stderr_buf);
        }
        if (result.stdout_buf && result.stdout_buf[0] != '\0') {
            cdo_error("DXC output: %s", result.stdout_buf);
        }
        cdo_error("Shader compilation failed: %s (exit code %d)",
                  source_path, result.exit_code);
        pal_spawn_result_free(&result);
        return result.exit_code;
    }

    pal_spawn_result_free(&result);
    return 0;
}

/// Invoke DXC to compile a single shader file with a configurable target profile.
/// Returns 0 on success, non-zero on failure.
static int invoke_dxc_with_profile(const char* dxc_path, const char* source_path,
                                   const char* output_path, const char* target_profile) {
    const char* args[] = {
        "-T", target_profile,
        "-Fo", output_path,
        source_path
    };

    PalSpawnOpts spawn_opts;
    memset(&spawn_opts, 0, sizeof(spawn_opts));
    spawn_opts.program = dxc_path;
    spawn_opts.args = args;
    spawn_opts.arg_count = 5;
    spawn_opts.capture_output = true;

    PalSpawnResult result;
    memset(&result, 0, sizeof(result));

    cdo_debug("DXC [%s]: %s -> %s", target_profile, source_path, output_path);

    int rc = pal_spawn(&spawn_opts, &result);
    if (rc != PAL_OK) {
        cdo_error("Failed to spawn DXC for: %s", source_path);
        pal_spawn_result_free(&result);
        return -1;
    }

    if (result.exit_code != 0) {
        if (result.stderr_buf && result.stderr_buf[0] != '\0') {
            cdo_error("DXC error: %s", result.stderr_buf);
        }
        if (result.stdout_buf && result.stdout_buf[0] != '\0') {
            cdo_error("DXC output: %s", result.stdout_buf);
        }
        cdo_error("Shader compilation failed: %s (exit code %d)",
                  source_path, result.exit_code);
        pal_spawn_result_free(&result);
        return -1;
    }

    pal_spawn_result_free(&result);
    return 0;
}

// ---------------------------------------------------------------------------
// pal_dir_walk callback
// ---------------------------------------------------------------------------

/// Callback for pal_dir_walk: processes each .hlsl file found.
static void shader_walk_callback(const char* path, bool is_dir, void* ctx) {
    if (is_dir) return;

    ShaderWalkCtx* wctx = (ShaderWalkCtx*)ctx;

    // Check if the file has .hlsl extension
    const char* ext = pal_path_ext(path);
    if (!ext || strcmp(ext, ".hlsl") != 0) {
        return;
    }

    // Build output path: output_dir / basename.dxil
    char basename[256];
    if (get_basename_no_ext(path, basename, sizeof(basename)) != 0) {
        cdo_error("Shader filename too long: %s", path);
        wctx->errors++;
        return;
    }

    char output_name[260];
    snprintf(output_name, sizeof(output_name), "%s.dxil", basename);

    char output_path[520];
    if (pal_path_join(output_path, sizeof(output_path),
                      wctx->output_dir, output_name) != 0) {
        cdo_error("Output path too long for shader: %s", path);
        wctx->errors++;
        return;
    }

    // Check if recompilation is needed
    if (!shader_needs_compile(path, output_path)) {
        wctx->skipped++;
        return;
    }

    // Invoke DXC
    if (invoke_dxc(wctx->dxc_path, path, output_path) != 0) {
        wctx->errors++;
        return;
    }

    wctx->compiled++;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int shader_compile(const char* shader_dir, const char* output_dir,
                   const char* dxc_path, int* compiled_count, int* skipped_count) {
    if (!shader_dir || !output_dir || !dxc_path) {
        cdo_error("shader_compile: NULL argument");
        return -1;
    }

    // Verify shader directory exists
    if (pal_path_exists(shader_dir) != 0) {
        cdo_error("Shader directory does not exist: %s", shader_dir);
        return -1;
    }

    // Verify DXC binary exists
    if (pal_path_exists(dxc_path) != 0) {
        cdo_error("DXC binary not found: %s", dxc_path);
        return -1;
    }

    // Ensure output directory exists
    if (pal_path_exists(output_dir) != 0) {
        int rc = pal_mkdir_p(output_dir);
        if (rc != 0) {
            cdo_error("Failed to create shader output directory: %s", output_dir);
            return -1;
        }
    }

    // Walk the shader directory and process each .hlsl file
    ShaderWalkCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.output_dir = output_dir;
    ctx.dxc_path = dxc_path;

    int rc = pal_dir_walk(shader_dir, shader_walk_callback, &ctx);
    if (rc != PAL_OK) {
        cdo_error("Failed to walk shader directory: %s", shader_dir);
        return -1;
    }

    // Report counts (Requirement 9.5)
    cdo_info("Compiled %d shader(s), skipped %d", ctx.compiled, ctx.skipped);

    // Output counts to caller
    if (compiled_count) *compiled_count = ctx.compiled;
    if (skipped_count)  *skipped_count  = ctx.skipped;

    // Return error if any shaders failed
    if (ctx.errors > 0) {
        cdo_error("%d shader(s) failed to compile", ctx.errors);
        return ctx.errors;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Extended API: shader_compile_ex
// ---------------------------------------------------------------------------

/// Walk callback for shader_compile_ex: processes each .hlsl file with
/// configurable target profile and force flag.
static void shader_walk_ex_callback(const char* path, bool is_dir, void* ctx) {
    if (is_dir) return;

    ShaderWalkExCtx* wctx = (ShaderWalkExCtx*)ctx;

    // Check if the file has .hlsl extension
    const char* ext = pal_path_ext(path);
    if (!ext || strcmp(ext, ".hlsl") != 0) {
        return;
    }

    // Build output path: output_dir / basename.dxil
    char basename[256];
    if (get_basename_no_ext(path, basename, sizeof(basename)) != 0) {
        cdo_error("Shader filename too long: %s", path);
        wctx->errors++;
        return;
    }

    char output_name[260];
    snprintf(output_name, sizeof(output_name), "%s.dxil", basename);

    char output_path[520];
    if (pal_path_join(output_path, sizeof(output_path),
                      wctx->output_dir, output_name) != 0) {
        cdo_error("Output path too long for shader: %s", path);
        wctx->errors++;
        return;
    }

    // Check if recompilation is needed (unless force is set)
    if (!wctx->force && !shader_needs_compile(path, output_path)) {
        wctx->skipped++;
        return;
    }

    // Invoke DXC with the configured target profile
    if (invoke_dxc_with_profile(wctx->dxc_path, path, output_path,
                                wctx->target_profile) != 0) {
        wctx->errors++;
        return;
    }

    wctx->compiled++;
}

int shader_compile_ex(const ShaderCompileOpts* opts, ShaderCompileResult* result) {
    // Initialize result if provided
    ShaderCompileResult local_result = {0};

    if (!opts || !opts->shader_dir || !opts->output_dir || !opts->dxc_path) {
        cdo_error("shader_compile_ex: NULL argument");
        return -1;
    }

    // Use default target profile if not specified
    const char* target_profile = opts->target_profile;
    if (!target_profile || target_profile[0] == '\0') {
        target_profile = "lib_6_3";
    }

    // Verify DXC binary exists
    if (pal_path_exists(opts->dxc_path) != 0) {
        cdo_error("DXC shader compiler not found at: %s", opts->dxc_path);
        cdo_info("Try: cdo tool install dxc");
        return -1;
    }

    // Verify shader directory exists
    if (pal_path_exists(opts->shader_dir) != 0) {
        cdo_error("Shader source directory does not exist: %s", opts->shader_dir);
        return -1;
    }

    // Ensure output directory exists
    if (pal_path_exists(opts->output_dir) != 0) {
        int rc = pal_mkdir_p(opts->output_dir);
        if (rc != 0) {
            cdo_error("Failed to create output directory: %s", opts->output_dir);
            return -1;
        }
    }

    // Walk the shader directory and process each .hlsl file
    ShaderWalkExCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.output_dir     = opts->output_dir;
    ctx.dxc_path       = opts->dxc_path;
    ctx.target_profile = target_profile;
    ctx.force          = opts->force;

    int rc = pal_dir_walk(opts->shader_dir, shader_walk_ex_callback, &ctx);
    if (rc != PAL_OK) {
        cdo_error("Failed to walk shader directory: %s", opts->shader_dir);
        return -1;
    }

    // Populate result
    local_result.compiled_count = ctx.compiled;
    local_result.skipped_count  = ctx.skipped;
    local_result.error_count    = ctx.errors;

    if (result) {
        *result = local_result;
    }

    // Return non-zero if any shaders failed
    if (ctx.errors > 0) {
        return -1;
    }

    return 0;
}
