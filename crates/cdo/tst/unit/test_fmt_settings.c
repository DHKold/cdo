// crates/cdo/tst/unit/test_fmt_settings.c
// Unit tests for format settings parsing from [workspace.settings.format]
// Validates: Requirements 5.3, 6.1
#include "cdo_ut.h"
#include "model/workspace.h"
#include "model/fmt_settings.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
// Helper: create a temporary workspace with a given cdo.toml content
// =============================================================================

static void get_temp_dir(char* buf, size_t size, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(buf, size, "%s/cdo_test_fmt_%s", tmp, suffix);
    pal_path_normalize(buf);
}

static int create_test_workspace(const char* root, const char* cdo_toml_content) {
    if (pal_mkdir_p(root) != PAL_OK) return -1;

    char toml_path[520];
    if (pal_path_join(toml_path, sizeof(toml_path), root, "cdo.toml") != 0) return -1;
    if (pal_file_write(toml_path, cdo_toml_content, strlen(cdo_toml_content)) != 0) return -1;

    return 0;
}

static void cleanup_test_workspace(const char* root) {
    pal_rmdir_r(root);
}

// =============================================================================
// Tests: Default settings when [workspace.settings.format] section is absent
// Requirement 5.3, 6.1: all fields default when section missing
// =============================================================================

TEST_SERIAL(fmt_settings_defaults_when_section_absent) {
    char root[520];
    get_temp_dir(root, sizeof(root), "defaults");

    const char* toml =
        "[workspace]\n"
        "members = []\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    // tool_path defaults to empty string
    TEST_ASSERT_STR_EQ(ws.format_settings.tool_path, "");

    // style defaults to empty string
    TEST_ASSERT_STR_EQ(ws.format_settings.style, "");

    // exclude_count defaults to 0
    TEST_ASSERT_EQ(ws.format_settings.exclude_count, 0);

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

// =============================================================================
// Tests: Partial section — only style set
// Requirement 5.3: style parsed, others remain default
// =============================================================================

TEST_SERIAL(fmt_settings_partial_only_style) {
    char root[520];
    get_temp_dir(root, sizeof(root), "partial_style");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.format]\n"
        "style = \"google\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_STR_EQ(ws.format_settings.style, "google");
    TEST_ASSERT_STR_EQ(ws.format_settings.tool_path, "");
    TEST_ASSERT_EQ(ws.format_settings.exclude_count, 0);

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

// =============================================================================
// Tests: Partial section — only tool-path set
// Requirement 5.3: tool_path parsed, others remain default
// =============================================================================

TEST_SERIAL(fmt_settings_partial_only_tool_path) {
    char root[520];
    get_temp_dir(root, sizeof(root), "partial_tool");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.format]\n"
        "tool-path = \"C:/tools/clang-format.exe\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_STR_EQ(ws.format_settings.tool_path, "C:/tools/clang-format.exe");
    TEST_ASSERT_STR_EQ(ws.format_settings.style, "");
    TEST_ASSERT_EQ(ws.format_settings.exclude_count, 0);

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

// =============================================================================
// Tests: Full section — all 3 fields set
// Requirement 5.3, 6.1: all values parsed correctly
// =============================================================================

TEST_SERIAL(fmt_settings_full_section) {
    char root[520];
    get_temp_dir(root, sizeof(root), "full");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.format]\n"
        "tool-path = \"/usr/bin/clang-format\"\n"
        "style = \"llvm\"\n"
        "exclude = [\"vendor/**\", \"generated/*.c\"]\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_STR_EQ(ws.format_settings.tool_path, "/usr/bin/clang-format");
    TEST_ASSERT_STR_EQ(ws.format_settings.style, "llvm");
    TEST_ASSERT_EQ(ws.format_settings.exclude_count, 2);
    TEST_ASSERT_STR_EQ(ws.format_settings.exclude_patterns[0], "vendor/**");
    TEST_ASSERT_STR_EQ(ws.format_settings.exclude_patterns[1], "generated/*.c");

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

// =============================================================================
// Tests: Multiple exclude patterns
// Requirement 6.1: exclude array with multiple patterns
// =============================================================================

TEST_SERIAL(fmt_settings_exclude_multiple_patterns) {
    char root[520];
    get_temp_dir(root, sizeof(root), "multi_excl");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.format]\n"
        "exclude = [\"third_party/**\", \"build/**\", \"test/fixtures/*.h\", \"docs/**/*.c\"]\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_EQ(ws.format_settings.exclude_count, 4);
    TEST_ASSERT_STR_EQ(ws.format_settings.exclude_patterns[0], "third_party/**");
    TEST_ASSERT_STR_EQ(ws.format_settings.exclude_patterns[1], "build/**");
    TEST_ASSERT_STR_EQ(ws.format_settings.exclude_patterns[2], "test/fixtures/*.h");
    TEST_ASSERT_STR_EQ(ws.format_settings.exclude_patterns[3], "docs/**/*.c");

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

// =============================================================================
// Tests: Exclude max 32 patterns (cap at 32)
// Requirement 6.1: only first 32 patterns kept
// =============================================================================

TEST_SERIAL(fmt_settings_exclude_max_32) {
    char root[520];
    get_temp_dir(root, sizeof(root), "max_excl");

    // Build a TOML with 35 exclude patterns
    char toml[4096];
    int off = snprintf(toml, sizeof(toml), "[workspace]\nmembers = []\n[workspace.settings.format]\nexclude = [");
    for (int i = 0; i < 35; i++) {
        if (i > 0) off += snprintf(toml + off, sizeof(toml) - off, ", ");
        off += snprintf(toml + off, sizeof(toml) - off, "\"pattern_%02d/**\"", i);
    }
    off += snprintf(toml + off, sizeof(toml) - off, "]\n");

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    // Should cap at 32
    TEST_ASSERT_EQ(ws.format_settings.exclude_count, 32);

    // First and last kept patterns should be correct
    TEST_ASSERT_STR_EQ(ws.format_settings.exclude_patterns[0], "pattern_00/**");
    TEST_ASSERT_STR_EQ(ws.format_settings.exclude_patterns[31], "pattern_31/**");

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}
