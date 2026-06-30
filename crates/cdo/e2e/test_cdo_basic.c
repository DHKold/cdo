/**
 * test_cdo_basic.c - Basic end-to-end test for the CDo build tool.
 *
 * Validates: Requirements 1.1, 1.3, 2.1, 2.2, 9.1, 9.2
 *
 * This test spawns cdo.exe against a minimal workspace fixture and verifies
 * that the build command succeeds and produces expected output.
 */

#include "cdo_e2e.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>

// =============================================================================
// Helper: Derive the workspace root path from __FILE__
//
// __FILE__ resolves to something like:
//   C:/Workspace/.../crates/cdo/e2e/test_cdo_basic.c
//
// We need the workspace root which is 3 levels up from the cdo crate dir:
//   crates/cdo/e2e/test_cdo_basic.c -> crates/cdo/ -> crates/ -> workspace root
// =============================================================================

static int get_workspace_root(char* buf, size_t buf_size) {
    // Use __FILE__ to derive the crate path
    // __FILE__ = .../crates/cdo/e2e/test_cdo_basic.c
    // Crate root = .../crates/cdo/
    // Workspace root = .../
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s", __FILE__);
    pal_path_normalize(file_path);

    // Walk up from __FILE__: e2e/ -> cdo/ -> crates/ -> workspace root
    // Find the last '/e2e/' in the path and cut before 'crates/cdo/e2e/'
    char* e2e_pos = strstr(file_path, "/crates/cdo/e2e/");
    if (e2e_pos == NULL) {
        // Fallback: try backslash variant
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

    // Find /e2e/ and cut there to get crate root: .../crates/cdo
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

// =============================================================================
// Test: cdo_build_minimal_workspace
//
// Copies the "minimal-ws" fixture into an isolated temp dir, spawns cdo.exe
// with "build" in that workspace, and asserts that the build succeeds.
//
// Validates: Requirements 1.1, 1.3, 2.1, 9.1, 9.2
// =============================================================================

TEST(cdo_build_minimal_workspace) {
    // --- Setup ---
    E2eEnv env = {0};
    int rc = e2e_env_create("cdo_build_minimal", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Set crate path so fixture resolution works
    char crate_path[512];
    rc = get_crate_path(crate_path, sizeof(crate_path));
    TEST_ASSERT_EQ(rc, 0);

    rc = e2e_env_set_crate_path(&env, crate_path);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Copy the minimal-ws fixture into the test environment
    rc = e2e_fixture_copy(&env, "minimal-ws");
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Derive the cdo.exe path (at workspace root)
    char ws_root[512];
    rc = get_workspace_root(ws_root, sizeof(ws_root));
    TEST_ASSERT_EQ(rc, 0);

    char cdo_exe[512];
    snprintf(cdo_exe, sizeof(cdo_exe), "%s/cdo.exe", ws_root);

    // Verify cdo.exe exists
    TEST_ASSERT_EQ(pal_path_exists(cdo_exe), 0);

    // Add vendored tools to PATH so cdo.exe can find the compiler in the fixture workspace
    char tools_path[512];
    snprintf(tools_path, sizeof(tools_path), "%s/.cdo/tools/w64devkit/bin", ws_root);
    const char* existing_path = getenv("PATH");
    char new_path[4096];
    snprintf(new_path, sizeof(new_path), "%s;%s", tools_path, existing_path ? existing_path : "");
    e2e_env_setvar(&env, "PATH", new_path);

    // --- Execute: spawn cdo.exe build in the fixture workspace ---
    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "build";
    opts.arg_count = 1;
    opts.working_dir = env.root_path;
    opts.timeout_ms = 60000; // 60 seconds should be plenty for a minimal build

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // --- Assert ---
    // Log subprocess output on failure so root cause is visible in test logs
    if (result.exit_code != 0) {
        fprintf(stderr, "[cdo_build_minimal_workspace] cdo.exe exited %d\n", result.exit_code);
        if (result.stdout_buf) fprintf(stderr, "[STDOUT] %s\n", result.stdout_buf);
        if (result.stderr_buf) fprintf(stderr, "[STDERR] %s\n", result.stderr_buf);
    }
    E2E_ASSERT_EXIT_CODE(&result, 0);

    // stdout should contain "Build completed" or "Compiling" indicating work was done
    // On the first build, all source files need compilation
    TEST_ASSERT(result.stdout_buf != NULL);
    bool has_build_output = (strstr(result.stdout_buf, "Compiling") != NULL) || (strstr(result.stdout_buf, "Build completed") != NULL);
    if (!has_build_output) {
        // Also check stderr since log output may go there
        if (result.stderr_buf != NULL) {
            has_build_output = (strstr(result.stderr_buf, "Compiling") != NULL) || (strstr(result.stderr_buf, "Build completed") != NULL);
        }
    }
    TEST_ASSERT(has_build_output);

    // --- Cleanup ---
    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// Test: cdo_build_minimal_workspace_produces_exe
//
// After building the minimal-ws fixture, verifies that the expected executable
// artifact exists in the build output directory.
//
// Validates: Requirements 1.1, 1.3, 2.2
// =============================================================================

TEST(cdo_build_minimal_workspace_produces_exe) {
    // --- Setup ---
    E2eEnv env = {0};
    int rc = e2e_env_create("cdo_build_produces_exe", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    char crate_path[512];
    rc = get_crate_path(crate_path, sizeof(crate_path));
    TEST_ASSERT_EQ(rc, 0);

    rc = e2e_env_set_crate_path(&env, crate_path);
    TEST_ASSERT_EQ(rc, E2E_OK);

    rc = e2e_fixture_copy(&env, "minimal-ws");
    TEST_ASSERT_EQ(rc, E2E_OK);

    char ws_root[512];
    rc = get_workspace_root(ws_root, sizeof(ws_root));
    TEST_ASSERT_EQ(rc, 0);

    char cdo_exe[512];
    snprintf(cdo_exe, sizeof(cdo_exe), "%s/cdo.exe", ws_root);

    // Add vendored tools to PATH so cdo.exe can find the compiler
    char tools_path[512];
    snprintf(tools_path, sizeof(tools_path), "%s/.cdo/tools/w64devkit/bin", ws_root);
    const char* existing_path = getenv("PATH");
    char new_path[4096];
    snprintf(new_path, sizeof(new_path), "%s;%s", tools_path, existing_path ? existing_path : "");
    e2e_env_setvar(&env, "PATH", new_path);

    // --- Execute: build ---
    E2eSpawnOpts opts = {0};
    opts.executable = cdo_exe;
    opts.args[0] = "build";
    opts.arg_count = 1;
    opts.working_dir = env.root_path;
    opts.timeout_ms = 60000;

    E2eSpawnResult result = {0};
    rc = e2e_spawn(&env, &opts, &result);
    TEST_ASSERT_EQ(rc, E2E_OK);

    if (result.exit_code != 0) {
        fprintf(stderr, "[cdo_build_minimal_workspace_produces_exe] cdo.exe exited %d\n", result.exit_code);
        if (result.stdout_buf) fprintf(stderr, "[STDOUT] %s\n", result.stdout_buf);
        if (result.stderr_buf) fprintf(stderr, "[STDERR] %s\n", result.stderr_buf);
    }
    E2E_ASSERT_EXIT_CODE(&result, 0);

    // --- Assert: the hello.exe artifact should exist ---
    char exe_path[512];
    snprintf(exe_path, sizeof(exe_path), "%s/build/debug/hello/hello.exe", env.root_path);
    E2E_ASSERT_FILE_EXISTS(exe_path);

    // --- Cleanup ---
    e2e_spawn_result_free(&result);
    e2e_env_destroy(&env);
    return 0;
}
