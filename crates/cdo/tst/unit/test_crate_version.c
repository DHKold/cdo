// crates/cdo/tst/unit/test_crate_version.c
// Unit tests for version and [install] section parsing from crate.toml.
// Validates: version field defaults and [install] section resource-base / shader-base parsing.
#include "cdo_ut.h"
#include "model/workspace.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

typedef struct {
    char root[512];
} VersionTestFixture;

static int ver_fixture_init(VersionTestFixture* f, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
    snprintf(f->root, sizeof(f->root), "%s/cdo_test_version_%s", tmp, suffix);
    pal_path_normalize(f->root);
    pal_rmdir_r(f->root);
    return 0;
}

static void ver_fixture_destroy(VersionTestFixture* f) {
    pal_rmdir_r(f->root);
}

/// Write a file at base/rel, creating parent dirs as needed.
static int ver_write_file(const char* base, const char* rel, const char* content) {
    char full[1024];
    if (pal_path_join(full, sizeof(full), base, rel) != 0) return 1;
    pal_path_normalize(full);

    char dir[1024];
    strncpy(dir, full, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char* last_sep = strrchr(dir, '/');
    if (last_sep) {
        *last_sep = '\0';
        if (pal_mkdir_p(dir) != 0) return 1;
    }
    return pal_file_write(full, content, strlen(content));
}

/// Create a minimal workspace at f->root with cdo.toml pointing to a single crate.
/// crate_toml_content is the full crate.toml content to use.
static int ver_create_workspace(VersionTestFixture* f, const char* crate_toml_content) {
    // Write cdo.toml
    const char* ws_toml =
        "[workspace]\n"
        "name = \"test-ws\"\n"
        "members = [\"crates/testcrate\"]\n";
    if (ver_write_file(f->root, "cdo.toml", ws_toml) != 0) return 1;

    // Write crate.toml
    if (ver_write_file(f->root, "crates/testcrate/crate.toml", crate_toml_content) != 0) return 1;

    // Create a lib/ module directory with a dummy file so scanner finds something
    if (ver_write_file(f->root, "crates/testcrate/lib/dummy.c", "// placeholder\n") != 0) return 1;

    return 0;
}

// ---------------------------------------------------------------------------
// Test: crate.toml with version = "1.2.3" populates crate->version
// ---------------------------------------------------------------------------

TEST(crate_version_explicit_value) {
    VersionTestFixture f;
    TEST_ASSERT_EQ(ver_fixture_init(&f, "ver_explicit"), 0);

    const char* crate_toml =
        "[crate]\n"
        "name = \"testcrate\"\n"
        "version = \"1.2.3\"\n";

    TEST_ASSERT_EQ(ver_create_workspace(&f, crate_toml), 0);

    Workspace ws = {0};
    int rc = workspace_load(f.root, &ws);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(ws.crate_count, 1);
    TEST_ASSERT_STR_EQ(ws.crates[0].version, "1.2.3");

    workspace_free(&ws);
    ver_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: crate.toml without version defaults to "0.0.0"
// ---------------------------------------------------------------------------

TEST(crate_version_defaults_to_zero) {
    VersionTestFixture f;
    TEST_ASSERT_EQ(ver_fixture_init(&f, "ver_default"), 0);

    const char* crate_toml =
        "[crate]\n"
        "name = \"testcrate\"\n";

    TEST_ASSERT_EQ(ver_create_workspace(&f, crate_toml), 0);

    Workspace ws = {0};
    int rc = workspace_load(f.root, &ws);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(ws.crate_count, 1);
    TEST_ASSERT_STR_EQ(ws.crates[0].version, "0.0.0");

    workspace_free(&ws);
    ver_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: [install] section with resource-base and shader-base populates correctly
// ---------------------------------------------------------------------------

TEST(crate_install_section_populated) {
    VersionTestFixture f;
    TEST_ASSERT_EQ(ver_fixture_init(&f, "install_pop"), 0);

    const char* crate_toml =
        "[crate]\n"
        "name = \"testcrate\"\n"
        "version = \"2.0.0\"\n"
        "\n"
        "[install]\n"
        "resource-base = \"data\"\n"
        "shader-base = \"shaders\"\n";

    TEST_ASSERT_EQ(ver_create_workspace(&f, crate_toml), 0);

    Workspace ws = {0};
    int rc = workspace_load(f.root, &ws);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(ws.crate_count, 1);
    TEST_ASSERT_STR_EQ(ws.crates[0].resource_base, "data");
    TEST_ASSERT_STR_EQ(ws.crates[0].shader_base, "shaders");

    workspace_free(&ws);
    ver_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: missing [install] section defaults resource_base="." and shader_base="."
// ---------------------------------------------------------------------------

TEST(crate_install_section_defaults) {
    VersionTestFixture f;
    TEST_ASSERT_EQ(ver_fixture_init(&f, "install_def"), 0);

    const char* crate_toml =
        "[crate]\n"
        "name = \"testcrate\"\n"
        "version = \"1.0.0\"\n";

    TEST_ASSERT_EQ(ver_create_workspace(&f, crate_toml), 0);

    Workspace ws = {0};
    int rc = workspace_load(f.root, &ws);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(ws.crate_count, 1);
    TEST_ASSERT_STR_EQ(ws.crates[0].resource_base, ".");
    TEST_ASSERT_STR_EQ(ws.crates[0].shader_base, ".");

    workspace_free(&ws);
    ver_fixture_destroy(&f);
    return 0;
}
