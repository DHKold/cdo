/**
 * test_e2e_spawn.c - Unit tests for e2e subprocess execution functions.
 *
 * Tests: e2e_spawn (stdout/stderr capture, exit code, timeout, env var merging,
 *        working directory override, spawn failure), e2e_spawn_result_free.
 *
 * Requirements validated: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 5.7
 */

#include "cdo_e2e.h"

// =============================================================================
// spawn_captures_stdout - Spawn echo, verify stdout_buf contains output
// Validates: Requirement 5.2
// =============================================================================

TEST(spawn_captures_stdout) {
    E2eEnv env = {0};
    int rc = e2e_env_create("spawn_stdout", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = "cmd.exe";
    opts.args[0] = "/C";
    opts.args[1] = "echo hello_spawn_test";
    opts.arg_count = 2;
    opts.timeout_ms = 10000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT(result.stdout_buf != NULL);
    TEST_ASSERT(strstr(result.stdout_buf, "hello_spawn_test") != NULL);
    TEST_ASSERT_EQ(result.timed_out, false);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// spawn_captures_stderr - Spawn echo to stderr, verify stderr_buf
// Validates: Requirement 5.2
// =============================================================================

TEST(spawn_captures_stderr) {
    E2eEnv env = {0};
    int rc = e2e_env_create("spawn_stderr", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = "cmd.exe";
    opts.args[0] = "/C";
    opts.args[1] = "echo stderr_output 1>&2";
    opts.arg_count = 2;
    opts.timeout_ms = 10000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT(result.stderr_buf != NULL);
    TEST_ASSERT(strstr(result.stderr_buf, "stderr_output") != NULL);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// spawn_captures_exit_code - Spawn "exit 42", verify exit_code == 42
// Validates: Requirement 5.2
// =============================================================================

TEST(spawn_captures_exit_code) {
    E2eEnv env = {0};
    int rc = e2e_env_create("spawn_exit_code", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = "cmd.exe";
    opts.args[0] = "/C";
    opts.args[1] = "exit 42";
    opts.arg_count = 2;
    opts.timeout_ms = 10000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT_EQ(result.exit_code, 42);
    TEST_ASSERT_EQ(result.timed_out, false);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// spawn_timeout_terminates - Spawn a long process with short timeout,
//                            verify timed_out == true
// Validates: Requirement 5.3
// =============================================================================

TEST(spawn_timeout_terminates) {
    E2eEnv env = {0};
    int rc = e2e_env_create("spawn_timeout", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = "cmd.exe";
    opts.args[0] = "/C";
    opts.args[1] = "ping -n 30 127.0.0.1 >nul";
    opts.arg_count = 2;
    opts.timeout_ms = 200; // Very short timeout to ensure it triggers

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT_EQ(result.timed_out, true);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// spawn_env_vars_merged - Set env var via e2e_env_setvar, spawn cmd that echoes
//                         the variable, verify stdout contains value
// Validates: Requirement 5.1
// =============================================================================

TEST(spawn_env_vars_merged) {
    E2eEnv env = {0};
    int rc = e2e_env_create("spawn_env_merged", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    rc = e2e_env_setvar(&env, "CDO_E2E_TEST_VAR", "env_level_value");
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = "cmd.exe";
    opts.args[0] = "/C";
    opts.args[1] = "echo %CDO_E2E_TEST_VAR%";
    opts.arg_count = 2;
    opts.timeout_ms = 10000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT(result.stdout_buf != NULL);
    TEST_ASSERT(strstr(result.stdout_buf, "env_level_value") != NULL);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// spawn_extra_env_merged - Pass extra_env to spawn opts, verify subprocess sees it
// Validates: Requirement 5.1
// =============================================================================

TEST(spawn_extra_env_merged) {
    E2eEnv env = {0};
    int rc = e2e_env_create("spawn_extra_env", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eEnvVar extra[1];
    memset(extra, 0, sizeof(extra));
    snprintf(extra[0].key, sizeof(extra[0].key), "CDO_E2E_EXTRA_VAR");
    snprintf(extra[0].value, sizeof(extra[0].value), "extra_val_123");

    E2eSpawnOpts opts = {0};
    opts.executable = "cmd.exe";
    opts.args[0] = "/C";
    opts.args[1] = "echo %CDO_E2E_EXTRA_VAR%";
    opts.arg_count = 2;
    opts.timeout_ms = 10000;
    opts.extra_env = extra;
    opts.extra_env_count = 1;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT(result.stdout_buf != NULL);
    TEST_ASSERT(strstr(result.stdout_buf, "extra_val_123") != NULL);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// spawn_working_dir_override - Set working_dir to a known path, spawn "cmd /C cd",
//                              verify output contains that path
// Validates: Requirement 5.6
// =============================================================================

TEST(spawn_working_dir_override) {
    E2eEnv env = {0};
    int rc = e2e_env_create("spawn_workdir", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Create a subdirectory within the test environment to use as working dir
    rc = e2e_env_mkdir(&env, "subdir");
    TEST_ASSERT_EQ(rc, E2E_OK);

    char workdir[512];
    snprintf(workdir, sizeof(workdir), "%s/subdir", env.root_path);

    E2eSpawnOpts opts = {0};
    opts.executable = "cmd.exe";
    opts.args[0] = "/C";
    opts.args[1] = "cd";
    opts.arg_count = 2;
    opts.timeout_ms = 10000;
    opts.working_dir = workdir;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT(result.stdout_buf != NULL);
    // On Windows, cd output uses backslashes, so check for "subdir" substring
    TEST_ASSERT(strstr(result.stdout_buf, "subdir") != NULL);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// spawn_failure_invalid_exe - Spawn non-existent executable,
//                             verify returns E2E_ERR_SPAWN
// Validates: Requirement 5.5
// =============================================================================

TEST(spawn_failure_invalid_exe) {
    E2eEnv env = {0};
    int rc = e2e_env_create("spawn_fail_exe", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = "C:\\nonexistent_path_xyz\\no_such_program_12345.exe";
    opts.args[0] = "--help";
    opts.arg_count = 1;
    opts.timeout_ms = 5000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_ERR_SPAWN);
    // error_desc should have some description of the failure
    TEST_ASSERT(strlen(result.error_desc) > 0);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// spawn_result_free_null_buffers - Verify e2e_spawn_result_free doesn't crash
//                                  when stdout_buf and stderr_buf are NULL
// Validates: Requirement 5.7
// =============================================================================

TEST(spawn_result_free_null_buffers) {
    E2eSpawnResult result = {0};
    result.exit_code = 0;
    result.stdout_buf = NULL;
    result.stderr_buf = NULL;
    result.timed_out = false;
    result.error_desc[0] = '\0';

    // This should not crash
    e2e_spawn_result_free(&result);

    // Verify the struct is still in a sane state after free
    TEST_ASSERT_NULL(result.stdout_buf);
    TEST_ASSERT_NULL(result.stderr_buf);
    return 0;
}

// =============================================================================
// spawn_default_working_dir - Don't set working_dir, verify uses env->root_path
// Validates: Requirement 5.6
// =============================================================================

TEST(spawn_default_working_dir) {
    E2eEnv env = {0};
    int rc = e2e_env_create("spawn_default_wd", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    E2eSpawnOpts opts = {0};
    opts.executable = "cmd.exe";
    opts.args[0] = "/C";
    opts.args[1] = "cd";
    opts.arg_count = 2;
    opts.timeout_ms = 10000;
    opts.working_dir = NULL; // Should default to env->root_path

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT(result.stdout_buf != NULL);

    // The output of 'cd' should contain the env root path (possibly with
    // backslashes on Windows). Extract the last component of root_path to check.
    // env.root_path uses forward slashes from PAL normalization, but cmd.exe
    // outputs backslashes. Check that the test name fragment appears in output.
    TEST_ASSERT(strstr(result.stdout_buf, "spawn_default_wd") != NULL);

    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}
