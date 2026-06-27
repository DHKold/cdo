// crates/cdo_pbt/src/unit/test_pal.c
// Unit tests for PAL path utilities (pal_path_exists, pal_path_join, pal_path_normalize, pal_path_ext)
#include "cdo_ut.h"
#include "pal/pal.h"

// --- pal_path_exists ---

TEST(pal_path_exists_returns_ok_for_existing) {
    // cdo.toml exists at the workspace root (cwd when tests run)
    int rc = pal_path_exists("cdo.toml");
    TEST_ASSERT_EQ(rc, PAL_OK);
    return 0;
}

TEST(pal_path_exists_returns_nonzero_for_missing) {
    int rc = pal_path_exists("__nonexistent_file_xyz_12345__.txt");
    TEST_ASSERT(rc != 0);
    return 0;
}

// --- pal_path_join ---

TEST(pal_path_join_basic) {
    char buf[64];
    int rc = pal_path_join(buf, sizeof(buf), "foo", "bar");
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_STR_EQ(buf, "foo/bar");
    return 0;
}

TEST(pal_path_join_no_duplicate_sep) {
    char buf[64];
    int rc = pal_path_join(buf, sizeof(buf), "foo/", "/bar");
    TEST_ASSERT_EQ(rc, 0);
    // Should not produce "foo//bar" — no duplicate separators
    TEST_ASSERT(strstr(buf, "//") == NULL);
    // Result should contain "foo" and "bar" joined by a single slash
    TEST_ASSERT(strstr(buf, "foo/bar") != NULL || strstr(buf, "foo/bar") != NULL);
    return 0;
}

TEST(pal_path_join_buffer_overflow) {
    char buf[4]; // Too small for "foo/bar" (7 chars + null)
    int rc = pal_path_join(buf, sizeof(buf), "foo", "bar");
    TEST_ASSERT(rc != 0);
    return 0;
}

// --- pal_path_normalize ---

TEST(pal_path_normalize_backslash) {
    char path[] = "foo\\bar\\baz";
    pal_path_normalize(path);
    TEST_ASSERT_STR_EQ(path, "foo/bar/baz");
    return 0;
}

TEST(pal_path_normalize_consecutive) {
    char path[] = "foo//bar///baz";
    pal_path_normalize(path);
    TEST_ASSERT_STR_EQ(path, "foo/bar/baz");
    return 0;
}

// --- pal_path_ext ---

TEST(pal_path_ext_basic) {
    const char* ext = pal_path_ext("file.txt");
    TEST_ASSERT_STR_EQ(ext, ".txt");
    return 0;
}

TEST(pal_path_ext_no_extension) {
    const char* ext = pal_path_ext("Makefile");
    TEST_ASSERT_STR_EQ(ext, "");
    return 0;
}

TEST(pal_path_ext_multi_dot) {
    const char* ext = pal_path_ext("archive.tar.gz");
    TEST_ASSERT_STR_EQ(ext, ".gz");
    return 0;
}
