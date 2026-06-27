// crates/cdo/tst/unit/test_shader_build.c
// Unit tests for shader_compile_ex — the core shader compilation utility.
// These test the retained shader compilation logic (core/shader.h), NOT the
// removed standalone `cdo shader` command.
// Requirements: 5.1, 5.2, 5.6, 5.9, 5.10, 5.11, 5.12, 5.13
#include "cdo_ut.h"
#include "core/shader.h"
#include "core/cli.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <sys/utime.h>
#else
#include <utime.h>
#include <sys/stat.h>
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Create a temporary directory. Returns 0 on success.
static int create_test_dir(const char* path) {
    return pal_mkdir_p(path);
}

/// Create a file with optional content at the given path.
static int create_test_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) return 1;
    if (content) {
        fputs(content, f);
    }
    fclose(f);
    return 0;
}

/// Remove a test directory recursively.
static int remove_test_dir(const char* path) {
    return pal_rmdir_r(path);
}

/// Set the modification time of a file to a specific Unix timestamp (seconds).
static int set_file_mtime(const char* path, time_t mtime_sec) {
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

/// Create a fake DXC executable that always succeeds (exit 0).
/// On Windows, we create a .bat file that exits 0.
static int create_fake_dxc(const char* dir, char* dxc_path, size_t dxc_path_size) {
    if (create_test_dir(dir) != 0) return 1;

#ifdef _WIN32
    pal_path_join(dxc_path, dxc_path_size, dir, "dxc.bat");
    FILE* f = fopen(dxc_path, "w");
    if (!f) return 1;
    fprintf(f, "@echo off\n");
    fprintf(f, "copy NUL \"%%4\" >NUL 2>&1\n");
    fprintf(f, "exit /b 0\n");
    fclose(f);
#else
    pal_path_join(dxc_path, dxc_path_size, dir, "dxc");
    FILE* f = fopen(dxc_path, "w");
    if (!f) return 1;
    fprintf(f, "#!/bin/sh\ntouch \"$4\"\nexit 0\n");
    fclose(f);
    chmod(dxc_path, 0755);
#endif
    return 0;
}

/// Create a fake DXC executable that always FAILS (exit 1).
static int create_fake_dxc_fail(const char* dir, char* dxc_path, size_t dxc_path_size) {
    if (create_test_dir(dir) != 0) return 1;

#ifdef _WIN32
    pal_path_join(dxc_path, dxc_path_size, dir, "dxc.bat");
    FILE* f = fopen(dxc_path, "w");
    if (!f) return 1;
    fprintf(f, "@echo off\n");
    fprintf(f, "echo FAKE DXC FAILURE 1>&2\n");
    fprintf(f, "exit /b 1\n");
    fclose(f);
#else
    pal_path_join(dxc_path, dxc_path_size, dir, "dxc");
    FILE* f = fopen(dxc_path, "w");
    if (!f) return 1;
    fprintf(f, "#!/bin/sh\necho 'FAKE DXC FAILURE' >&2\nexit 1\n");
    fclose(f);
    chmod(dxc_path, 0755);
#endif
    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex with NULL opts → returns non-zero
// ---------------------------------------------------------------------------

TEST(shader_compile_ex_null_opts_returns_error) {
    int rc = shader_compile_ex(NULL, NULL);
    TEST_ASSERT_NEQ(rc, 0);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex with missing source directory → returns non-zero
// Requirement 5.6
// ---------------------------------------------------------------------------

TEST_SERIAL(shader_compile_ex_missing_source_dir_returns_error) {
    char dxc_path[512];
    const char* dxc_dir = "__test_build_dxc_dir_miss__";
    int setup = create_fake_dxc(dxc_dir, dxc_path, sizeof(dxc_path));
    TEST_ASSERT_EQ(setup, 0);

    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = "__test_build_missing_src_xyz__";
    compile_opts.output_dir     = "__test_build_out_miss__";
    compile_opts.dxc_path       = dxc_path;
    compile_opts.target_profile = "lib_6_3";
    compile_opts.force          = false;

    ShaderCompileResult result = {0};
    int rc = shader_compile_ex(&compile_opts, &result);
    TEST_ASSERT_NEQ(rc, 0);

    remove_test_dir(dxc_dir);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex with empty source directory → 0 compiled, 0 skipped, returns 0
// Requirement 5.11
// ---------------------------------------------------------------------------

TEST_SERIAL(shader_compile_ex_empty_source_dir_returns_zero) {
    char dxc_path[512];
    const char* dxc_dir = "__test_build_dxc_dir_empty__";
    int setup = create_fake_dxc(dxc_dir, dxc_path, sizeof(dxc_path));
    TEST_ASSERT_EQ(setup, 0);

    const char* src_dir = "__test_build_empty_src__";
    TEST_ASSERT_EQ(create_test_dir(src_dir), 0);

    const char* out_dir = "__test_build_empty_out__";

    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = src_dir;
    compile_opts.output_dir     = out_dir;
    compile_opts.dxc_path       = dxc_path;
    compile_opts.target_profile = "lib_6_3";
    compile_opts.force          = false;

    ShaderCompileResult result = {0};
    int rc = shader_compile_ex(&compile_opts, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(result.compiled_count, 0);
    TEST_ASSERT_EQ(result.skipped_count, 0);
    TEST_ASSERT_EQ(result.error_count, 0);

    remove_test_dir(dxc_dir);
    remove_test_dir(src_dir);
    remove_test_dir(out_dir);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex incremental - source newer than output → recompiles
// Requirement 5.9
// ---------------------------------------------------------------------------

TEST_SERIAL(shader_compile_ex_incremental_source_newer_recompiles) {
    char dxc_path[512];
    const char* dxc_dir = "__test_build_dxc_incr_new__";
    int setup = create_fake_dxc(dxc_dir, dxc_path, sizeof(dxc_path));
    TEST_ASSERT_EQ(setup, 0);

    const char* src_dir = "__test_build_incr_new_src__";
    TEST_ASSERT_EQ(create_test_dir(src_dir), 0);

    char hlsl_path[256];
    pal_path_join(hlsl_path, sizeof(hlsl_path), src_dir, "test.hlsl");
    TEST_ASSERT_EQ(create_test_file(hlsl_path, "// shader"), 0);

    const char* out_dir = "__test_build_incr_new_out__";
    TEST_ASSERT_EQ(create_test_dir(out_dir), 0);

    char dxil_path[256];
    pal_path_join(dxil_path, sizeof(dxil_path), out_dir, "test.dxil");
    TEST_ASSERT_EQ(create_test_file(dxil_path, "old"), 0);

    // Set output to be old (year 2020) and source to be newer (year 2024)
    set_file_mtime(dxil_path, 1577836800);  // 2020-01-01
    set_file_mtime(hlsl_path, 1704067200);  // 2024-01-01

    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = src_dir;
    compile_opts.output_dir     = out_dir;
    compile_opts.dxc_path       = dxc_path;
    compile_opts.target_profile = "lib_6_3";
    compile_opts.force          = false;

    ShaderCompileResult result = {0};
    int rc = shader_compile_ex(&compile_opts, &result);

    // Source newer → should attempt compilation (not skip)
    TEST_ASSERT_EQ(result.skipped_count, 0);
    TEST_ASSERT_EQ(result.compiled_count + result.error_count, 1);

    remove_test_dir(dxc_dir);
    remove_test_dir(src_dir);
    remove_test_dir(out_dir);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex incremental - source older than output → skips
// Requirement 5.9
// ---------------------------------------------------------------------------

TEST_SERIAL(shader_compile_ex_incremental_source_older_skips) {
    char dxc_path[512];
    const char* dxc_dir = "__test_build_dxc_incr_old__";
    int setup = create_fake_dxc(dxc_dir, dxc_path, sizeof(dxc_path));
    TEST_ASSERT_EQ(setup, 0);

    const char* src_dir = "__test_build_incr_old_src__";
    TEST_ASSERT_EQ(create_test_dir(src_dir), 0);

    char hlsl_path[256];
    pal_path_join(hlsl_path, sizeof(hlsl_path), src_dir, "test.hlsl");
    TEST_ASSERT_EQ(create_test_file(hlsl_path, "// shader"), 0);

    const char* out_dir = "__test_build_incr_old_out__";
    TEST_ASSERT_EQ(create_test_dir(out_dir), 0);

    char dxil_path[256];
    pal_path_join(dxil_path, sizeof(dxil_path), out_dir, "test.dxil");
    TEST_ASSERT_EQ(create_test_file(dxil_path, "compiled"), 0);

    // Set source to be old (year 2020) and output to be newer (year 2024)
    set_file_mtime(hlsl_path, 1577836800);  // 2020-01-01
    set_file_mtime(dxil_path, 1704067200);  // 2024-01-01

    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = src_dir;
    compile_opts.output_dir     = out_dir;
    compile_opts.dxc_path       = dxc_path;
    compile_opts.target_profile = "lib_6_3";
    compile_opts.force          = false;

    ShaderCompileResult result = {0};
    int rc = shader_compile_ex(&compile_opts, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(result.compiled_count, 0);
    TEST_ASSERT_EQ(result.skipped_count, 1);
    TEST_ASSERT_EQ(result.error_count, 0);

    remove_test_dir(dxc_dir);
    remove_test_dir(src_dir);
    remove_test_dir(out_dir);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex --force → recompiles regardless of mtime
// Requirement 5.13
// ---------------------------------------------------------------------------

TEST_SERIAL(shader_compile_ex_force_recompiles_regardless) {
    char dxc_path[512];
    const char* dxc_dir = "__test_build_dxc_force__";
    int setup = create_fake_dxc(dxc_dir, dxc_path, sizeof(dxc_path));
    TEST_ASSERT_EQ(setup, 0);

    const char* src_dir = "__test_build_force_src__";
    TEST_ASSERT_EQ(create_test_dir(src_dir), 0);

    char hlsl_path[256];
    pal_path_join(hlsl_path, sizeof(hlsl_path), src_dir, "test.hlsl");
    TEST_ASSERT_EQ(create_test_file(hlsl_path, "// shader"), 0);

    const char* out_dir = "__test_build_force_out__";
    TEST_ASSERT_EQ(create_test_dir(out_dir), 0);

    char dxil_path[256];
    pal_path_join(dxil_path, sizeof(dxil_path), out_dir, "test.dxil");
    TEST_ASSERT_EQ(create_test_file(dxil_path, "compiled"), 0);

    // Source is old, output is new — would normally skip
    set_file_mtime(hlsl_path, 1577836800);  // 2020-01-01
    set_file_mtime(dxil_path, 1704067200);  // 2024-01-01

    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = src_dir;
    compile_opts.output_dir     = out_dir;
    compile_opts.dxc_path       = dxc_path;
    compile_opts.target_profile = "lib_6_3";
    compile_opts.force          = true; // <-- Force recompile

    ShaderCompileResult result = {0};
    int rc = shader_compile_ex(&compile_opts, &result);

    // Force means it should attempt to compile, not skip
    TEST_ASSERT_EQ(result.skipped_count, 0);
    TEST_ASSERT_EQ(result.compiled_count + result.error_count, 1);

    remove_test_dir(dxc_dir);
    remove_test_dir(src_dir);
    remove_test_dir(out_dir);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex error continuation - one shader fails, rest attempted
// Requirement 5.12
// ---------------------------------------------------------------------------

TEST_SERIAL(shader_compile_ex_error_continuation) {
    char dxc_path[512];
    const char* dxc_dir = "__test_build_dxc_errcont__";
    int setup = create_fake_dxc_fail(dxc_dir, dxc_path, sizeof(dxc_path));
    TEST_ASSERT_EQ(setup, 0);

    const char* src_dir = "__test_build_errcont_src__";
    TEST_ASSERT_EQ(create_test_dir(src_dir), 0);

    char hlsl_a[256], hlsl_b[256], hlsl_c[256];
    pal_path_join(hlsl_a, sizeof(hlsl_a), src_dir, "a.hlsl");
    pal_path_join(hlsl_b, sizeof(hlsl_b), src_dir, "b.hlsl");
    pal_path_join(hlsl_c, sizeof(hlsl_c), src_dir, "c.hlsl");

    TEST_ASSERT_EQ(create_test_file(hlsl_a, "// shader a"), 0);
    TEST_ASSERT_EQ(create_test_file(hlsl_b, "// shader b"), 0);
    TEST_ASSERT_EQ(create_test_file(hlsl_c, "// shader c"), 0);

    const char* out_dir = "__test_build_errcont_out__";

    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = src_dir;
    compile_opts.output_dir     = out_dir;
    compile_opts.dxc_path       = dxc_path;
    compile_opts.target_profile = "lib_6_3";
    compile_opts.force          = true;

    ShaderCompileResult result = {0};
    int rc = shader_compile_ex(&compile_opts, &result);

    // Should return non-zero (errors occurred)
    TEST_ASSERT_NEQ(rc, 0);

    // All 3 shaders should have been attempted (error continuation)
    TEST_ASSERT_EQ(result.error_count, 3);
    TEST_ASSERT_EQ(result.compiled_count, 0);
    TEST_ASSERT_EQ(result.skipped_count, 0);

    remove_test_dir(dxc_dir);
    remove_test_dir(src_dir);
    remove_test_dir(out_dir);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex with DXC path that doesn't exist → returns non-zero
// Requirement 5.1
// ---------------------------------------------------------------------------

TEST_SERIAL(shader_compile_ex_dxc_not_found_returns_error) {
    const char* src_dir = "__test_build_dxc_nf_src__";
    TEST_ASSERT_EQ(create_test_dir(src_dir), 0);

    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = src_dir;
    compile_opts.output_dir     = "__test_build_dxc_nf_out__";
    compile_opts.dxc_path       = "__nonexistent_dxc_path__/dxc.exe";
    compile_opts.target_profile = "lib_6_3";
    compile_opts.force          = false;

    ShaderCompileResult result = {0};
    int rc = shader_compile_ex(&compile_opts, &result);
    TEST_ASSERT_NEQ(rc, 0);

    remove_test_dir(src_dir);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex with NULL shader_dir field → returns non-zero
// ---------------------------------------------------------------------------

TEST(shader_compile_ex_null_shader_dir_returns_error) {
    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = NULL;
    compile_opts.output_dir     = "out";
    compile_opts.dxc_path       = "dxc";
    compile_opts.target_profile = "lib_6_3";

    int rc = shader_compile_ex(&compile_opts, NULL);
    TEST_ASSERT_NEQ(rc, 0);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex with NULL output_dir field → returns non-zero
// ---------------------------------------------------------------------------

TEST(shader_compile_ex_null_output_dir_returns_error) {
    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = "shaders";
    compile_opts.output_dir     = NULL;
    compile_opts.dxc_path       = "dxc";
    compile_opts.target_profile = "lib_6_3";

    int rc = shader_compile_ex(&compile_opts, NULL);
    TEST_ASSERT_NEQ(rc, 0);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex with NULL dxc_path field → returns non-zero
// ---------------------------------------------------------------------------

TEST(shader_compile_ex_null_dxc_path_returns_error) {
    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = "shaders";
    compile_opts.output_dir     = "out";
    compile_opts.dxc_path       = NULL;
    compile_opts.target_profile = "lib_6_3";

    int rc = shader_compile_ex(&compile_opts, NULL);
    TEST_ASSERT_NEQ(rc, 0);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex with empty target_profile → uses default "lib_6_3"
// ---------------------------------------------------------------------------

TEST_SERIAL(shader_compile_ex_empty_target_uses_default) {
    char dxc_path[512];
    const char* dxc_dir = "__test_build_dxc_deftgt__";
    int setup = create_fake_dxc(dxc_dir, dxc_path, sizeof(dxc_path));
    TEST_ASSERT_EQ(setup, 0);

    const char* src_dir = "__test_build_deftgt_src__";
    TEST_ASSERT_EQ(create_test_dir(src_dir), 0);

    const char* out_dir = "__test_build_deftgt_out__";

    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = src_dir;
    compile_opts.output_dir     = out_dir;
    compile_opts.dxc_path       = dxc_path;
    compile_opts.target_profile = "";  // Empty → should use default
    compile_opts.force          = false;

    ShaderCompileResult result = {0};
    int rc = shader_compile_ex(&compile_opts, &result);
    TEST_ASSERT_EQ(rc, 0);

    remove_test_dir(dxc_dir);
    remove_test_dir(src_dir);
    remove_test_dir(out_dir);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: shader_compile_ex incremental - output does not exist → compiles
// Requirement 5.9
// ---------------------------------------------------------------------------

TEST_SERIAL(shader_compile_ex_no_output_file_compiles) {
    char dxc_path[512];
    const char* dxc_dir = "__test_build_dxc_noout__";
    int setup = create_fake_dxc(dxc_dir, dxc_path, sizeof(dxc_path));
    TEST_ASSERT_EQ(setup, 0);

    const char* src_dir = "__test_build_noout_src__";
    TEST_ASSERT_EQ(create_test_dir(src_dir), 0);

    char hlsl_path[256];
    pal_path_join(hlsl_path, sizeof(hlsl_path), src_dir, "shader.hlsl");
    TEST_ASSERT_EQ(create_test_file(hlsl_path, "// shader"), 0);

    // Output directory exists but NO .dxil file → should compile
    const char* out_dir = "__test_build_noout_out__";
    TEST_ASSERT_EQ(create_test_dir(out_dir), 0);

    ShaderCompileOpts compile_opts = {0};
    compile_opts.shader_dir     = src_dir;
    compile_opts.output_dir     = out_dir;
    compile_opts.dxc_path       = dxc_path;
    compile_opts.target_profile = "lib_6_3";
    compile_opts.force          = false;

    ShaderCompileResult result = {0};
    int rc = shader_compile_ex(&compile_opts, &result);

    // Should attempt compilation (not skip)
    TEST_ASSERT_EQ(result.skipped_count, 0);
    TEST_ASSERT_EQ(result.compiled_count + result.error_count, 1);

    remove_test_dir(dxc_dir);
    remove_test_dir(src_dir);
    remove_test_dir(out_dir);

    return 0;
}
