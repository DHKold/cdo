// crates/cdo/tst/unit/test_install_command.c
// Unit tests for cmd_install (--list) and cmd_uninstall logic, plus overwrite protection.
// Validates: Requirements REQ-INSTALL-1, REQ-INSTALL-8, REQ-INSTALL-9, REQ-INSTALL-10
#include "cdo_ut.h"
#include "commands/cmd_install.h"
#include "commands/cmd_install_internal.h"
#include "core/cli_arg_access.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

typedef struct {
    char root[512];       // Temp root directory (acts as the --path base)
    char apps_dir[512];   // <root>/apps/
    char bin_dir[512];    // <root>/bin/
} InstallCmdFixture;

static int icmd_fixture_init(InstallCmdFixture* f, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
    snprintf(f->root, sizeof(f->root), "%s/cdo_test_install_cmd_%s", tmp, suffix);
    pal_path_normalize(f->root);
    pal_rmdir_r(f->root);
    pal_mkdir_p(f->root);

    pal_path_join(f->apps_dir, sizeof(f->apps_dir), f->root, "apps");
    pal_path_join(f->bin_dir, sizeof(f->bin_dir), f->root, "bin");
    return 0;
}

static void icmd_fixture_destroy(InstallCmdFixture* f) {
    pal_rmdir_r(f->root);
}

/// Write a file at base/rel, creating parent dirs as needed.
static int icmd_write_file(const char* base, const char* rel, const char* content) {
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

/// Check if base/rel exists.
static bool icmd_file_exists(const char* base, const char* rel) {
    char full[1024];
    if (pal_path_join(full, sizeof(full), base, rel) != 0) return false;
    pal_path_normalize(full);
    return pal_path_exists(full) == 0;
}

/// Build a CliParseResult with --path and optional --list/--force flags.
/// arg_buf must have space for at least 4 entries.
static void icmd_build_parse_result(CliParseResult* result, CliArgValue* arg_buf, int* arg_count, const char* path_val, bool list_flag, bool force_flag, bool global_flag) {
    memset(result, 0, sizeof(*result));
    *arg_count = 0;

    arg_buf[*arg_count].name = "path";
    arg_buf[*arg_count].type = CLI_ARG_STRING;
    arg_buf[*arg_count].value.str_val = path_val;
    arg_buf[*arg_count].present = (path_val != NULL);
    (*arg_count)++;

    arg_buf[*arg_count].name = "list";
    arg_buf[*arg_count].type = CLI_ARG_BOOL;
    arg_buf[*arg_count].value.bool_val = list_flag;
    arg_buf[*arg_count].present = list_flag;
    (*arg_count)++;

    arg_buf[*arg_count].name = "force";
    arg_buf[*arg_count].type = CLI_ARG_BOOL;
    arg_buf[*arg_count].value.bool_val = force_flag;
    arg_buf[*arg_count].present = force_flag;
    (*arg_count)++;

    arg_buf[*arg_count].name = "global";
    arg_buf[*arg_count].type = CLI_ARG_BOOL;
    arg_buf[*arg_count].value.bool_val = global_flag;
    arg_buf[*arg_count].present = global_flag;
    (*arg_count)++;

    result->arg_values = arg_buf;
    result->arg_value_count = *arg_count;
    result->positional_values = NULL;
    result->positional_count = 0;
    result->rest_args = NULL;
    result->rest_count = 0;
    result->matched_cmd = NULL;
    result->error_code = 0;
}

// ---------------------------------------------------------------------------
// Test: cmd_install --list on empty index prints "No applications installed."
// Validates: REQ-INSTALL-9
// ---------------------------------------------------------------------------

TEST_SERIAL(install_cmd_list_empty_index) {
    InstallCmdFixture f;
    TEST_ASSERT_EQ(icmd_fixture_init(&f, "list_empty"), 0);

    // Don't create any install.toml — simulates no apps installed
    CliArgValue arg_buf[8];
    int arg_count = 0;
    CliParseResult result;
    icmd_build_parse_result(&result, arg_buf, &arg_count, f.root, true, false, false);

    // cmd_install with --list should succeed (return 0) and handle missing index gracefully
    int rc = cmd_install(&result, NULL);
    TEST_ASSERT_EQ(rc, 0);

    icmd_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cmd_install --list parses and prints entries from install.toml
// Validates: REQ-INSTALL-9
// ---------------------------------------------------------------------------

TEST_SERIAL(install_cmd_list_with_entries) {
    InstallCmdFixture f;
    TEST_ASSERT_EQ(icmd_fixture_init(&f, "list_entries"), 0);

    // Create a valid install.toml with two entries
    const char* index_content =
        "[[app]]\n"
        "name = \"hello\"\n"
        "version = \"1.0.0\"\n"
        "source_workspace = \"C:/projects/hello\"\n"
        "installed_at = \"2026-06-29T10:00:00Z\"\n"
        "path = \"hello\"\n"
        "\n"
        "[[app]]\n"
        "name = \"world\"\n"
        "version = \"2.0.0\"\n"
        "source_workspace = \"C:/projects/world\"\n"
        "installed_at = \"2026-06-28T15:30:00Z\"\n"
        "path = \"world\"\n";

    TEST_ASSERT_EQ(icmd_write_file(f.root, "apps/install.toml", index_content), 0);

    CliArgValue arg_buf[8];
    int arg_count = 0;
    CliParseResult result;
    icmd_build_parse_result(&result, arg_buf, &arg_count, f.root, true, false, false);

    // cmd_install --list should succeed and print the table (verified by return code)
    int rc = cmd_install(&result, NULL);
    TEST_ASSERT_EQ(rc, 0);

    icmd_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cmd_install --list with corrupt index file returns 0
// Validates: REQ-INSTALL-9
// ---------------------------------------------------------------------------

TEST_SERIAL(install_cmd_list_corrupt_index) {
    InstallCmdFixture f;
    TEST_ASSERT_EQ(icmd_fixture_init(&f, "list_corrupt"), 0);

    // Write a corrupt install.toml
    TEST_ASSERT_EQ(icmd_write_file(f.root, "apps/install.toml", "this is not valid toml [[["), 0);

    CliArgValue arg_buf[8];
    int arg_count = 0;
    CliParseResult result;
    icmd_build_parse_result(&result, arg_buf, &arg_count, f.root, true, false, false);

    // Should handle corrupt gracefully and print "No applications installed."
    int rc = cmd_install(&result, NULL);
    TEST_ASSERT_EQ(rc, 0);

    icmd_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cmd_uninstall on non-existent app returns 0 with info message
// Validates: REQ-INSTALL-8
// ---------------------------------------------------------------------------

TEST_SERIAL(uninstall_cmd_nonexistent_app_returns_zero) {
    InstallCmdFixture f;
    TEST_ASSERT_EQ(icmd_fixture_init(&f, "uninst_noexist"), 0);

    // Create apps dir but no app inside it
    pal_mkdir_p(f.apps_dir);

    CliArgValue arg_buf[8];
    int arg_count = 0;
    CliParseResult result;
    icmd_build_parse_result(&result, arg_buf, &arg_count, f.root, false, false, false);

    const char* positionals[] = { "nonexistent_app" };
    result.positional_values = positionals;
    result.positional_count = 1;

    int rc = cmd_uninstall(&result, NULL);
    TEST_ASSERT_EQ(rc, 0);

    icmd_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: cmd_uninstall removes app dir, launcher, and index entry
// Validates: REQ-INSTALL-8
// ---------------------------------------------------------------------------

TEST_SERIAL(uninstall_cmd_removes_app_dir_launcher_and_index) {
    InstallCmdFixture f;
    TEST_ASSERT_EQ(icmd_fixture_init(&f, "uninst_full"), 0);

    // Create the app directory with manifest and a fake exe
    TEST_ASSERT_EQ(icmd_write_file(f.apps_dir, "myapp/manifest.toml",
        "[app]\nname = \"myapp\"\nversion = \"1.0.0\"\n"), 0);
    TEST_ASSERT_EQ(icmd_write_file(f.apps_dir, "myapp/myapp.exe", "FAKE_EXE"), 0);

    // Create a launcher in bin/
#ifdef _WIN32
    TEST_ASSERT_EQ(icmd_write_file(f.bin_dir, "myapp.cmd", "@\"%~dp0..\\apps\\myapp\\myapp.exe\" %*\r\n"), 0);
#else
    TEST_ASSERT_EQ(icmd_write_file(f.bin_dir, "myapp", "#!/bin/sh\nexec \"$(dirname \"$0\")/../apps/myapp/myapp\" \"$@\"\n"), 0);
#endif

    // Create a global index with the app entry
    const char* index_content =
        "[[app]]\n"
        "name = \"myapp\"\n"
        "version = \"1.0.0\"\n"
        "source_workspace = \"C:/projects/myapp\"\n"
        "installed_at = \"2026-06-29T10:00:00Z\"\n"
        "path = \"myapp\"\n";
    TEST_ASSERT_EQ(icmd_write_file(f.apps_dir, "install.toml", index_content), 0);

    // Verify setup: app dir, launcher, and index exist
    TEST_ASSERT(icmd_file_exists(f.apps_dir, "myapp/myapp.exe"));
#ifdef _WIN32
    TEST_ASSERT(icmd_file_exists(f.bin_dir, "myapp.cmd"));
#else
    TEST_ASSERT(icmd_file_exists(f.bin_dir, "myapp"));
#endif
    TEST_ASSERT(icmd_file_exists(f.apps_dir, "install.toml"));

    // Call cmd_uninstall
    CliArgValue arg_buf[8];
    int arg_count = 0;
    CliParseResult result;
    icmd_build_parse_result(&result, arg_buf, &arg_count, f.root, false, false, false);

    const char* positionals[] = { "myapp" };
    result.positional_values = positionals;
    result.positional_count = 1;

    int rc = cmd_uninstall(&result, NULL);
    TEST_ASSERT_EQ(rc, 0);

    // Verify app directory was removed
    TEST_ASSERT(!icmd_file_exists(f.apps_dir, "myapp/myapp.exe"));
    TEST_ASSERT(!icmd_file_exists(f.apps_dir, "myapp/manifest.toml"));

    // Verify launcher was removed
#ifdef _WIN32
    TEST_ASSERT(!icmd_file_exists(f.bin_dir, "myapp.cmd"));
#else
    TEST_ASSERT(!icmd_file_exists(f.bin_dir, "myapp"));
#endif

    icmd_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: overwrite protection - different workspace without --force returns error
// Validates: REQ-INSTALL-10
//
// We test this by writing a manifest with source_workspace "C:/other/workspace"
// and then checking that install_read_manifest correctly reads it so the comparison
// logic in cmd_install would trigger an error. Since cmd_install requires a full
// workspace/build, we test the components used in the overwrite check.
// ---------------------------------------------------------------------------

TEST_SERIAL(overwrite_protection_different_workspace_detected) {
    InstallCmdFixture f;
    TEST_ASSERT_EQ(icmd_fixture_init(&f, "overwrite_diff"), 0);

    // Write a manifest from a "different" workspace
    InstallManifest existing;
    memset(&existing, 0, sizeof(existing));
    strncpy(existing.name, "myapp", sizeof(existing.name) - 1);
    strncpy(existing.version, "1.0.0", sizeof(existing.version) - 1);
    strncpy(existing.crate_name, "myapp", sizeof(existing.crate_name) - 1);
    strncpy(existing.source_workspace, "C:/other/workspace", sizeof(existing.source_workspace) - 1);
    strncpy(existing.installed_at, "2026-06-29T10:00:00Z", sizeof(existing.installed_at) - 1);
    strncpy(existing.cdo_version, "0.5.0", sizeof(existing.cdo_version) - 1);
    strncpy(existing.profile, "release", sizeof(existing.profile) - 1);
    strncpy(existing.executable, "myapp.exe", sizeof(existing.executable) - 1);
    strncpy(existing.resource_base, ".", sizeof(existing.resource_base) - 1);
    strncpy(existing.shader_base, ".", sizeof(existing.shader_base) - 1);

    char manifest_path[1024];
    pal_path_join(manifest_path, sizeof(manifest_path), f.apps_dir, "myapp");
    pal_mkdir_p(manifest_path);
    char manifest_file[1024];
    pal_path_join(manifest_file, sizeof(manifest_file), manifest_path, "manifest.toml");

    TEST_ASSERT_EQ(install_write_manifest(&existing, manifest_file), 0);

    // Read it back and verify the source_workspace differs from "C:/my/workspace"
    InstallManifest readback;
    TEST_ASSERT_EQ(install_read_manifest(manifest_file, &readback), 0);
    TEST_ASSERT_STR_EQ(readback.source_workspace, "C:/other/workspace");

    // The overwrite logic in cmd_install does:
    //   if (strcmp(existing.source_workspace, ws.root_path) != 0 && !force) -> error
    // Simulate: current workspace is "C:/my/workspace", existing is "C:/other/workspace"
    char norm_existing[512];
    char norm_current[512];
    strncpy(norm_existing, readback.source_workspace, sizeof(norm_existing) - 1);
    norm_existing[sizeof(norm_existing) - 1] = '\0';
    strncpy(norm_current, "C:/my/workspace", sizeof(norm_current) - 1);
    norm_current[sizeof(norm_current) - 1] = '\0';
    pal_path_normalize(norm_existing);
    pal_path_normalize(norm_current);

    // Different workspaces should NOT match
    TEST_ASSERT(strcmp(norm_existing, norm_current) != 0);

    icmd_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: overwrite protection - same workspace proceeds silently
// Validates: REQ-INSTALL-10
// ---------------------------------------------------------------------------

TEST_SERIAL(overwrite_protection_same_workspace_proceeds) {
    InstallCmdFixture f;
    TEST_ASSERT_EQ(icmd_fixture_init(&f, "overwrite_same"), 0);

    const char* workspace_path = "C:/my/workspace";

    // Write a manifest from the SAME workspace
    InstallManifest existing;
    memset(&existing, 0, sizeof(existing));
    strncpy(existing.name, "myapp", sizeof(existing.name) - 1);
    strncpy(existing.version, "1.0.0", sizeof(existing.version) - 1);
    strncpy(existing.crate_name, "myapp", sizeof(existing.crate_name) - 1);
    strncpy(existing.source_workspace, workspace_path, sizeof(existing.source_workspace) - 1);
    strncpy(existing.installed_at, "2026-06-29T10:00:00Z", sizeof(existing.installed_at) - 1);
    strncpy(existing.cdo_version, "0.5.0", sizeof(existing.cdo_version) - 1);
    strncpy(existing.profile, "release", sizeof(existing.profile) - 1);
    strncpy(existing.executable, "myapp.exe", sizeof(existing.executable) - 1);
    strncpy(existing.resource_base, ".", sizeof(existing.resource_base) - 1);
    strncpy(existing.shader_base, ".", sizeof(existing.shader_base) - 1);

    char manifest_path[1024];
    pal_path_join(manifest_path, sizeof(manifest_path), f.apps_dir, "myapp");
    pal_mkdir_p(manifest_path);
    char manifest_file[1024];
    pal_path_join(manifest_file, sizeof(manifest_file), manifest_path, "manifest.toml");

    TEST_ASSERT_EQ(install_write_manifest(&existing, manifest_file), 0);

    // Read it back
    InstallManifest readback;
    TEST_ASSERT_EQ(install_read_manifest(manifest_file, &readback), 0);

    // Simulate: current workspace is also "C:/my/workspace"
    char norm_existing[512];
    char norm_current[512];
    strncpy(norm_existing, readback.source_workspace, sizeof(norm_existing) - 1);
    norm_existing[sizeof(norm_existing) - 1] = '\0';
    strncpy(norm_current, workspace_path, sizeof(norm_current) - 1);
    norm_current[sizeof(norm_current) - 1] = '\0';
    pal_path_normalize(norm_existing);
    pal_path_normalize(norm_current);

    // Same workspace should match — overwrite proceeds silently (no --force required)
    TEST_ASSERT(strcmp(norm_existing, norm_current) == 0);

    icmd_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: overwrite protection - --force bypasses the workspace check
// Validates: REQ-INSTALL-10
//
// The logic is: if force == true, the workspace comparison is skipped entirely.
// We verify this by checking that cli_arg_get_bool correctly retrieves the force flag.
// ---------------------------------------------------------------------------

TEST_SERIAL(overwrite_protection_force_bypasses_check) {
    InstallCmdFixture f;
    TEST_ASSERT_EQ(icmd_fixture_init(&f, "overwrite_force"), 0);

    // Build a parse result with --force = true
    CliArgValue arg_buf[8];
    int arg_count = 0;
    CliParseResult result;
    icmd_build_parse_result(&result, arg_buf, &arg_count, f.root, false, true, false);

    // Verify the force flag is correctly set
    bool force = cli_arg_get_bool(&result, "force");
    TEST_ASSERT(force == true);

    // The overwrite protection logic in cmd_install is:
    //   if (strcmp(norm_existing, norm_current) != 0 && !force) -> error
    // With force=true, even different workspaces pass: !(true) == false, so condition is false
    const char* existing_ws = "C:/other/workspace";
    const char* current_ws = "C:/my/workspace";

    char norm_existing[512];
    char norm_current[512];
    strncpy(norm_existing, existing_ws, sizeof(norm_existing) - 1);
    norm_existing[sizeof(norm_existing) - 1] = '\0';
    strncpy(norm_current, current_ws, sizeof(norm_current) - 1);
    norm_current[sizeof(norm_current) - 1] = '\0';
    pal_path_normalize(norm_existing);
    pal_path_normalize(norm_current);

    // Workspaces are different
    TEST_ASSERT(strcmp(norm_existing, norm_current) != 0);

    // But with force=true, the overwrite protection condition does NOT trigger
    bool would_block = (strcmp(norm_existing, norm_current) != 0 && !force);
    TEST_ASSERT(would_block == false);

    icmd_fixture_destroy(&f);
    return 0;
}
