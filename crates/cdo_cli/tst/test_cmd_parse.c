/**
 * test_cmd_parse.c - Unit tests for argument parsing (cli_cmd_parse).
 *
 * Covers: command matching, long/short option parsing, combined booleans,
 * rest separator, unknown option errors, missing required argument errors,
 * zero-allocation path, subcommand resolution, empty argv, and positionals.
 *
 * Validates: Requirements 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 1.5, 1.6
 */

#include "cdo_ut.h"
#include "../api/cmd/cli_cmd.h"
#include "../api/cli_errors.h"

#include <string.h>

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

static CliCmdSpec make_spec(const char* name, const char* desc) {
    CliCmdSpec s;
    memset(&s, 0, sizeof(s));
    s.name = name;
    s.description = desc;
    return s;
}

/* ========================================================================= */
/* Test: Match command name from argv produces correct matched_cmd.          */
/* Requirement 2.1                                                           */
/* ========================================================================= */

TEST(parse_matches_command_name) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("build", "Build the project");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "build" };
    int argc = 2;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "build");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Long option --name=value parsing.                                   */
/* Requirement 2.2                                                           */
/* ========================================================================= */

TEST(parse_long_option_equals_value) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "output", .short_name = 'o', .type = CLI_ARG_STRING, .description = "Output path" },
    };
    CliCmdSpec spec = make_spec("build", "Build the project");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "build", "--output=dist/bin" };
    int argc = 3;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT(result.arg_value_count >= 1);
    TEST_ASSERT_STR_EQ(result.arg_values[0].name, "output");
    TEST_ASSERT_EQ(result.arg_values[0].type, CLI_ARG_STRING);
    TEST_ASSERT_STR_EQ(result.arg_values[0].value.str_val, "dist/bin");
    TEST_ASSERT(result.arg_values[0].present == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Long option --name value (space-separated) parsing.                 */
/* Requirement 2.2                                                           */
/* ========================================================================= */

TEST(parse_long_option_space_value) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "target", .short_name = 't', .type = CLI_ARG_STRING, .description = "Target name" },
    };
    CliCmdSpec spec = make_spec("build", "Build the project");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "build", "--target", "release" };
    int argc = 4;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT(result.arg_value_count >= 1);
    TEST_ASSERT_STR_EQ(result.arg_values[0].name, "target");
    TEST_ASSERT_STR_EQ(result.arg_values[0].value.str_val, "release");
    TEST_ASSERT(result.arg_values[0].present == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Short option -x value (space-separated) parsing.                    */
/* Requirement 2.3                                                           */
/* ========================================================================= */

TEST(parse_short_option_space_value) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "output", .short_name = 'o', .type = CLI_ARG_STRING, .description = "Output path" },
    };
    CliCmdSpec spec = make_spec("build", "Build the project");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "build", "-o", "/tmp/out" };
    int argc = 4;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT(result.arg_value_count >= 1);
    TEST_ASSERT_STR_EQ(result.arg_values[0].name, "output");
    TEST_ASSERT_STR_EQ(result.arg_values[0].value.str_val, "/tmp/out");
    TEST_ASSERT(result.arg_values[0].present == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Short option -xvalue (attached value) parsing.                      */
/* Requirement 2.3                                                           */
/* ========================================================================= */

TEST(parse_short_option_attached_value) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "output", .short_name = 'o', .type = CLI_ARG_STRING, .description = "Output path" },
    };
    CliCmdSpec spec = make_spec("build", "Build the project");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "build", "-o/tmp/out" };
    int argc = 3;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT(result.arg_value_count >= 1);
    TEST_ASSERT_STR_EQ(result.arg_values[0].name, "output");
    TEST_ASSERT_STR_EQ(result.arg_values[0].value.str_val, "/tmp/out");
    TEST_ASSERT(result.arg_values[0].present == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Combined short booleans -abc expanded to individual flags.           */
/* Requirement 2.4                                                           */
/* ========================================================================= */

TEST(parse_combined_short_booleans) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "alpha",   .short_name = 'a', .type = CLI_ARG_BOOL, .description = "Alpha flag" },
        { .long_name = "bravo",   .short_name = 'b', .type = CLI_ARG_BOOL, .description = "Bravo flag" },
        { .long_name = "charlie", .short_name = 'c', .type = CLI_ARG_BOOL, .description = "Charlie flag" },
    };
    CliCmdSpec spec = make_spec("run", "Run something");
    spec.args = args;
    spec.arg_count = 3;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "run", "-abc" };
    int argc = 3;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_EQ(result.arg_value_count, 3);

    /* All three flags should be present and true */
    for (int i = 0; i < result.arg_value_count; i++) {
        TEST_ASSERT(result.arg_values[i].present == true);
        TEST_ASSERT(result.arg_values[i].value.bool_val == true);
    }

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Rest separator -- captures remaining tokens in rest_args.            */
/* Requirement 1.6                                                           */
/* ========================================================================= */

TEST(parse_rest_separator_captures_remaining) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("run", "Run a command");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "run", "--", "extra1", "extra2", "extra3" };
    int argc = 6;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_EQ(result.rest_count, 3);
    TEST_ASSERT_STR_EQ(result.rest_args[0], "extra1");
    TEST_ASSERT_STR_EQ(result.rest_args[1], "extra2");
    TEST_ASSERT_STR_EQ(result.rest_args[2], "extra3");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Unknown option returns CLI_ERR_PARSE with token in error_token.      */
/* Requirement 2.6                                                           */
/* ========================================================================= */

TEST(parse_unknown_option_returns_error) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("build", "Build the project");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "build", "--unknown-flag" };
    int argc = 3;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_ERR_PARSE);
    TEST_ASSERT_EQ(result.error_code, CLI_ERR_PARSE);
    TEST_ASSERT(result.error_token != NULL);
    TEST_ASSERT_STR_EQ(result.error_token, "--unknown-flag");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Missing required argument returns CLI_ERR_VALIDATE with message.     */
/* Requirement 2.5                                                           */
/* ========================================================================= */

TEST(parse_missing_required_argument_returns_error) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "name", .short_name = 'n', .type = CLI_ARG_STRING, .description = "Project name", .required = true },
    };
    CliCmdSpec spec = make_spec("init", "Initialize project");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    /* Invoke without providing --name */
    const char* argv[] = { "prog", "init" };
    int argc = 2;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_ERR_VALIDATE);
    TEST_ASSERT_EQ(result.error_code, CLI_ERR_VALIDATE);
    /* error_msg should contain something descriptive */
    TEST_ASSERT(strlen(result.error_msg) > 0);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Zero-allocation path - result fits in caller-provided buffer.        */
/* Requirement 2.7                                                           */
/* ========================================================================= */

TEST(parse_zero_alloc_result_in_caller_buffer) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "verbose", .short_name = 'v', .type = CLI_ARG_BOOL, .description = "Verbose" },
        { .long_name = "output",  .short_name = 'o', .type = CLI_ARG_STRING, .description = "Output" },
    };
    CliCmdSpec spec = make_spec("build", "Build the project");
    spec.args = args;
    spec.arg_count = 2;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "build", "-v", "--output=out.bin" };
    int argc = 4;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    /* Verify the result uses our caller-provided buffer (pointer equality) */
    TEST_ASSERT(result.arg_values == result_buf);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Subcommand resolution (e.g., "deps add" matches nested spec).       */
/* Requirements 1.2, 2.1                                                     */
/* ========================================================================= */

TEST(parse_subcommand_resolution) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    /* Register parent "deps" */
    CliCmdSpec deps = make_spec("deps", "Dependency management");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &deps), CLI_OK);

    /* Register subcommand "deps add" with an argument */
    static const CliArgSpec add_args[] = {
        { .long_name = "registry", .short_name = 'r', .type = CLI_ARG_STRING, .description = "Package registry URL" },
    };
    CliCmdSpec add = make_spec("add", "Add a dependency");
    add.args = add_args;
    add.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register_sub(reg, "deps", &add), CLI_OK);

    const char* argv[] = { "prog", "deps", "add", "--registry=https://pkg.dev" };
    int argc = 4;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "add");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Empty argv (argc=0 or argc=1 with only program name) returns error. */
/* Edge case for Requirement 2.1                                             */
/* ========================================================================= */

TEST(parse_empty_argv_returns_error) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("build", "Build the project");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    /* argc = 1 means only the program name, no command token */
    const char* argv[] = { "prog" };
    int argc = 1;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    /* Should report an error: no command specified */
    TEST_ASSERT(rc != CLI_OK);
    TEST_ASSERT(result.matched_cmd == NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(parse_zero_argc_returns_error) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("build", "Build the project");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 0, NULL, result_buf, 16, &result);
    TEST_ASSERT(rc != CLI_OK);
    TEST_ASSERT(result.matched_cmd == NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Positional argument capture with correct ordering.                  */
/* Requirement 1.5                                                           */
/* ========================================================================= */

TEST(parse_positional_args_ordered) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliPositionalSpec positionals[] = {
        { .name = "source", .description = "Source file", .cardinality = CLI_CARD_EXACTLY_ONE },
        { .name = "dest",   .description = "Destination", .cardinality = CLI_CARD_EXACTLY_ONE },
    };
    CliCmdSpec spec = make_spec("copy", "Copy a file");
    spec.positionals = positionals;
    spec.positional_count = 2;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "copy", "input.txt", "output.txt" };
    int argc = 4;
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_EQ(result.positional_count, 2);
    TEST_ASSERT_STR_EQ(result.positional_values[0], "input.txt");
    TEST_ASSERT_STR_EQ(result.positional_values[1], "output.txt");

    cli_cmd_registry_destroy(reg);
    return 0;
}
