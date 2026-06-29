// crates/cdo/tst/unit/test_fmt_invoke.c
// Unit tests for formatter discovery and invocation logic
// Validates: Requirements 1.2, 1.4, 1.5, 3.2, 3.3, 4.1, 4.2
#include "cdo_ut.h"
#include "commands/cmd_fmt.h"
#include "model/fmt_settings.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
// Helpers
// =============================================================================

static void get_temp_dir(char* buf, size_t size, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(buf, size, "%s/cdo_test_fmt_inv_%s", tmp, suffix);
    pal_path_normalize(buf);
}

/// Create a dummy file with minimal content at the given path.
/// Automatically creates parent directories.
static int create_dummy_file(const char* dir, const char* filename) {
    char filepath[520];
    if (pal_path_join(filepath, sizeof(filepath), dir, filename) != 0) return -1;
    const char* content = "/* dummy */\n";
    return pal_file_write(filepath, content, strlen(content));
}

static void cleanup_dir(const char* root) {
    pal_rmdir_r(root);
}

/// Try to find clang-format on this machine. Returns true if available.
static bool has_clang_format(void) {
    const char* args[] = { "--version" };
    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.program = "clang-format";
    opts.args = args;
    opts.arg_count = 1;
    opts.capture_output = true;
    opts.timeout_ms = 5000;

    PalSpawnResult result;
    memset(&result, 0, sizeof(result));
    int rc = pal_spawn(&opts, &result);
    bool found = (rc == 0 && result.exit_code == 0);
    pal_spawn_result_free(&result);
    return found;
}

// =============================================================================
// Test: Formatter found in .cdo/tools/ → used
// Requirement 4.1: search .cdo/tools/clang-format/ first
// =============================================================================

TEST_SERIAL(fmt_find_formatter_in_cdo_tools) {
    char root[520];
    get_temp_dir(root, sizeof(root), "cdo_tools");
    cleanup_dir(root);
    pal_mkdir_p(root);

    // Create .cdo/tools/clang-format/clang-format.exe dummy file
    char tools_dir[520];
    pal_path_join(tools_dir, sizeof(tools_dir), root, ".cdo/tools/clang-format");
    pal_mkdir_p(tools_dir);
    TEST_ASSERT_EQ(create_dummy_file(tools_dir, "clang-format.exe"), 0);

    FmtSettings settings = {0};
    char out_path[260] = {0};

    int rc = fmt_find_formatter(root, &settings, out_path, sizeof(out_path));
    TEST_ASSERT_EQ(rc, 0);

    // Verify the returned path contains the workspace-local tool
    TEST_ASSERT(strstr(out_path, ".cdo/tools/clang-format/clang-format.exe") != NULL);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: Formatter found on PATH → used
// Requirement 4.1: fall back to system PATH
// Note: If clang-format is not on the test machine's PATH, this test verifies
// the function doesn't crash and returns an appropriate error code.
// =============================================================================

TEST_SERIAL(fmt_find_formatter_on_path) {
    char root[520];
    get_temp_dir(root, sizeof(root), "on_path");
    cleanup_dir(root);
    pal_mkdir_p(root);

    // No local tools, no configured path — falls through to PATH check
    FmtSettings settings = {0};
    char out_path[260] = {0};

    int rc = fmt_find_formatter(root, &settings, out_path, sizeof(out_path));

    if (has_clang_format()) {
        // clang-format is on PATH — should find it
        TEST_ASSERT_EQ(rc, 0);
        TEST_ASSERT(strstr(out_path, "clang-format") != NULL);
    } else {
        // clang-format not on PATH — should return error (non-zero)
        TEST_ASSERT(rc != 0);
    }

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: Formatter not found → returns error
// Requirement 4.2: error message with install suggestion when not found
// =============================================================================

TEST_SERIAL(fmt_find_formatter_not_found) {
    char root[520];
    get_temp_dir(root, sizeof(root), "not_found");
    cleanup_dir(root);
    pal_mkdir_p(root);

    // Point tool_path to a file that does not exist → that fails
    // Then no .cdo/tools/ dir exists, and PATH check depends on the machine.
    // To guarantee "not found", set tool_path to a non-existent path.
    // The function checks tool_path first; if it doesn't exist, returns error
    // immediately without checking local tools or PATH.
    FmtSettings settings = {0};
    strncpy(settings.tool_path, "/nonexistent/path/to/formatter", sizeof(settings.tool_path) - 1);

    char out_path[260] = {0};
    int rc = fmt_find_formatter(root, &settings, out_path, sizeof(out_path));

    // Should return error since configured path doesn't exist
    TEST_ASSERT(rc != 0);
    // out_path should remain empty
    TEST_ASSERT_EQ((int)out_path[0], 0);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: Configured tool_path in settings → uses that path directly
// Requirement 4.1 (tool-path override)
// =============================================================================

TEST_SERIAL(fmt_find_formatter_configured_path) {
    char root[520];
    get_temp_dir(root, sizeof(root), "cfg_path");
    cleanup_dir(root);
    pal_mkdir_p(root);

    // Create a file at a custom path to simulate a configured formatter
    char custom_dir[520];
    pal_path_join(custom_dir, sizeof(custom_dir), root, "my_tools");
    pal_mkdir_p(custom_dir);

    char custom_formatter[520];
    pal_path_join(custom_formatter, sizeof(custom_formatter), custom_dir, "my-formatter.exe");
    const char* content = "fake formatter";
    pal_file_write(custom_formatter, content, strlen(content));

    // Configure settings to point at that file
    FmtSettings settings = {0};
    strncpy(settings.tool_path, custom_formatter, sizeof(settings.tool_path) - 1);

    char out_path[260] = {0};
    int rc = fmt_find_formatter(root, &settings, out_path, sizeof(out_path));

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_STR_EQ(out_path, custom_formatter);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: fmt_invoke with file_count=0 → returns -1 (invalid args)
// Requirement 1.2: invoke formatter on files
// =============================================================================

TEST(fmt_invoke_no_files) {
    FmtSettings settings = {0};
    FmtInvokeResult result = {0};
    char files[1][260];
    memset(files, 0, sizeof(files));

    // file_count = 0 → should return -1 (invalid args)
    int rc = fmt_invoke("clang-format", &settings, files, 0, false, &result);
    TEST_ASSERT_EQ(rc, -1);

    return 0;
}

// =============================================================================
// Test: FmtInvokeResult initialized to zeros works correctly
// Validates the result struct can be zero-initialized and accumulates
// =============================================================================

TEST(fmt_invoke_result_init) {
    FmtInvokeResult result = {0};

    TEST_ASSERT_EQ(result.formatted, 0);
    TEST_ASSERT_EQ(result.conformant, 0);
    TEST_ASSERT_EQ(result.errored, 0);
    TEST_ASSERT_EQ(result.nonconformant, 0);

    // Simulate accumulation (as would happen across multiple batches)
    result.formatted += 3;
    result.conformant += 5;
    result.errored += 1;
    result.nonconformant += 2;

    TEST_ASSERT_EQ(result.formatted, 3);
    TEST_ASSERT_EQ(result.conformant, 5);
    TEST_ASSERT_EQ(result.errored, 1);
    TEST_ASSERT_EQ(result.nonconformant, 2);

    return 0;
}

// =============================================================================
// Test: fmt_invoke with NULL arguments → returns -1
// Validates defensive null-checking
// =============================================================================

TEST(fmt_invoke_null_args) {
    FmtSettings settings = {0};
    FmtInvokeResult result = {0};
    char files[1][260];
    memset(files, 0, sizeof(files));

    // NULL formatter_path
    TEST_ASSERT_EQ(fmt_invoke(NULL, &settings, files, 1, false, &result), -1);

    // NULL settings
    TEST_ASSERT_EQ(fmt_invoke("clang-format", NULL, files, 1, false, &result), -1);

    // NULL files
    TEST_ASSERT_EQ(fmt_invoke("clang-format", &settings, NULL, 1, false, &result), -1);

    // NULL result
    TEST_ASSERT_EQ(fmt_invoke("clang-format", &settings, files, 1, false, NULL), -1);

    return 0;
}

// =============================================================================
// Test: Style argument passed correctly (non-crash, settings with style)
// Requirement 5.3: --style=<value> passed to formatter
// Note: Without actual clang-format, we verify the function doesn't crash
// when a style is configured. If clang-format is available, we do a real run.
// =============================================================================

TEST_SERIAL(fmt_invoke_style_argument) {
    if (!has_clang_format()) {
        // Can't test actual invocation without clang-format installed.
        // Verify the function doesn't crash with a bogus formatter path.
        FmtSettings settings = {0};
        strncpy(settings.style, "llvm", sizeof(settings.style) - 1);

        FmtInvokeResult result = {0};
        char files[1][260];
        strncpy(files[0], "nonexistent_file.c", sizeof(files[0]) - 1);

        // Invoke with a non-existent formatter — should fail gracefully
        int rc = fmt_invoke("/nonexistent/formatter", &settings, files, 1, false, &result);
        // Non-zero return because spawn will fail
        TEST_ASSERT(rc != 0);
        // Errored count should be incremented
        TEST_ASSERT_EQ(result.errored, 1);
        return 0;
    }

    // clang-format is available — create a real source file and format it with style
    char root[520];
    get_temp_dir(root, sizeof(root), "style_arg");
    cleanup_dir(root);
    pal_mkdir_p(root);

    char filepath[520];
    pal_path_join(filepath, sizeof(filepath), root, "test.c");
    const char* source = "int main(){return 0;}\n";
    pal_file_write(filepath, source, strlen(source));

    FmtSettings settings = {0};
    strncpy(settings.style, "llvm", sizeof(settings.style) - 1);

    FmtInvokeResult result = {0};
    char files[1][260];
    strncpy(files[0], filepath, sizeof(files[0]) - 1);

    int rc = fmt_invoke("clang-format", &settings, files, 1, false, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(result.formatted, 1);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: Check mode — conformant file → exit 0
// Requirement 3.3: all conformant → exit 0
// =============================================================================

TEST_SERIAL(fmt_invoke_check_conformant) {
    if (!has_clang_format()) return 0; // Skip if no clang-format

    char root[520];
    get_temp_dir(root, sizeof(root), "check_ok");
    cleanup_dir(root);
    pal_mkdir_p(root);

    // Write a well-formatted file (LLVM style)
    char filepath[520];
    pal_path_join(filepath, sizeof(filepath), root, "good.c");
    const char* source = "int main() { return 0; }\n";
    pal_file_write(filepath, source, strlen(source));

    FmtSettings settings = {0};
    strncpy(settings.style, "llvm", sizeof(settings.style) - 1);

    FmtInvokeResult result = {0};
    char files[1][260];
    strncpy(files[0], filepath, sizeof(files[0]) - 1);

    int rc = fmt_invoke("clang-format", &settings, files, 1, true, &result);

    // File is already conformant → exit 0
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(result.conformant, 1);
    TEST_ASSERT_EQ(result.nonconformant, 0);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: Check mode — non-conformant file → exit 1 + file listed
// Requirement 3.2: non-conformant files → exit 1 with file list
// =============================================================================

TEST_SERIAL(fmt_invoke_check_nonconformant) {
    if (!has_clang_format()) return 0; // Skip if no clang-format

    char root[520];
    get_temp_dir(root, sizeof(root), "check_bad");
    cleanup_dir(root);
    pal_mkdir_p(root);

    // Write a poorly-formatted file
    char filepath[520];
    pal_path_join(filepath, sizeof(filepath), root, "bad.c");
    const char* source = "int    main(   ){return       0;}\n";
    pal_file_write(filepath, source, strlen(source));

    FmtSettings settings = {0};
    strncpy(settings.style, "llvm", sizeof(settings.style) - 1);

    FmtInvokeResult result = {0};
    char files[1][260];
    strncpy(files[0], filepath, sizeof(files[0]) - 1);

    int rc = fmt_invoke("clang-format", &settings, files, 1, true, &result);

    // File would be changed → exit non-zero
    TEST_ASSERT(rc != 0);
    TEST_ASSERT(result.nonconformant > 0);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: Formatter error on a file → reported, continues, exits non-zero
// Requirement 1.5: formatter error → report, continue, exit non-zero
// =============================================================================

TEST_SERIAL(fmt_invoke_formatter_error) {
    if (!has_clang_format()) return 0; // Skip if no clang-format

    char root[520];
    get_temp_dir(root, sizeof(root), "fmt_err");
    cleanup_dir(root);
    pal_mkdir_p(root);

    // Create a file with extremely malformed content that clang-format can't parse
    // Note: clang-format is quite forgiving, so we use a non-existent file path
    // to trigger an error (clang-format errors when the input file doesn't exist)
    FmtSettings settings = {0};
    FmtInvokeResult result = {0};
    char files[1][260];
    snprintf(files[0], sizeof(files[0]), "%s/this_file_does_not_exist.c", root);

    int rc = fmt_invoke("clang-format", &settings, files, 1, false, &result);

    // Non-existent file → formatter returns error
    TEST_ASSERT(rc != 0);
    TEST_ASSERT(result.errored > 0);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: fmt_find_formatter with NULL arguments → returns error
// =============================================================================

TEST(fmt_find_formatter_null_args) {
    FmtSettings settings = {0};
    char out_path[260] = {0};

    // NULL ws_root
    TEST_ASSERT(fmt_find_formatter(NULL, &settings, out_path, sizeof(out_path)) != 0);

    // NULL settings
    TEST_ASSERT(fmt_find_formatter("/tmp", NULL, out_path, sizeof(out_path)) != 0);

    // NULL out_path
    TEST_ASSERT(fmt_find_formatter("/tmp", &settings, NULL, 260) != 0);

    // Zero path_size
    TEST_ASSERT(fmt_find_formatter("/tmp", &settings, out_path, 0) != 0);

    return 0;
}
