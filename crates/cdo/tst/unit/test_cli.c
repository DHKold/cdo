// crates/cdo/tst/unit/test_cli.c
// Unit tests for CLI argument parsing via the new cdo_cli framework.
// The old cdo_cli_parse() / CdoOptions tests have been removed (task 5.5).
// Parsing correctness is now validated by test_parse_integration.c.
#include "cdo_ut.h"
#include "cmd/cli_cmd.h"
#include "core/registry_setup.h"

// --- Registry-based parsing ---

TEST(cli_parse_build_via_registry) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = {"cdo", "build"};
    CliArgValue arg_buf[32];
    CliParseResult result = {0};
    int rc = cli_cmd_parse(reg, 2, argv, arg_buf, 32, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "build");

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(cli_parse_unknown_command_error) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = {"cdo", "bluild"};
    CliArgValue arg_buf[32];
    CliParseResult result = {0};
    int rc = cli_cmd_parse(reg, 2, argv, arg_buf, 32, &result);
    TEST_ASSERT(rc != 0);

    // Verify suggestions are available
    char suggestions[4][32];
    int n = cli_cmd_suggest(reg, "bluild", suggestions, 4);
    TEST_ASSERT(n > 0);
    TEST_ASSERT_STR_EQ(suggestions[0], "build");

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(cli_parse_verbose_flag) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = {"cdo", "test", "--verbose"};
    CliArgValue arg_buf[32];
    CliParseResult result = {0};
    int rc = cli_cmd_parse(reg, 3, argv, arg_buf, 32, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "test");

    // Check verbose flag is set
    bool verbose = false;
    for (int i = 0; i < result.arg_value_count; i++) {
        if (result.arg_values[i].name && strcmp(result.arg_values[i].name, "verbose") == 0) {
            verbose = result.arg_values[i].value.bool_val;
            break;
        }
    }
    TEST_ASSERT(verbose == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}
