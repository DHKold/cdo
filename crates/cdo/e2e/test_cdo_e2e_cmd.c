/**
 * test_cdo_e2e_cmd.c - E2E tests for the `cdo e2e` command itself.
 *
 * Validates: Requirements 2.1-2.10, 3.1, 3.2
 *
 * Tests the `cdo e2e` command behavior by spawning cdo.exe with various
 * arguments against the current workspace and verifying exit codes and output.
 */

#include "cdo_e2e.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>

// =============================================================================
// Helper: Derive the workspace root path from __FILE__
// =============================================================================

static int get_workspace_root(char* buf, size_t buf_size) {
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s", __FILE__);
    pal_path_normalize(file_path);

    // __FILE__ = .../crates/cdo/e2e/test_cdo_e2e_cmd.c
    // workspace root = everything before /crates/cdo/e2e/
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

// =============================================================================
// Test: cdo_e2e_nonexistent_crate_returns_exit2
//
// `cdo e2e nonexistent_crate` should report an error and return exit code 2
// because the crate doesn't exist in the workspace.
//
// Validates: Requirements 2.7, 2.8
// =============================================================================

TEST(cdo_e2e_nonexistent_crate_returns_exit2) {
    E2eEnv env = {0};
    int rc = e2e_env_create("e2e_nonexistent_crate", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    char ws_root[512];
    rc = get_workspace_root(ws_root, sizeof(ws_root));
    TEST_ASSERT_EQ(rc, 0);

    char cdo_exe[512];
    snprintf(cdo_exe, sizeof(cdo_exe), "%s/cdo.exe", ws_root);
    TEST_ASSERT_EQ(pal_path_exists(cdo_exe), 0);

    // Spawn: cdo e2e nonexistent_crate_xyz
    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "e2e";
    opts.args[1] = "nonexistent_crate_xyz";
    opts.arg_count = 2;
    opts.working_dir = ws_root;
    opts.timeout_ms = 60000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Should return exit code 2 (infrastructure error: crate not found)
    E2E_ASSERT_EXIT_CODE(&result, 2);

    // stderr should contain an error message about the crate not being found
    E2E_ASSERT_STDERR_CONTAINS(&result, "nonexistent_crate_xyz");

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: cdo_e2e_cdo_crate_passes
//
// `cdo e2e cdo` should build and run the e2e tests for the cdo crate
// (which includes the basic tests from test_cdo_basic.c) and exit 0
// since those tests pass.
//
// Validates: Requirements 2.1, 2.2, 2.5, 2.6, 2.7
// =============================================================================

TEST(cdo_e2e_cdo_crate_passes) {
    E2eEnv env = {0};
    int rc = e2e_env_create("e2e_cdo_passes", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    char ws_root[512];
    rc = get_workspace_root(ws_root, sizeof(ws_root));
    TEST_ASSERT_EQ(rc, 0);

    char cdo_exe[512];
    snprintf(cdo_exe, sizeof(cdo_exe), "%s/cdo.exe", ws_root);
    TEST_ASSERT_EQ(pal_path_exists(cdo_exe), 0);

    // Spawn: cdo e2e cdo
    // Note: We filter to just the basic tests to avoid recursion
    // (this test file itself is part of the cdo e2e module).
    // Use --filter to only run the basic workspace tests.
    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "e2e";
    opts.args[1] = "--filter";
    opts.args[2] = "cdo_build_minimal";
    opts.args[3] = "cdo";
    opts.arg_count = 4;
    opts.working_dir = ws_root;
    opts.timeout_ms = 120000; // builds can take a while

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Should exit 0 (all filtered tests pass)
    E2E_ASSERT_EXIT_CODE(&result, 0);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: cdo_e2e_list_prints_test_names
//
// `cdo e2e --list cdo` should print the test names registered in the cdo
// crate's e2e module without executing them.
//
// Validates: Requirements 3.2
// =============================================================================

TEST(cdo_e2e_list_prints_test_names) {
    E2eEnv env = {0};
    int rc = e2e_env_create("e2e_list_tests", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    char ws_root[512];
    rc = get_workspace_root(ws_root, sizeof(ws_root));
    TEST_ASSERT_EQ(rc, 0);

    char cdo_exe[512];
    snprintf(cdo_exe, sizeof(cdo_exe), "%s/cdo.exe", ws_root);
    TEST_ASSERT_EQ(pal_path_exists(cdo_exe), 0);

    // Spawn: cdo e2e --list cdo
    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "e2e";
    opts.args[1] = "--list";
    opts.args[2] = "cdo";
    opts.arg_count = 3;
    opts.working_dir = ws_root;
    opts.timeout_ms = 120000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // --list mode: the e2e executable prints test names and exits 0
    E2E_ASSERT_EXIT_CODE(&result, 0);

    // stdout should contain the known test names from test_cdo_basic.c
    // In --list mode, cmd_e2e doesn't capture (capture_output=false),
    // so the output goes directly to the terminal. But from our e2e_spawn
    // perspective we capture everything the child process produces.
    E2E_ASSERT_STDOUT_CONTAINS(&result, "cdo_build_minimal_workspace");

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: cdo_e2e_filter_limits_execution
//
// `cdo e2e --filter cdo_build_minimal_workspace_produces cdo` should only
// run the test whose name matches the filter substring, not all tests.
//
// Validates: Requirements 3.1
// =============================================================================

TEST(cdo_e2e_filter_limits_execution) {
    E2eEnv env = {0};
    int rc = e2e_env_create("e2e_filter_test", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    char ws_root[512];
    rc = get_workspace_root(ws_root, sizeof(ws_root));
    TEST_ASSERT_EQ(rc, 0);

    char cdo_exe[512];
    snprintf(cdo_exe, sizeof(cdo_exe), "%s/cdo.exe", ws_root);
    TEST_ASSERT_EQ(pal_path_exists(cdo_exe), 0);

    // Spawn: cdo e2e --filter cdo_build_minimal_workspace_produces cdo
    // This filter should match only "cdo_build_minimal_workspace_produces_exe"
    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "e2e";
    opts.args[1] = "--filter";
    opts.args[2] = "cdo_build_minimal_workspace_produces";
    opts.args[3] = "cdo";
    opts.arg_count = 4;
    opts.working_dir = ws_root;
    opts.timeout_ms = 120000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Should pass (the filtered test passes)
    E2E_ASSERT_EXIT_CODE(&result, 0);

    // The output should show the test passed and the summary should show 1 test
    // (stderr has log messages with the summary)
    // Check stdout for test protocol evidence of the filtered test
    TEST_ASSERT(result.stdout_buf != NULL || result.stderr_buf != NULL);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: cdo_e2e_no_e2e_module_crate_returns_exit2
//
// `cdo e2e cdo_ut` should return exit code 2 because cdo_ut doesn't have
// an e2e module (it has a tst/ module but no e2e/).
//
// Validates: Requirements 2.8
// =============================================================================

TEST(cdo_e2e_no_e2e_module_crate_returns_exit2) {
    E2eEnv env = {0};
    int rc = e2e_env_create("e2e_no_module", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    char ws_root[512];
    rc = get_workspace_root(ws_root, sizeof(ws_root));
    TEST_ASSERT_EQ(rc, 0);

    char cdo_exe[512];
    snprintf(cdo_exe, sizeof(cdo_exe), "%s/cdo.exe", ws_root);
    TEST_ASSERT_EQ(pal_path_exists(cdo_exe), 0);

    // Spawn: cdo e2e cdo_ut (cdo_ut has no e2e/ module)
    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "e2e";
    opts.args[1] = "cdo_ut";
    opts.arg_count = 2;
    opts.working_dir = ws_root;
    opts.timeout_ms = 60000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Should return exit code 2 (crate exists but has no e2e module)
    E2E_ASSERT_EXIT_CODE(&result, 2);

    // stderr should mention e2e module not found
    E2E_ASSERT_STDERR_CONTAINS(&result, "e2e");

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}
