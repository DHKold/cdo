/**
 * test_cmd_validate.c - Unit tests for argument validation and type coercion.
 *
 * Tests validation through cli_cmd_parse() which invokes validation internally.
 * Covers: bool, int (range), enum (set membership), string (length), required,
 * custom parse/validate functions, and error message format.
 *
 * Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8
 */

#include "cdo_ut.h"
#include "../api/cmd/cli_cmd.h"
#include "../api/cli_errors.h"

#include <string.h>
#include <stdlib.h>

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
/* Test: Bool type - flag presence sets true, absence defaults to false.      */
/* Requirement 3.7                                                           */
/* ========================================================================= */

TEST(validate_bool_flag_present_is_true) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "verbose", .short_name = 'v', .type = CLI_ARG_BOOL, .description = "Verbose output" },
    };
    CliCmdSpec spec = make_spec("build", "Build");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "build", "--verbose" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT_EQ(result.arg_value_count, 1);
    TEST_ASSERT(result.arg_values[0].present == true);
    TEST_ASSERT(result.arg_values[0].value.bool_val == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(validate_bool_flag_absent_not_in_results) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "verbose", .short_name = 'v', .type = CLI_ARG_BOOL, .description = "Verbose output" },
    };
    CliCmdSpec spec = make_spec("build", "Build");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    /* Invoke without the flag */
    const char* argv[] = { "prog", "build" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 2, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    /* Flag not provided means it shouldn't be in arg_values (or present=false) */
    TEST_ASSERT_EQ(result.arg_value_count, 0);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Int type - valid integer string is coerced to int value.             */
/* Requirement 3.2                                                           */
/* ========================================================================= */

TEST(validate_int_valid_value) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "count", .short_name = 'c', .type = CLI_ARG_INT, .description = "Item count", .int_min = 1, .int_max = 100 },
    };
    CliCmdSpec spec = make_spec("run", "Run");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "run", "--count=50" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT_EQ(result.arg_value_count, 1);
    TEST_ASSERT(result.arg_values[0].present == true);
    TEST_ASSERT_EQ(result.arg_values[0].value.int_val, 50);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Int type - out of range returns CLI_ERR_VALIDATE.                    */
/* Requirement 3.3                                                           */
/* ========================================================================= */

TEST(validate_int_out_of_range) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "count", .short_name = 'c', .type = CLI_ARG_INT, .description = "Item count", .int_min = 1, .int_max = 100 },
    };
    CliCmdSpec spec = make_spec("run", "Run");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "run", "--count=200" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_ERR_VALIDATE);
    TEST_ASSERT(strlen(result.error_msg) > 0);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Int type - non-numeric string returns CLI_ERR_VALIDATE.              */
/* Requirement 3.3                                                           */
/* ========================================================================= */

TEST(validate_int_non_numeric) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "count", .short_name = 'c', .type = CLI_ARG_INT, .description = "Item count", .int_min = 1, .int_max = 100 },
    };
    CliCmdSpec spec = make_spec("run", "Run");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "run", "--count=abc" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_ERR_VALIDATE);
    TEST_ASSERT(strlen(result.error_msg) > 0);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Enum type - valid value in allowed set is accepted.                  */
/* Requirement 3.2                                                           */
/* ========================================================================= */

TEST(validate_enum_valid_value) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const char* profiles[] = { "debug", "release", "test", NULL };
    static const CliArgSpec args[] = {
        { .long_name = "profile", .short_name = 'p', .type = CLI_ARG_ENUM, .description = "Build profile", .enum_values = profiles },
    };
    CliCmdSpec spec = make_spec("build", "Build");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "build", "--profile=debug" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT_EQ(result.arg_value_count, 1);
    TEST_ASSERT_STR_EQ(result.arg_values[0].value.str_val, "debug");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Enum type - value not in set returns CLI_ERR_VALIDATE.               */
/* Requirement 3.3                                                           */
/* ========================================================================= */

TEST(validate_enum_invalid_value) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const char* profiles[] = { "debug", "release", "test", NULL };
    static const CliArgSpec args[] = {
        { .long_name = "profile", .short_name = 'p', .type = CLI_ARG_ENUM, .description = "Build profile", .enum_values = profiles },
    };
    CliCmdSpec spec = make_spec("build", "Build");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "build", "--profile=invalid" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_ERR_VALIDATE);
    TEST_ASSERT(strlen(result.error_msg) > 0);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: String type - length within min/max is accepted.                     */
/* Requirement 3.3                                                           */
/* ========================================================================= */

TEST(validate_string_valid_length) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "name", .short_name = 'n', .type = CLI_ARG_STRING, .description = "Project name", .str_min_len = 3, .str_max_len = 20 },
    };
    CliCmdSpec spec = make_spec("init", "Init");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "init", "--name=myproject" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT_STR_EQ(result.arg_values[0].value.str_val, "myproject");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: String type - too short returns CLI_ERR_VALIDATE.                    */
/* Requirement 3.3                                                           */
/* ========================================================================= */

TEST(validate_string_too_short) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "name", .short_name = 'n', .type = CLI_ARG_STRING, .description = "Project name", .str_min_len = 3, .str_max_len = 20 },
    };
    CliCmdSpec spec = make_spec("init", "Init");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "init", "--name=ab" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_ERR_VALIDATE);
    TEST_ASSERT(strlen(result.error_msg) > 0);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: String type - too long returns CLI_ERR_VALIDATE.                     */
/* Requirement 3.3                                                           */
/* ========================================================================= */

TEST(validate_string_too_long) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "name", .short_name = 'n', .type = CLI_ARG_STRING, .description = "Project name", .str_min_len = 3, .str_max_len = 10 },
    };
    CliCmdSpec spec = make_spec("init", "Init");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "init", "--name=verylongprojectname" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_ERR_VALIDATE);
    TEST_ASSERT(strlen(result.error_msg) > 0);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Required argument missing returns CLI_ERR_VALIDATE.                  */
/* Requirement 3.6                                                           */
/* ========================================================================= */

TEST(validate_required_missing) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "name", .short_name = 'n', .type = CLI_ARG_STRING, .description = "Name", .required = true },
    };
    CliCmdSpec spec = make_spec("init", "Init");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "init" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 2, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_ERR_VALIDATE);
    TEST_ASSERT(strlen(result.error_msg) > 0);
    /* Error message should mention the argument name */
    TEST_ASSERT(strstr(result.error_msg, "name") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Custom parse function is invoked when set.                           */
/* Requirement 3.4, 3.8                                                      */
/* ========================================================================= */

typedef struct {
    bool invoked;
    char received_input[64];
} CustomParseCtx;

static int custom_parse_fn(const char* input, void* out, char* err_buf, int err_buf_size, void* ctx) {
    (void)out; (void)err_buf; (void)err_buf_size;
    CustomParseCtx* c = (CustomParseCtx*)ctx;
    c->invoked = true;
    if (input) {
        strncpy(c->received_input, input, sizeof(c->received_input) - 1);
        c->received_input[sizeof(c->received_input) - 1] = '\0';
    }
    return 0;
}

TEST(validate_custom_parse_invoked) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static CustomParseCtx ctx = { .invoked = false };
    memset(&ctx, 0, sizeof(ctx));

    static CliArgSpec args[] = {
        { .long_name = "path", .short_name = 'p', .type = CLI_ARG_STRING, .description = "File path", .custom_parse = custom_parse_fn, .custom_ctx = &ctx },
    };
    CliCmdSpec spec = make_spec("check", "Check");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "check", "--path=/usr/local/bin" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT(ctx.invoked == true);
    TEST_ASSERT_STR_EQ(ctx.received_input, "/usr/local/bin");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Custom validate function is invoked after parsing.                   */
/* Requirement 3.5, 3.8                                                      */
/* ========================================================================= */

typedef struct {
    bool invoked;
} CustomValidateCtx;

static int custom_validate_fn(const void* value, char* err_buf, int err_buf_size, void* ctx) {
    (void)value; (void)err_buf; (void)err_buf_size;
    CustomValidateCtx* c = (CustomValidateCtx*)ctx;
    c->invoked = true;
    return 0;
}

TEST(validate_custom_validate_invoked) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static CustomValidateCtx ctx = { .invoked = false };
    memset(&ctx, 0, sizeof(ctx));

    static CliArgSpec args[] = {
        { .long_name = "port", .short_name = 0, .type = CLI_ARG_INT, .description = "Port number", .custom_validate = custom_validate_fn, .custom_ctx = &ctx },
    };
    CliCmdSpec spec = make_spec("serve", "Serve");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "serve", "--port=8080" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT(ctx.invoked == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Error message format contains argument name and constraint info.     */
/* Requirement 3.6                                                           */
/* ========================================================================= */

TEST(validate_error_message_format) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const CliArgSpec args[] = {
        { .long_name = "count", .short_name = 'c', .type = CLI_ARG_INT, .description = "Count", .int_min = 1, .int_max = 10 },
    };
    CliCmdSpec spec = make_spec("run", "Run");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "prog", "run", "--count=50" };
    CliArgValue result_buf[16];
    CliParseResult result;
    memset(&result, 0, sizeof(result));

    int rc = cli_cmd_parse(reg, 3, argv, result_buf, 16, &result);
    TEST_ASSERT_EQ(rc, CLI_ERR_VALIDATE);
    /* Error message should follow format: "argument '--name': <constraint> (got: <value>)" */
    TEST_ASSERT(strstr(result.error_msg, "--count") != NULL);
    TEST_ASSERT(strstr(result.error_msg, "50") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}
