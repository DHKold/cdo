/**
 * test_e2e_assert.c — Unit tests for E2E assertion macros.
 *
 * Tests each assertion macro for:
 *   - Pass case (macro does not trigger, test continues)
 *   - Fail case (macro triggers return 1, verified via helper function)
 *
 * For file-based assertions, uses PAL functions directly to create temp files.
 *
 * Requirements validated: 6.1–6.9
 */

#include "cdo_e2e.h"

#ifdef _WIN32
#include <process.h>
#define get_pid() _getpid()
#else
#include <unistd.h>
#define get_pid() getpid()
#endif

// ============================================================================
// Helper functions for testing failure cases
//
// The E2E_ASSERT_* macros call `return 1` on failure, so we cannot invoke them
// directly in a TEST that expects to pass. Instead, static helpers wrap the
// macro — if the macro fires, the helper returns 1; otherwise it returns 0.
// ============================================================================

static int helper_assert_exit_code(E2eSpawnResult* result, int expected) {
    E2E_ASSERT_EXIT_CODE(result, expected);
    return 0;
}

static int helper_assert_stdout_contains(E2eSpawnResult* result, const char* substring) {
    E2E_ASSERT_STDOUT_CONTAINS(result, substring);
    return 0;
}

static int helper_assert_stderr_contains(E2eSpawnResult* result, const char* substring) {
    E2E_ASSERT_STDERR_CONTAINS(result, substring);
    return 0;
}

static int helper_assert_file_exists(const char* path) {
    E2E_ASSERT_FILE_EXISTS(path);
    return 0;
}

static int helper_assert_file_not_exists(const char* path) {
    E2E_ASSERT_FILE_NOT_EXISTS(path);
    return 0;
}

static int helper_assert_file_contains(const char* filepath, const char* substring) {
    E2E_ASSERT_FILE_CONTAINS(filepath, substring);
    return 0;
}

// ============================================================================
// E2E_ASSERT_EXIT_CODE — pass when codes match
// ============================================================================

TEST(assert_exit_code_pass) {
    E2eSpawnResult result = {0};
    result.exit_code = 0;
    E2E_ASSERT_EXIT_CODE(&result, 0);

    result.exit_code = 42;
    E2E_ASSERT_EXIT_CODE(&result, 42);

    result.exit_code = -1;
    E2E_ASSERT_EXIT_CODE(&result, -1);
    return 0;
}

// ============================================================================
// E2E_ASSERT_EXIT_CODE — fail when codes differ
// ============================================================================

TEST(assert_exit_code_fail) {
    E2eSpawnResult result = {0};
    result.exit_code = 1;
    int rc = helper_assert_exit_code(&result, 42);
    TEST_ASSERT_EQ(rc, 1);

    result.exit_code = 0;
    rc = helper_assert_exit_code(&result, 1);
    TEST_ASSERT_EQ(rc, 1);
    return 0;
}

// ============================================================================
// E2E_ASSERT_STDOUT_CONTAINS — pass when substring found
// ============================================================================

TEST(assert_stdout_contains_pass) {
    char stdout_data[] = "hello world from cdo";
    E2eSpawnResult result = {0};
    result.stdout_buf = stdout_data;

    E2E_ASSERT_STDOUT_CONTAINS(&result, "hello");
    E2E_ASSERT_STDOUT_CONTAINS(&result, "world");
    E2E_ASSERT_STDOUT_CONTAINS(&result, "from cdo");
    return 0;
}

// ============================================================================
// E2E_ASSERT_STDOUT_CONTAINS — fail when substring not found
// ============================================================================

TEST(assert_stdout_contains_fail) {
    char stdout_data[] = "hello world";
    E2eSpawnResult result = {0};
    result.stdout_buf = stdout_data;

    int rc = helper_assert_stdout_contains(&result, "goodbye");
    TEST_ASSERT_EQ(rc, 1);
    return 0;
}

// ============================================================================
// E2E_ASSERT_STDOUT_CONTAINS — fail when stdout_buf is NULL
// ============================================================================

TEST(assert_stdout_contains_null_buf) {
    E2eSpawnResult result = {0};
    result.stdout_buf = NULL;

    int rc = helper_assert_stdout_contains(&result, "anything");
    TEST_ASSERT_EQ(rc, 1);
    return 0;
}

// ============================================================================
// E2E_ASSERT_STDERR_CONTAINS — pass when substring found
// ============================================================================

TEST(assert_stderr_contains_pass) {
    char stderr_data[] = "error: something went wrong";
    E2eSpawnResult result = {0};
    result.stderr_buf = stderr_data;

    E2E_ASSERT_STDERR_CONTAINS(&result, "error:");
    E2E_ASSERT_STDERR_CONTAINS(&result, "something");
    E2E_ASSERT_STDERR_CONTAINS(&result, "went wrong");
    return 0;
}

// ============================================================================
// E2E_ASSERT_STDERR_CONTAINS — fail when substring not found
// ============================================================================

TEST(assert_stderr_contains_fail) {
    char stderr_data[] = "error: file not found";
    E2eSpawnResult result = {0};
    result.stderr_buf = stderr_data;

    int rc = helper_assert_stderr_contains(&result, "permission denied");
    TEST_ASSERT_EQ(rc, 1);
    return 0;
}

// ============================================================================
// E2E_ASSERT_STDERR_CONTAINS — fail when stderr_buf is NULL
// ============================================================================

TEST(assert_stderr_contains_null_buf) {
    E2eSpawnResult result = {0};
    result.stderr_buf = NULL;

    int rc = helper_assert_stderr_contains(&result, "anything");
    TEST_ASSERT_EQ(rc, 1);
    return 0;
}

// ============================================================================
// E2E_ASSERT_FILE_EXISTS — pass when file exists
// ============================================================================

TEST(assert_file_exists_pass) {
    // Create a temp directory and file using PAL directly
    char tmp_dir[260];
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/cdo_e2e_test_assert_exists_%d", getenv("TEMP") ? getenv("TEMP") : "/tmp", (int)get_pid());
    pal_mkdir_p(tmp_dir);

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/testfile.txt", tmp_dir);
    const char* content = "test content";
    pal_file_write(filepath, content, strlen(content));

    E2E_ASSERT_FILE_EXISTS(filepath);

    // Cleanup
    pal_rmdir_r(tmp_dir);
    return 0;
}

// ============================================================================
// E2E_ASSERT_FILE_EXISTS — fail when file does not exist
// ============================================================================

TEST(assert_file_exists_fail) {
    int rc = helper_assert_file_exists("/nonexistent/path/to/file_that_does_not_exist_12345.txt");
    TEST_ASSERT_EQ(rc, 1);
    return 0;
}

// ============================================================================
// E2E_ASSERT_FILE_NOT_EXISTS — pass when file does not exist
// ============================================================================

TEST(assert_file_not_exists_pass) {
    E2E_ASSERT_FILE_NOT_EXISTS("/nonexistent/path/to/file_that_does_not_exist_12345.txt");
    return 0;
}

// ============================================================================
// E2E_ASSERT_FILE_NOT_EXISTS — fail when file exists
// ============================================================================

TEST(assert_file_not_exists_fail) {
    // Create a temp directory and file using PAL directly
    char tmp_dir[260];
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/cdo_e2e_test_assert_notex_%d", getenv("TEMP") ? getenv("TEMP") : "/tmp", (int)get_pid());
    pal_mkdir_p(tmp_dir);

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/present.txt", tmp_dir);
    const char* content = "exists";
    pal_file_write(filepath, content, strlen(content));

    int rc = helper_assert_file_not_exists(filepath);
    TEST_ASSERT_EQ(rc, 1);

    // Cleanup
    pal_rmdir_r(tmp_dir);
    return 0;
}

// ============================================================================
// E2E_ASSERT_FILE_CONTAINS — pass when content matches
// ============================================================================

TEST(assert_file_contains_pass) {
    // Create a temp directory and file using PAL directly
    char tmp_dir[260];
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/cdo_e2e_test_assert_fc_%d", getenv("TEMP") ? getenv("TEMP") : "/tmp", (int)get_pid());
    pal_mkdir_p(tmp_dir);

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/data.txt", tmp_dir);
    const char* content = "line1\nline2 with keyword\nline3";
    pal_file_write(filepath, content, strlen(content));

    E2E_ASSERT_FILE_CONTAINS(filepath, "keyword");
    E2E_ASSERT_FILE_CONTAINS(filepath, "line1");

    // Cleanup
    pal_rmdir_r(tmp_dir);
    return 0;
}

// ============================================================================
// E2E_ASSERT_FILE_CONTAINS — fail when content doesn't match
// ============================================================================

TEST(assert_file_contains_fail) {
    // Create a temp directory and file using PAL directly
    char tmp_dir[260];
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/cdo_e2e_test_assert_fcf_%d", getenv("TEMP") ? getenv("TEMP") : "/tmp", (int)get_pid());
    pal_mkdir_p(tmp_dir);

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/data.txt", tmp_dir);
    const char* content = "hello world";
    pal_file_write(filepath, content, strlen(content));

    int rc = helper_assert_file_contains(filepath, "goodbye");
    TEST_ASSERT_EQ(rc, 1);

    // Cleanup
    pal_rmdir_r(tmp_dir);
    return 0;
}

// ============================================================================
// E2E_ASSERT_FILE_CONTAINS — fail when file is unreadable/missing
// ============================================================================

TEST(assert_file_contains_unreadable) {
    int rc = helper_assert_file_contains("/nonexistent/path/no_such_file.txt", "anything");
    TEST_ASSERT_EQ(rc, 1);
    return 0;
}
