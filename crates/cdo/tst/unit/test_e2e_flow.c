/**
 * test_e2e_flow.c - Integration tests for end-to-end CLI flow.
 *
 * Validates: Requirements 8.1, 8.2, 9.3, 14.4, 4.2, 16.3
 *
 * Tests the full round-trip through the registry, parser, help generation,
 * completion scripts, suggestion engine, and log-level resolution:
 * - Top-level help (--help with no command) lists all commands
 * - Command-specific help (build --help) lists correct options
 * - Completion script generation via cli_cmd_completion_script (bash)
 * - Unknown command triggers suggestion output pipeline
 * - --verbose and --quiet conflict resolution via resolve_log_level logic
 * - NULL registry detection (documents the error path exists in main.cpp)
 */
#include "cdo_ut.h"
#include "core/registry_setup.h"
#include "core/cli_arg_access.h"
#include "core/log.h"
#include "cmd/cli_cmd.h"
#include "term/cli_term.h"

#include <string.h>
#include <stdio.h>

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

#define ARG_BUF_SIZE 32
#define HELP_BUF_SIZE 4096
#define SCRIPT_BUF_SIZE 8192

/// All 14 command names expected in top-level help output.
static const char* ALL_COMMANDS[] = {
    "build", "run", "test", "clean", "new", "init",
    "deps", "catalog", "cache", "hook", "fmt", "tool",
    "doctor", "help"
};
#define COMMAND_COUNT 14

/// Parse a simulated argv. Returns cli_cmd_parse rc.
static int parse(CliCmdRegistry* reg, int argc, const char** argv, CliArgValue* buf, CliParseResult* result) {
    memset(buf, 0, ARG_BUF_SIZE * sizeof(CliArgValue));
    memset(result, 0, sizeof(*result));
    return cli_cmd_parse(reg, argc, argv, buf, ARG_BUF_SIZE, result);
}

/// Replicate the resolve_log_level logic from main.cpp for testing.
/// Precedence: --quiet > --verbose > --log-level > default (INFO).
static CdoLogLevel resolve_log_level_from_result(const CliParseResult* result) {
    if (cli_arg_get_bool(result, "quiet")) {
        return CDO_LOG_LEVEL_ERROR;
    }
    if (cli_arg_get_bool(result, "verbose")) {
        return CDO_LOG_LEVEL_DEBUG;
    }
    const char* level_str = cli_arg_get_str(result, "log-level");
    if (level_str) {
        if (strcmp(level_str, "error") == 0) return CDO_LOG_LEVEL_ERROR;
        if (strcmp(level_str, "warn") == 0)  return CDO_LOG_LEVEL_WARN;
        if (strcmp(level_str, "info") == 0)  return CDO_LOG_LEVEL_INFO;
        if (strcmp(level_str, "debug") == 0) return CDO_LOG_LEVEL_DEBUG;
        if (strcmp(level_str, "trace") == 0) return CDO_LOG_LEVEL_TRACE;
    }
    return CDO_LOG_LEVEL_INFO;
}

/* ========================================================================= */
/* Test: Top-level help lists all 14 commands (Req 8.1)                      */
/* ========================================================================= */

TEST(e2e_top_level_help_lists_all_commands) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    CliTermInfo term = {0};
    term.columns = 80;
    term.stdout_tty = false;
    term.color_level = CLI_COLOR_NONE;

    char help_buf[HELP_BUF_SIZE];
    cli_cmd_help(reg, NULL, &term, help_buf, sizeof(help_buf));

    /* Help output should be non-empty */
    TEST_ASSERT(help_buf[0] != '\0');

    /* Verify all 14 commands appear in the help output */
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strstr(help_buf, ALL_COMMANDS[i]) == NULL) {
            char msg[128];
            snprintf(msg, sizeof(msg), "top-level help missing command: %s", ALL_COMMANDS[i]);
            cdo_ut_record_failure(__FILE__, __LINE__, msg, "not found", ALL_COMMANDS[i]);
            cli_cmd_registry_destroy(reg);
            return 1;
        }
    }

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Command-specific help for 'build' includes correct options (Req 8.2)*/
/* ========================================================================= */

TEST(e2e_build_help_includes_command_options) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    /* Parse "cdo build --help" to get the matched_cmd for build */
    const char* argv[] = { "cdo", "build", "--help" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse(reg, 3, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "build");

    CliTermInfo term = {0};
    term.columns = 80;
    term.stdout_tty = false;
    term.color_level = CLI_COLOR_NONE;

    char help_buf[HELP_BUF_SIZE];
    cli_cmd_help(reg, result.matched_cmd, &term, help_buf, sizeof(help_buf));

    /* Help output should be non-empty */
    TEST_ASSERT(help_buf[0] != '\0');

    /* Should contain "build" as the command name */
    TEST_ASSERT(strstr(help_buf, "build") != NULL);

    /* Should list build-specific option --no-cache */
    TEST_ASSERT(strstr(help_buf, "no-cache") != NULL);

    /* Should list global options like --verbose and --release */
    TEST_ASSERT(strstr(help_buf, "verbose") != NULL);
    TEST_ASSERT(strstr(help_buf, "release") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Top-level help is non-trivial with structured content (Req 8.1)    */
/* ========================================================================= */

TEST(e2e_top_level_help_has_structured_content) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    CliTermInfo term = {0};
    term.columns = 80;
    term.stdout_tty = false;
    term.color_level = CLI_COLOR_NONE;

    char help_buf[HELP_BUF_SIZE];
    cli_cmd_help(reg, NULL, &term, help_buf, sizeof(help_buf));

    /* Help output should be substantial (list all 14 commands with descriptions) */
    int len = (int)strlen(help_buf);
    TEST_ASSERT(len > 100);

    /* Should contain newlines indicating multi-line structure */
    TEST_ASSERT(strstr(help_buf, "\n") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Bash completion script is valid (Req 14.4 integration)              */
/* Exercises the full path: registry -> completion_script generation          */
/* ========================================================================= */

TEST(e2e_completions_bash_produces_valid_script) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char script_buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_BASH, script_buf, sizeof(script_buf));

    /* Script should be non-empty */
    TEST_ASSERT(written > 0);
    TEST_ASSERT(script_buf[0] != '\0');

    /* Should contain bash-specific patterns */
    TEST_ASSERT(strstr(script_buf, "complete -F") != NULL);
    TEST_ASSERT(strstr(script_buf, "COMPREPLY") != NULL);

    /* Should contain all registered commands */
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strstr(script_buf, ALL_COMMANDS[i]) == NULL) {
            char msg[128];
            snprintf(msg, sizeof(msg), "bash completion missing command: %s", ALL_COMMANDS[i]);
            cdo_ut_record_failure(__FILE__, __LINE__, msg, "not found", ALL_COMMANDS[i]);
            cli_cmd_registry_destroy(reg);
            return 1;
        }
    }

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Unknown command triggers "Did you mean:" suggestion pipeline (9.3)  */
/* Full integration: parse error -> error_token -> cli_cmd_suggest -> output */
/* ========================================================================= */

TEST(e2e_unknown_command_triggers_suggestion) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    /* "cdo bluild" is a typo for "build" */
    const char* argv[] = { "cdo", "bluild" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 2, argv, buf, &result);

    /* Parse should fail with an error */
    TEST_ASSERT_NEQ(rc, 0);
    TEST_ASSERT(result.error_code != 0);

    /* error_token should capture the unrecognized token */
    TEST_ASSERT(result.error_token != NULL);
    TEST_ASSERT_STR_EQ(result.error_token, "bluild");

    /* Run the suggestion engine (same pipeline as main.cpp) */
    char suggestions[4][32];
    int n = cli_cmd_suggest(reg, result.error_token, suggestions, 4);

    /* Should suggest at least "build" */
    TEST_ASSERT(n >= 1);
    TEST_ASSERT_STR_EQ(suggestions[0], "build");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Suggestion for "tset" -> "test" (alternate typo) (Req 9.3)          */
/* ========================================================================= */

TEST(e2e_unknown_command_tset_suggests_test) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "tset" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 2, argv, buf, &result);
    TEST_ASSERT_NEQ(rc, 0);
    TEST_ASSERT(result.error_token != NULL);
    TEST_ASSERT_STR_EQ(result.error_token, "tset");

    char suggestions[4][32];
    int n = cli_cmd_suggest(reg, result.error_token, suggestions, 4);
    TEST_ASSERT(n >= 1);

    /* "test" should be among the suggestions */
    bool found_test = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(suggestions[i], "test") == 0) {
            found_test = true;
            break;
        }
    }
    TEST_ASSERT(found_test);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: --verbose and --quiet conflict resolution (Req 4.2)                 */
/* --quiet takes precedence -> log level should be ERROR                     */
/* ========================================================================= */

TEST(e2e_quiet_takes_precedence_over_verbose) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    /* Both --quiet and --verbose provided */
    const char* argv[] = { "cdo", "build", "--quiet", "--verbose" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 4, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);

    /* Apply the same precedence logic as main.cpp */
    CdoLogLevel level = resolve_log_level_from_result(&result);

    /* --quiet takes precedence: level should be ERROR */
    TEST_ASSERT_EQ((int)level, (int)CDO_LOG_LEVEL_ERROR);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: --verbose alone sets log level to DEBUG (Req 4.2)                   */
/* ========================================================================= */

TEST(e2e_verbose_sets_debug_level) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "build", "--verbose" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 3, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);

    CdoLogLevel level = resolve_log_level_from_result(&result);
    TEST_ASSERT_EQ((int)level, (int)CDO_LOG_LEVEL_DEBUG);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: --quiet alone sets log level to ERROR (Req 4.2)                     */
/* ========================================================================= */

TEST(e2e_quiet_sets_error_level) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "build", "--quiet" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 3, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);

    CdoLogLevel level = resolve_log_level_from_result(&result);
    TEST_ASSERT_EQ((int)level, (int)CDO_LOG_LEVEL_ERROR);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Default log level is INFO when no flags provided (Req 4.2)          */
/* ========================================================================= */

TEST(e2e_default_log_level_is_info) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "build" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 2, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);

    CdoLogLevel level = resolve_log_level_from_result(&result);
    TEST_ASSERT_EQ((int)level, (int)CDO_LOG_LEVEL_INFO);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Registry creation succeeds under normal conditions (Req 14.4, 16.3) */
/* Documents that the NULL-registry error path exists and that normal         */
/* creation always succeeds. The error path (NULL return) is exercised in    */
/* main.cpp: "fatal: failed to initialize CLI registry" + exit(1).           */
/* ========================================================================= */

TEST(e2e_registry_creation_never_null_normally) {
    /* Under normal conditions (no memory exhaustion), cdo_registry_create()
     * should always return a valid registry pointer. This documents the
     * invariant that the NULL path in main.cpp is purely for catastrophic
     * allocation failures. */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    /* Verify the registry is functional: can list commands */
    const char* argv[] = { "cdo", "build" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse(reg, 2, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Full round-trip parse -> help -> output for command-specific (8.2)   */
/* Exercises: registry create -> parse -> detect --help -> generate help      */
/* ========================================================================= */

TEST(e2e_full_roundtrip_test_command_help) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    /* Parse "cdo test --help" */
    const char* argv[] = { "cdo", "test", "--help" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse(reg, 3, argv, buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "test");

    /* Verify --help is detected */
    bool help_flag = cli_arg_get_bool(&result, "help");
    TEST_ASSERT(help_flag == true);

    /* Generate help for the test command */
    CliTermInfo term = {0};
    term.columns = 80;
    term.stdout_tty = false;
    term.color_level = CLI_COLOR_NONE;

    char help_buf[HELP_BUF_SIZE];
    cli_cmd_help(reg, result.matched_cmd, &term, help_buf, sizeof(help_buf));

    /* Should contain test-specific options */
    TEST_ASSERT(strstr(help_buf, "test") != NULL);
    TEST_ASSERT(strstr(help_buf, "coverage") != NULL);
    TEST_ASSERT(strstr(help_buf, "filter") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Full round-trip parse error -> suggestion -> output (Req 9.3, 14.4) */
/* Simulates the complete error handling path from main.cpp                  */
/* ========================================================================= */

TEST(e2e_full_roundtrip_error_suggestion_output) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    /* Simulate: cdo fmtt (typo for fmt) */
    const char* argv[] = { "cdo", "fmtt" };
    CliArgValue buf[ARG_BUF_SIZE];
    CliParseResult result;

    int rc = parse(reg, 2, argv, buf, &result);
    TEST_ASSERT_NEQ(rc, 0);

    /* Verify the error message is populated */
    TEST_ASSERT(result.error_msg[0] != '\0');

    /* Verify the error token is captured */
    TEST_ASSERT(result.error_token != NULL);
    TEST_ASSERT_STR_EQ(result.error_token, "fmtt");

    /* Run suggestion (same as main.cpp error path) */
    char suggestions[4][32];
    int n = cli_cmd_suggest(reg, result.error_token, suggestions, 4);
    TEST_ASSERT(n >= 1);

    /* "fmt" should be among the suggestions */
    bool found_fmt = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(suggestions[i], "fmt") == 0) {
            found_fmt = true;
            break;
        }
    }
    TEST_ASSERT(found_fmt);

    cli_cmd_registry_destroy(reg);
    return 0;
}
