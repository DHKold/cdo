// crates/cdo_pbt/src/unit/test_scanner.c
// Unit tests for Scanner module (scanner_scan_sources, scanner_scan_headers, filelist_free)
#include "cdo_ut.h"
#include "model/scanner.h"
#include "pal/pal.h"

#include <string.h>

// --- Helper: create a temporary crate directory with src/ and files ---

typedef struct {
    char root[512];
} TempCrate;

static int temp_crate_create(TempCrate* tc, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(tc->root, sizeof(tc->root), "%s/cdo_test_scanner_%s", tmp, suffix);
    return pal_mkdir_p(tc->root);
}

static void temp_crate_destroy(TempCrate* tc) {
    pal_rmdir_r(tc->root);
}

static int temp_crate_add_file(TempCrate* tc, const char* rel_path, const char* content) {
    char full[1024];
    if (pal_path_join(full, sizeof(full), tc->root, rel_path) != 0) return 1;
    pal_path_normalize(full);

    // Ensure parent directory exists by extracting the directory portion
    char dir[1024];
    strncpy(dir, full, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    // Find last slash
    char* last_sep = strrchr(dir, '/');
    if (last_sep) {
        *last_sep = '\0';
        if (pal_mkdir_p(dir) != PAL_OK) return 1;
    }

    return pal_file_write(full, content, strlen(content));
}

// --- Tests ---

TEST(scanner_scan_sources_finds_c_files) {
    // Create a temp crate with .c and .cpp files in src/
    TempCrate tc;
    TEST_ASSERT_EQ(temp_crate_create(&tc, "finds_c"), PAL_OK);

    TEST_ASSERT_EQ(temp_crate_add_file(&tc, "src/main.c", "int main(){return 0;}\n"), 0);
    TEST_ASSERT_EQ(temp_crate_add_file(&tc, "src/util.cpp", "void util(){}\n"), 0);
    TEST_ASSERT_EQ(temp_crate_add_file(&tc, "src/sub/deep.c", "void deep(){}\n"), 0);

    FileList fl = {0};
    int rc = scanner_scan_sources(tc.root, NULL, 0, &fl);
    TEST_ASSERT_EQ(rc, 0);

    // Should find all 3 source files
    TEST_ASSERT(fl.count >= 3);

    filelist_free(&fl);
    temp_crate_destroy(&tc);
    return 0;
}

TEST(scanner_scan_sources_excludes_patterns) {
    // Create a temp crate with files, some under a "vendor" subdirectory
    TempCrate tc;
    TEST_ASSERT_EQ(temp_crate_create(&tc, "excludes"), PAL_OK);

    TEST_ASSERT_EQ(temp_crate_add_file(&tc, "src/app.c", "void app(){}\n"), 0);
    TEST_ASSERT_EQ(temp_crate_add_file(&tc, "src/vendor/lib.c", "void lib(){}\n"), 0);
    TEST_ASSERT_EQ(temp_crate_add_file(&tc, "src/vendor/ext.cpp", "void ext(){}\n"), 0);

    // Exclude everything under src/vendor/
    const char* excludes[] = { "src/vendor/**" };
    FileList fl = {0};
    int rc = scanner_scan_sources(tc.root, excludes, 1, &fl);
    TEST_ASSERT_EQ(rc, 0);

    // Only app.c should be found (vendor files excluded)
    TEST_ASSERT_EQ(fl.count, 1);

    filelist_free(&fl);
    temp_crate_destroy(&tc);
    return 0;
}

TEST(scanner_scan_headers_finds_h_files) {
    // Create a temp crate with .h files in include/
    TempCrate tc;
    TEST_ASSERT_EQ(temp_crate_create(&tc, "headers"), PAL_OK);

    TEST_ASSERT_EQ(temp_crate_add_file(&tc, "include/api.h", "#pragma once\n"), 0);
    TEST_ASSERT_EQ(temp_crate_add_file(&tc, "include/types.hpp", "#pragma once\n"), 0);
    TEST_ASSERT_EQ(temp_crate_add_file(&tc, "include/sub/internal.h", "#pragma once\n"), 0);

    FileList fl = {0};
    int rc = scanner_scan_headers(tc.root, &fl);
    TEST_ASSERT_EQ(rc, 0);

    // Should find all 3 header files
    TEST_ASSERT(fl.count >= 3);

    filelist_free(&fl);
    temp_crate_destroy(&tc);
    return 0;
}

TEST(scanner_scan_sources_no_src_dir) {
    // Create a temp crate root with NO src/ directory
    TempCrate tc;
    TEST_ASSERT_EQ(temp_crate_create(&tc, "nosrc"), PAL_OK);

    // Don't create any src/ subdirectory — just the root exists
    FileList fl = {0};
    int rc = scanner_scan_sources(tc.root, NULL, 0, &fl);

    // Should return 0 (success) with an empty FileList, per scanner.c behavior
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(fl.count, 0);

    filelist_free(&fl);
    temp_crate_destroy(&tc);
    return 0;
}
