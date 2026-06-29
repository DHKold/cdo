// crates/cdo_pbt/src/unit/test_deps.c
// Unit tests for Deps module: metadata detection and lock file read/write
#include "cdo_ut.h"
#include "model/deps.h"
#include "pal/pal.h"

// --- Helper: unique temp dir base ---
// Uses a fixed prefix under the current working directory to avoid conflicts.
#define TEST_DEPS_TMP_BASE "__test_deps_tmp__"

static void cleanup_test_dir(void) {
    pal_rmdir_r(TEST_DEPS_TMP_BASE);
}

// --- dep_detect_metadata ---

/* Requirement 9.1: dir with cdo-package.toml → DEP_META_CDO_NATIVE */
TEST(dep_detect_metadata_cdo_native) {
    cleanup_test_dir();

    // Create temp dir with cdo-package.toml in the root
    char dep_dir[512];
    if (pal_path_join(dep_dir, sizeof(dep_dir), TEST_DEPS_TMP_BASE, "native_dep") != 0)
        return 1;
    TEST_ASSERT_EQ(pal_mkdir_p(dep_dir), 0);

    char toml_path[512];
    if (pal_path_join(toml_path, sizeof(toml_path), dep_dir, "cdo-package.toml") != 0)
        return 1;
    TEST_ASSERT_EQ(pal_file_write(toml_path, "[package]\nname = \"test\"\n", 23), 0);

    DepMetadataKind kind = dep_detect_metadata(dep_dir);
    TEST_ASSERT_EQ(kind, DEP_META_CDO_NATIVE);

    cleanup_test_dir();
    return 0;
}

/* Requirement 9.2: dir with lib/cmake/FooConfig.cmake → DEP_META_CMAKE */
TEST(dep_detect_metadata_cmake) {
    cleanup_test_dir();

    // Create temp dir with lib/cmake/FooConfig.cmake
    char dep_dir[512];
    if (pal_path_join(dep_dir, sizeof(dep_dir), TEST_DEPS_TMP_BASE, "cmake_dep") != 0)
        return 1;
    TEST_ASSERT_EQ(pal_mkdir_p(dep_dir), 0);

    char cmake_dir[512];
    if (pal_path_join(cmake_dir, sizeof(cmake_dir), dep_dir, "lib/cmake") != 0)
        return 1;
    TEST_ASSERT_EQ(pal_mkdir_p(cmake_dir), 0);

    char cmake_file[512];
    if (pal_path_join(cmake_file, sizeof(cmake_file), cmake_dir, "FooConfig.cmake") != 0)
        return 1;
    TEST_ASSERT_EQ(pal_file_write(cmake_file, "# cmake config\n", 16), 0);

    DepMetadataKind kind = dep_detect_metadata(dep_dir);
    TEST_ASSERT_EQ(kind, DEP_META_CMAKE);

    cleanup_test_dir();
    return 0;
}

/* Requirement 9.3: dir with lib/pkgconfig/foo.pc → DEP_META_PKGCONFIG */
TEST(dep_detect_metadata_pkgconfig) {
    cleanup_test_dir();

    // Create temp dir with lib/pkgconfig/foo.pc
    char dep_dir[512];
    if (pal_path_join(dep_dir, sizeof(dep_dir), TEST_DEPS_TMP_BASE, "pc_dep") != 0)
        return 1;
    TEST_ASSERT_EQ(pal_mkdir_p(dep_dir), 0);

    char pc_dir[512];
    if (pal_path_join(pc_dir, sizeof(pc_dir), dep_dir, "lib/pkgconfig") != 0)
        return 1;
    TEST_ASSERT_EQ(pal_mkdir_p(pc_dir), 0);

    char pc_file[512];
    if (pal_path_join(pc_file, sizeof(pc_file), pc_dir, "foo.pc") != 0)
        return 1;
    TEST_ASSERT_EQ(pal_file_write(pc_file, "prefix=/usr\n", 12), 0);

    DepMetadataKind kind = dep_detect_metadata(dep_dir);
    TEST_ASSERT_EQ(kind, DEP_META_PKGCONFIG);

    cleanup_test_dir();
    return 0;
}

/* Requirement 9.4: empty dir → DEP_META_NONE */
TEST(dep_detect_metadata_empty) {
    cleanup_test_dir();

    // Create an empty temp dir
    char dep_dir[512];
    if (pal_path_join(dep_dir, sizeof(dep_dir), TEST_DEPS_TMP_BASE, "empty_dep") != 0)
        return 1;
    TEST_ASSERT_EQ(pal_mkdir_p(dep_dir), 0);

    DepMetadataKind kind = dep_detect_metadata(dep_dir);
    TEST_ASSERT_EQ(kind, DEP_META_NONE);

    cleanup_test_dir();
    return 0;
}

// --- dep_lock_write / dep_lock_read round trip ---

/* Requirements 9.5, 9.6: write DepSpec array then read back → equivalent entries */
TEST(dep_lock_write_read_roundtrip) {
    cleanup_test_dir();
    TEST_ASSERT_EQ(pal_mkdir_p(TEST_DEPS_TMP_BASE), 0);

    char lock_path[512];
    if (pal_path_join(lock_path, sizeof(lock_path), TEST_DEPS_TMP_BASE, "test.lock") != 0)
        return 1;

    // Create sample DepSpec entries
    DepSpec specs[2];
    memset(specs, 0, sizeof(specs));

    strcpy(specs[0].name, "libfoo");
    strcpy(specs[0].version, "1.2.3");
    specs[0].source = DEP_REGISTRY;
    strcpy(specs[0].url, "https://registry.example.com/libfoo-1.2.3.tar.gz");
    strcpy(specs[0].checksum, "sha256:abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    specs[0].metadata_kind = DEP_META_CMAKE;

    strcpy(specs[1].name, "libbar");
    strcpy(specs[1].version, "0.5.0");
    specs[1].source = DEP_REGISTRY;
    strcpy(specs[1].url, "https://registry.example.com/libbar-0.5.0.tar.gz");
    strcpy(specs[1].checksum, "sha256:1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    specs[1].metadata_kind = DEP_META_PKGCONFIG;

    // Write lock file
    int rc = dep_lock_write(lock_path, specs, 2);
    TEST_ASSERT_EQ(rc, 0);

    // Read it back
    DepSpec* read_specs = NULL;
    int read_count = 0;
    rc = dep_lock_read(lock_path, &read_specs, &read_count);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(read_count, 2);
    TEST_ASSERT(read_specs != NULL);

    // Verify first entry
    TEST_ASSERT_STR_EQ(read_specs[0].name, "libfoo");
    TEST_ASSERT_STR_EQ(read_specs[0].version, "1.2.3");
    TEST_ASSERT_STR_EQ(read_specs[0].checksum, "sha256:abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    TEST_ASSERT_EQ(read_specs[0].metadata_kind, DEP_META_CMAKE);

    // Verify second entry
    TEST_ASSERT_STR_EQ(read_specs[1].name, "libbar");
    TEST_ASSERT_STR_EQ(read_specs[1].version, "0.5.0");
    TEST_ASSERT_STR_EQ(read_specs[1].checksum, "sha256:1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    TEST_ASSERT_EQ(read_specs[1].metadata_kind, DEP_META_PKGCONFIG);

    // Cleanup
    free(read_specs);
    cleanup_test_dir();
    return 0;
}
