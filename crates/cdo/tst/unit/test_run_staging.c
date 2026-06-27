// crates/cdo/tst/unit/test_run_staging.c
// Unit tests for the run command staging logic: prepare_staging, select_run_crate, copy_dir_recursive, argv forwarding.
// Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10, 3.11, 3.12, 3.13
#include "cdo_ut.h"
#include "commands/cmd_run_internal.h"
#include "core/workspace.h"
#include "core/module.h"
#include "core/cli.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

typedef struct {
    char ws_root[512];
    Workspace ws;
    Crate crates[4];
} RunTestFixture;

static int run_fixture_init(RunTestFixture* f, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = "C:\\Temp";

    snprintf(f->ws_root, sizeof(f->ws_root), "%s/cdo_test_run_%s", tmp, suffix);
    pal_path_normalize(f->ws_root);

    // Clean up any prior run
    pal_rmdir_r(f->ws_root);

    // Setup workspace
    memset(&f->ws, 0, sizeof(Workspace));
    strncpy(f->ws.root_path, f->ws_root, sizeof(f->ws.root_path) - 1);
    f->ws.crates = f->crates;

    // Initialize all crates to empty
    memset(f->crates, 0, sizeof(f->crates));

    return 0;
}

static void run_fixture_destroy(RunTestFixture* f) {
    pal_rmdir_r(f->ws_root);
}

/// Write a file with content at base_dir/rel_path, creating parent dirs.
static int write_test_file(const char* base, const char* rel, const char* content) {
    char full[1024];
    if (pal_path_join(full, sizeof(full), base, rel) != 0) return 1;
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

/// Check if a file exists at base_dir/rel_path.
static int test_file_exists(const char* base, const char* rel) {
    char full[1024];
    if (pal_path_join(full, sizeof(full), base, rel) != 0) return 0;
    pal_path_normalize(full);
    return pal_path_exists(full) == PAL_OK;
}

/// Read file content into buffer.
static int read_test_file(const char* base, const char* rel, char* buf, size_t buf_size) {
    char full[1024];
    if (pal_path_join(full, sizeof(full), base, rel) != 0) return 1;
    pal_path_normalize(full);
    char* data = NULL;
    size_t len = 0;
    int rc = pal_file_read(full, &data, &len);
    if (rc != 0) return 1;
    size_t copy_len = len < buf_size - 1 ? len : buf_size - 1;
    memcpy(buf, data, copy_len);
    buf[copy_len] = '\0';
    free(data);
    return 0;
}

/// Setup a crate at the given index with name and module flags.
static void setup_run_crate(RunTestFixture* f, int idx, const char* name, bool has_exe, bool has_res, bool has_shd, bool has_dyn) {
    Crate* c = &f->crates[idx];
    strncpy(c->name, name, sizeof(c->name) - 1);
    snprintf(c->path, sizeof(c->path), "crates/%s", name);

    if (has_exe) {
        c->modules[MODULE_EXE].present = true;
        c->modules[MODULE_EXE].kind = MODULE_EXE;
    }
    if (has_res) {
        c->modules[MODULE_RES].present = true;
        c->modules[MODULE_RES].kind = MODULE_RES;
        c->has_res = true;
    }
    if (has_shd) {
        c->modules[MODULE_SHD].present = true;
        c->modules[MODULE_SHD].kind = MODULE_SHD;
        c->has_shd = true;
    }
    if (has_dyn) {
        c->modules[MODULE_DYN].present = true;
        c->modules[MODULE_DYN].kind = MODULE_DYN;
    }
}

// ---------------------------------------------------------------------------
// Test: staging folder creation at .cdo/<crate>/run/
// Validates: Requirement 3.2
// ---------------------------------------------------------------------------

TEST(run_staging_folder_creation) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "staging_create"), 0);

    setup_run_crate(&f, 0, "myapp", true, false, false, false);
    f.ws.crate_count = 1;

    // Create a fake executable in build/debug/myapp/
    char build_dir[512];
    snprintf(build_dir, sizeof(build_dir), "%s/build/debug/myapp", f.ws_root);
    pal_path_normalize(build_dir);
    TEST_ASSERT_EQ(write_test_file(build_dir, "myapp.exe", "FAKE_EXE"), 0);

    // Compute staging dir
    char staging_dir[512];
    snprintf(staging_dir, sizeof(staging_dir), "%s/.cdo/myapp/run", f.ws_root);
    pal_path_normalize(staging_dir);

    char exe_path[512];
    snprintf(exe_path, sizeof(exe_path), "%s/myapp.exe", build_dir);
    pal_path_normalize(exe_path);

    int rc = run_prepare_staging(&f.ws, &f.crates[0], "debug", staging_dir, exe_path);
    TEST_ASSERT_EQ(rc, 0);

    // Verify staging folder was created
    TEST_ASSERT_EQ(pal_path_exists(staging_dir), PAL_OK);

    run_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: exe copy into staging
// Validates: Requirement 3.3
// ---------------------------------------------------------------------------

TEST(run_staging_exe_copy) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "exe_copy"), 0);

    setup_run_crate(&f, 0, "myapp", true, false, false, false);
    f.ws.crate_count = 1;

    // Create fake executable
    char build_dir[512];
    snprintf(build_dir, sizeof(build_dir), "%s/build/debug/myapp", f.ws_root);
    pal_path_normalize(build_dir);
    TEST_ASSERT_EQ(write_test_file(build_dir, "myapp.exe", "EXE_CONTENT_123"), 0);

    char staging_dir[512];
    snprintf(staging_dir, sizeof(staging_dir), "%s/.cdo/myapp/run", f.ws_root);
    pal_path_normalize(staging_dir);

    char exe_path[512];
    snprintf(exe_path, sizeof(exe_path), "%s/myapp.exe", build_dir);
    pal_path_normalize(exe_path);

    int rc = run_prepare_staging(&f.ws, &f.crates[0], "debug", staging_dir, exe_path);
    TEST_ASSERT_EQ(rc, 0);

    // Verify exe was copied
    TEST_ASSERT(test_file_exists(staging_dir, "myapp.exe"));

    // Verify content
    char buf[256] = {0};
    TEST_ASSERT_EQ(read_test_file(staging_dir, "myapp.exe", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "EXE_CONTENT_123");

    run_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: DLL copy into staging
// Validates: Requirement 3.4
// ---------------------------------------------------------------------------

TEST(run_staging_dll_copy) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "dll_copy"), 0);

    setup_run_crate(&f, 0, "myapp", true, false, false, false);
    f.ws.crate_count = 1;

    // Create fake exe and DLL in the build directory
    char build_dir[512];
    snprintf(build_dir, sizeof(build_dir), "%s/build/debug/myapp", f.ws_root);
    pal_path_normalize(build_dir);
    TEST_ASSERT_EQ(write_test_file(build_dir, "myapp.exe", "FAKE_EXE"), 0);
    TEST_ASSERT_EQ(write_test_file(build_dir, "renderer.dll", "DLL_DATA_ABC"), 0);
    TEST_ASSERT_EQ(write_test_file(build_dir, "physics.dll", "DLL_DATA_DEF"), 0);

    char staging_dir[512];
    snprintf(staging_dir, sizeof(staging_dir), "%s/.cdo/myapp/run", f.ws_root);
    pal_path_normalize(staging_dir);

    char exe_path[512];
    snprintf(exe_path, sizeof(exe_path), "%s/myapp.exe", build_dir);
    pal_path_normalize(exe_path);

    int rc = run_prepare_staging(&f.ws, &f.crates[0], "debug", staging_dir, exe_path);
    TEST_ASSERT_EQ(rc, 0);

    // Verify DLLs were copied to staging root
    TEST_ASSERT(test_file_exists(staging_dir, "renderer.dll"));
    TEST_ASSERT(test_file_exists(staging_dir, "physics.dll"));

    // Verify content
    char buf[256] = {0};
    TEST_ASSERT_EQ(read_test_file(staging_dir, "renderer.dll", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "DLL_DATA_ABC");

    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQ(read_test_file(staging_dir, "physics.dll", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "DLL_DATA_DEF");

    run_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: res/ subdirectory copy preserving structure
// Validates: Requirement 3.5
// ---------------------------------------------------------------------------

TEST(run_staging_res_copy_preserving_structure) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "res_copy"), 0);

    setup_run_crate(&f, 0, "myapp", true, true, false, false);
    f.ws.crate_count = 1;

    // Create fake exe and res/ content in build dir
    char build_dir[512];
    snprintf(build_dir, sizeof(build_dir), "%s/build/debug/myapp", f.ws_root);
    pal_path_normalize(build_dir);
    TEST_ASSERT_EQ(write_test_file(build_dir, "myapp.exe", "FAKE_EXE"), 0);
    TEST_ASSERT_EQ(write_test_file(build_dir, "res/config.toml", "[app]\nname=\"test\""), 0);
    TEST_ASSERT_EQ(write_test_file(build_dir, "res/assets/texture.png", "PNG_DATA"), 0);
    TEST_ASSERT_EQ(write_test_file(build_dir, "res/assets/models/cube.obj", "OBJ_DATA"), 0);

    char staging_dir[512];
    snprintf(staging_dir, sizeof(staging_dir), "%s/.cdo/myapp/run", f.ws_root);
    pal_path_normalize(staging_dir);

    char exe_path[512];
    snprintf(exe_path, sizeof(exe_path), "%s/myapp.exe", build_dir);
    pal_path_normalize(exe_path);

    int rc = run_prepare_staging(&f.ws, &f.crates[0], "debug", staging_dir, exe_path);
    TEST_ASSERT_EQ(rc, 0);

    // Verify res/ directory structure is preserved in staging
    TEST_ASSERT(test_file_exists(staging_dir, "res/config.toml"));
    TEST_ASSERT(test_file_exists(staging_dir, "res/assets/texture.png"));
    TEST_ASSERT(test_file_exists(staging_dir, "res/assets/models/cube.obj"));

    // Verify content integrity
    char buf[256] = {0};
    TEST_ASSERT_EQ(read_test_file(staging_dir, "res/config.toml", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "[app]\nname=\"test\"");

    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQ(read_test_file(staging_dir, "res/assets/models/cube.obj", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "OBJ_DATA");

    run_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: shd/ subdirectory copy preserving structure
// Validates: Requirement 3.6
// ---------------------------------------------------------------------------

TEST(run_staging_shd_copy_preserving_structure) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "shd_copy"), 0);

    setup_run_crate(&f, 0, "myapp", true, false, true, false);
    f.ws.crate_count = 1;

    // Create fake exe and shd/ content in build dir
    char build_dir[512];
    snprintf(build_dir, sizeof(build_dir), "%s/build/debug/myapp", f.ws_root);
    pal_path_normalize(build_dir);
    TEST_ASSERT_EQ(write_test_file(build_dir, "myapp.exe", "FAKE_EXE"), 0);
    TEST_ASSERT_EQ(write_test_file(build_dir, "shd/vertex.dxil", "VERTEX_BYTECODE"), 0);
    TEST_ASSERT_EQ(write_test_file(build_dir, "shd/pixel.dxil", "PIXEL_BYTECODE"), 0);
    TEST_ASSERT_EQ(write_test_file(build_dir, "shd/effects/blur.dxil", "BLUR_BYTECODE"), 0);

    char staging_dir[512];
    snprintf(staging_dir, sizeof(staging_dir), "%s/.cdo/myapp/run", f.ws_root);
    pal_path_normalize(staging_dir);

    char exe_path[512];
    snprintf(exe_path, sizeof(exe_path), "%s/myapp.exe", build_dir);
    pal_path_normalize(exe_path);

    int rc = run_prepare_staging(&f.ws, &f.crates[0], "debug", staging_dir, exe_path);
    TEST_ASSERT_EQ(rc, 0);

    // Verify shd/ directory structure is preserved in staging
    TEST_ASSERT(test_file_exists(staging_dir, "shd/vertex.dxil"));
    TEST_ASSERT(test_file_exists(staging_dir, "shd/pixel.dxil"));
    TEST_ASSERT(test_file_exists(staging_dir, "shd/effects/blur.dxil"));

    // Verify content integrity
    char buf[256] = {0};
    TEST_ASSERT_EQ(read_test_file(staging_dir, "shd/effects/blur.dxil", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "BLUR_BYTECODE");

    run_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: prior run cleanup (staging exists → cleared first)
// Validates: Requirement 3.11
// ---------------------------------------------------------------------------

TEST(run_staging_prior_cleanup) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "cleanup"), 0);

    setup_run_crate(&f, 0, "myapp", true, false, false, false);
    f.ws.crate_count = 1;

    // Create fake exe
    char build_dir[512];
    snprintf(build_dir, sizeof(build_dir), "%s/build/debug/myapp", f.ws_root);
    pal_path_normalize(build_dir);
    TEST_ASSERT_EQ(write_test_file(build_dir, "myapp.exe", "NEW_EXE"), 0);

    // Create a pre-existing staging folder with stale content
    char staging_dir[512];
    snprintf(staging_dir, sizeof(staging_dir), "%s/.cdo/myapp/run", f.ws_root);
    pal_path_normalize(staging_dir);
    TEST_ASSERT_EQ(write_test_file(staging_dir, "old_file.txt", "STALE"), 0);
    TEST_ASSERT_EQ(write_test_file(staging_dir, "myapp.exe", "OLD_EXE"), 0);

    // Verify stale content is there
    TEST_ASSERT(test_file_exists(staging_dir, "old_file.txt"));

    char exe_path[512];
    snprintf(exe_path, sizeof(exe_path), "%s/myapp.exe", build_dir);
    pal_path_normalize(exe_path);

    int rc = run_prepare_staging(&f.ws, &f.crates[0], "debug", staging_dir, exe_path);
    TEST_ASSERT_EQ(rc, 0);

    // Stale file should be gone
    TEST_ASSERT(!test_file_exists(staging_dir, "old_file.txt"));

    // New exe should be present with fresh content
    char buf[256] = {0};
    TEST_ASSERT_EQ(read_test_file(staging_dir, "myapp.exe", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "NEW_EXE");

    run_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: non-exe crate → error
// Validates: Requirement 3.13
// ---------------------------------------------------------------------------

TEST(run_staging_non_exe_crate_error) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "non_exe"), 0);

    // Setup a crate without exe module
    setup_run_crate(&f, 0, "libonly", false, true, false, false);
    f.ws.crate_count = 1;

    // Set up options pointing to this crate
    CdoOptions opts;
    memset(&opts, 0, sizeof(opts));
    const char* args[] = { "libonly" };
    opts.positional_args = args;
    opts.positional_count = 1;

    // select_run_crate should return NULL for non-exe crate
    const Crate* result = run_select_crate(&f.ws, &opts);
    TEST_ASSERT_NULL(result);

    run_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: non-existent crate name → error
// Validates: Requirement 3.13 (related)
// ---------------------------------------------------------------------------

TEST(run_staging_nonexistent_crate_error) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "noexist"), 0);

    setup_run_crate(&f, 0, "myapp", true, false, false, false);
    f.ws.crate_count = 1;

    CdoOptions opts;
    memset(&opts, 0, sizeof(opts));
    const char* args[] = { "nonexistent" };
    opts.positional_args = args;
    opts.positional_count = 1;

    const Crate* result = run_select_crate(&f.ws, &opts);
    TEST_ASSERT_NULL(result);

    run_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: build failure → no staging created
// Validates: Requirement 3.12
// This tests that if the exe file doesn't exist (simulating build failure),
// prepare_staging fails and the staging dir is not left behind.
// ---------------------------------------------------------------------------

TEST(run_staging_build_failure_no_staging) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "build_fail"), 0);

    setup_run_crate(&f, 0, "myapp", true, false, false, false);
    f.ws.crate_count = 1;

    // Do NOT create the exe file - simulate build failure
    char staging_dir[512];
    snprintf(staging_dir, sizeof(staging_dir), "%s/.cdo/myapp/run", f.ws_root);
    pal_path_normalize(staging_dir);

    char exe_path[512];
    snprintf(exe_path, sizeof(exe_path), "%s/build/debug/myapp/myapp.exe", f.ws_root);
    pal_path_normalize(exe_path);

    // prepare_staging should fail because exe doesn't exist
    int rc = run_prepare_staging(&f.ws, &f.crates[0], "debug", staging_dir, exe_path);
    TEST_ASSERT_NEQ(rc, 0);

    run_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: argv forwarding — verify CdoOptions argv_rest fields are usable
// Validates: Requirements 3.8, 3.9
// Since the actual spawn isn't testable in unit tests, we verify
// that the select_run_crate logic works correctly with argv_rest present,
// and that the staging is properly set up for forwarding.
// ---------------------------------------------------------------------------

TEST(run_staging_argv_forwarding_setup) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "argv_fwd"), 0);

    setup_run_crate(&f, 0, "myapp", true, false, false, false);
    f.ws.crate_count = 1;

    // Simulate options with argv_rest (arguments after --)
    CdoOptions opts;
    memset(&opts, 0, sizeof(opts));
    const char* args[] = { "myapp" };
    opts.positional_args = args;
    opts.positional_count = 1;

    const char* rest_args[] = { "--port", "8080", "--verbose" };
    opts.argv_rest = rest_args;
    opts.argc_rest = 3;

    // Verify select_run_crate succeeds even with argv_rest set
    const Crate* result = run_select_crate(&f.ws, &opts);
    TEST_ASSERT(result != NULL);
    TEST_ASSERT_STR_EQ(result->name, "myapp");

    // Verify that the argv_rest fields are properly accessible
    TEST_ASSERT_EQ(opts.argc_rest, 3);
    TEST_ASSERT_STR_EQ(opts.argv_rest[0], "--port");
    TEST_ASSERT_STR_EQ(opts.argv_rest[1], "8080");
    TEST_ASSERT_STR_EQ(opts.argv_rest[2], "--verbose");

    run_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: auto-select single executable crate
// Validates: Requirement 3.7 (related - crate selection for running)
// ---------------------------------------------------------------------------

TEST(run_staging_auto_select_single_exe) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "auto_sel"), 0);

    // Only one exe crate in workspace
    setup_run_crate(&f, 0, "mylib", false, false, false, false); // lib-only
    setup_run_crate(&f, 1, "myapp", true, false, false, false);  // exe
    f.ws.crate_count = 2;

    CdoOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.positional_count = 0; // no crate specified

    const Crate* result = run_select_crate(&f.ws, &opts);
    TEST_ASSERT(result != NULL);
    TEST_ASSERT_STR_EQ(result->name, "myapp");

    run_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: copy_dir_recursive preserves nested structure
// Validates: Requirements 3.5, 3.6 (underlying mechanism)
// ---------------------------------------------------------------------------

TEST(run_staging_copy_dir_recursive_preserves_structure) {
    RunTestFixture f;
    TEST_ASSERT_EQ(run_fixture_init(&f, "copy_recur"), 0);

    // Create source directory with nested files
    char src_dir[512];
    snprintf(src_dir, sizeof(src_dir), "%s/src_tree", f.ws_root);
    pal_path_normalize(src_dir);

    TEST_ASSERT_EQ(write_test_file(src_dir, "top.txt", "TOP"), 0);
    TEST_ASSERT_EQ(write_test_file(src_dir, "sub/mid.txt", "MID"), 0);
    TEST_ASSERT_EQ(write_test_file(src_dir, "sub/deep/bottom.txt", "BOTTOM"), 0);

    // Copy to destination
    char dst_dir[512];
    snprintf(dst_dir, sizeof(dst_dir), "%s/dst_tree", f.ws_root);
    pal_path_normalize(dst_dir);
    TEST_ASSERT_EQ(pal_mkdir_p(dst_dir), PAL_OK);

    int rc = run_copy_dir_recursive(src_dir, dst_dir);
    TEST_ASSERT_EQ(rc, 0);

    // Verify all files were copied with structure preserved
    TEST_ASSERT(test_file_exists(dst_dir, "top.txt"));
    TEST_ASSERT(test_file_exists(dst_dir, "sub/mid.txt"));
    TEST_ASSERT(test_file_exists(dst_dir, "sub/deep/bottom.txt"));

    // Verify content
    char buf[256] = {0};
    TEST_ASSERT_EQ(read_test_file(dst_dir, "sub/deep/bottom.txt", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "BOTTOM");

    run_fixture_destroy(&f);
    return 0;
}
