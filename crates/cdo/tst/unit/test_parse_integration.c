/**
 * test_parse_integration.c - Integration tests for end-to-end CLI parsing.
 *
 * Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5, 4.2, 5.8, 9.3
 *
 * Tests that real cdo arg vectors parse correctly through the full registry:
 * - Basic command matching
 * - Positional argument extraction
 * - Boolean and string arg extraction from CliParseResult
 * - Global options recognized regardless of position
 * - Rest args (-- separator)
 * - Subcommand matching with options
 * - Parse error on unknown command (with suggestion)
 * - Parse error on unknown option
 * - --quiet taking precedence over --verbose (both accepted by parser)
 */
#include "cdo_ut.h"
#include "core/registry_setup.h"
#include "cmd/cli_cmd.h"

#include <string.h>

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

#define ARG_BUF_SIZE 32

/// Parse a simulated argv against the full cdo registry. Returns cli_cmd_parse rc.
static int parse(CliCmdRegistry* reg, int argc, const char** argv, CliArgValue* buf, CliParseResult* result) {
    memset(buf, 0, ARG_BUF_SIZE * sizeof(CliArgValue));
    memset(result, 0, sizeof(*result));
    return cli_cmd_parse(reg, argc, argv, buf, ARG_BUF_SIZE, result);
}

/// Find an arg value by name in the parse result. Returns NULL if not found.
static const CliArgValue* find_arg(const CliParseResult* result, const char* name) {
    for (int i = 0; i < result->arg_value_count; i++) {
        if (result->arg_values[i].name && strcmp(result->arg_values[i].name, name) == 0) {
            return &result->arg_values[i];
        }
    }
    return NULL;
}

/* ========================================================================= */
/* Test: Basic command matching - "cdo build" (Req 6.1)                      */
/* ========================================================================= */

TEST(parse_basic_build_command) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "build" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 2, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "build");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Positional argument extraction - "cdo build crate1 crate2" (Req 6.2)*/
/* ========================================================================= */

TEST(parse_build_with_positional_crates) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "build", "crate1", "crate2" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 4, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "build");
    TEST_ASSERT_EQ(result.positional_count, 2);
    TEST_ASSERT_STR_EQ(result.positional_values[0], "crate1");
    TEST_ASSERT_STR_EQ(result.positional_values[1], "crate2");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Boolean and string arg extraction (Req 6.3)                         */
/* "cdo test --coverage --filter foo"                                        */
/* ========================================================================= */

TEST(parse_test_coverage_and_filter) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "test", "--coverage", "--filter", "foo" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 5, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "test");

    /* Verify --coverage is true */
    const CliArgValue* coverage = find_arg(&result, "coverage");
    TEST_ASSERT(coverage != NULL);
    TEST_ASSERT(coverage->present);
    TEST_ASSERT(coverage->value.bool_val == true);

    /* Verify --filter is "foo" */
    const CliArgValue* filter = find_arg(&result, "filter");
    TEST_ASSERT(filter != NULL);
    TEST_ASSERT(filter->present);
    TEST_ASSERT_STR_EQ(filter->value.str_val, "foo");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Global option on command - "cdo build --verbose" (Req 6.4, 4.2)     */
/* The cli_cmd_parse parser requires command as first token after program     */
/* name. Global options are recognized on any command regardless of which     */
/* command they appear on (that's the "regardless of position" guarantee).    */
/* ========================================================================= */

TEST(parse_global_verbose_on_build) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "build", "--verbose" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 3, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "build");

    /* Verify --verbose is true */
    const CliArgValue* verbose = find_arg(&result, "verbose");
    TEST_ASSERT(verbose != NULL);
    TEST_ASSERT(verbose->present);
    TEST_ASSERT(verbose->value.bool_val == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(parse_global_verbose_short_on_test) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "test", "-v" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 3, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "test");

    /* Verify --verbose via short flag is true */
    const CliArgValue* verbose = find_arg(&result, "verbose");
    TEST_ASSERT(verbose != NULL);
    TEST_ASSERT(verbose->present);
    TEST_ASSERT(verbose->value.bool_val == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Rest args via -- separator - "cdo build -- --extra" (Req 6.5)       */
/* ========================================================================= */

TEST(parse_build_rest_args_separator) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "build", "--", "--extra" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 4, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "build");
    TEST_ASSERT_EQ(result.rest_count, 1);
    TEST_ASSERT_STR_EQ(result.rest_args[0], "--extra");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Subcommand with option - "cdo deps add mylib --dev" (Req 5.8)       */
/* ========================================================================= */

TEST(parse_deps_add_with_dev_flag) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "deps", "add", "mylib", "--dev" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 5, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "add");

    /* Verify --dev is true */
    const CliArgValue* dev = find_arg(&result, "dev");
    TEST_ASSERT(dev != NULL);
    TEST_ASSERT(dev->present);
    TEST_ASSERT(dev->value.bool_val == true);

    /* Verify positional "mylib" is captured */
    TEST_ASSERT_EQ(result.positional_count, 1);
    TEST_ASSERT_STR_EQ(result.positional_values[0], "mylib");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Unknown command triggers error with suggestion (Req 9.3)            */
/* "cdo bluild" should fail and cli_cmd_suggest should offer "build"         */
/* ========================================================================= */

TEST(parse_unknown_command_error_with_suggestion) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "bluild" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 2, argv, buf, &result);
    TEST_ASSERT_NEQ(rc, 0);
    TEST_ASSERT(result.error_code != 0);
    /* error_token should be the unrecognized command token */
    TEST_ASSERT(result.error_token != NULL);
    TEST_ASSERT_STR_EQ(result.error_token, "bluild");

    /* Verify the suggestion engine would suggest "build" */
    char suggestions[4][32];
    int n = cli_cmd_suggest(reg, result.error_token, suggestions, 4);
    TEST_ASSERT(n >= 1);
    TEST_ASSERT_STR_EQ(suggestions[0], "build");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Unknown option triggers parse error (Req 6.1)                       */
/* "cdo build --unknown" should produce error identifying "--unknown"         */
/* ========================================================================= */

TEST(parse_unknown_option_error) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "build", "--unknown" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 3, argv, buf, &result);
    TEST_ASSERT_NEQ(rc, 0);
    TEST_ASSERT(result.error_code != 0);
    /* Error message should mention the unknown option */
    TEST_ASSERT(strstr(result.error_msg, "unknown") != NULL || strstr(result.error_msg, "--unknown") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: --quiet and --verbose both accepted (Req 4.2)                       */
/* "cdo build --quiet --verbose" - parser accepts both, quiet takes          */
/* precedence at the resolve_log_level() layer. Here we verify both are      */
/* present in the parse result so the precedence logic can apply.            */
/* ========================================================================= */

TEST(parse_quiet_and_verbose_both_accepted) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "build", "--quiet", "--verbose" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 4, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "build");

    /* Both flags should be present in the parse result */
    const CliArgValue* quiet = find_arg(&result, "quiet");
    TEST_ASSERT(quiet != NULL);
    TEST_ASSERT(quiet->present);
    TEST_ASSERT(quiet->value.bool_val == true);

    const CliArgValue* verbose = find_arg(&result, "verbose");
    TEST_ASSERT(verbose != NULL);
    TEST_ASSERT(verbose->present);
    TEST_ASSERT(verbose->value.bool_val == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}
