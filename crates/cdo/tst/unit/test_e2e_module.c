// crates/cdo/tst/unit/test_e2e_module.c
// Unit tests for MODULE_E2E scanner detection and artifact naming.
// Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.7, 7.7
#include "cdo_ut.h"
#include "model/scanner.h"
#include "model/module.h"
#include "model/workspace.h"
#include "pal/pal.h"

#include <string.h>
#include <stdio.h>

// --- Helper: create a temporary crate directory for e2e module scanning tests ---

typedef struct {
    char root[512];
} TempE2eCrate;

static int temp_e2e_crate_create(TempE2eCrate* tc, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(tc->root, sizeof(tc->root), "%s/cdo_test_e2e_module_%s", tmp, suffix);
    return pal_mkdir_p(tc->root);
}

static void temp_e2e_crate_destroy(TempE2eCrate* tc) {
    pal_rmdir_r(tc->root);
}

static int temp_e2e_crate_add_file(TempE2eCrate* tc, const char* rel_path, const char* content) {
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

// --- Test: MODULE_E2E enum value exists (compile-time check) ---
// This test simply references MODULE_E2E to verify the enum variant is defined.

TEST(e2e_module_enum_value_exists) {
    ModuleKind kind = MODULE_E2E;
    TEST_ASSERT_EQ((int)kind, (int)MODULE_E2E);
    // MODULE_E2E should be index 7 (after MODULE_SHD=6)
    TEST_ASSERT_EQ((int)MODULE_E2E, 7);
    // MODULE_KIND_COUNT should be updated to 8
    TEST_ASSERT_EQ(MODULE_KIND_COUNT, 8);
    return 0;
}

// --- Test: Scanner detects e2e/ directory with .c source files ---

TEST(e2e_module_scanner_detects_e2e_dir) {
    TempE2eCrate tc;
    TEST_ASSERT_EQ(temp_e2e_crate_create(&tc, "detect_e2e"), PAL_OK);

    // Need at least one other module for scanner to succeed
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);

    // Create e2e/ directory with a source file
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/test_basic.c", "#include \"cdo_ut.h\"\nTEST(basic) { return 0; }\n"), 0);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    // MODULE_E2E should be detected as present
    TEST_ASSERT(crate.modules[MODULE_E2E].present == true);

    // Should have discovered the test source file
    TEST_ASSERT(crate.modules[MODULE_E2E].sources.count >= 1);

    // Verify the source file is the one we created
    bool found_test_basic = false;
    for (int i = 0; i < crate.modules[MODULE_E2E].sources.count; i++) {
        if (strstr(crate.modules[MODULE_E2E].sources.paths[i], "test_basic.c") != NULL) {
            found_test_basic = true;
            break;
        }
    }
    TEST_ASSERT(found_test_basic);

    crate_cleanup(&crate);
    temp_e2e_crate_destroy(&tc);
    return 0;
}

// --- Test: Scanner detects e2e/ directory with .cpp source files ---

TEST(e2e_module_scanner_detects_cpp_sources) {
    TempE2eCrate tc;
    TEST_ASSERT_EQ(temp_e2e_crate_create(&tc, "detect_cpp"), PAL_OK);

    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/test_advanced.cpp", "#include \"cdo_ut.h\"\nTEST(adv) { return 0; }\n"), 0);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(crate.modules[MODULE_E2E].present == true);
    TEST_ASSERT(crate.modules[MODULE_E2E].sources.count >= 1);

    bool found_cpp = false;
    for (int i = 0; i < crate.modules[MODULE_E2E].sources.count; i++) {
        if (strstr(crate.modules[MODULE_E2E].sources.paths[i], "test_advanced.cpp") != NULL) {
            found_cpp = true;
            break;
        }
    }
    TEST_ASSERT(found_cpp);

    crate_cleanup(&crate);
    temp_e2e_crate_destroy(&tc);
    return 0;
}

// --- Test: Scanner excludes e2e/fixtures/ from source discovery ---

TEST(e2e_module_scanner_excludes_fixtures) {
    TempE2eCrate tc;
    TEST_ASSERT_EQ(temp_e2e_crate_create(&tc, "exclude_fixtures"), PAL_OK);

    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);

    // Source file outside fixtures/ - should be discovered
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/test_main.c", "int main(){return 0;}\n"), 0);

    // Source files inside fixtures/ - should be excluded
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/fixtures/basic-ws/cdo.toml", "[workspace]\nmembers=[]\n"), 0);
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/fixtures/basic-ws/src/main.c", "int main(){return 0;}\n"), 0);
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/fixtures/complex/lib/core.c", "void core(){}\n"), 0);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(crate.modules[MODULE_E2E].present == true);

    // Only the test_main.c should be in sources (fixtures/ excluded)
    TEST_ASSERT_EQ(crate.modules[MODULE_E2E].sources.count, 1);

    // Verify none of the fixture .c files appear in sources
    for (int i = 0; i < crate.modules[MODULE_E2E].sources.count; i++) {
        TEST_ASSERT(strstr(crate.modules[MODULE_E2E].sources.paths[i], "/fixtures/") == NULL);
    }

    crate_cleanup(&crate);
    temp_e2e_crate_destroy(&tc);
    return 0;
}

// --- Test: Scanner excludes deeply nested e2e/fixtures/ subdirectories ---

TEST(e2e_module_scanner_excludes_nested_fixtures) {
    TempE2eCrate tc;
    TEST_ASSERT_EQ(temp_e2e_crate_create(&tc, "nested_fixtures"), PAL_OK);

    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);

    // Source files outside fixtures/
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/test_one.c", "int one(){return 0;}\n"), 0);
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/helpers/util.c", "void util(){}\n"), 0);

    // Deeply nested fixture files - should all be excluded
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/fixtures/ws/crates/app/lib/app.c", "void app(){}\n"), 0);
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/fixtures/ws/crates/app/exe/main.c", "int main(){return 0;}\n"), 0);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(crate.modules[MODULE_E2E].present == true);

    // Only the two non-fixture .c files should be found
    TEST_ASSERT_EQ(crate.modules[MODULE_E2E].sources.count, 2);

    // Verify no fixture paths appear
    for (int i = 0; i < crate.modules[MODULE_E2E].sources.count; i++) {
        TEST_ASSERT(strstr(crate.modules[MODULE_E2E].sources.paths[i], "/fixtures/") == NULL);
    }

    crate_cleanup(&crate);
    temp_e2e_crate_destroy(&tc);
    return 0;
}

// --- Test: Scanner ignores e2e/ when it has no .c/.cpp files outside fixtures/ ---

TEST(e2e_module_scanner_ignores_empty_e2e) {
    TempE2eCrate tc;
    TEST_ASSERT_EQ(temp_e2e_crate_create(&tc, "empty_e2e"), PAL_OK);

    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);

    // e2e/ directory exists but only contains fixtures (no test sources outside)
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/fixtures/ws/main.c", "int main(){return 0;}\n"), 0);
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/fixtures/ws/cdo.toml", "[workspace]\n"), 0);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    // MODULE_E2E should NOT be marked as present when there are no sources outside fixtures/
    TEST_ASSERT(crate.modules[MODULE_E2E].present == false);

    crate_cleanup(&crate);
    temp_e2e_crate_destroy(&tc);
    return 0;
}

// --- Test: Scanner ignores e2e/ with only non-source files outside fixtures/ ---

TEST(e2e_module_scanner_ignores_e2e_with_only_noncode_files) {
    TempE2eCrate tc;
    TEST_ASSERT_EQ(temp_e2e_crate_create(&tc, "noncode_e2e"), PAL_OK);

    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "lib/placeholder.c", "void f(){}\n"), 0);

    // e2e/ has files but none are .c or .cpp
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/readme.md", "# E2E tests\n"), 0);
    TEST_ASSERT_EQ(temp_e2e_crate_add_file(&tc, "e2e/config.toml", "[config]\n"), 0);

    Crate crate;
    crate_init(&crate);

    int rc = scanner_scan_modules(tc.root, &crate, NULL, 0);
    TEST_ASSERT_EQ(rc, 0);

    // MODULE_E2E should NOT be present (no .c/.cpp sources outside fixtures/)
    TEST_ASSERT(crate.modules[MODULE_E2E].present == false);

    crate_cleanup(&crate);
    temp_e2e_crate_destroy(&tc);
    return 0;
}

// --- Test: module_artifact_name returns correct name for MODULE_E2E ---

TEST(e2e_module_artifact_name_correct) {
    char buf[128];

    // Test with a simple crate name
    int rc = module_artifact_name("myapp", MODULE_E2E, buf, sizeof(buf));
    TEST_ASSERT_EQ(rc, 0);

#ifdef _WIN32
    TEST_ASSERT_STR_EQ(buf, "myapp_e2e.exe");
#else
    TEST_ASSERT_STR_EQ(buf, "myapp_e2e");
#endif

    return 0;
}

// --- Test: module_artifact_name for MODULE_E2E with underscore in crate name ---

TEST(e2e_module_artifact_name_underscore_crate) {
    char buf[128];

    int rc = module_artifact_name("my_crate", MODULE_E2E, buf, sizeof(buf));
    TEST_ASSERT_EQ(rc, 0);

#ifdef _WIN32
    TEST_ASSERT_STR_EQ(buf, "my_crate_e2e.exe");
#else
    TEST_ASSERT_STR_EQ(buf, "my_crate_e2e");
#endif

    return 0;
}

// --- Test: module_artifact_name for MODULE_E2E with buffer too small ---

TEST(e2e_module_artifact_name_buffer_too_small) {
    char buf[4]; // Too small for "x_e2e.exe" or "x_e2e"

    int rc = module_artifact_name("x", MODULE_E2E, buf, sizeof(buf));
    TEST_ASSERT_NEQ(rc, 0);

    return 0;
}
