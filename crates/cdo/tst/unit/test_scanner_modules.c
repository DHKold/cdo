// crates/cdo/tst/unit/test_scanner_modules.c
// Unit tests for scanner_scan_modules detecting res/ and shd/ module directories.
// Validates: Requirements 1.1, 1.2, 4.1, 4.2
#include "cdo_ut.h"
#include "core/scanner.h"
#include "core/workspace.h"
#include "pal/pal.h"

#include <string.h>
#include <stdio.h>

// --- Helper: create a temporary crate directory for module scanning tests ---

typedef struct {
    char root[512];
} TempModuleCrate;

static int temp_module_crate_create(TempModuleCrate* tc, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(tc->root, sizeof(tc->root), "%s/cdo_test_scanner_modules_%s", tmp, suffix);
    return pal_mkdir_p(tc->root);
}

static void temp_module_crate_destroy(TempModuleCrate* tc) {
    pal_rmdir_r(tc->root);
}

static int temp_module_crate_add_file(TempModuleCrate* tc, const char* rel_path, const char* content) {
    char full[1024];
    if (pal_path_join(full, sizeof(full), tc->root, rel_path) != 0) return 1;
    pal_path_normalize(full);

    // Ensure parent directory exists
    char dir[1024];
    strncpy(dir, full, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char* last_sep = strrchr(dir, '/');
    if (last_sep) {
        *last_sep = '\0';
        if (pal_mkdir_p(dir) != PAL_OK) return 1;
    }

    return pal_file_write(full, content, strlen(content));
}

static void crate_init(Crate* crate) {
    memset(crate, 0, sizeof(Crate));
}

static void crate_cleanup(Crate* crate) {
    for (int i = 0; i < MODULE_KIND_COUNT; i++) {
        if (crate->modules[i].sources.paths) {
            filelist_free(&crate->modules[i].sources);
        }
    }
}

// --- Test: res/ directory is detected as MODULE_RES ---

TEST(scanner_modules_res_dir_detected) {
    TempModuleCrate tc;
    TEST_ASSERT_EQ(temp_module_crate_create(&tc, "res_detected"), PAL_OK);

    // Create a res/ directory with a file (need at least lib/ or exe/ for scanner to succeed)
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "res/config.toml", "[app]\nname = \"test\"\n"), 0);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(crate.modules[MODULE_RES].present == true);
    TEST_ASSERT(crate.has_res == true);

    crate_cleanup(&crate);
    temp_module_crate_destroy(&tc);
    return 0;
}

// --- Test: shd/ directory is detected as MODULE_SHD ---

TEST(scanner_modules_shd_dir_detected) {
    TempModuleCrate tc;
    TEST_ASSERT_EQ(temp_module_crate_create(&tc, "shd_detected"), PAL_OK);

    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "shd/vertex.hlsl", "float4 main() : SV_POSITION { return 0; }\n"), 0);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(crate.modules[MODULE_SHD].present == true);
    TEST_ASSERT(crate.has_shd == true);

    crate_cleanup(&crate);
    temp_module_crate_destroy(&tc);
    return 0;
}

// --- Test: res/ scanner finds all file types (not just .c/.cpp) ---

TEST(scanner_modules_res_finds_all_file_types) {
    TempModuleCrate tc;
    TEST_ASSERT_EQ(temp_module_crate_create(&tc, "res_all_types"), PAL_OK);

    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);

    // Add various file types to res/
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "res/config.toml", "key = \"val\"\n"), 0);
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "res/data.json", "{}\n"), 0);
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "res/readme.txt", "hello\n"), 0);
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "res/assets/texture.png", "PNG_DATA\n"), 0);
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "res/scripts/init.lua", "print('hi')\n"), 0);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(crate.modules[MODULE_RES].present == true);
    // All 5 files should be found regardless of extension
    TEST_ASSERT_EQ(crate.modules[MODULE_RES].sources.count, 5);

    crate_cleanup(&crate);
    temp_module_crate_destroy(&tc);
    return 0;
}

// --- Test: shd/ scanner finds only .hlsl files ---

TEST(scanner_modules_shd_finds_only_hlsl) {
    TempModuleCrate tc;
    TEST_ASSERT_EQ(temp_module_crate_create(&tc, "shd_hlsl_only"), PAL_OK);

    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);

    // Add .hlsl files and non-hlsl files to shd/
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "shd/vertex.hlsl", "float4 VS() : SV_POSITION { return 0; }\n"), 0);
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "shd/pixel.hlsl", "float4 PS() : SV_TARGET { return 0; }\n"), 0);
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "shd/compute.hlsl", "[numthreads(1,1,1)] void CS() {}\n"), 0);
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "shd/readme.txt", "shader docs\n"), 0);
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "shd/common.h", "#pragma once\n"), 0);
    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "shd/notes.md", "# notes\n"), 0);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(crate.modules[MODULE_SHD].present == true);
    // Only the 3 .hlsl files should be found
    TEST_ASSERT_EQ(crate.modules[MODULE_SHD].sources.count, 3);

    crate_cleanup(&crate);
    temp_module_crate_destroy(&tc);
    return 0;
}

// --- Test: empty res/ and shd/ directories are detected but have empty file lists ---

TEST(scanner_modules_empty_res_detected_with_empty_list) {
    TempModuleCrate tc;
    TEST_ASSERT_EQ(temp_module_crate_create(&tc, "empty_res"), PAL_OK);

    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);

    // Create empty res/ directory (mkdir_p on the directory itself)
    char res_dir[1024];
    pal_path_join(res_dir, sizeof(res_dir), tc.root, "res");
    pal_path_normalize(res_dir);
    TEST_ASSERT_EQ(pal_mkdir_p(res_dir), PAL_OK);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(crate.modules[MODULE_RES].present == true);
    TEST_ASSERT(crate.has_res == true);
    TEST_ASSERT_EQ(crate.modules[MODULE_RES].sources.count, 0);

    crate_cleanup(&crate);
    temp_module_crate_destroy(&tc);
    return 0;
}

TEST(scanner_modules_empty_shd_detected_with_empty_list) {
    TempModuleCrate tc;
    TEST_ASSERT_EQ(temp_module_crate_create(&tc, "empty_shd"), PAL_OK);

    TEST_ASSERT_EQ(temp_module_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);

    // Create empty shd/ directory
    char shd_dir[1024];
    pal_path_join(shd_dir, sizeof(shd_dir), tc.root, "shd");
    pal_path_normalize(shd_dir);
    TEST_ASSERT_EQ(pal_mkdir_p(shd_dir), PAL_OK);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(crate.modules[MODULE_SHD].present == true);
    TEST_ASSERT(crate.has_shd == true);
    TEST_ASSERT_EQ(crate.modules[MODULE_SHD].sources.count, 0);

    crate_cleanup(&crate);
    temp_module_crate_destroy(&tc);
    return 0;
}
