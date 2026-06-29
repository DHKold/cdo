/**
 * test_integration.c - Cross-module integration tests for cdo_cli.
 *
 * Exercises the full lifecycle across term detection, command registry,
 * argument parsing, and styled output to verify all modules work together
 * correctly without crashes or resource leaks.
 */

#include "cdo_ut.h"
#include "../api/term/cli_term.h"
#include "../api/cmd/cli_cmd.h"
#include "../api/out/cli_out.h"
#include "../api/cli_errors.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
static FILE* open_tmp(void) {
    char path[MAX_PATH];
    char tmp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp_dir);
    GetTempFileNameA(tmp_dir, "cdo", 0, path);
    return fopen(path, "w+bTD"); /* T=temp, D=delete-on-close */
}
#else
static FILE* open_tmp(void) { return tmpfile(); }
#endif

/* ========================================================================= */
/* Test: Full lifecycle - term detect, registry, parse, output.              */
/* ========================================================================= */

TEST(integration_full_lifecycle) {
    /* Step 1: Terminal detection */
    CliTermInfo info;
    memset(&info, 0, sizeof(info));
    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    /* Step 2: Enable ANSI (no-op on non-Windows, should succeed) */
    rc = cli_term_enable_ansi();
    TEST_ASSERT_EQ(rc, CLI_OK);

    /* Step 3: Create registry and register a command with args */
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliArgSpec args[] = {
        { .long_name = "verbose", .short_name = 'v', .type = CLI_ARG_BOOL, .description = "Enable verbose output" },
        { .long_name = "output", .short_name = 'o', .type = CLI_ARG_STRING, .description = "Output file path", .default_value = "out.txt" },
        { .long_name = "jobs", .short_name = 'j', .type = CLI_ARG_INT, .description = "Parallel job count", .default_value = "4" },
    };

    CliCmdSpec build_spec;
    memset(&build_spec, 0, sizeof(build_spec));
    build_spec.name = "build";
    build_spec.description = "Build the project";
    build_spec.long_description = "Build the project with the specified options.";
    build_spec.args = args;
    build_spec.arg_count = 3;

    rc = cli_cmd_register(reg, &build_spec);
    TEST_ASSERT_EQ(rc, CLI_OK);

    /* Step 4: Parse argv with the command (argv[0] is program name) */
    const char* argv[] = { "prog", "build", "--verbose", "--output", "result.bin", "-j", "8" };
    int argc = 7;

    CliArgValue result_buf[16];
    CliParseResult parse_result;
    memset(&parse_result, 0, sizeof(parse_result));

    rc = cli_cmd_parse(reg, argc, argv, result_buf, 16, &parse_result);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT_EQ(parse_result.error_code, CLI_OK);

    /* Step 5: Assert matched command and arg values */
    TEST_ASSERT(parse_result.matched_cmd != NULL);
    TEST_ASSERT_STR_EQ(parse_result.matched_cmd->name, "build");

    /* Find verbose arg - should be true */
    bool found_verbose = false;
    bool found_output = false;
    bool found_jobs = false;
    for (int i = 0; i < parse_result.arg_value_count; i++) {
        if (strcmp(parse_result.arg_values[i].name, "verbose") == 0) {
            TEST_ASSERT(parse_result.arg_values[i].present);
            TEST_ASSERT_EQ(parse_result.arg_values[i].value.bool_val, true);
            found_verbose = true;
        } else if (strcmp(parse_result.arg_values[i].name, "output") == 0) {
            TEST_ASSERT(parse_result.arg_values[i].present);
            TEST_ASSERT_STR_EQ(parse_result.arg_values[i].value.str_val, "result.bin");
            found_output = true;
        } else if (strcmp(parse_result.arg_values[i].name, "jobs") == 0) {
            TEST_ASSERT(parse_result.arg_values[i].present);
            TEST_ASSERT_EQ(parse_result.arg_values[i].value.int_val, 8);
            found_jobs = true;
        }
    }
    TEST_ASSERT(found_verbose);
    TEST_ASSERT(found_output);
    TEST_ASSERT(found_jobs);

    /* Step 6: Initialize output context */
    CliOutCtx* out_ctx = cli_out_init(&info);
    TEST_ASSERT(out_ctx != NULL);

    /* Step 7: Write styled output to a temp file */
    FILE* f = open_tmp();
    TEST_ASSERT(f != NULL);

    CliStyle style = { .fg = CLI_FG_GREEN, .bold = true };
    cli_out_styled(out_ctx, f, style, "Build succeeded");
    cli_out_line(out_ctx, f, CLI_STYLE_NONE, " - all targets complete");

    fflush(f);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    TEST_ASSERT(len > 0);
    fclose(f);

    /* Step 8: Generate help text */
    char help_buf[2048];
    int help_len = cli_cmd_help(reg, &build_spec, &info, help_buf, sizeof(help_buf));
    TEST_ASSERT(help_len > 0);
    TEST_ASSERT(strstr(help_buf, "build") != NULL);
    TEST_ASSERT(strstr(help_buf, "verbose") != NULL);

    /* Step 9: Cleanup */
    cli_out_destroy(out_ctx);
    cli_cmd_registry_destroy(reg);

    return 0;
}

/* ========================================================================= */
/* Test: Create/destroy cycle - no leaks, no crashes.                        */
/* ========================================================================= */

TEST(integration_create_destroy_no_leaks) {
    CliTermInfo info;
    memset(&info, 0, sizeof(info));
    info.color_level = CLI_COLOR_BASIC_16;
    info.unicode = true;
    info.stdout_tty = true;
    info.columns = 80;

    /* Cycle registry create/destroy multiple times */
    for (int i = 0; i < 10; i++) {
        CliCmdRegistry* reg = cli_cmd_registry_create();
        TEST_ASSERT(reg != NULL);

        /* Register some commands each time */
        CliCmdSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.name = "cmd";
        spec.description = "A command";
        int rc = cli_cmd_register(reg, &spec);
        TEST_ASSERT_EQ(rc, CLI_OK);

        /* Register a subcommand */
        CliCmdSpec sub;
        memset(&sub, 0, sizeof(sub));
        sub.name = "sub";
        sub.description = "A subcommand";
        rc = cli_cmd_register_sub(reg, "cmd", &sub);
        TEST_ASSERT_EQ(rc, CLI_OK);

        cli_cmd_registry_destroy(reg);
    }

    /* Cycle output context create/destroy multiple times */
    for (int i = 0; i < 10; i++) {
        CliOutCtx* ctx = cli_out_init(&info);
        TEST_ASSERT(ctx != NULL);
        cli_out_destroy(ctx);
    }

    /* Verify NULL destroy is safe (repeated) */
    cli_out_destroy(NULL);
    cli_cmd_registry_destroy(NULL);

    return 0;
}
