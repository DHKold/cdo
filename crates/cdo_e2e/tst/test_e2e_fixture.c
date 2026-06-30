/// @file test_e2e_fixture.c
/// @brief Unit tests for e2e_fixture_copy function.
/// Tests cover: basic multi-file fixture copy, empty directory preservation,
/// fixture not found error, name validation (length + chars), deep nesting,
/// crate path not set error, and multi-level nested fixture copy.
/// Requirements: 7.1-7.8

#include "cdo_e2e.h"
#include "cdo_ut.h"
#include "pal/pal.h"
#include <string.h>
#include <stdio.h>

// Helper: create a "fake crate" directory with e2e/fixtures/ structure.
// Returns 0 on success. Caller must clean up with pal_rmdir_r.
static int create_fake_crate(char* crate_root, size_t crate_root_size) {
    // Use e2e_env_create to get a unique temp dir as our fake crate root
    E2eEnv helper;
    int rc = e2e_env_create("fake_crate", &helper);
    if (rc != 0) return rc;

    // Copy the path out
    snprintf(crate_root, crate_root_size, "%s", helper.root_path);

    // Create the e2e/fixtures directory inside
    char fixtures_dir[512];
    pal_path_join(fixtures_dir, sizeof(fixtures_dir), crate_root, "e2e/fixtures");
    rc = pal_mkdir_p(fixtures_dir);

    // Destroy the helper env struct (but keep the directory)
    helper.keep_temps = true;
    e2e_env_destroy(&helper);

    return rc;
}

// Helper: create a file inside the fake crate's fixture tree
static int fixture_write_file(const char* crate_root, const char* fixture_name, const char* rel_path, const char* content) {
    char full_path[512];
    char fixture_base[512];
    pal_path_join(fixture_base, sizeof(fixture_base), crate_root, "e2e/fixtures");

    char fixture_dir[512];
    pal_path_join(fixture_dir, sizeof(fixture_dir), fixture_base, fixture_name);

    char file_path[512];
    pal_path_join(file_path, sizeof(file_path), fixture_dir, rel_path);

    // Ensure parent directory exists by finding last slash
    char parent[512];
    snprintf(parent, sizeof(parent), "%s", file_path);
    char* last_slash = strrchr(parent, '/');
    if (!last_slash) last_slash = strrchr(parent, '\\');
    if (last_slash) {
        *last_slash = '\0';
        int rc = pal_mkdir_p(parent);
        if (rc != 0) return rc;
    }

    return pal_file_write(file_path, content, strlen(content));
}

// Helper: create a directory inside the fake crate's fixture tree
static int fixture_mkdir(const char* crate_root, const char* fixture_name, const char* rel_path) {
    char fixture_base[512];
    pal_path_join(fixture_base, sizeof(fixture_base), crate_root, "e2e/fixtures");

    char fixture_dir[512];
    pal_path_join(fixture_dir, sizeof(fixture_dir), fixture_base, fixture_name);

    char dir_path[512];
    pal_path_join(dir_path, sizeof(dir_path), fixture_dir, rel_path);

    return pal_mkdir_p(dir_path);
}


// ============================================================================
// Test 1: fixture_copy_basic
// Create a fixture with 2 files and 1 subdir, copy it, verify files exist with correct content
// ============================================================================
TEST(fixture_copy_basic) {
    char crate_root[512];
    TEST_ASSERT(create_fake_crate(crate_root, sizeof(crate_root)) == 0);

    // Create fixture "basic-fixture" with files
    TEST_ASSERT(fixture_write_file(crate_root, "basic-fixture", "hello.txt", "Hello World") == 0);
    TEST_ASSERT(fixture_write_file(crate_root, "basic-fixture", "subdir/nested.txt", "Nested Content") == 0);

    // Create test environment and set crate path
    E2eEnv env;
    TEST_ASSERT(e2e_env_create("fixture_copy_basic", &env) == 0);
    TEST_ASSERT(e2e_env_set_crate_path(&env, crate_root) == 0);

    // Copy fixture
    int rc = e2e_fixture_copy(&env, "basic-fixture");
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Verify files were copied correctly
    char path_buf[512];
    pal_path_join(path_buf, sizeof(path_buf), env.root_path, "hello.txt");
    TEST_ASSERT(pal_path_exists(path_buf) == 0);

    char* content = NULL;
    size_t len = 0;
    TEST_ASSERT(pal_file_read(path_buf, &content, &len) == 0);
    TEST_ASSERT(strcmp(content, "Hello World") == 0);
    free(content);

    pal_path_join(path_buf, sizeof(path_buf), env.root_path, "subdir/nested.txt");
    TEST_ASSERT(pal_path_exists(path_buf) == 0);

    content = NULL;
    len = 0;
    TEST_ASSERT(pal_file_read(path_buf, &content, &len) == 0);
    TEST_ASSERT(strcmp(content, "Nested Content") == 0);
    free(content);

    // Verify subdirectory exists
    char subdir_path[512];
    pal_path_join(subdir_path, sizeof(subdir_path), env.root_path, "subdir");
    TEST_ASSERT(pal_path_exists(subdir_path) == 0);

    // Cleanup
    e2e_env_destroy(&env);
    pal_rmdir_r(crate_root);
    return 0;
}

// ============================================================================
// Test 2: fixture_copy_empty_dir
// Fixture with an empty subdirectory, verify it's preserved after copy
// ============================================================================
TEST(fixture_copy_empty_dir) {
    char crate_root[512];
    TEST_ASSERT(create_fake_crate(crate_root, sizeof(crate_root)) == 0);

    // Create fixture with a file and an empty subdir
    TEST_ASSERT(fixture_write_file(crate_root, "empty-dir-fixture", "readme.txt", "readme") == 0);
    TEST_ASSERT(fixture_mkdir(crate_root, "empty-dir-fixture", "empty-subdir") == 0);

    // Create test environment
    E2eEnv env;
    TEST_ASSERT(e2e_env_create("fixture_copy_empty_dir", &env) == 0);
    TEST_ASSERT(e2e_env_set_crate_path(&env, crate_root) == 0);

    // Copy fixture
    int rc = e2e_fixture_copy(&env, "empty-dir-fixture");
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Verify the file was copied
    char path_buf[512];
    pal_path_join(path_buf, sizeof(path_buf), env.root_path, "readme.txt");
    TEST_ASSERT(pal_path_exists(path_buf) == 0);

    // Verify the empty subdirectory was preserved
    pal_path_join(path_buf, sizeof(path_buf), env.root_path, "empty-subdir");
    TEST_ASSERT(pal_path_exists(path_buf) == 0);

    // Cleanup
    e2e_env_destroy(&env);
    pal_rmdir_r(crate_root);
    return 0;
}

// ============================================================================
// Test 3: fixture_not_found
// Request a fixture name that doesn't exist, expect E2E_ERR_NOT_FOUND
// ============================================================================
TEST(fixture_not_found) {
    char crate_root[512];
    TEST_ASSERT(create_fake_crate(crate_root, sizeof(crate_root)) == 0);

    E2eEnv env;
    TEST_ASSERT(e2e_env_create("fixture_not_found", &env) == 0);
    TEST_ASSERT(e2e_env_set_crate_path(&env, crate_root) == 0);

    // Try to copy a fixture that doesn't exist
    int rc = e2e_fixture_copy(&env, "nonexistent-fixture");
    TEST_ASSERT_EQ(rc, E2E_ERR_NOT_FOUND);

    // Cleanup
    e2e_env_destroy(&env);
    pal_rmdir_r(crate_root);
    return 0;
}

// ============================================================================
// Test 4: fixture_name_too_long
// Fixture name exceeding 64 chars should return E2E_ERR_INVALID
// ============================================================================
TEST(fixture_name_too_long) {
    char crate_root[512];
    TEST_ASSERT(create_fake_crate(crate_root, sizeof(crate_root)) == 0);

    E2eEnv env;
    TEST_ASSERT(e2e_env_create("fixture_name_too_long", &env) == 0);
    TEST_ASSERT(e2e_env_set_crate_path(&env, crate_root) == 0);

    // Create a name that's 65 characters (1 over limit)
    char long_name[66];
    memset(long_name, 'a', 65);
    long_name[65] = '\0';

    int rc = e2e_fixture_copy(&env, long_name);
    TEST_ASSERT_EQ(rc, E2E_ERR_INVALID);

    // Verify a name exactly at 64 chars is accepted (returns NOT_FOUND since fixture doesn't exist)
    char exact_name[65];
    memset(exact_name, 'b', 64);
    exact_name[64] = '\0';

    rc = e2e_fixture_copy(&env, exact_name);
    TEST_ASSERT_EQ(rc, E2E_ERR_NOT_FOUND); // Valid name, but fixture doesn't exist

    // Cleanup
    e2e_env_destroy(&env);
    pal_rmdir_r(crate_root);
    return 0;
}

// ============================================================================
// Test 5: fixture_name_invalid_chars
// Fixture name with spaces or special chars should return E2E_ERR_INVALID
// ============================================================================
TEST(fixture_name_invalid_chars) {
    char crate_root[512];
    TEST_ASSERT(create_fake_crate(crate_root, sizeof(crate_root)) == 0);

    E2eEnv env;
    TEST_ASSERT(e2e_env_create("fixture_name_invalid_chars", &env) == 0);
    TEST_ASSERT(e2e_env_set_crate_path(&env, crate_root) == 0);

    // Names with spaces
    TEST_ASSERT_EQ(e2e_fixture_copy(&env, "has space"), E2E_ERR_INVALID);

    // Names with dots
    TEST_ASSERT_EQ(e2e_fixture_copy(&env, "has.dot"), E2E_ERR_INVALID);

    // Names with slashes
    TEST_ASSERT_EQ(e2e_fixture_copy(&env, "has/slash"), E2E_ERR_INVALID);

    // Names with backslashes
    TEST_ASSERT_EQ(e2e_fixture_copy(&env, "has\\back"), E2E_ERR_INVALID);

    // Names with special characters
    TEST_ASSERT_EQ(e2e_fixture_copy(&env, "has@symbol"), E2E_ERR_INVALID);
    TEST_ASSERT_EQ(e2e_fixture_copy(&env, "has!bang"), E2E_ERR_INVALID);

    // Empty name
    TEST_ASSERT_EQ(e2e_fixture_copy(&env, ""), E2E_ERR_INVALID);

    // NULL name
    TEST_ASSERT_EQ(e2e_fixture_copy(&env, NULL), E2E_ERR_INVALID);

    // Valid names with hyphens and underscores (should NOT return INVALID)
    int rc = e2e_fixture_copy(&env, "valid-name_123");
    TEST_ASSERT(rc != E2E_ERR_INVALID); // Should be NOT_FOUND since fixture doesn't exist

    // Cleanup
    e2e_env_destroy(&env);
    pal_rmdir_r(crate_root);
    return 0;
}


// ============================================================================
// Test 6: fixture_deep_nesting
// Create fixture with directories 16 levels deep, verify copy works
// ============================================================================
TEST(fixture_deep_nesting) {
    char crate_root[512];
    TEST_ASSERT(create_fake_crate(crate_root, sizeof(crate_root)) == 0);

    // Build a path 16 levels deep: level1/level2/.../level16/deep.txt
    char deep_rel_path[512] = "";
    for (int i = 1; i <= 16; i++) {
        char level[32];
        snprintf(level, sizeof(level), "%slevel%d", (i > 1) ? "/" : "", i);
        strncat(deep_rel_path, level, sizeof(deep_rel_path) - strlen(deep_rel_path) - 1);
    }
    // Append filename
    strncat(deep_rel_path, "/deep.txt", sizeof(deep_rel_path) - strlen(deep_rel_path) - 1);

    TEST_ASSERT(fixture_write_file(crate_root, "deep-fixture", deep_rel_path, "deep content") == 0);

    // Create test environment
    E2eEnv env;
    TEST_ASSERT(e2e_env_create("fixture_deep_nesting", &env) == 0);
    TEST_ASSERT(e2e_env_set_crate_path(&env, crate_root) == 0);

    // Copy fixture
    int rc = e2e_fixture_copy(&env, "deep-fixture");
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Verify the deeply nested file was copied
    char verify_path[512];
    pal_path_join(verify_path, sizeof(verify_path), env.root_path, deep_rel_path);
    TEST_ASSERT(pal_path_exists(verify_path) == 0);

    char* content = NULL;
    size_t len = 0;
    TEST_ASSERT(pal_file_read(verify_path, &content, &len) == 0);
    TEST_ASSERT(strcmp(content, "deep content") == 0);
    free(content);

    // Cleanup
    e2e_env_destroy(&env);
    pal_rmdir_r(crate_root);
    return 0;
}

// ============================================================================
// Test 7: fixture_crate_path_not_set
// Don't call e2e_env_set_crate_path, expect E2E_ERR_INVALID
// ============================================================================
TEST(fixture_crate_path_not_set) {
    E2eEnv env;
    TEST_ASSERT(e2e_env_create("fixture_crate_path_not_set", &env) == 0);

    // Do NOT set crate_path - it should be empty/zeroed from e2e_env_create
    int rc = e2e_fixture_copy(&env, "some-fixture");
    TEST_ASSERT_EQ(rc, E2E_ERR_INVALID);

    // Cleanup
    e2e_env_destroy(&env);
    return 0;
}

// ============================================================================
// Test 8: fixture_copy_multilevel
// Fixture with nested subdirectories and multiple files at different levels
// ============================================================================
TEST(fixture_copy_multilevel) {
    char crate_root[512];
    TEST_ASSERT(create_fake_crate(crate_root, sizeof(crate_root)) == 0);

    // Create a multi-level fixture structure:
    // multilevel/
    //   root.txt
    //   src/
    //     main.c
    //     utils/
    //       helper.c
    //       helper.h
    //   config/
    //     settings.toml
    //   docs/
    //     (empty dir)
    TEST_ASSERT(fixture_write_file(crate_root, "multilevel", "root.txt", "root content") == 0);
    TEST_ASSERT(fixture_write_file(crate_root, "multilevel", "src/main.c", "int main() { return 0; }") == 0);
    TEST_ASSERT(fixture_write_file(crate_root, "multilevel", "src/utils/helper.c", "void help() {}") == 0);
    TEST_ASSERT(fixture_write_file(crate_root, "multilevel", "src/utils/helper.h", "#pragma once") == 0);
    TEST_ASSERT(fixture_write_file(crate_root, "multilevel", "config/settings.toml", "[settings]\nkey = \"value\"") == 0);
    TEST_ASSERT(fixture_mkdir(crate_root, "multilevel", "docs") == 0);

    // Create test environment
    E2eEnv env;
    TEST_ASSERT(e2e_env_create("fixture_copy_multilevel", &env) == 0);
    TEST_ASSERT(e2e_env_set_crate_path(&env, crate_root) == 0);

    // Copy fixture
    int rc = e2e_fixture_copy(&env, "multilevel");
    TEST_ASSERT_EQ(rc, E2E_OK);

    // Verify all files exist with correct content
    char path_buf[512];
    char* content = NULL;
    size_t len = 0;

    // root.txt
    pal_path_join(path_buf, sizeof(path_buf), env.root_path, "root.txt");
    TEST_ASSERT(pal_path_exists(path_buf) == 0);
    TEST_ASSERT(pal_file_read(path_buf, &content, &len) == 0);
    TEST_ASSERT(strcmp(content, "root content") == 0);
    free(content); content = NULL;

    // src/main.c
    pal_path_join(path_buf, sizeof(path_buf), env.root_path, "src/main.c");
    TEST_ASSERT(pal_path_exists(path_buf) == 0);
    TEST_ASSERT(pal_file_read(path_buf, &content, &len) == 0);
    TEST_ASSERT(strcmp(content, "int main() { return 0; }") == 0);
    free(content); content = NULL;

    // src/utils/helper.c
    pal_path_join(path_buf, sizeof(path_buf), env.root_path, "src/utils/helper.c");
    TEST_ASSERT(pal_path_exists(path_buf) == 0);
    TEST_ASSERT(pal_file_read(path_buf, &content, &len) == 0);
    TEST_ASSERT(strcmp(content, "void help() {}") == 0);
    free(content); content = NULL;

    // src/utils/helper.h
    pal_path_join(path_buf, sizeof(path_buf), env.root_path, "src/utils/helper.h");
    TEST_ASSERT(pal_path_exists(path_buf) == 0);
    TEST_ASSERT(pal_file_read(path_buf, &content, &len) == 0);
    TEST_ASSERT(strcmp(content, "#pragma once") == 0);
    free(content); content = NULL;

    // config/settings.toml
    pal_path_join(path_buf, sizeof(path_buf), env.root_path, "config/settings.toml");
    TEST_ASSERT(pal_path_exists(path_buf) == 0);
    TEST_ASSERT(pal_file_read(path_buf, &content, &len) == 0);
    TEST_ASSERT(strcmp(content, "[settings]\nkey = \"value\"") == 0);
    free(content); content = NULL;

    // docs/ (empty dir should exist)
    pal_path_join(path_buf, sizeof(path_buf), env.root_path, "docs");
    TEST_ASSERT(pal_path_exists(path_buf) == 0);

    // Cleanup
    e2e_env_destroy(&env);
    pal_rmdir_r(crate_root);
    return 0;
}
