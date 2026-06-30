/**
 * registry_setup.c - CLI registry creation and command registration.
 *
 * Creates the CliCmdRegistry and registers all 14 active commands with their
 * argument specs, positional specs, and global options. Subcommands are
 * registered in task 3.3.
 */
#include "core/registry_setup.h"
#include "core/registry_subcmds.h"
#include "commands/cmd_build.h"
#include "commands/cmd_run.h"
#include "commands/cmd_test.h"
#include "commands/cmd_clean.h"
#include "commands/cmd_new.h"
#include "commands/cmd_doctor.h"
#include "commands/cmd_hook.h"
#include "commands/cmd_fmt.h"
#include "commands/cmd_install.h"
#include "commands/cmd_e2e.h"

#include <stddef.h>
#include <string.h>

/* --- Global Option Enum Values --- */

static const char* s_color_values[] = { "auto", "always", "never", NULL };
static const char* s_log_level_values[] = { "error", "warn", "info", "debug", "trace", NULL };
static const char* s_completions_values[] = { "bash", "zsh", "powershell", NULL };

/* --- Global Options Definition --- */

static const CliArgSpec s_global_options[] = {
    { .long_name = "verbose",      .short_name = 'v', .type = CLI_ARG_BOOL,   .description = "Enable verbose (debug-level) output" },
    { .long_name = "quiet",        .short_name = 'q', .type = CLI_ARG_BOOL,   .description = "Suppress all output except errors" },
    { .long_name = "help",         .short_name = 'h', .type = CLI_ARG_BOOL,   .description = "Show help for the command" },
    { .long_name = "release",      .short_name = 'r', .type = CLI_ARG_BOOL,   .description = "Build in release mode with optimizations" },
    { .long_name = "color",        .short_name = 0,   .type = CLI_ARG_ENUM,   .description = "Control color output (auto, always, never)", .default_value = "auto", .enum_values = s_color_values },
    { .long_name = "log-level",    .short_name = 0,   .type = CLI_ARG_ENUM,   .description = "Set the log verbosity level", .default_value = "info", .enum_values = s_log_level_values },
    { .long_name = "completions",  .short_name = 0,   .type = CLI_ARG_ENUM,   .description = "Output a shell completion script and exit (bash, zsh, powershell)", .enum_values = s_completions_values },
    { .long_name = "profile",      .short_name = 0,   .type = CLI_ARG_STRING, .description = "Build profile to use" },
    { .long_name = "jobs",         .short_name = 'j', .type = CLI_ARG_INT,    .description = "Number of parallel jobs", .int_min = 1 },
    { .long_name = "lock-timeout", .short_name = 0,   .type = CLI_ARG_INT,    .description = "Lock acquisition timeout in seconds", .int_min = 0 },
};

static const int s_global_option_count = sizeof(s_global_options) / sizeof(s_global_options[0]);

/* --- Command-Specific Options --- */

static const CliArgSpec s_build_options[] = {
    { .long_name = "no-cache", .short_name = 0, .type = CLI_ARG_BOOL, .description = "Disable build cache lookup and population" },
};

static const CliArgSpec s_test_options[] = {
    { .long_name = "coverage", .short_name = 0, .type = CLI_ARG_BOOL,   .description = "Build with coverage instrumentation and report" },
    { .long_name = "list",     .short_name = 0, .type = CLI_ARG_BOOL,   .description = "List available tests without running them" },
    { .long_name = "filter",   .short_name = 0, .type = CLI_ARG_STRING, .description = "Run only tests matching pattern" },
};

static const CliArgSpec s_clean_options[] = {
    { .long_name = "cache", .short_name = 0, .type = CLI_ARG_BOOL, .description = "Also clear the build cache" },
};

static const CliArgSpec s_e2e_options[] = {
    { .long_name = "filter",     .short_name = 0, .type = CLI_ARG_STRING, .description = "Run only tests matching pattern" },
    { .long_name = "list",       .short_name = 0, .type = CLI_ARG_BOOL,   .description = "List available tests without running them" },
    { .long_name = "timeout",    .short_name = 0, .type = CLI_ARG_INT,    .description = "Per-test timeout in seconds" },
    { .long_name = "keep-temps", .short_name = 0, .type = CLI_ARG_BOOL,   .description = "Preserve temporary directories after tests" },
};

static const CliArgSpec s_fmt_options[] = {
    { .long_name = "check", .short_name = 0, .type = CLI_ARG_BOOL, .description = "Check formatting without modifying files" },
};

static const CliArgSpec s_init_options[] = {
    { .long_name = "venv", .short_name = 0, .type = CLI_ARG_BOOL, .description = "Create a virtual environment (.cdo/ with local binary)" },
};

static const CliArgSpec s_install_options[] = {
    { .long_name = "path",   .short_name = 0, .type = CLI_ARG_STRING, .description = "Base install directory (default: ~/.cdo/)" },
    { .long_name = "global", .short_name = 0, .type = CLI_ARG_BOOL,   .description = "Install to system-wide location" },
    { .long_name = "debug",  .short_name = 0, .type = CLI_ARG_BOOL,   .description = "Preserve debug symbols (skip strip)" },
    { .long_name = "force",  .short_name = 0, .type = CLI_ARG_BOOL,   .description = "Overwrite without warning" },
    { .long_name = "list",   .short_name = 0, .type = CLI_ARG_BOOL,   .description = "List installed applications" },
};

static const CliArgSpec s_uninstall_options[] = {
    { .long_name = "path",   .short_name = 0, .type = CLI_ARG_STRING, .description = "Base directory to uninstall from (default: ~/.cdo/)" },
    { .long_name = "global", .short_name = 0, .type = CLI_ARG_BOOL,   .description = "Uninstall from system-wide location" },
};

/* --- Positional Specs --- */

static const CliPositionalSpec s_pos_crates[] = {
    { .name = "CRATE", .description = "Crate names to operate on", .cardinality = CLI_CARD_ZERO_OR_MORE },
};

static const CliPositionalSpec s_pos_target[] = {
    { .name = "CRATE", .description = "Executable crate to run", .cardinality = CLI_CARD_ZERO_OR_ONE },
};

static const CliPositionalSpec s_pos_new[] = {
    { .name = "NAME", .description = "Project name", .cardinality = CLI_CARD_EXACTLY_ONE },
    { .name = "TEMPLATE", .description = "Template to use (default: executable)", .cardinality = CLI_CARD_ZERO_OR_ONE },
};

static const CliPositionalSpec s_pos_clean[] = {
    { .name = "CRATE", .description = "Crate to clean (default: all)", .cardinality = CLI_CARD_ZERO_OR_ONE },
};

static const CliPositionalSpec s_pos_init[] = {
    { .name = "TEMPLATE", .description = "Template to use", .cardinality = CLI_CARD_ZERO_OR_ONE },
};

static const CliPositionalSpec s_pos_hook[] = {
    { .name = "ARGS", .description = "Hook subcommand and arguments", .cardinality = CLI_CARD_ZERO_OR_MORE },
};

static const CliPositionalSpec s_pos_install[] = {
    { .name = "CRATE", .description = "Executable crate to install", .cardinality = CLI_CARD_ZERO_OR_ONE },
};

static const CliPositionalSpec s_pos_uninstall[] = {
    { .name = "NAME", .description = "Name of the installed app to remove", .cardinality = CLI_CARD_EXACTLY_ONE },
};

/* --- Merged Arg Spec Buffers ---
 * Each command needs global options + its own options merged into a single array.
 * We use static buffers sized to max(global + command-specific).
 */

#define MAX_ARGS 16

static CliArgSpec s_build_args[MAX_ARGS];
static CliArgSpec s_run_args[MAX_ARGS];
static CliArgSpec s_test_args[MAX_ARGS];
static CliArgSpec s_clean_args[MAX_ARGS];
static CliArgSpec s_new_args[MAX_ARGS];
static CliArgSpec s_init_args[MAX_ARGS];
static CliArgSpec s_deps_args[MAX_ARGS];
static CliArgSpec s_catalog_args[MAX_ARGS];
static CliArgSpec s_cache_args[MAX_ARGS];
static CliArgSpec s_hook_args[MAX_ARGS];
static CliArgSpec s_fmt_args[MAX_ARGS];
static CliArgSpec s_tool_args[MAX_ARGS];
static CliArgSpec s_doctor_args[MAX_ARGS];
static CliArgSpec s_help_args[MAX_ARGS];
static CliArgSpec s_install_args[MAX_ARGS];
static CliArgSpec s_uninstall_args[MAX_ARGS];
static CliArgSpec s_e2e_args[MAX_ARGS];

/// Merge global options with command-specific options into a destination buffer.
/// Returns total count of merged args.
static int merge_args(CliArgSpec* dest, const CliArgSpec* cmd_opts, int cmd_opt_count) {
    int total = 0;
    memcpy(dest, s_global_options, (size_t)s_global_option_count * sizeof(CliArgSpec));
    total = s_global_option_count;
    if (cmd_opts && cmd_opt_count > 0) {
        memcpy(dest + total, cmd_opts, (size_t)cmd_opt_count * sizeof(CliArgSpec));
        total += cmd_opt_count;
    }
    return total;
}

/* --- Public API --- */

const CliArgSpec* cdo_global_options(int* count) {
    if (count) {
        *count = s_global_option_count;
    }
    return s_global_options;
}

CliCmdRegistry* cdo_registry_create(void) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    if (!reg) {
        return NULL;
    }

    /* Merge args for each command */
    int build_arg_count   = merge_args(s_build_args,   s_build_options, 1);
    int run_arg_count     = merge_args(s_run_args,     NULL, 0);
    int test_arg_count    = merge_args(s_test_args,    s_test_options, 3);
    int clean_arg_count   = merge_args(s_clean_args,   s_clean_options, 1);
    int new_arg_count     = merge_args(s_new_args,     NULL, 0);
    int init_arg_count    = merge_args(s_init_args,    s_init_options, 1);
    int deps_arg_count    = merge_args(s_deps_args,    NULL, 0);
    int catalog_arg_count = merge_args(s_catalog_args, NULL, 0);
    int cache_arg_count   = merge_args(s_cache_args,   NULL, 0);
    int hook_arg_count    = merge_args(s_hook_args,    NULL, 0);
    int fmt_arg_count     = merge_args(s_fmt_args,     s_fmt_options, 1);
    int tool_arg_count    = merge_args(s_tool_args,    NULL, 0);
    int doctor_arg_count  = merge_args(s_doctor_args,  NULL, 0);
    int help_arg_count    = merge_args(s_help_args,    NULL, 0);
    int install_arg_count = merge_args(s_install_args, s_install_options, 5);
    int uninstall_arg_count = merge_args(s_uninstall_args, s_uninstall_options, 2);
    int e2e_arg_count   = merge_args(s_e2e_args,     s_e2e_options, 4);

    /* Register all 16 active commands */

    CliCmdSpec spec_build = { .name = "build", .description = "Build crates in the workspace", .handler = cmd_build, .args = s_build_args, .arg_count = build_arg_count, .positionals = s_pos_crates, .positional_count = 1 };
    cli_cmd_register(reg, &spec_build);

    CliCmdSpec spec_run = { .name = "run", .description = "Build and run an executable", .handler = cmd_run, .args = s_run_args, .arg_count = run_arg_count, .positionals = s_pos_target, .positional_count = 1 };
    cli_cmd_register(reg, &spec_run);

    CliCmdSpec spec_test = { .name = "test", .description = "Build and run tests", .handler = cmd_test, .args = s_test_args, .arg_count = test_arg_count, .positionals = s_pos_crates, .positional_count = 1 };
    cli_cmd_register(reg, &spec_test);

    CliCmdSpec spec_clean = { .name = "clean", .description = "Remove build artifacts", .handler = cmd_clean, .args = s_clean_args, .arg_count = clean_arg_count, .positionals = s_pos_clean, .positional_count = 1 };
    cli_cmd_register(reg, &spec_clean);

    CliCmdSpec spec_new = { .name = "new", .description = "Create a new project from template", .handler = cmd_new, .args = s_new_args, .arg_count = new_arg_count, .positionals = s_pos_new, .positional_count = 2 };
    cli_cmd_register(reg, &spec_new);

    CliCmdSpec spec_init = { .name = "init", .description = "Initialize a project in current directory", .handler = cmd_init, .args = s_init_args, .arg_count = init_arg_count, .positionals = s_pos_init, .positional_count = 1 };
    cli_cmd_register(reg, &spec_init);

    CliCmdSpec spec_install = { .name = "install", .description = "Build and install an app system-wide", .handler = cmd_install, .args = s_install_args, .arg_count = install_arg_count, .positionals = s_pos_install, .positional_count = 1 };
    cli_cmd_register(reg, &spec_install);

    CliCmdSpec spec_uninstall = { .name = "uninstall", .description = "Remove a previously installed app", .handler = cmd_uninstall, .args = s_uninstall_args, .arg_count = uninstall_arg_count, .positionals = s_pos_uninstall, .positional_count = 1 };
    cli_cmd_register(reg, &spec_uninstall);

    CliCmdSpec spec_deps = { .name = "deps", .description = "Manage dependencies (add, remove, list)", .args = s_deps_args, .arg_count = deps_arg_count };
    cli_cmd_register(reg, &spec_deps);

    CliCmdSpec spec_catalog = { .name = "catalog", .description = "Browse and search the package/tool catalog", .args = s_catalog_args, .arg_count = catalog_arg_count };
    cli_cmd_register(reg, &spec_catalog);

    CliCmdSpec spec_cache = { .name = "cache", .description = "Manage the build cache (stats, clear)", .args = s_cache_args, .arg_count = cache_arg_count };
    cli_cmd_register(reg, &spec_cache);

    CliCmdSpec spec_hook = { .name = "hook", .description = "List or manually run lifecycle hooks", .handler = cmd_hook, .args = s_hook_args, .arg_count = hook_arg_count, .positionals = s_pos_hook, .positional_count = 1 };
    cli_cmd_register(reg, &spec_hook);

    CliCmdSpec spec_fmt = { .name = "fmt", .description = "Format source files", .handler = cmd_fmt, .args = s_fmt_args, .arg_count = fmt_arg_count, .positionals = s_pos_crates, .positional_count = 1 };
    cli_cmd_register(reg, &spec_fmt);

    CliCmdSpec spec_tool = { .name = "tool", .description = "Manage local tool installations", .args = s_tool_args, .arg_count = tool_arg_count };
    cli_cmd_register(reg, &spec_tool);

    CliCmdSpec spec_doctor = { .name = "doctor", .description = "Check environment health", .handler = cmd_doctor, .args = s_doctor_args, .arg_count = doctor_arg_count };
    cli_cmd_register(reg, &spec_doctor);

    CliCmdSpec spec_help = { .name = "help", .description = "Show help information", .args = s_help_args, .arg_count = help_arg_count };
    cli_cmd_register(reg, &spec_help);

    CliCmdSpec spec_e2e = { .name = "e2e", .description = "Build and run end-to-end tests", .handler = cmd_e2e, .args = s_e2e_args, .arg_count = e2e_arg_count, .positionals = s_pos_crates, .positional_count = 1 };
    cli_cmd_register(reg, &spec_e2e);

    /* Register subcommands (deps add/remove/list, catalog list/search, cache stats/clear, tool install/list/remove) */
    if (cdo_register_subcommands(reg) != 0) {
        cli_cmd_registry_destroy(reg);
        return NULL;
    }

    return reg;
}
