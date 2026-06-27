// crates/cdo/tst/unit/test_dep_propagation.c
// Unit tests for propagate_dep_modules: res/shd/dyn propagation, transitive deps, conflict detection, exe/tst exclusion.
// Validates: Requirements 2.3, 2.4, 2.5, 2.6, 2.7, 2.8
#include "cdo_ut.h"
#include "commands/cmd_build_internal.h"
#include "core/workspace.h"
#include "core/module.h"
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
    Crate crates[4]; // max 4 crates for transitive tests
    int dep_indices_a[4]; // dep indices for crate A
    int dep_indices_b[4]; // dep indices for crate B
} DepTestFixture;

static int dep_fixture_init(DepTestFixture* f, const char* suffix, int crate_count) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = "C:\\Temp";

    snprintf(f->ws_root, sizeof(f->ws_root), "%s/cdo_test_deps_%s", tmp, suffix);
    pal_path_normalize(f->ws_root);

    // Clean up any prior run
    pal_rmdir_r(f->ws_root);

    // Setup workspace
    memset(&f->ws, 0, sizeof(Workspace));
    strncpy(f->ws.root_path, f->ws_root, sizeof(f->ws.root_path) - 1);
    f->ws.crate_count = crate_count;
    f->ws.crates = f->crates;

    // Initialize all crates to empty
    memset(f->crates, 0, sizeof(f->crates));
    memset(f->dep_indices_a, 0, sizeof(f->dep_indices_a));
    memset(f->dep_indices_b, 0, sizeof(f->dep_indices_b));

    return 0;
}

static void dep_fixture_destroy(DepTestFixture* f) {
    pal_rmdir_r(f->ws_root);
}

/// Write a file with content at base_dir/rel_path, creating parent dirs.
static int write_file(const char* base, const char* rel, const char* content) {
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
static int file_exists(const char* base, const char* rel) {
    char full[1024];
    if (pal_path_join(full, sizeof(full), base, rel) != 0) return 0;
    pal_path_normalize(full);
    return pal_path_exists(full) == PAL_OK;
}

/// Read file content at base_dir/rel_path into buf.
static int read_file_content(const char* base, const char* rel, char* buf, size_t buf_size) {
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

/// Setup a crate with a given name at a given index. Optionally marks modules as present.
static void setup_crate(DepTestFixture* f, int idx, const char* name, bool has_res, bool has_shd, bool has_dyn) {
    Crate* c = &f->crates[idx];
    strncpy(c->name, name, sizeof(c->name) - 1);
    snprintf(c->path, sizeof(c->path), "crates/%s", name);
    c->has_res = has_res;
    c->has_shd = has_shd;

    if (has_res) {
        c->modules[MODULE_RES].present = true;
        c->modules[MODULE_RES].kind = MODULE_RES;
    }
    if (has_shd) {
        c->modules[MODULE_SHD].present = true;
        c->modules[MODULE_SHD].kind = MODULE_SHD;
    }
    if (has_dyn) {
        c->modules[MODULE_DYN].present = true;
        c->modules[MODULE_DYN].kind = MODULE_DYN;
    }
}

/// Helper to compute build dir for a crate. Returns path like: <ws_root>/build/<profile>/<name>/
static void compute_build_dir(DepTestFixture* f, const char* crate_name, const char* profile, char* out, size_t out_size) {
    char build_base[512];
    snprintf(build_base, sizeof(build_base), "%s/build/%s/%s", f->ws_root, profile, crate_name);
    pal_path_normalize(build_base);
    strncpy(out, build_base, out_size - 1);
    out[out_size - 1] = '\0';
}

// ---------------------------------------------------------------------------
// Test: res propagation — dep's built res files copied to dependent's res dir
// Validates: Requirement 2.3
// ---------------------------------------------------------------------------

TEST(dep_propagation_res_copied) {
    DepTestFixture f;
    TEST_ASSERT_EQ(dep_fixture_init(&f, "res_copy", 2), 0);

    // Crate 0: "app" depends on crate 1: "assets"
    setup_crate(&f, 0, "app", false, false, false);
    setup_crate(&f, 1, "assets", true, false, false);

    f.dep_indices_a[0] = 1;
    f.crates[0].dep_indices = f.dep_indices_a;
    f.crates[0].dep_count = 1;

    // Create the dep's built res files at build/debug/assets/res/
    char dep_build_dir[512];
    compute_build_dir(&f, "assets", "debug", dep_build_dir, sizeof(dep_build_dir));
    char dep_res_dir[512];
    pal_path_join(dep_res_dir, sizeof(dep_res_dir), dep_build_dir, "res");

    TEST_ASSERT_EQ(write_file(dep_res_dir, "config.toml", "[settings]\nkey=val"), 0);
    TEST_ASSERT_EQ(write_file(dep_res_dir, "data/info.json", "{\"v\":1}"), 0);

    // Create build dir for app so propagate_dep_modules can write into it
    char app_build_dir[512];
    compute_build_dir(&f, "app", "debug", app_build_dir, sizeof(app_build_dir));
    TEST_ASSERT_EQ(pal_mkdir_p(app_build_dir), PAL_OK);

    int rc = propagate_dep_modules(&f.ws, &f.crates[0], "debug");
    TEST_ASSERT_EQ(rc, 0);

    // Verify files were propagated to build/debug/app/res/
    char app_res_dir[512];
    pal_path_join(app_res_dir, sizeof(app_res_dir), app_build_dir, "res");

    TEST_ASSERT(file_exists(app_res_dir, "config.toml"));
    TEST_ASSERT(file_exists(app_res_dir, "data/info.json"));

    char buf[256] = {0};
    TEST_ASSERT_EQ(read_file_content(app_res_dir, "config.toml", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "[settings]\nkey=val");

    dep_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: shd propagation — dep's compiled shaders copied to dependent's shd dir
// Validates: Requirement 2.4
// ---------------------------------------------------------------------------

TEST(dep_propagation_shd_copied) {
    DepTestFixture f;
    TEST_ASSERT_EQ(dep_fixture_init(&f, "shd_copy", 2), 0);

    // Crate 0: "game" depends on crate 1: "shaders"
    setup_crate(&f, 0, "game", false, false, false);
    setup_crate(&f, 1, "shaders", false, true, false);

    f.dep_indices_a[0] = 1;
    f.crates[0].dep_indices = f.dep_indices_a;
    f.crates[0].dep_count = 1;

    // Create dep's built shd files at build/debug/shaders/shd/
    char dep_build_dir[512];
    compute_build_dir(&f, "shaders", "debug", dep_build_dir, sizeof(dep_build_dir));
    char dep_shd_dir[512];
    pal_path_join(dep_shd_dir, sizeof(dep_shd_dir), dep_build_dir, "shd");

    TEST_ASSERT_EQ(write_file(dep_shd_dir, "vertex.dxil", "DXIL_VERTEX_BYTECODE"), 0);
    TEST_ASSERT_EQ(write_file(dep_shd_dir, "effects/blur.dxil", "DXIL_BLUR_BYTECODE"), 0);

    // Create build dir for game
    char game_build_dir[512];
    compute_build_dir(&f, "game", "debug", game_build_dir, sizeof(game_build_dir));
    TEST_ASSERT_EQ(pal_mkdir_p(game_build_dir), PAL_OK);

    int rc = propagate_dep_modules(&f.ws, &f.crates[0], "debug");
    TEST_ASSERT_EQ(rc, 0);

    // Verify shaders were propagated to build/debug/game/shd/
    char game_shd_dir[512];
    pal_path_join(game_shd_dir, sizeof(game_shd_dir), game_build_dir, "shd");

    TEST_ASSERT(file_exists(game_shd_dir, "vertex.dxil"));
    TEST_ASSERT(file_exists(game_shd_dir, "effects/blur.dxil"));

    char buf[256] = {0};
    TEST_ASSERT_EQ(read_file_content(game_shd_dir, "effects/blur.dxil", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "DXIL_BLUR_BYTECODE");

    dep_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: dyn propagation — dep's DLL/SO copied adjacent to dependent's exe
// Validates: Requirement 2.5
// ---------------------------------------------------------------------------

TEST(dep_propagation_dyn_copied) {
    DepTestFixture f;
    TEST_ASSERT_EQ(dep_fixture_init(&f, "dyn_copy", 2), 0);

    // Crate 0: "app" depends on crate 1: "renderer"
    setup_crate(&f, 0, "app", false, false, false);
    setup_crate(&f, 1, "renderer", false, false, true);

    f.dep_indices_a[0] = 1;
    f.crates[0].dep_indices = f.dep_indices_a;
    f.crates[0].dep_count = 1;

    // Create the dep's DLL at build/debug/renderer/<artifact>
    char dep_build_dir[512];
    compute_build_dir(&f, "renderer", "debug", dep_build_dir, sizeof(dep_build_dir));
    TEST_ASSERT_EQ(pal_mkdir_p(dep_build_dir), PAL_OK);

    // Compute the expected artifact name for dyn module
    char artifact_name[128];
    TEST_ASSERT_EQ(module_artifact_name("renderer", MODULE_DYN, artifact_name, sizeof(artifact_name)), 0);

    // Write a fake DLL file at the dep's build dir
    TEST_ASSERT_EQ(write_file(dep_build_dir, artifact_name, "FAKE_DLL_CONTENT"), 0);

    // Create build dir for app
    char app_build_dir[512];
    compute_build_dir(&f, "app", "debug", app_build_dir, sizeof(app_build_dir));
    TEST_ASSERT_EQ(pal_mkdir_p(app_build_dir), PAL_OK);

    int rc = propagate_dep_modules(&f.ws, &f.crates[0], "debug");
    TEST_ASSERT_EQ(rc, 0);

    // Verify DLL was copied adjacent to exe (in crate build dir root)
    TEST_ASSERT(file_exists(app_build_dir, artifact_name));

    char buf[256] = {0};
    TEST_ASSERT_EQ(read_file_content(app_build_dir, artifact_name, buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "FAKE_DLL_CONTENT");

    dep_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: transitive deps — A→B→C, A receives C's res/shd/dyn
// Validates: Requirement 2.6
// ---------------------------------------------------------------------------

TEST(dep_propagation_transitive) {
    DepTestFixture f;
    TEST_ASSERT_EQ(dep_fixture_init(&f, "transitive", 3), 0);

    // Crate 0: "app" depends on crate 1: "mid"
    // Crate 1: "mid" depends on crate 2: "core_lib"
    // Transitive: app receives core_lib's res/shd/dyn via BFS dep_indices
    setup_crate(&f, 0, "app", false, false, false);
    setup_crate(&f, 1, "mid", false, false, false);
    setup_crate(&f, 2, "core_lib", true, true, true);

    // App's dep_indices includes BOTH mid and core_lib (BFS resolved by workspace_resolve)
    f.dep_indices_a[0] = 1;
    f.dep_indices_a[1] = 2;
    f.crates[0].dep_indices = f.dep_indices_a;
    f.crates[0].dep_count = 2;

    // Mid's dep_indices includes core_lib
    f.dep_indices_b[0] = 2;
    f.crates[1].dep_indices = f.dep_indices_b;
    f.crates[1].dep_count = 1;

    // Create core_lib's built outputs
    char core_build_dir[512];
    compute_build_dir(&f, "core_lib", "debug", core_build_dir, sizeof(core_build_dir));

    // res output
    char core_res_dir[512];
    pal_path_join(core_res_dir, sizeof(core_res_dir), core_build_dir, "res");
    TEST_ASSERT_EQ(write_file(core_res_dir, "shared.cfg", "core_config"), 0);

    // shd output
    char core_shd_dir[512];
    pal_path_join(core_shd_dir, sizeof(core_shd_dir), core_build_dir, "shd");
    TEST_ASSERT_EQ(write_file(core_shd_dir, "common.dxil", "CORE_SHADER"), 0);

    // dyn output
    char dyn_artifact[128];
    TEST_ASSERT_EQ(module_artifact_name("core_lib", MODULE_DYN, dyn_artifact, sizeof(dyn_artifact)), 0);
    TEST_ASSERT_EQ(write_file(core_build_dir, dyn_artifact, "CORE_DLL"), 0);

    // Create app's build dir
    char app_build_dir[512];
    compute_build_dir(&f, "app", "debug", app_build_dir, sizeof(app_build_dir));
    TEST_ASSERT_EQ(pal_mkdir_p(app_build_dir), PAL_OK);

    int rc = propagate_dep_modules(&f.ws, &f.crates[0], "debug");
    TEST_ASSERT_EQ(rc, 0);

    // Verify app received core_lib's res
    char app_res_dir[512];
    pal_path_join(app_res_dir, sizeof(app_res_dir), app_build_dir, "res");
    TEST_ASSERT(file_exists(app_res_dir, "shared.cfg"));

    // Verify app received core_lib's shd
    char app_shd_dir[512];
    pal_path_join(app_shd_dir, sizeof(app_shd_dir), app_build_dir, "shd");
    TEST_ASSERT(file_exists(app_shd_dir, "common.dxil"));

    // Verify app received core_lib's dyn
    TEST_ASSERT(file_exists(app_build_dir, dyn_artifact));

    // Verify content integrity
    char buf[256] = {0};
    TEST_ASSERT_EQ(read_file_content(app_res_dir, "shared.cfg", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "core_config");

    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQ(read_file_content(app_shd_dir, "common.dxil", buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "CORE_SHADER");

    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQ(read_file_content(app_build_dir, dyn_artifact, buf, sizeof(buf)), 0);
    TEST_ASSERT_STR_EQ(buf, "CORE_DLL");

    dep_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: conflict detection — two deps with same relative path → error
// Validates: Requirement 2.7
// ---------------------------------------------------------------------------

TEST(dep_propagation_conflict_detection) {
    DepTestFixture f;
    TEST_ASSERT_EQ(dep_fixture_init(&f, "conflict", 3), 0);

    // Crate 0: "app" depends on crate 1: "dep_a" and crate 2: "dep_b"
    // Both dep_a and dep_b provide res/overlapping.txt → conflict
    setup_crate(&f, 0, "app", false, false, false);
    setup_crate(&f, 1, "dep_a", true, false, false);
    setup_crate(&f, 2, "dep_b", true, false, false);

    f.dep_indices_a[0] = 1;
    f.dep_indices_a[1] = 2;
    f.crates[0].dep_indices = f.dep_indices_a;
    f.crates[0].dep_count = 2;

    // Create dep_a's built res with a file at "overlapping.txt"
    char dep_a_build_dir[512];
    compute_build_dir(&f, "dep_a", "debug", dep_a_build_dir, sizeof(dep_a_build_dir));
    char dep_a_res_dir[512];
    pal_path_join(dep_a_res_dir, sizeof(dep_a_res_dir), dep_a_build_dir, "res");
    TEST_ASSERT_EQ(write_file(dep_a_res_dir, "overlapping.txt", "from dep_a"), 0);

    // Create dep_b's built res with the SAME relative path "overlapping.txt"
    char dep_b_build_dir[512];
    compute_build_dir(&f, "dep_b", "debug", dep_b_build_dir, sizeof(dep_b_build_dir));
    char dep_b_res_dir[512];
    pal_path_join(dep_b_res_dir, sizeof(dep_b_res_dir), dep_b_build_dir, "res");
    TEST_ASSERT_EQ(write_file(dep_b_res_dir, "overlapping.txt", "from dep_b"), 0);

    // Create app's build dir
    char app_build_dir[512];
    compute_build_dir(&f, "app", "debug", app_build_dir, sizeof(app_build_dir));
    TEST_ASSERT_EQ(pal_mkdir_p(app_build_dir), PAL_OK);

    // This should FAIL with conflict error (non-zero return code)
    int rc = propagate_dep_modules(&f.ws, &f.crates[0], "debug");
    TEST_ASSERT_NEQ(rc, 0);

    dep_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: exe/tst modules excluded from propagation
// Validates: Requirement 2.8
// The dep system only propagates res, shd, dyn. A dependency that has only
// exe and tst modules produces nothing to propagate (no error, no copies).
// ---------------------------------------------------------------------------

TEST(dep_propagation_exe_tst_excluded) {
    DepTestFixture f;
    TEST_ASSERT_EQ(dep_fixture_init(&f, "exe_tst_excl", 2), 0);

    // Crate 0: "consumer" depends on crate 1: "tool"
    // "tool" has exe and tst modules but NO res/shd/dyn
    setup_crate(&f, 0, "consumer", false, false, false);

    // Setup "tool" with exe and tst only
    Crate* tool = &f.crates[1];
    strncpy(tool->name, "tool", sizeof(tool->name) - 1);
    snprintf(tool->path, sizeof(tool->path), "crates/tool");
    tool->modules[MODULE_EXE].present = true;
    tool->modules[MODULE_EXE].kind = MODULE_EXE;
    tool->modules[MODULE_TST].present = true;
    tool->modules[MODULE_TST].kind = MODULE_TST;
    tool->has_res = false;
    tool->has_shd = false;

    f.dep_indices_a[0] = 1;
    f.crates[0].dep_indices = f.dep_indices_a;
    f.crates[0].dep_count = 1;

    // Create build dirs (even though tool has exe/tst, nothing should be propagated)
    char tool_build_dir[512];
    compute_build_dir(&f, "tool", "debug", tool_build_dir, sizeof(tool_build_dir));
    TEST_ASSERT_EQ(pal_mkdir_p(tool_build_dir), PAL_OK);

    char consumer_build_dir[512];
    compute_build_dir(&f, "consumer", "debug", consumer_build_dir, sizeof(consumer_build_dir));
    TEST_ASSERT_EQ(pal_mkdir_p(consumer_build_dir), PAL_OK);

    // propagate should succeed with no work
    int rc = propagate_dep_modules(&f.ws, &f.crates[0], "debug");
    TEST_ASSERT_EQ(rc, 0);

    // Verify no res/ or shd/ directory was created in consumer's build dir
    char consumer_res_dir[512];
    pal_path_join(consumer_res_dir, sizeof(consumer_res_dir), consumer_build_dir, "res");
    TEST_ASSERT(pal_path_exists(consumer_res_dir) != PAL_OK);

    char consumer_shd_dir[512];
    pal_path_join(consumer_shd_dir, sizeof(consumer_shd_dir), consumer_build_dir, "shd");
    TEST_ASSERT(pal_path_exists(consumer_shd_dir) != PAL_OK);

    dep_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: no deps → early return, no crash
// Validates: Requirement 2.3 (edge case: zero deps)
// ---------------------------------------------------------------------------

TEST(dep_propagation_no_deps_succeeds) {
    DepTestFixture f;
    TEST_ASSERT_EQ(dep_fixture_init(&f, "no_deps", 1), 0);

    setup_crate(&f, 0, "solo", false, false, false);
    f.crates[0].dep_count = 0;
    f.crates[0].dep_indices = NULL;

    int rc = propagate_dep_modules(&f.ws, &f.crates[0], "debug");
    TEST_ASSERT_EQ(rc, 0);

    dep_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: NULL inputs → returns error (defensive)
// ---------------------------------------------------------------------------

TEST(dep_propagation_null_inputs_returns_error) {
    DepTestFixture f;
    TEST_ASSERT_EQ(dep_fixture_init(&f, "null_input", 1), 0);
    setup_crate(&f, 0, "test", false, false, false);

    TEST_ASSERT_NEQ(propagate_dep_modules(NULL, &f.crates[0], "debug"), 0);
    TEST_ASSERT_NEQ(propagate_dep_modules(&f.ws, NULL, "debug"), 0);
    TEST_ASSERT_NEQ(propagate_dep_modules(&f.ws, &f.crates[0], NULL), 0);

    dep_fixture_destroy(&f);
    return 0;
}
