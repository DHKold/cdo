/**
 * test_completions.c - Unit tests for shell completion script generation.
 *
 * Validates: Requirements 13.1, 13.2
 *
 * Tests that cli_cmd_completion_script() generates valid completion scripts
 * for bash, zsh, and PowerShell shells using the full cdo registry with
 * all 14 commands.
 */
#include "cdo_ut.h"
#include "core/registry_setup.h"
#include "cmd/cli_cmd.h"

#include <string.h>
#include <stdio.h>

/* ========================================================================= */
/* Helpers                                                                   */
/* ========================================================================= */

#define SCRIPT_BUF_SIZE 8192

/// All 14 command names that must appear in completion scripts.
static const char* ALL_COMMANDS[] = {
    "build", "run", "test", "clean", "new", "init",
    "deps", "catalog", "cache", "hook", "fmt", "tool",
    "doctor", "help"
};
#define COMMAND_COUNT 14

/* ========================================================================= */
/* Test: Bash completion script generation produces non-empty output         */
/* ========================================================================= */

TEST(completions_bash_generates_non_empty_script) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_BASH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(buf[0] != '\0');

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Bash script contains "complete -F" registration pattern             */
/* ========================================================================= */

TEST(completions_bash_has_complete_f_pattern) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_BASH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(strstr(buf, "complete -F") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Bash script contains the program name "cdo"                         */
/* ========================================================================= */

TEST(completions_bash_contains_program_name) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_BASH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(strstr(buf, "cdo") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Bash script contains all 14 command names                           */
/* ========================================================================= */

TEST(completions_bash_contains_all_commands) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_BASH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);

    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strstr(buf, ALL_COMMANDS[i]) == NULL) {
            char msg[128];
            snprintf(msg, sizeof(msg), "bash script missing command: %s", ALL_COMMANDS[i]);
            cdo_ut_record_failure(__FILE__, __LINE__, msg, "not found", ALL_COMMANDS[i]);
            cli_cmd_registry_destroy(reg);
            return 1;
        }
    }

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Bash script contains COMPREPLY pattern                              */
/* ========================================================================= */

TEST(completions_bash_has_compreply) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_BASH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(strstr(buf, "COMPREPLY") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Zsh completion script generation produces non-empty output          */
/* ========================================================================= */

TEST(completions_zsh_generates_non_empty_script) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_ZSH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(buf[0] != '\0');

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Zsh script contains "_cdo" function definition                      */
/* ========================================================================= */

TEST(completions_zsh_has_function_definition) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_ZSH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(strstr(buf, "_cdo") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Zsh script contains "compadd" keyword                               */
/* ========================================================================= */

TEST(completions_zsh_has_compadd) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_ZSH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(strstr(buf, "compadd") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Zsh script contains "compdef" registration                          */
/* ========================================================================= */

TEST(completions_zsh_has_compdef) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_ZSH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(strstr(buf, "compdef") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: Zsh script contains all 14 command names                            */
/* ========================================================================= */

TEST(completions_zsh_contains_all_commands) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_ZSH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);

    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strstr(buf, ALL_COMMANDS[i]) == NULL) {
            char msg[128];
            snprintf(msg, sizeof(msg), "zsh script missing command: %s", ALL_COMMANDS[i]);
            cdo_ut_record_failure(__FILE__, __LINE__, msg, "not found", ALL_COMMANDS[i]);
            cli_cmd_registry_destroy(reg);
            return 1;
        }
    }

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: PowerShell completion script generation produces non-empty output    */
/* ========================================================================= */

TEST(completions_powershell_generates_non_empty_script) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_POWERSHELL, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(buf[0] != '\0');

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: PowerShell script contains "Register-ArgumentCompleter"             */
/* ========================================================================= */

TEST(completions_powershell_has_register_argument_completer) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_POWERSHELL, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(strstr(buf, "Register-ArgumentCompleter") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: PowerShell script contains the program name "cdo"                   */
/* ========================================================================= */

TEST(completions_powershell_contains_program_name) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_POWERSHELL, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(strstr(buf, "cdo") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: PowerShell script contains all 14 command names                     */
/* ========================================================================= */

TEST(completions_powershell_contains_all_commands) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_POWERSHELL, buf, sizeof(buf));
    TEST_ASSERT(written > 0);

    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strstr(buf, ALL_COMMANDS[i]) == NULL) {
            char msg[128];
            snprintf(msg, sizeof(msg), "powershell script missing command: %s", ALL_COMMANDS[i]);
            cdo_ut_record_failure(__FILE__, __LINE__, msg, "not found", ALL_COMMANDS[i]);
            cli_cmd_registry_destroy(reg);
            return 1;
        }
    }

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ========================================================================= */
/* Test: PowerShell script contains CompletionResult pattern                 */
/* ========================================================================= */

TEST(completions_powershell_has_completion_result) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    char buf[SCRIPT_BUF_SIZE];
    int written = cli_cmd_completion_script(reg, "cdo", CLI_SHELL_POWERSHELL, buf, sizeof(buf));
    TEST_ASSERT(written > 0);
    TEST_ASSERT(strstr(buf, "CompletionResult") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}
