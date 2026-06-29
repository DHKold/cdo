/**
 * test_cmd_complete.c - Unit tests for shell completion generation.
 *
 * Tests completion script generation for bash, zsh, and PowerShell,
 * as well as runtime candidate completion for command prefixes,
 * option prefixes, enum values, and subcommand traversal.
 */

#include "cdo_ut.h"
#include "../api/cmd/cli_cmd.h"
#include "../api/cli_errors.h"
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
// Test: Bash completion script contains program name and "complete" keyword.
// =============================================================================

TEST(complete_bash_script_has_function) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("build", "Build");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    char buf[4096];
    int written = cli_cmd_completion_script(reg, "myapp", CLI_SHELL_BASH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);

    /* Must contain program name and bash completion keywords */
    TEST_ASSERT(strstr(buf, "myapp") != NULL);
    TEST_ASSERT(strstr(buf, "complete") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Zsh completion script contains compdef or compadd.
// =============================================================================

TEST(complete_zsh_script_has_compdef) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("build", "Build");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    char buf[4096];
    int written = cli_cmd_completion_script(reg, "myapp", CLI_SHELL_ZSH, buf, sizeof(buf));
    TEST_ASSERT(written > 0);

    /* Must contain zsh completion keywords */
    bool has_compdef = (strstr(buf, "compdef") != NULL);
    bool has_compadd = (strstr(buf, "compadd") != NULL);
    TEST_ASSERT(has_compdef || has_compadd);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: PowerShell completion script contains Register-ArgumentCompleter.
// =============================================================================

TEST(complete_powershell_script_has_register) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("build", "Build");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    char buf[4096];
    int written = cli_cmd_completion_script(reg, "myapp", CLI_SHELL_POWERSHELL, buf, sizeof(buf));
    TEST_ASSERT(written > 0);

    /* Must contain PowerShell completion keyword */
    TEST_ASSERT(strstr(buf, "Register-ArgumentCompleter") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Runtime completion filters commands by prefix.
// =============================================================================

TEST(complete_runtime_command_prefix) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec_build = make_spec("build", "Build the project");
    CliCmdSpec spec_test = make_spec("test", "Run tests");
    CliCmdSpec spec_clean = make_spec("clean", "Clean artifacts");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec_build), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec_test), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec_clean), CLI_OK);

    const char* argv[] = { "b" };
    char buf[1024];
    int count = cli_cmd_complete(reg, 1, argv, 0, buf, sizeof(buf));
    TEST_ASSERT(count >= 1);

    /* "build" matches prefix "b" */
    TEST_ASSERT(strstr(buf, "build") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Runtime completion filters options by prefix.
// =============================================================================

TEST(complete_runtime_option_prefix) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliArgSpec args[] = {
        { .long_name = "verbose", .short_name = 'v', .type = CLI_ARG_BOOL, .description = "Verbose output" },
        { .long_name = "version", .short_name = 0, .type = CLI_ARG_BOOL, .description = "Show version" },
    };

    CliCmdSpec spec = make_spec("build", "Build the project");
    spec.args = args;
    spec.arg_count = 2;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    /* Complete "--ver" after command "build" */
    const char* argv[] = { "build", "--ver" };
    char buf[1024];
    int count = cli_cmd_complete(reg, 2, argv, 1, buf, sizeof(buf));
    TEST_ASSERT(count >= 2);

    /* Both --verbose and --version match "--ver" */
    TEST_ASSERT(strstr(buf, "verbose") != NULL || strstr(buf, "--verbose") != NULL);
    TEST_ASSERT(strstr(buf, "version") != NULL || strstr(buf, "--version") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Runtime completion shows enum values for an enum argument.
// =============================================================================

TEST(complete_runtime_enum_values) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    static const char* formats[] = { "json", "yaml", "toml", NULL };
    CliArgSpec args[] = {
        { .long_name = "format", .short_name = 'f', .type = CLI_ARG_ENUM, .description = "Output format", .enum_values = formats },
    };

    CliCmdSpec spec = make_spec("export", "Export data");
    spec.args = args;
    spec.arg_count = 1;
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    /* Complete after "--format" to get enum values */
    const char* argv[] = { "export", "--format", "" };
    char buf[1024];
    int count = cli_cmd_complete(reg, 3, argv, 2, buf, sizeof(buf));
    TEST_ASSERT(count >= 1);

    /* At least some enum values should appear */
    bool has_json = (strstr(buf, "json") != NULL);
    bool has_yaml = (strstr(buf, "yaml") != NULL);
    bool has_toml = (strstr(buf, "toml") != NULL);
    TEST_ASSERT(has_json || has_yaml || has_toml);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Subcommand traversal - completing after parent shows subcommands.
// =============================================================================

TEST(complete_subcommand_traversal) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec parent = make_spec("deps", "Dependency management");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &parent), CLI_OK);

    CliCmdSpec child_add = make_spec("add", "Add a dependency");
    CliCmdSpec child_rm = make_spec("remove", "Remove a dependency");
    TEST_ASSERT_EQ(cli_cmd_register_sub(reg, "deps", &child_add), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register_sub(reg, "deps", &child_rm), CLI_OK);

    /* Complete after "deps" with empty token */
    const char* argv[] = { "deps", "" };
    char buf[1024];
    int count = cli_cmd_complete(reg, 2, argv, 1, buf, sizeof(buf));
    TEST_ASSERT(count >= 2);

    /* Both subcommands should appear */
    TEST_ASSERT(strstr(buf, "add") != NULL);
    TEST_ASSERT(strstr(buf, "remove") != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Buffer too small returns -1.
// =============================================================================

TEST(complete_buffer_too_small) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("build", "Build");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec), CLI_OK);

    const char* argv[] = { "b" };
    char buf[2];
    int count = cli_cmd_complete(reg, 1, argv, 0, buf, 2);
    TEST_ASSERT_EQ(count, -1);

    cli_cmd_registry_destroy(reg);
    return 0;
}
