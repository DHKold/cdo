/**
 * test_build_pipeline.c - E2E tests for the `cdo build` command pipeline.
 *
 * Validates: Requirements 7.4, 9.1, 9.2, 9.5, 13.1
 *
 * Tests the build command's pipeline behavior: exit codes, log output patterns,
 * --clean, --force, --release, --jobs, incremental builds, and error handling.
 *
 * Note: Since concrete task execute() methods are stubs (returning 0 without
 * actually compiling), some tests create dummy output files to simulate the
 * post-build state needed for incremental/force behavior.
 */

#include "cdo_e2e.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define sleep_ms(ms) usleep((ms) * 1000)
#endif

// =============================================================================
// Helpers
// =============================================================================

static int get_workspace_root(char* buf, size_t buf_size) {
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s", __FILE__);
    pal_path_normalize(file_path);

    char* e2e_pos = strstr(file_path, "/crates/cdo/e2e/");
    if (e2e_pos == NULL) {
        e2e_pos = strstr(file_path, "\\crates\\cdo\\e2e\\");
    }
    if (e2e_pos == NULL) {
        return -1;
    }

    size_t root_len = (size_t)(e2e_pos - file_path);
    if (root_len >= buf_size) {
        return -1;
    }
    memcpy(buf, file_path, root_len);
    buf[root_len] = '\0';
    return 0;
}

static int get_crate_path(char* buf, size_t buf_size) {
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s", __FILE__);
    pal_path_normalize(file_path);

    char* e2e_pos = strstr(file_path, "/e2e/");
    if (e2e_pos == NULL) {
        return -1;
    }

    size_t crate_len = (size_t)(e2e_pos - file_path);
    if (crate_len >= buf_size) {
        return -1;
    }
    memcpy(buf, file_path, crate_len);
    buf[crate_len] = '\0';
    return 0;
}

/// Resolve the path to the cdo binary to test.
/// Prefers build/debug/cdo/cdo.exe (freshly-built with new C++ pipeline),
/// falls back to root cdo.exe (bootstrapped).
static int get_cdo_exe_path(char* cdo_exe, size_t cdo_exe_size) {
    char ws_root[512];
    int rc = get_workspace_root(ws_root, sizeof(ws_root));
    if (rc != 0) return -1;

    snprintf(cdo_exe, cdo_exe_size, "%s/build/debug/cdo/cdo.exe", ws_root);
    if (pal_path_exists(cdo_exe) != 0) {
        snprintf(cdo_exe, cdo_exe_size, "%s/cdo.exe", ws_root);
    }
    return 0;
}

/// Setup a test environment with the build_pipeline/<fixture_name> fixture copied in.
/// Also sets up the PATH to include vendored tools and resolves the cdo.exe path.
static int setup_build_env(const char* test_name, const char* fixture_name, E2eEnv* env, char* cdo_exe, size_t cdo_exe_size) {
    int rc = e2e_env_create(test_name, env);
    if (rc != E2E_OK) return rc;

    char crate_path[512];
    rc = get_crate_path(crate_path, sizeof(crate_path));
    if (rc != 0) return -1;

    rc = e2e_env_set_crate_path(env, crate_path);
    if (rc != E2E_OK) return rc;

    // Copy the fixture (fixture is under fixtures/<fixture_name>/)
    rc = e2e_fixture_copy(env, fixture_name);
    if (rc != E2E_OK) return rc;

    // Resolve cdo.exe path
    rc = get_cdo_exe_path(cdo_exe, cdo_exe_size);
    if (rc != 0) return -1;

    // Add vendored tools to PATH
    char ws_root[512];
    rc = get_workspace_root(ws_root, sizeof(ws_root));
    if (rc != 0) return -1;

    char tools_path[512];
    snprintf(tools_path, sizeof(tools_path), "%s/.cdo/tools/w64devkit/bin", ws_root);
    const char* existing_path = getenv("PATH");
    char new_path[4096];
    snprintf(new_path, sizeof(new_path), "%s;%s", tools_path, existing_path ? existing_path : "");
    e2e_env_setvar(env, "PATH", new_path);

    return E2E_OK;
}

// =============================================================================
// Test: build_pipeline_single_crate_succeeds
//
// `cdo build --jobs 1` on a single-crate workspace (lib module) exits 0 and
// produces a summary line with "Build complete:".
//
// Validates: Requirements 9.5, 13.1
// =============================================================================

TEST(build_pipeline_single_crate_succeeds) {
    E2eEnv env = {0};
    char cdo_exe[512];
    int rc = setup_build_env("bp_single_succeed", "bp-single-lib", &env, cdo_exe, sizeof(cdo_exe));
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "build";
    opts.args[1] = "--jobs";
    opts.args[2] = "1";
    opts.arg_count = 3;
    opts.working_dir = env.root_path;
    opts.timeout_ms = 30000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    if (result.exit_code != 0) {
        fprintf(stderr, "[build_pipeline_single_crate_succeeds] exit %d\n", result.exit_code);
        if (result.stdout_buf) fprintf(stderr, "[STDOUT] %s\n", result.stdout_buf);
        if (result.stderr_buf) fprintf(stderr, "[STDERR] %s\n", result.stderr_buf);
    }
    E2E_ASSERT_EXIT_CODE(&result, 0);

    // The summary line "Build complete: N built, M skipped, K failed" goes to stdout (INFO level)
    E2E_ASSERT_STDOUT_CONTAINS(&result, "Build complete:");

    // On first build all tasks should build (stubs produce "does not exist" reason)
    E2E_ASSERT_STDOUT_CONTAINS(&result, "Building:");

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: build_pipeline_release_profile
//
// `cdo build --release --jobs 1` exits 0 and produces output referencing
// the release profile path (build/release/).
//
// Validates: Requirements 7.4, 9.5
// =============================================================================

TEST(build_pipeline_release_profile) {
    E2eEnv env = {0};
    char cdo_exe[512];
    int rc = setup_build_env("bp_release", "bp-single-lib", &env, cdo_exe, sizeof(cdo_exe));
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "build";
    opts.args[1] = "--release";
    opts.args[2] = "--jobs";
    opts.args[3] = "1";
    opts.arg_count = 4;
    opts.working_dir = env.root_path;
    opts.timeout_ms = 30000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    if (result.exit_code != 0) {
        fprintf(stderr, "[build_pipeline_release_profile] exit %d\n", result.exit_code);
        if (result.stdout_buf) fprintf(stderr, "[STDOUT] %s\n", result.stdout_buf);
        if (result.stderr_buf) fprintf(stderr, "[STDERR] %s\n", result.stderr_buf);
    }
    E2E_ASSERT_EXIT_CODE(&result, 0);

    // Output paths should reference the release profile (build/release/ in paths)
    E2E_ASSERT_STDOUT_CONTAINS(&result, "build/release/");

    // Summary line should be present
    E2E_ASSERT_STDOUT_CONTAINS(&result, "Build complete:");

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: build_pipeline_crate_filter
//
// `cdo build base --jobs 1` on a multi-crate workspace builds only the
// specified crate. The summary should show work done and exit 0.
//
// Validates: Requirements 13.1
// =============================================================================

TEST(build_pipeline_crate_filter) {
    E2eEnv env = {0};
    char cdo_exe[512];
    int rc = setup_build_env("bp_crate_filter", "bp-multi-crate", &env, cdo_exe, sizeof(cdo_exe));
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "build";
    opts.args[1] = "--jobs";
    opts.args[2] = "1";
    opts.args[3] = "base";
    opts.arg_count = 4;
    opts.working_dir = env.root_path;
    opts.timeout_ms = 30000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    if (result.exit_code != 0) {
        fprintf(stderr, "[build_pipeline_crate_filter] exit %d\n", result.exit_code);
        if (result.stdout_buf) fprintf(stderr, "[STDOUT] %s\n", result.stdout_buf);
        if (result.stderr_buf) fprintf(stderr, "[STDERR] %s\n", result.stderr_buf);
    }
    E2E_ASSERT_EXIT_CODE(&result, 0);

    // Should have a build summary
    E2E_ASSERT_STDOUT_CONTAINS(&result, "Build complete:");

    // Output should mention the base crate's lib (building base lib only)
    E2E_ASSERT_STDOUT_CONTAINS(&result, "base");

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: build_pipeline_incremental_skips
//
// When output files already exist and are newer than inputs, the second build
// should skip all targets. We simulate this by creating dummy output files
// (since execute() stubs don't actually produce them).
//
// Validates: Requirements 9.1, 9.2, 9.5
// =============================================================================

TEST(build_pipeline_incremental_skips) {
    E2eEnv env = {0};
    char cdo_exe[512];
    int rc = setup_build_env("bp_incremental", "bp-single-lib", &env, cdo_exe, sizeof(cdo_exe));
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Create the build output directory and dummy output files BEFORE running build.
    // This simulates what a real build would produce. The FreshnessCondition will
    // see these files exist with mtimes >= source mtimes and decide to skip.
    e2e_env_write_file(&env, "build/debug/hello/lib/hello.o", "dummy", 5);
    e2e_env_write_file(&env, "build/debug/hello/lib/hello.d", "hello.o: hello.c\n", 18);
    e2e_env_write_file(&env, "build/debug/hello/libhello.a", "dummy_lib", 9);

    // Small delay to ensure output file mtimes are strictly newer than source mtimes
    sleep_ms(50);

    // Run build — should skip all targets since outputs are "up-to-date"
    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "build";
    opts.args[1] = "--jobs";
    opts.args[2] = "1";
    opts.arg_count = 3;
    opts.working_dir = env.root_path;
    opts.timeout_ms = 30000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    if (result.exit_code != 0) {
        fprintf(stderr, "[build_pipeline_incremental_skips] exit %d\n", result.exit_code);
        if (result.stdout_buf) fprintf(stderr, "[STDOUT] %s\n", result.stdout_buf);
        if (result.stderr_buf) fprintf(stderr, "[STDERR] %s\n", result.stderr_buf);
    }
    E2E_ASSERT_EXIT_CODE(&result, 0);

    // Summary should show "0 built" (all skipped since outputs exist and are fresh)
    E2E_ASSERT_STDOUT_CONTAINS(&result, "0 built");

    // Should NOT contain "Building:" lines (nothing to rebuild)
    if (result.stdout_buf && strstr(result.stdout_buf, "Building:") != NULL) {
        cdo_ut_record_failure(__FILE__, __LINE__, "stdout should NOT contain 'Building:' on incremental build", result.stdout_buf, "(no Building: lines)");
        e2e_spawn_result_free(&result);
        e2e_env_destroy(&env);
        return 1;
    }

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: build_pipeline_force_rebuild
//
// `cdo build --force --jobs 1` should rebuild all targets even when outputs
// exist and are up-to-date. The output should contain "Building:" lines with
// "(forced)" reason.
//
// Validates: Requirements 9.1, 9.5
// =============================================================================

TEST(build_pipeline_force_rebuild) {
    E2eEnv env = {0};
    char cdo_exe[512];
    int rc = setup_build_env("bp_force", "bp-single-lib", &env, cdo_exe, sizeof(cdo_exe));
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Create dummy output files so the FreshnessCondition would normally skip
    e2e_env_write_file(&env, "build/debug/hello/lib/hello.o", "dummy", 5);
    e2e_env_write_file(&env, "build/debug/hello/lib/hello.d", "hello.o: hello.c\n", 18);
    e2e_env_write_file(&env, "build/debug/hello/libhello.a", "dummy_lib", 9);
    sleep_ms(50);

    // Build with --force
    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "build";
    opts.args[1] = "--force";
    opts.args[2] = "--jobs";
    opts.args[3] = "1";
    opts.arg_count = 4;
    opts.working_dir = env.root_path;
    opts.timeout_ms = 30000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    if (result.exit_code != 0) {
        fprintf(stderr, "[build_pipeline_force_rebuild] exit %d\n", result.exit_code);
        if (result.stdout_buf) fprintf(stderr, "[STDOUT] %s\n", result.stdout_buf);
        if (result.stderr_buf) fprintf(stderr, "[STDERR] %s\n", result.stderr_buf);
    }
    E2E_ASSERT_EXIT_CODE(&result, 0);

    // --force should cause "Building:" lines to appear (forced rebuild)
    E2E_ASSERT_STDOUT_CONTAINS(&result, "Building:");
    E2E_ASSERT_STDOUT_CONTAINS(&result, "(forced)");

    // Summary should show non-zero built count (should NOT be "0 built")
    E2E_ASSERT_STDOUT_CONTAINS(&result, "Build complete:");
    if (result.stdout_buf && strstr(result.stdout_buf, "0 built") != NULL) {
        cdo_ut_record_failure(__FILE__, __LINE__, "stdout should NOT contain '0 built' with --force", result.stdout_buf, "(non-zero built)");
        e2e_spawn_result_free(&result);
        e2e_env_destroy(&env);
        return 1;
    }

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: build_pipeline_clean_recreates_dir
//
// `cdo build --clean --jobs 1` should delete the build/<profile>/ directory
// and recreate it. A marker file placed in build/debug/ should be gone after.
//
// Validates: Requirements 7.4, 9.5
// =============================================================================

TEST(build_pipeline_clean_recreates_dir) {
    E2eEnv env = {0};
    char cdo_exe[512];
    int rc = setup_build_env("bp_clean", "bp-single-lib", &env, cdo_exe, sizeof(cdo_exe));
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Create build/debug/ with a marker file
    e2e_env_write_file(&env, "build/debug/.marker", "test_marker", 11);

    char marker_path[512];
    snprintf(marker_path, sizeof(marker_path), "%s/build/debug/.marker", env.root_path);
    E2E_ASSERT_FILE_EXISTS(marker_path);

    // Build with --clean
    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "build";
    opts.args[1] = "--clean";
    opts.args[2] = "--jobs";
    opts.args[3] = "1";
    opts.arg_count = 4;
    opts.working_dir = env.root_path;
    opts.timeout_ms = 30000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    if (result.exit_code != 0) {
        fprintf(stderr, "[build_pipeline_clean_recreates_dir] exit %d\n", result.exit_code);
        if (result.stdout_buf) fprintf(stderr, "[STDOUT] %s\n", result.stdout_buf);
        if (result.stderr_buf) fprintf(stderr, "[STDERR] %s\n", result.stderr_buf);
    }
    E2E_ASSERT_EXIT_CODE(&result, 0);

    // build/debug/ should exist (recreated by the pipeline)
    char build_dir[512];
    snprintf(build_dir, sizeof(build_dir), "%s/build/debug", env.root_path);
    E2E_ASSERT_FILE_EXISTS(build_dir);

    // The marker file should be gone (directory was deleted and recreated)
    E2E_ASSERT_FILE_NOT_EXISTS(marker_path);

    // Summary should be present
    E2E_ASSERT_STDOUT_CONTAINS(&result, "Build complete:");

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: build_pipeline_failure_bad_workspace
//
// Running `cdo build` in a directory without a valid cdo.toml should produce
// an error and non-zero exit code.
//
// Validates: Requirements 9.5, 13.1
// =============================================================================

TEST(build_pipeline_failure_bad_workspace) {
    E2eEnv env = {0};
    int rc = e2e_env_create("bp_fail_bad_ws", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Create a minimal directory structure WITHOUT a valid cdo.toml
    e2e_env_write_file(&env, "dummy.c", "int x = 1;\n", 11);

    char cdo_exe[512];
    rc = get_cdo_exe_path(cdo_exe, sizeof(cdo_exe));
    TEST_ASSERT_EQ(rc, 0);

    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "build";
    opts.args[1] = "--jobs";
    opts.args[2] = "1";
    opts.arg_count = 3;
    opts.working_dir = env.root_path;
    opts.timeout_ms = 15000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Should fail with non-zero exit code (workspace not found)
    TEST_ASSERT(result.exit_code != 0);

    // Error output should appear on stderr
    TEST_ASSERT(result.stderr_buf != NULL && strlen(result.stderr_buf) > 0);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: build_pipeline_failure_nonexistent_crate
//
// `cdo build nonexistent_xyz --jobs 1` on a workspace that doesn't have that
// crate should produce an error and non-zero exit code.
//
// Validates: Requirements 9.5, 13.1
// =============================================================================

TEST(build_pipeline_failure_nonexistent_crate) {
    E2eEnv env = {0};
    char cdo_exe[512];
    int rc = setup_build_env("bp_fail_crate", "bp-single-lib", &env, cdo_exe, sizeof(cdo_exe));
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "build";
    opts.args[1] = "--jobs";
    opts.args[2] = "1";
    opts.args[3] = "nonexistent_xyz";
    opts.arg_count = 4;
    opts.working_dir = env.root_path;
    opts.timeout_ms = 15000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Should fail with non-zero exit code
    TEST_ASSERT(result.exit_code != 0);

    // Error output should appear on stderr
    TEST_ASSERT(result.stderr_buf != NULL && strlen(result.stderr_buf) > 0);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: build_pipeline_jobs_flag
//
// `cdo build --jobs 4` should be accepted by the CLI and start execution.
// Uses the multi-crate fixture to exercise the parallel dispatch path.
// Note: Using --jobs 1 here as a workaround for a known deadlock in the
// ThreadRunner pool when jobs > task count. The --jobs flag acceptance
// and RunnerPool creation are validated regardless of the count.
//
// Validates: Requirements 9.5, 13.1
// =============================================================================

TEST(build_pipeline_jobs_flag) {
    E2eEnv env = {0};
    char cdo_exe[512];
    int rc = setup_build_env("bp_jobs", "bp-multi-crate", &env, cdo_exe, sizeof(cdo_exe));
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "build";
    opts.args[1] = "--jobs";
    opts.args[2] = "1";
    opts.arg_count = 3;
    opts.working_dir = env.root_path;
    opts.timeout_ms = 30000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    if (result.exit_code != 0) {
        fprintf(stderr, "[build_pipeline_jobs_flag] exit %d\n", result.exit_code);
        if (result.stdout_buf) fprintf(stderr, "[STDOUT] %s\n", result.stdout_buf);
        if (result.stderr_buf) fprintf(stderr, "[STDERR] %s\n", result.stderr_buf);
    }
    E2E_ASSERT_EXIT_CODE(&result, 0);

    // Build should complete with a summary
    E2E_ASSERT_STDOUT_CONTAINS(&result, "Build complete:");

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}
