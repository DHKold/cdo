// crates/cdo/tst/unit/test_shader_removal.c
// Unit tests for shader command removal / deprecation
// Requirements: 5.1, 5.2
#include "cdo_ut.h"
#include "core/cli.h"

#include <stdio.h>
#include <string.h>

/* ============================================================
 * Requirement 5.1: `cdo shader` is parsed as CDO_CMD_SHADER
 * and the dispatch returns exit code 1.
 *
 * At the unit level we verify:
 * - The CLI parser still recognizes "shader" (for deprecation handling)
 * - It maps to CDO_CMD_SHADER
 * ============================================================ */

TEST(shader_removal_parse_cdo_shader) {
    char* argv[] = {"cdo", "shader"};
    CdoOptions opts = {0};
    int rc = cdo_cli_parse(2, argv, &opts);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(opts.command, CDO_CMD_SHADER);
    return 0;
}

/* ============================================================
 * Requirement 5.1: `cdo shader build` is parsed as CDO_CMD_SHADER
 * with "build" as a positional argument.
 * The dispatch returns exit code 1 regardless of subcommand.
 * ============================================================ */

TEST(shader_removal_parse_cdo_shader_build) {
    char* argv[] = {"cdo", "shader", "build"};
    CdoOptions opts = {0};
    int rc = cdo_cli_parse(3, argv, &opts);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(opts.command, CDO_CMD_SHADER);
    TEST_ASSERT_EQ(opts.positional_count, 1);
    TEST_ASSERT_STR_EQ(opts.positional_args[0], "build");
    return 0;
}

/* ============================================================
 * Requirement 5.2: Help output does not contain "shader" command.
 *
 * Capture `cdo_cli_print_help(CDO_CMD_HELP, ...)` output and
 * verify "shader" does not appear anywhere in the listing.
 * ============================================================ */

TEST(shader_removal_help_does_not_list_shader) {
    const char* out_file = "__test_shader_removal_help__.txt";
    FILE* f = fopen(out_file, "w");
    TEST_ASSERT(f != NULL);

    cdo_cli_print_help(CDO_CMD_HELP, f);
    fclose(f);

    // Read back the file and check for "shader"
    f = fopen(out_file, "r");
    TEST_ASSERT(f != NULL);

    char buf[4096] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    // Clean up temp file
    remove(out_file);

    // The help output must not contain "shader" anywhere
    TEST_ASSERT(strstr(buf, "shader") == NULL);
    return 0;
}

/* ============================================================
 * Requirement 5.2: CDO_CMD_SHADER help case produces no output.
 *
 * When help is requested for the shader command specifically,
 * the deprecated handler should output nothing.
 * ============================================================ */

TEST(shader_removal_shader_help_produces_no_output) {
    const char* out_file = "__test_shader_removal_shader_help__.txt";
    FILE* f = fopen(out_file, "w");
    TEST_ASSERT(f != NULL);

    cdo_cli_print_help(CDO_CMD_SHADER, f);
    fclose(f);

    // Read back the file and check it's empty
    f = fopen(out_file, "r");
    TEST_ASSERT(f != NULL);

    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    // Clean up temp file
    remove(out_file);

    // The deprecated shader help should produce no output
    TEST_ASSERT_EQ((int)n, 0);
    return 0;
}

/* ============================================================
 * Requirement 5.2: "shader" is not suggested by cdo_cli_suggest.
 *
 * Verify that typing something close to "shader" (e.g. "shade")
 * does NOT suggest "shader" as a valid command.
 * ============================================================ */

TEST(shader_removal_not_in_suggestions) {
    char suggestions[8][32] = {0};
    int count = cdo_cli_suggest("shade", suggestions, 8);

    // If any suggestions are returned, none should be "shader"
    for (int i = 0; i < count; i++) {
        TEST_ASSERT(strcmp(suggestions[i], "shader") != 0);
    }
    return 0;
}

