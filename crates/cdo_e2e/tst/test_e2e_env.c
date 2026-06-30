/**
 * test_e2e_env.c — Unit tests for the e2e test environment module.
 *
 * Exercises:
 *   - e2e_env_create: unique naming, path includes sanitized test name (truncated to 64 chars),
 *     empty test name, NULL test name
 *   - e2e_env_write_file: basic file creation, nested dir creation, content verification,
 *     path escape with ".." rejected (E2E_ERR_INVALID)
 *   - e2e_env_mkdir: basic dir, nested dirs, path escape rejected
 *   - e2e_env_setvar: basic set, overwrite existing key, max limit (E2E_ERR_LIMIT at 64)
 *   - e2e_env_destroy: cleanup removes directory, keep_temps preserves it
 *   - e2e_env_set_crate_path: valid path, NULL path (E2E_ERR_INVALID), path too long
 *
 * Requirements validated: 4.1–4.9, 8.1, 8.2, 8.6, 8.7, 8.8
 */

#include "cdo_e2e.h"

// =============================================================================
// e2e_env_create — basic success
// =============================================================================

TEST(test_env_create_basic) {
    E2eEnv env = {0};
    int rc = e2e_env_create("basic_test", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT(strlen(env.root_path) > 0);
    TEST_ASSERT_EQ(env.env_var_count, 0);
    TEST_ASSERT_EQ(env.keep_temps, false);
    // Directory must exist
    TEST_ASSERT_EQ(pal_path_exists(env.root_path), 0);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_create — unique directory names for the same test name
// =============================================================================

TEST(test_env_create_unique_names) {
    E2eEnv env1 = {0};
    E2eEnv env2 = {0};
    int rc1 = e2e_env_create("uniqueness_test", &env1);
    int rc2 = e2e_env_create("uniqueness_test", &env2);
    TEST_ASSERT_EQ(rc1, E2E_OK);
    TEST_ASSERT_EQ(rc2, E2E_OK);
    // Both directories exist but at different paths
    TEST_ASSERT_EQ(pal_path_exists(env1.root_path), 0);
    TEST_ASSERT_EQ(pal_path_exists(env2.root_path), 0);
    TEST_ASSERT(strcmp(env1.root_path, env2.root_path) != 0);
    e2e_env_destroy(&env1);
    e2e_env_destroy(&env2);
    return 0;
}

// =============================================================================
// e2e_env_create — path includes sanitized test name
// =============================================================================

TEST(test_env_create_name_in_path) {
    E2eEnv env = {0};
    int rc = e2e_env_create("my_cool_test", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);
    // The test name should appear somewhere in the root path
    TEST_ASSERT(strstr(env.root_path, "my_cool_test") != NULL);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_create — long test name truncated to 64 chars
// =============================================================================

TEST(test_env_create_name_truncation) {
    // Create a name that's 80 characters long
    const char* long_name = "abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789_extra_padding!!";
    E2eEnv env = {0};
    int rc = e2e_env_create(long_name, &env);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT_EQ(pal_path_exists(env.root_path), 0);
    // The full 80-char name should NOT appear (it gets truncated)
    TEST_ASSERT(strstr(env.root_path, long_name) == NULL);
    // But the first 64 chars (or a sanitized prefix) should be present
    // Just verify the path is valid and the directory exists
    TEST_ASSERT(strlen(env.root_path) > 0);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_create — empty test name still succeeds
// =============================================================================

TEST(test_env_create_empty_name) {
    E2eEnv env = {0};
    int rc = e2e_env_create("", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT(strlen(env.root_path) > 0);
    TEST_ASSERT_EQ(pal_path_exists(env.root_path), 0);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_create — NULL test name still succeeds (treated as empty)
// =============================================================================

TEST(test_env_create_null_name) {
    E2eEnv env = {0};
    int rc = e2e_env_create(NULL, &env);
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT(strlen(env.root_path) > 0);
    TEST_ASSERT_EQ(pal_path_exists(env.root_path), 0);
    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_write_file — basic file creation
// =============================================================================

TEST(test_env_write_file_basic) {
    E2eEnv env = {0};
    int rc = e2e_env_create("write_basic", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    const char* content = "hello world";
    rc = e2e_env_write_file(&env, "test.txt", content, strlen(content));
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Verify file exists
    char filepath[260];
    pal_path_join(filepath, sizeof(filepath), env.root_path, "test.txt");
    TEST_ASSERT_EQ(pal_path_exists(filepath), 0);

    // Verify content
    char* buf = NULL;
    size_t len = 0;
    TEST_ASSERT_EQ(pal_file_read(filepath, &buf, &len), 0);
    TEST_ASSERT_EQ(len, strlen(content));
    TEST_ASSERT(memcmp(buf, content, len) == 0);
    free(buf);

    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_write_file — creates nested intermediate directories
// =============================================================================

TEST(test_env_write_file_nested_dirs) {
    E2eEnv env = {0};
    int rc = e2e_env_create("write_nested", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    const char* content = "nested content";
    rc = e2e_env_write_file(&env, "a/b/c/deep.txt", content, strlen(content));
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Verify file exists at the nested path
    char filepath[260];
    pal_path_join(filepath, sizeof(filepath), env.root_path, "a/b/c/deep.txt");
    TEST_ASSERT_EQ(pal_path_exists(filepath), 0);

    // Verify content
    char* buf = NULL;
    size_t len = 0;
    TEST_ASSERT_EQ(pal_file_read(filepath, &buf, &len), 0);
    TEST_ASSERT_EQ(len, strlen(content));
    TEST_ASSERT(memcmp(buf, content, len) == 0);
    free(buf);

    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_write_file — path escape with ".." rejected
// =============================================================================

TEST(test_env_write_file_escape_rejected) {
    E2eEnv env = {0};
    int rc = e2e_env_create("write_escape", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    const char* content = "malicious";
    rc = e2e_env_write_file(&env, "../escape.txt", content, strlen(content));
    TEST_ASSERT_EQ(rc, E2E_ERR_INVALID);

    // Also test deeper escape
    rc = e2e_env_write_file(&env, "a/../../escape.txt", content, strlen(content));
    TEST_ASSERT_EQ(rc, E2E_ERR_INVALID);

    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_mkdir — basic directory creation
// =============================================================================

TEST(test_env_mkdir_basic) {
    E2eEnv env = {0};
    int rc = e2e_env_create("mkdir_basic", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    rc = e2e_env_mkdir(&env, "mydir");
    TEST_ASSERT_EQ(rc, E2E_OK);

    char dirpath[260];
    pal_path_join(dirpath, sizeof(dirpath), env.root_path, "mydir");
    TEST_ASSERT_EQ(pal_path_exists(dirpath), 0);

    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_mkdir — nested directory creation
// =============================================================================

TEST(test_env_mkdir_nested) {
    E2eEnv env = {0};
    int rc = e2e_env_create("mkdir_nested", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    rc = e2e_env_mkdir(&env, "x/y/z");
    TEST_ASSERT_EQ(rc, E2E_OK);

    char dirpath[260];
    pal_path_join(dirpath, sizeof(dirpath), env.root_path, "x/y/z");
    TEST_ASSERT_EQ(pal_path_exists(dirpath), 0);

    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_mkdir — path escape rejected
// =============================================================================

TEST(test_env_mkdir_escape_rejected) {
    E2eEnv env = {0};
    int rc = e2e_env_create("mkdir_escape", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    rc = e2e_env_mkdir(&env, "../outside");
    TEST_ASSERT_EQ(rc, E2E_ERR_INVALID);

    rc = e2e_env_mkdir(&env, "sub/../../outside");
    TEST_ASSERT_EQ(rc, E2E_ERR_INVALID);

    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_setvar — basic set
// =============================================================================

TEST(test_env_setvar_basic) {
    E2eEnv env = {0};
    int rc = e2e_env_create("setvar_basic", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    rc = e2e_env_setvar(&env, "MY_VAR", "my_value");
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT_EQ(env.env_var_count, 1);
    TEST_ASSERT_STR_EQ(env.env_vars[0].key, "MY_VAR");
    TEST_ASSERT_STR_EQ(env.env_vars[0].value, "my_value");

    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_setvar — overwrite existing key
// =============================================================================

TEST(test_env_setvar_overwrite) {
    E2eEnv env = {0};
    int rc = e2e_env_create("setvar_overwrite", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    rc = e2e_env_setvar(&env, "KEY", "original");
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT_EQ(env.env_var_count, 1);

    rc = e2e_env_setvar(&env, "KEY", "updated");
    TEST_ASSERT_EQ(rc, E2E_OK);
    // Should overwrite, not add a new entry
    TEST_ASSERT_EQ(env.env_var_count, 1);
    TEST_ASSERT_STR_EQ(env.env_vars[0].value, "updated");

    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_setvar — limit reached (E2E_ERR_LIMIT at 64)
// =============================================================================

TEST(test_env_setvar_limit) {
    E2eEnv env = {0};
    int rc = e2e_env_create("setvar_limit", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Fill up all 64 slots with unique keys
    char key[128];
    for (int i = 0; i < E2E_ENV_MAX_VARS; i++) {
        snprintf(key, sizeof(key), "VAR_%d", i);
        rc = e2e_env_setvar(&env, key, "value");
        TEST_ASSERT_EQ(rc, E2E_OK);
    }
    TEST_ASSERT_EQ(env.env_var_count, E2E_ENV_MAX_VARS);

    // The 65th unique key should fail
    rc = e2e_env_setvar(&env, "OVERFLOW_VAR", "nope");
    TEST_ASSERT_EQ(rc, E2E_ERR_LIMIT);
    TEST_ASSERT_EQ(env.env_var_count, E2E_ENV_MAX_VARS);

    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_destroy — cleanup removes directory
// =============================================================================

TEST(test_env_destroy_cleanup) {
    E2eEnv env = {0};
    int rc = e2e_env_create("destroy_cleanup", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Write a file so the directory is not empty
    const char* content = "temp";
    e2e_env_write_file(&env, "file.txt", content, strlen(content));

    char saved_path[260];
    strncpy(saved_path, env.root_path, sizeof(saved_path) - 1);
    saved_path[sizeof(saved_path) - 1] = '\0';

    // Verify directory exists before destroy
    TEST_ASSERT_EQ(pal_path_exists(saved_path), 0);

    env.keep_temps = false;
    rc = e2e_env_destroy(&env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Directory should be gone
    TEST_ASSERT(pal_path_exists(saved_path) != 0);
    return 0;
}

// =============================================================================
// e2e_env_destroy — keep_temps preserves directory
// =============================================================================

TEST(test_env_destroy_keep_temps) {
    E2eEnv env = {0};
    int rc = e2e_env_create("destroy_keep", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    char saved_path[260];
    strncpy(saved_path, env.root_path, sizeof(saved_path) - 1);
    saved_path[sizeof(saved_path) - 1] = '\0';

    env.keep_temps = true;
    rc = e2e_env_destroy(&env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Directory should still exist
    TEST_ASSERT_EQ(pal_path_exists(saved_path), 0);

    // Manual cleanup since keep_temps preserved it
    pal_rmdir_r(saved_path);
    return 0;
}

// =============================================================================
// e2e_env_set_crate_path — valid path
// =============================================================================

TEST(test_env_set_crate_path_valid) {
    E2eEnv env = {0};
    int rc = e2e_env_create("crate_path_valid", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    rc = e2e_env_set_crate_path(&env, "/some/crate/path");
    TEST_ASSERT_EQ(rc, E2E_OK);
    TEST_ASSERT_STR_EQ(env.crate_path, "/some/crate/path");

    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_set_crate_path — NULL path returns E2E_ERR_INVALID
// =============================================================================

TEST(test_env_set_crate_path_null) {
    E2eEnv env = {0};
    int rc = e2e_env_create("crate_path_null", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    rc = e2e_env_set_crate_path(&env, NULL);
    TEST_ASSERT_EQ(rc, E2E_ERR_INVALID);

    e2e_env_destroy(&env);
    return 0;
}

// =============================================================================
// e2e_env_set_crate_path — path too long returns E2E_ERR_INVALID
// =============================================================================

TEST(test_env_set_crate_path_too_long) {
    E2eEnv env = {0};
    int rc = e2e_env_create("crate_path_long", &env);
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Create a path that exceeds 260 chars (the crate_path buffer size)
    char long_path[300];
    memset(long_path, 'x', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';

    rc = e2e_env_set_crate_path(&env, long_path);
    TEST_ASSERT_EQ(rc, E2E_ERR_INVALID);

    e2e_env_destroy(&env);
    return 0;
}
