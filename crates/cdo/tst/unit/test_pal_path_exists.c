// crates/cdo/tst/unit/test_pal_path_exists.c
// Unit tests for pal_path_exists return code convention fix.
// Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5
#include "cdo_ut.h"
#include "pal/pal.h"

// --- NULL input ---

TEST(pal_path_exists_null_returns_not_found) {
    int rc = pal_path_exists(NULL);
    TEST_ASSERT_EQ(rc, PAL_ERR_NOT_FOUND);
    return 0;
}

// --- Empty string input ---

TEST(pal_path_exists_empty_string_returns_not_found) {
    int rc = pal_path_exists("");
    TEST_ASSERT_EQ(rc, PAL_ERR_NOT_FOUND);
    return 0;
}

// --- Existing file returns PAL_OK ---

TEST(pal_path_exists_existing_file_returns_ok) {
    // cdo.toml exists at the workspace root (cwd when tests run)
    int rc = pal_path_exists("cdo.toml");
    TEST_ASSERT_EQ(rc, PAL_OK);
    return 0;
}

// --- Existing directory returns PAL_OK ---

TEST(pal_path_exists_existing_directory_returns_ok) {
    // "crates" directory exists at the workspace root
    int rc = pal_path_exists("crates");
    TEST_ASSERT_EQ(rc, PAL_OK);
    return 0;
}

// --- Non-existent path returns PAL_ERR_NOT_FOUND ---

TEST(pal_path_exists_nonexistent_path_returns_not_found) {
    int rc = pal_path_exists("__completely_nonexistent_path_xyz_98765__");
    TEST_ASSERT_EQ(rc, PAL_ERR_NOT_FOUND);
    return 0;
}
