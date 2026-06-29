/**
 * test_cmd_help.c - Unit tests for help text generation.
 *
 * Tests the cli_cmd_help() function for top-level listing, command-specific
 * usage synopsis, argument grouping, column alignment, subcommand listing,
 * color suppression, and buffer overflow handling.
 */

#include "cdo_ut.h"
#include "../api/cmd/cli_cmd.h"
#include "../api/cli_errors.h"
#include "../api/term/cli_term.h"
#include <string.h>

// =============================================================================
// Helper: Create a minimal CliCmdSpec.
// =============================================================================

static CliCmdSpec make_spec(const char* name, const char* description) {
    CliCmdSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.name = name;
    spec.description = description;
    return spec;
}

// =============================================================================
// Test: Top-level help lists all registered command names and descriptions.
// =============================================================================

TEST(help_top_level_lists_commands) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec_build = make_spec("build", "Build the project");
    CliCmdSpec spec_test = make_spec("test", "Run unit tests");
    CliCmdSpec spec_clean = make_spec("clean", "Remove build artifacts");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec_build), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec_test), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec_clean), CLI_OK);

    CliTermInfo term;
    memset(&term, 0, sizeof(term));
    term.stdout_tty = true;
    term.color_level = CLI_COLOR_NONE;
    term.columns = 80;

    char buf[2048];
    int written = cli_cmd_help(reg, NULL, &term, buf, sizeof(buf));
    TEST_ASSERT(written > 0);

    /* All command names must appear */
    TEST_ASSERT(strstr(buf, "build") != NULL);
    TEST_ASSERT(strstr(buf, "test") != NULL);
    TEST_ASSERT(strstr(buf, "clean") != NULL);

    /* All descriptions must appear */
    TEST_ASSERT(strstr(buf, "Build the project") != NULL);
    TEST_ASSERT(strstr(buf, "Run unit tests") != NULL);
    TEST_ASSERT(strstr(buf, "Remove build artifacts") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Command-specific help shows usage synopsis with [OPTIONS] pattern.
// =============================================================================

TEST(help_command_shows_usage_synopsis) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliArgSpec args[] = {
        { .long_name = "verbose", .short_name = 'v', .type = CLI_ARG_BOOL, .description = "Enable verbose output" },
        { .long_name = "output", .short_name = 'o', .type = CLI_ARG_STRING, .description = "Output file path" },
    };

    CliCmdSpec spec = make_spec("build", "Build the project");
    spec.args = args;
    spec.arg_count = 2;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    CliTermInfo term;
    memset(&term, 0, sizeof(term));
    term.stdout_tty = true;
    term.color_level = CLI_COLOR_NONE;
    term.columns = 80;

    char buf[2048];
    int written = cli_cmd_help(reg, &spec, &term, buf, sizeof(buf));
    TEST_ASSERT(written > 0);

    /* Should contain usage pattern with [OPTIONS] */
    TEST_ASSERT(strstr(buf, "build") != NULL);
    TEST_ASSERT(strstr(buf, "[OPTIONS]") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Help output contains both required and optional argument names.
// =============================================================================

TEST(help_arguments_grouped) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliArgSpec args[] = {
        { .long_name = "target", .short_name = 't', .type = CLI_ARG_STRING, .description = "Build target", .required = true },
        { .long_name = "jobs", .short_name = 'j', .type = CLI_ARG_INT, .description = "Parallel jobs", .required = false },
    };

    CliCmdSpec spec = make_spec("build", "Build the project");
    spec.args = args;
    spec.arg_count = 2;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    CliTermInfo term;
    memset(&term, 0, sizeof(term));
    term.stdout_tty = true;
    term.color_level = CLI_COLOR_NONE;
    term.columns = 80;

    char buf[2048];
    int written = cli_cmd_help(reg, &spec, &term, buf, sizeof(buf));
    TEST_ASSERT(written > 0);

    /* Both argument names must appear */
    TEST_ASSERT(strstr(buf, "target") != NULL);
    TEST_ASSERT(strstr(buf, "jobs") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Column alignment with different name lengths does not crash.
// =============================================================================

TEST(help_column_alignment) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliArgSpec args[] = {
        { .long_name = "v", .short_name = 'v', .type = CLI_ARG_BOOL, .description = "Verbose" },
        { .long_name = "output-directory", .short_name = 'o', .type = CLI_ARG_STRING, .description = "Output dir" },
        { .long_name = "max-retries", .short_name = 0, .type = CLI_ARG_INT, .description = "Max retries" },
    };

    CliCmdSpec spec = make_spec("build", "Build");
    spec.args = args;
    spec.arg_count = 3;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    CliTermInfo term;
    memset(&term, 0, sizeof(term));
    term.stdout_tty = true;
    term.color_level = CLI_COLOR_NONE;
    term.columns = 80;

    char buf[2048];
    int written = cli_cmd_help(reg, &spec, &term, buf, sizeof(buf));
    /* Just verify it produces output without crashing */
    TEST_ASSERT(written > 0);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Help for parent command lists subcommand names.
// =============================================================================

TEST(help_subcommand_listing) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec parent = make_spec("deps", "Dependency management");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &parent), CLI_OK);

    CliCmdSpec child_add = make_spec("add", "Add a dependency");
    CliCmdSpec child_rm = make_spec("remove", "Remove a dependency");
    TEST_ASSERT_EQ(cli_cmd_register_sub(reg, "deps", &child_add), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register_sub(reg, "deps", &child_rm), CLI_OK);

    CliTermInfo term;
    memset(&term, 0, sizeof(term));
    term.stdout_tty = true;
    term.color_level = CLI_COLOR_NONE;
    term.columns = 80;

    char buf[2048];
    int written = cli_cmd_help(reg, &parent, &term, buf, sizeof(buf));
    TEST_ASSERT(written > 0);

    /* Subcommand names must appear */
    TEST_ASSERT(strstr(buf, "add") != NULL);
    TEST_ASSERT(strstr(buf, "remove") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: With CLI_COLOR_NONE, output must not contain ANSI escape sequences.
// =============================================================================

TEST(help_no_color_when_none) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("build", "Build the project");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    CliTermInfo term;
    memset(&term, 0, sizeof(term));
    term.stdout_tty = true;
    term.color_level = CLI_COLOR_NONE;
    term.columns = 80;

    char buf[2048];
    int written = cli_cmd_help(reg, &spec, &term, buf, sizeof(buf));
    TEST_ASSERT(written > 0);

    /* Must NOT contain ANSI escape sequences */
    TEST_ASSERT(strstr(buf, "\033[") == NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Buffer too small returns -1.
// =============================================================================

TEST(help_buffer_too_small) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("build", "Build the project");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    CliTermInfo term;
    memset(&term, 0, sizeof(term));
    term.stdout_tty = true;
    term.color_level = CLI_COLOR_NONE;
    term.columns = 80;

    char buf[5];
    int written = cli_cmd_help(reg, &spec, &term, buf, 5);
    TEST_ASSERT_EQ(written, -1);

    cli_cmd_registry_destroy(reg);
    return 0;
}
