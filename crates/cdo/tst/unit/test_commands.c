// crates/cdo/tst/unit/test_commands.c
// Unit tests for command module dispatch (cmd_tool_list, cmd_deps_add, cmd_build)
#include "cdo_ut.h"
#include "commands/cmd_tool.h"
#include "commands/cmd_deps.h"
#include "commands/cmd_build.h"
#include "cmd/cli_cmd.h"
#include "model/workspace.h"
#include "core/log.h"

// --- cmd_tool_list ---

TEST_SERIAL(cmd_tool_resolves_valid) {
    // Invoke "tool list" via the new interface. No network needed.
    CliParseResult result = {0};
    int rc = cmd_tool_list(&result, NULL);
    TEST_ASSERT_EQ(rc, 0);

    return 0;
}

// --- cmd_deps_add ---

TEST_SERIAL(cmd_deps_add_resolves) {
    // Invoke deps add with a package name.
    // Since we're likely not in a crate directory with crate.toml at cwd,
    // this should fail gracefully with a non-zero return code.
    const char* positional[] = {"test_nonexistent_pkg"};

    CliArgValue arg_buf[2];
    arg_buf[0].name = "dev";
    arg_buf[0].type = CLI_ARG_BOOL;
    arg_buf[0].value.bool_val = false;
    arg_buf[0].present = false;

    arg_buf[1].name = "version";
    arg_buf[1].type = CLI_ARG_STRING;
    arg_buf[1].value.str_val = NULL;
    arg_buf[1].present = false;

    CliParseResult result = {0};
    result.arg_values = arg_buf;
    result.arg_value_count = 2;
    result.positional_values = positional;
    result.positional_count = 1;

    int rc = cmd_deps_add(&result, NULL);
    TEST_ASSERT(rc != 0);

    return 0;
}

// --- cmd_build ---

TEST_SERIAL(cmd_build_compiles_sources) {
    // Invoke cmd_build on cdo_ut crate (small library, won't re-link running binary)
    const char* positional[] = {"cdo_ut"};

    CliArgValue arg_buf[4];
    int arg_count = 0;

    arg_buf[arg_count].name = "release";
    arg_buf[arg_count].type = CLI_ARG_BOOL;
    arg_buf[arg_count].value.bool_val = false;
    arg_buf[arg_count].present = false;
    arg_count++;

    arg_buf[arg_count].name = "jobs";
    arg_buf[arg_count].type = CLI_ARG_INT;
    arg_buf[arg_count].value.int_val = 1;
    arg_buf[arg_count].present = true;
    arg_count++;

    CliParseResult result = {0};
    result.arg_values = arg_buf;
    result.arg_value_count = arg_count;
    result.positional_values = positional;
    result.positional_count = 1;

    int rc = cmd_build(&result, NULL);
    TEST_ASSERT_EQ(rc, 0);

    return 0;
}

TEST_SERIAL(cmd_build_failure_reports_error) {
    // Invoke cmd_build with a non-existent crate name.
    const char* positional[] = {"__nonexistent_crate__"};

    CliArgValue arg_buf[2];
    int arg_count = 0;

    arg_buf[arg_count].name = "release";
    arg_buf[arg_count].type = CLI_ARG_BOOL;
    arg_buf[arg_count].value.bool_val = false;
    arg_buf[arg_count].present = false;
    arg_count++;

    CliParseResult result = {0};
    result.arg_values = arg_buf;
    result.arg_value_count = arg_count;
    result.positional_values = positional;
    result.positional_count = 1;

    int rc = cmd_build(&result, NULL);
    TEST_ASSERT(rc != 0);

    return 0;
}
