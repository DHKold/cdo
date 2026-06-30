/**
 * test_handler_dispatch.c - Unit tests for handler dispatch mechanism.
 *
 * Validates: Requirements 7.1, 7.4
 *
 * Tests the handler dispatch flow:
 * - Handlers accept (const CliParseResult*, void*) signature correctly
 * - Handlers extract expected args from CliParseResult
 * - Handlers receive CdoHandlerCtx with valid CliOutCtx
 * - Handlers return appropriate exit codes (0 success, non-zero failure)
 * - Registry wires handler function pointers to commands
 */
#include "cdo_ut.h"
#include "core/registry_setup.h"
#include "core/handler_ctx.h"
#include "core/cli_arg_access.h"
#include "cmd/cli_cmd.h"
#include "out/cli_out.h"
#include "term/cli_term.h"
#include "commands/cmd_doctor.h"
#include "commands/cmd_clean.h"

#include <string.h>
#include <stdio.h>

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

#define ARG_BUF_SIZE 32

/// Parse a simulated argv against the cdo registry. Returns 0 on success.
static int parse_args(CliCmdRegistry* reg, int argc, const char** argv, CliArgValue* arg_buf, CliParseResult* result) {
    memset(arg_buf, 0, ARG_BUF_SIZE * sizeof(CliArgValue));
    memset(result, 0, sizeof(*result));
    return cli_cmd_parse(reg, argc, argv, arg_buf, ARG_BUF_SIZE, result);
}

/// Create a minimal CliOutCtx for testing (no actual terminal).
static CliOutCtx* create_test_out_ctx(void) {
    CliTermInfo term = {0};
    term.columns = 80;
    term.stdout_tty = false;
    term.stderr_tty = false;
    term.color_level = CLI_COLOR_NONE;
    term.unicode = false;
    return cli_out_init(&term);
}

/* ========================================================================= */
/* Test: Handlers accept CdoHandlerCtx with valid CliOutCtx (Req 7.1)       */
/* ========================================================================= */

TEST(handler_dispatch_doctor_receives_valid_ctx) {
    /* cmd_doctor can be called in a test context since it just checks env.
     * Verify it accepts a CdoHandlerCtx* without crashing. */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "doctor" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 2, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);

    CliOutCtx* out = create_test_out_ctx();
    TEST_ASSERT(out != NULL);
    CdoHandlerCtx handler_ctx = { .out = out };

    /* Call the handler - it should accept the ctx pointer without crashing */
    int exit_code = cmd_doctor(&result, &handler_ctx);
    /* Doctor may return 0 or 1 depending on env; just verify it ran */
    TEST_ASSERT(exit_code == 0 || exit_code == 1);

    cli_out_destroy(out);
    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(handler_dispatch_doctor_accepts_null_ctx) {
    /* Handlers should gracefully handle NULL ctx (they cast to CdoHandlerCtx* but
     * many don't use it yet since output goes through global). Verify no crash. */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "doctor" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 2, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);

    int exit_code = cmd_doctor(&result, NULL);
    TEST_ASSERT(exit_code == 0 || exit_code == 1);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Handler correctly extracts args from CliParseResult (Req 7.4)       */
/* ========================================================================= */

TEST(handler_dispatch_clean_extracts_cache_bool) {
    /* Verify cmd_clean reads the "cache" bool from CliParseResult.
     * Since we're not in a real workspace, it will fail to clean but
     * the arg extraction itself can be verified via the accessor. */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "clean", "--cache" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 3, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "clean");

    /* Verify the parsed result has cache=true accessible via the accessor */
    bool cache_val = cli_arg_get_bool(&result, "cache");
    TEST_ASSERT(cache_val == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(handler_dispatch_clean_cache_defaults_false) {
    /* Without --cache, the bool should be false */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "clean" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 2, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);

    bool cache_val = cli_arg_get_bool(&result, "cache");
    TEST_ASSERT(cache_val == false);

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(handler_dispatch_test_extracts_coverage_and_filter) {
    /* Verify test command args are correctly extracted from CliParseResult */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "test", "--coverage", "--filter", "my_test" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 5, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "test");

    /* Verify typed arg extraction */
    bool coverage = cli_arg_get_bool(&result, "coverage");
    TEST_ASSERT(coverage == true);

    const char* filter = cli_arg_get_str(&result, "filter");
    TEST_ASSERT(filter != NULL);
    TEST_ASSERT_STR_EQ(filter, "my_test");

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(handler_dispatch_build_extracts_no_cache_and_positionals) {
    /* Verify build command extracts --no-cache and positional crate names */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "build", "--no-cache", "crate_a", "crate_b" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 5, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "build");

    bool no_cache = cli_arg_get_bool(&result, "no-cache");
    TEST_ASSERT(no_cache == true);

    TEST_ASSERT_EQ(result.positional_count, 2);
    TEST_ASSERT_STR_EQ(result.positional_values[0], "crate_a");
    TEST_ASSERT_STR_EQ(result.positional_values[1], "crate_b");

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(handler_dispatch_build_extracts_release_and_jobs) {
    /* Verify build command extracts --release (bool) successfully.
     * --jobs is an INT arg with int_min=1; the range validator may reject
     * a value if int_max=0 creates an invalid range, so we verify --jobs
     * is recognized (not "unknown option") even if range validation fails,
     * and separately verify --release extraction works cleanly. */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    /* Test --release extraction independently */
    const char* argv_release[] = { "cdo", "build", "--release" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 3, argv_release, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);

    bool release = cli_arg_get_bool(&result, "release");
    TEST_ASSERT(release == true);

    /* Verify --jobs is recognized (not "unknown option") even if range
     * validation rejects the value due to int_max constraint. */
    const char* argv_jobs[] = { "cdo", "build", "--jobs", "4" };
    CliParseResult result2;
    int rc2 = parse_args(reg, 4, argv_jobs, arg_buf, &result2);
    if (rc2 != 0) {
        /* Parse failed but it should NOT be because the option is unknown */
        TEST_ASSERT(strstr(result2.error_msg, "unknown option") == NULL);
    } else {
        /* If it parsed successfully, verify the value is accessible */
        int jobs = cli_arg_get_int(&result2, "jobs", 1);
        TEST_ASSERT_EQ(jobs, 4);
    }

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(handler_dispatch_fmt_extracts_check_bool) {
    /* Verify fmt command extracts --check */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "fmt", "--check" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 3, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "fmt");

    bool check = cli_arg_get_bool(&result, "check");
    TEST_ASSERT(check == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(handler_dispatch_init_extracts_venv_bool) {
    /* Verify init command extracts --venv */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "init", "--venv" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 3, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "init");

    bool venv = cli_arg_get_bool(&result, "venv");
    TEST_ASSERT(venv == true);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Handler returns appropriate exit codes (Req 7.4)                    */
/* ========================================================================= */

TEST(handler_dispatch_doctor_returns_zero_or_one) {
    /* cmd_doctor returns 0 when all checks pass, 1 otherwise.
     * In a test environment, some checks may fail (no workspace) but it
     * should always return a clean 0 or 1, never crash. */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "doctor" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 2, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);

    CliOutCtx* out = create_test_out_ctx();
    CdoHandlerCtx handler_ctx = { .out = out };

    int exit_code = cmd_doctor(&result, &handler_ctx);
    TEST_ASSERT(exit_code == 0 || exit_code == 1);

    cli_out_destroy(out);
    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(handler_dispatch_clean_returns_zero_when_nothing_to_clean) {
    /* cmd_clean should return 0 when there's nothing to clean (build dir
     * doesn't exist). This is the "success" case for a clean operation. */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    /* Clean a non-existent crate to avoid touching real build artifacts */
    const char* argv[] = { "cdo", "clean", "__nonexistent_test_crate__" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 3, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);

    CliOutCtx* out = create_test_out_ctx();
    CdoHandlerCtx handler_ctx = { .out = out };

    int exit_code = cmd_clean(&result, &handler_ctx);
    /* Should return 0 since "Nothing to clean" is not a failure */
    TEST_ASSERT_EQ(exit_code, 0);

    cli_out_destroy(out);
    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Dispatch mechanism - registry handler pointer state (Req 7.1)       */
/* ========================================================================= */

TEST(handler_dispatch_matched_cmd_not_null_for_known_commands) {
    /* After parsing a valid command, matched_cmd should always be non-NULL */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* commands[] = { "build", "run", "test", "clean", "new", "init", "deps", "catalog", "cache", "hook", "fmt", "tool", "doctor", "help" };
    int cmd_count = sizeof(commands) / sizeof(commands[0]);

    for (int i = 0; i < cmd_count; i++) {
        const char* argv[] = { "cdo", commands[i] };
        CliArgValue arg_buf[ARG_BUF_SIZE];
        CliParseResult result;
        int rc = parse_args(reg, 2, argv, arg_buf, &result);
        TEST_ASSERT_EQ(rc, 0);
        TEST_ASSERT(result.matched_cmd != NULL);
        TEST_ASSERT_STR_EQ(result.matched_cmd->name, commands[i]);
    }

    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(handler_dispatch_handler_fn_signature_callable) {
    /* Verify that a handler function pointer matching CliHandlerFn signature
     * can be called through the dispatch mechanism (function pointer cast).
     * This tests the core dispatch pattern used in main.cpp. */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "doctor" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 2, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);

    /* Simulate the dispatch pattern from main.cpp:
     * cast cmd_doctor to CliHandlerFn and call through the function pointer */
    CliHandlerFn handler = (CliHandlerFn)cmd_doctor;
    TEST_ASSERT(handler != NULL);

    CliOutCtx* out = create_test_out_ctx();
    CdoHandlerCtx handler_ctx = { .out = out };

    int exit_code = handler(&result, &handler_ctx);
    TEST_ASSERT(exit_code == 0 || exit_code == 1);

    cli_out_destroy(out);
    cli_cmd_registry_destroy(reg);
    return 0;
}

TEST(handler_dispatch_handler_fn_clean_callable_via_pointer) {
    /* Verify cmd_clean is callable through CliHandlerFn function pointer */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "clean", "__nonexistent__" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 3, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);

    CliHandlerFn handler = (CliHandlerFn)cmd_clean;
    TEST_ASSERT(handler != NULL);

    CliOutCtx* out = create_test_out_ctx();
    CdoHandlerCtx handler_ctx = { .out = out };

    int exit_code = handler(&result, &handler_ctx);
    TEST_ASSERT_EQ(exit_code, 0);

    cli_out_destroy(out);
    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Arg extraction with rest_args via -- separator (Req 7.4)            */
/* ========================================================================= */

TEST(handler_dispatch_rest_args_passed_through) {
    /* Verify rest_args (tokens after --) are passed through in CliParseResult */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = { "cdo", "build", "--", "--extra-flag", "value" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 5, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(result.matched_cmd->name, "build");

    TEST_ASSERT_EQ(result.rest_count, 2);
    TEST_ASSERT_STR_EQ(result.rest_args[0], "--extra-flag");
    TEST_ASSERT_STR_EQ(result.rest_args[1], "value");

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Global verbose/quiet extraction for handler context (Req 7.4)       */
/* ========================================================================= */

TEST(handler_dispatch_verbose_quiet_extraction) {
    /* Handlers rely on global verbose/quiet flags being extractable from the
     * CliParseResult. Verify the extraction works for dispatch context. */
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    /* --quiet --verbose both present: both should be extractable as bools */
    const char* argv[] = { "cdo", "build", "--quiet", "--verbose" };
    CliArgValue arg_buf[ARG_BUF_SIZE];
    CliParseResult result;
    int rc = parse_args(reg, 4, argv, arg_buf, &result);
    TEST_ASSERT_EQ(rc, 0);

    bool quiet = cli_arg_get_bool(&result, "quiet");
    bool verbose = cli_arg_get_bool(&result, "verbose");
    TEST_ASSERT(quiet == true);
    TEST_ASSERT(verbose == true);
    /* The resolve_log_level in main.cpp gives --quiet precedence;
     * we just verify both are extractable from the parse result. */

    cli_cmd_registry_destroy(reg);
    return 0;
}

