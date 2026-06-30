// crates/cdo/tst/unit/test_install_paths.c
// Unit tests for install path resolution helpers: install_resolve_base_dir, install_resolve_bin_dir.
// Validates: Task 4.3 — default path, --path override, --global platform dir, bin dir resolution.
#include "cdo_ut.h"
#include "commands/cmd_install_internal.h"
#include "core/cli_arg_access.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Build a CliParseResult from an array of CliArgValues.
static CliParseResult make_parse_result(CliArgValue* vals, int count) {
    CliParseResult r;
    memset(&r, 0, sizeof(r));
    r.arg_values = vals;
    r.arg_value_count = count;
    return r;
}

/// Build a CliArgValue for a bool flag.
static CliArgValue make_bool_arg(const char* name, bool val, bool present) {
    CliArgValue v;
    memset(&v, 0, sizeof(v));
    v.name = name;
    v.type = CLI_ARG_BOOL;
    v.value.bool_val = val;
    v.present = present;
    return v;
}

/// Build a CliArgValue for a string option.
static CliArgValue make_str_arg(const char* name, const char* val, bool present) {
    CliArgValue v;
    memset(&v, 0, sizeof(v));
    v.name = name;
    v.type = CLI_ARG_STRING;
    v.value.str_val = val;
    v.present = present;
    return v;
}

// ---------------------------------------------------------------------------
// Test: default base dir resolves to ~/.cdo/
// ---------------------------------------------------------------------------

TEST_SERIAL(install_paths_default_base_dir) {
    // No --path, no --global => default to ~/.cdo/
    CliArgValue args[2];
    memset(args, 0, sizeof(args));
    args[0].name = "path";
    args[0].type = CLI_ARG_STRING;
    args[0].value.str_val = NULL;
    args[0].present = false;
    args[1].name = "global";
    args[1].type = CLI_ARG_BOOL;
    args[1].value.bool_val = false;
    args[1].present = false;

    CliParseResult result;
    memset(&result, 0, sizeof(result));
    result.arg_values = args;
    result.arg_value_count = 2;

    // Compute expected first
    char expected[512];
    char home[512];
    TEST_ASSERT_EQ(pal_get_home_dir(home, sizeof(home)), 0);
    TEST_ASSERT_EQ(pal_path_join(expected, sizeof(expected), home, ".cdo"), 0);

    char base_dir[512];
    memset(base_dir, 0, sizeof(base_dir));
    int rc = install_resolve_base_dir(&result, base_dir, sizeof(base_dir));
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_STR_EQ(base_dir, expected);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: --path /custom/dir resolves to the provided path
// ---------------------------------------------------------------------------

TEST(install_paths_custom_path_option) {
    const char* custom = "C:/my/custom/install";
    CliArgValue args[] = {
        make_str_arg("path", custom, true),
        make_bool_arg("global", false, false),
    };
    CliParseResult result = make_parse_result(args, 2);

    char base_dir[512] = {0};
    int rc = install_resolve_base_dir(&result, base_dir, sizeof(base_dir));
    TEST_ASSERT_EQ(rc, 0);

    // Path should be normalized version of what was given
    char expected[512];
    strncpy(expected, custom, sizeof(expected) - 1);
    expected[sizeof(expected) - 1] = '\0';
    pal_path_normalize(expected);

    TEST_ASSERT_STR_EQ(base_dir, expected);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: --global resolves to platform-specific system dir
// On Windows: %LOCALAPPDATA%\Programs\cdo
// ---------------------------------------------------------------------------

TEST(install_paths_global_base_dir) {
    CliArgValue args[] = {
        make_str_arg("path", NULL, false),
        make_bool_arg("global", true, true),
    };
    CliParseResult result = make_parse_result(args, 2);

    char base_dir[512] = {0};
    int rc = install_resolve_base_dir(&result, base_dir, sizeof(base_dir));
    TEST_ASSERT_EQ(rc, 0);

#ifdef _WIN32
    // Expected: <LOCALAPPDATA>/Programs/cdo (normalized)
    const char* local_app_data = getenv("LOCALAPPDATA");
    TEST_ASSERT(local_app_data != NULL);
    char expected[512];
    snprintf(expected, sizeof(expected), "%s/Programs/cdo", local_app_data);
    pal_path_normalize(expected);
    TEST_ASSERT_STR_EQ(base_dir, expected);
#else
    TEST_ASSERT_STR_EQ(base_dir, "/usr/local/lib/cdo");
#endif

    return 0;
}

// ---------------------------------------------------------------------------
// Test: --path takes priority over --global
// ---------------------------------------------------------------------------

TEST(install_paths_path_overrides_global) {
    const char* custom = "D:/overridden/path";
    CliArgValue args[] = {
        make_str_arg("path", custom, true),
        make_bool_arg("global", true, true),
    };
    CliParseResult result = make_parse_result(args, 2);

    char base_dir[512] = {0};
    int rc = install_resolve_base_dir(&result, base_dir, sizeof(base_dir));
    TEST_ASSERT_EQ(rc, 0);

    char expected[512];
    strncpy(expected, custom, sizeof(expected) - 1);
    expected[sizeof(expected) - 1] = '\0';
    pal_path_normalize(expected);

    TEST_ASSERT_STR_EQ(base_dir, expected);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: resolve_bin_dir default returns <base>/bin/
// ---------------------------------------------------------------------------

TEST(install_paths_bin_dir_default) {
    CliArgValue args[] = {
        make_str_arg("path", NULL, false),
        make_bool_arg("global", false, false),
    };
    CliParseResult result = make_parse_result(args, 2);

    const char* base = "C:/Users/test/.cdo";
    char bin_dir[512] = {0};
    int rc = install_resolve_bin_dir(&result, base, bin_dir, sizeof(bin_dir));
    TEST_ASSERT_EQ(rc, 0);

    char expected[512];
    pal_path_join(expected, sizeof(expected), base, "bin");

    TEST_ASSERT_STR_EQ(bin_dir, expected);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: resolve_bin_dir with --global
// On Unix: returns /usr/local/bin/
// On Windows: returns <base>/bin/ (same as default)
// ---------------------------------------------------------------------------

TEST(install_paths_bin_dir_global) {
    CliArgValue args[] = {
        make_str_arg("path", NULL, false),
        make_bool_arg("global", true, true),
    };
    CliParseResult result = make_parse_result(args, 2);

    const char* base = "/usr/local/lib/cdo";
    char bin_dir[512] = {0};
    int rc = install_resolve_bin_dir(&result, base, bin_dir, sizeof(bin_dir));
    TEST_ASSERT_EQ(rc, 0);

#ifdef _WIN32
    // On Windows, --global still uses <base>/bin/
    char expected[512];
    pal_path_join(expected, sizeof(expected), base, "bin");
    TEST_ASSERT_STR_EQ(bin_dir, expected);
#else
    // On Unix with --global (and no --path), uses /usr/local/bin
    TEST_ASSERT_STR_EQ(bin_dir, "/usr/local/bin");
#endif

    return 0;
}

// ---------------------------------------------------------------------------
// Test: resolve_bin_dir with --global and --path uses <base>/bin (path wins)
// ---------------------------------------------------------------------------

TEST(install_paths_bin_dir_global_with_path) {
    const char* custom = "/opt/my-install";
    CliArgValue args[] = {
        make_str_arg("path", custom, true),
        make_bool_arg("global", true, true),
    };
    CliParseResult result = make_parse_result(args, 2);

    char bin_dir[512] = {0};
    int rc = install_resolve_bin_dir(&result, custom, bin_dir, sizeof(bin_dir));
    TEST_ASSERT_EQ(rc, 0);

    // When --path is set, bin is always <base>/bin even if --global is also set
    char expected[512];
    pal_path_join(expected, sizeof(expected), custom, "bin");
    TEST_ASSERT_STR_EQ(bin_dir, expected);
    return 0;
}
