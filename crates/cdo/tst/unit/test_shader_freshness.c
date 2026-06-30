// crates/cdo/tst/unit/test_shader_freshness.c
// Unit tests for shader freshness check using compiler_link_is_fresh()
// The shader freshness logic is identical to link freshness: compare input mtime vs output mtime.
// Validates: Requirements 7.6, 7.7
#include "cdo_ut.h"
#include "core/compiler.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <sys/utime.h>
#include <windows.h>
#else
#include <utime.h>
#include <sys/stat.h>
#endif

// =============================================================================
// Helpers
// =============================================================================

static void get_temp_dir(char* buf, size_t size, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
    snprintf(buf, size, "%s/cdo_test_shader_fresh_%s", tmp, suffix);
    pal_path_normalize(buf);
}

/// Set the modification time of a file to a specific Unix timestamp (seconds).
static int set_mtime(const char* path, time_t mtime_sec) {
#ifdef _WIN32
    struct _utimbuf times;
    times.actime  = mtime_sec;
    times.modtime = mtime_sec;
    return _utime(path, &times);
#else
    struct utimbuf times;
    times.actime  = mtime_sec;
    times.modtime = mtime_sec;
    return utime(path, &times);
#endif
}

/// Create a file with content. Parent directory must already exist.
static int create_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) return 1;
    if (content) fputs(content, f);
    fclose(f);
    return 0;
}

// =============================================================================
// Test: Shader source older than output -> skip compilation (return true)
// Requirement 7.7: shader source mtime < output mtime -> skip recompilation
// =============================================================================

TEST_SERIAL(shader_fresh_source_older) {
    char root[520];
    get_temp_dir(root, sizeof(root), "src_older");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create shader source file
    char shader_src[520];
    pal_path_join(shader_src, sizeof(shader_src), root, "vertex.hlsl");
    create_file(shader_src, "float4 VS() : SV_POSITION { return 0; }");

    // Create compiled shader output
    char shader_out[520];
    pal_path_join(shader_out, sizeof(shader_out), root, "vertex.hlsl.dxil");
    create_file(shader_out, "compiled_shader_binary");

    // Source is older than output
    set_mtime(shader_src, 1577836800);  // 2020-01-01
    set_mtime(shader_out, 1704067200);  // 2024-01-01

    // Use compiler_link_is_fresh with shader source as input, shader output as output
    const char* inputs[] = { shader_src };
    bool fresh = compiler_link_is_fresh(shader_out, inputs, 1);

    // Source older than output -> fresh (skip shader compilation)
    TEST_ASSERT(fresh == true);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: Shader source newer than output -> recompile (return false)
// Requirement 7.7: shader source mtime > output mtime -> must recompile
// =============================================================================

TEST_SERIAL(shader_fresh_source_newer) {
    char root[520];
    get_temp_dir(root, sizeof(root), "src_newer");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create shader source file
    char shader_src[520];
    pal_path_join(shader_src, sizeof(shader_src), root, "pixel.hlsl");
    create_file(shader_src, "float4 PS() : SV_TARGET { return 1; }");

    // Create compiled shader output (older than source)
    char shader_out[520];
    pal_path_join(shader_out, sizeof(shader_out), root, "pixel.hlsl.spv");
    create_file(shader_out, "old_compiled_spirv");

    // Source is newer than output (source modified after last compilation)
    set_mtime(shader_src, 1735689600); // 2025-01-01 (newer)
    set_mtime(shader_out, 1704067200); // 2024-01-01

    const char* inputs[] = { shader_src };
    bool fresh = compiler_link_is_fresh(shader_out, inputs, 1);

    // Source newer than output -> not fresh (must recompile shader)
    TEST_ASSERT(fresh == false);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: Shader output missing -> always compile (return false)
// Requirement 7.7: output (.dxil, .spv) missing -> always compile
// =============================================================================

TEST_SERIAL(shader_fresh_output_missing) {
    char root[520];
    get_temp_dir(root, sizeof(root), "out_missing");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create shader source file
    char shader_src[520];
    pal_path_join(shader_src, sizeof(shader_src), root, "compute.hlsl");
    create_file(shader_src, "[numthreads(1,1,1)] void CS() {}");

    // Output file does NOT exist
    char shader_out[520];
    pal_path_join(shader_out, sizeof(shader_out), root, "compute.hlsl.dxil");
    // Not creating the output file

    const char* inputs[] = { shader_src };
    bool fresh = compiler_link_is_fresh(shader_out, inputs, 1);

    // Output missing -> not fresh (must compile shader)
    TEST_ASSERT(fresh == false);

    pal_rmdir_r(root);
    return 0;
}
