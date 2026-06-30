/**
 * registry_subcmds.c - Subcommand registration for parent commands.
 *
 * Registers subcommands under: deps (add, remove, list), catalog (list, search),
 * cache (stats, clear), and tool (install, list, remove).
 *
 * Each subcommand merges the global options with its own command-specific options.
 * Global options are fetched via cdo_global_options() from registry_setup.c.
 */
#include "core/registry_subcmds.h"
#include "core/registry_setup.h"

#include <stddef.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Internal helper: merge global options with command-specific options.
 *
 * Allocates a static buffer (per-call-site) large enough for globals + locals.
 * Since registration happens once at startup and specs are static, we use
 * file-scoped static arrays sized generously.
 * -------------------------------------------------------------------------- */

#define MAX_MERGED_ARGS 32

/// Merge global options with command-specific options into a static buffer.
/// Returns the total count. The merged array is valid for the lifetime of the process.
static int merge_args(const CliArgSpec* local, int local_count, CliArgSpec* out, int out_cap) {
    int global_count = 0;
    const CliArgSpec* globals = cdo_global_options(&global_count);

    int total = global_count + local_count;
    if (total > out_cap) {
        total = out_cap;
    }

    /* Copy globals first */
    int g = (global_count < out_cap) ? global_count : out_cap;
    memcpy(out, globals, (size_t)g * sizeof(CliArgSpec));

    /* Append locals */
    if (local && local_count > 0) {
        int l = (total - g < local_count) ? (total - g) : local_count;
        memcpy(out + g, local, (size_t)l * sizeof(CliArgSpec));
    }

    return total;
}

/* ==========================================================================
 * deps subcommands: add, remove, list
 * ========================================================================== */

/* --- deps add: --dev (bool), --version (string), positional: package name --- */

static const CliArgSpec s_deps_add_local[] = {
    {
        .long_name   = "dev",
        .short_name  = 0,
        .type        = CLI_ARG_BOOL,
        .description = "Add as a development-only dependency",
    },
    {
        .long_name   = "version",
        .short_name  = 0,
        .type        = CLI_ARG_STRING,
        .description = "Version constraint for the dependency",
    },
};

static CliArgSpec s_deps_add_args[MAX_MERGED_ARGS];
static int s_deps_add_arg_count = 0;

static const CliPositionalSpec s_deps_add_positionals[] = {
    { .name = "package", .description = "Package name to add", .cardinality = CLI_CARD_EXACTLY_ONE },
};

/* --- deps remove: positional: package name --- */

static CliArgSpec s_deps_remove_args[MAX_MERGED_ARGS];
static int s_deps_remove_arg_count = 0;

static const CliPositionalSpec s_deps_remove_positionals[] = {
    { .name = "package", .description = "Package name to remove", .cardinality = CLI_CARD_EXACTLY_ONE },
};

/* --- deps list: no extra options, no positionals --- */

static CliArgSpec s_deps_list_args[MAX_MERGED_ARGS];
static int s_deps_list_arg_count = 0;

/* ==========================================================================
 * catalog subcommands: list, search
 * ========================================================================== */

/* --- catalog list: --tools (bool), --packages (bool) --- */

static const CliArgSpec s_catalog_list_local[] = {
    {
        .long_name   = "tools",
        .short_name  = 0,
        .type        = CLI_ARG_BOOL,
        .description = "Show only tool entries",
    },
    {
        .long_name   = "packages",
        .short_name  = 0,
        .type        = CLI_ARG_BOOL,
        .description = "Show only package entries",
    },
};

static CliArgSpec s_catalog_list_args[MAX_MERGED_ARGS];
static int s_catalog_list_arg_count = 0;

/* --- catalog search: positional: query --- */

static CliArgSpec s_catalog_search_args[MAX_MERGED_ARGS];
static int s_catalog_search_arg_count = 0;

static const CliPositionalSpec s_catalog_search_positionals[] = {
    { .name = "query", .description = "Search query string", .cardinality = CLI_CARD_EXACTLY_ONE },
};

/* ==========================================================================
 * cache subcommands: stats, clear
 * ========================================================================== */

static CliArgSpec s_cache_stats_args[MAX_MERGED_ARGS];
static int s_cache_stats_arg_count = 0;

static CliArgSpec s_cache_clear_args[MAX_MERGED_ARGS];
static int s_cache_clear_arg_count = 0;

/* ==========================================================================
 * tool subcommands: install, list, remove
 * ========================================================================== */

/* --- tool install: positional: tool name --- */

static CliArgSpec s_tool_install_args[MAX_MERGED_ARGS];
static int s_tool_install_arg_count = 0;

static const CliPositionalSpec s_tool_install_positionals[] = {
    { .name = "tool-name", .description = "Name of the tool to install", .cardinality = CLI_CARD_EXACTLY_ONE },
};

/* --- tool list: no extra options, no positionals --- */

static CliArgSpec s_tool_list_args[MAX_MERGED_ARGS];
static int s_tool_list_arg_count = 0;

/* --- tool remove: positional: tool name --- */

static CliArgSpec s_tool_remove_args[MAX_MERGED_ARGS];
static int s_tool_remove_arg_count = 0;

static const CliPositionalSpec s_tool_remove_positionals[] = {
    { .name = "tool-name", .description = "Name of the tool to remove", .cardinality = CLI_CARD_EXACTLY_ONE },
};

/* ==========================================================================
 * Public API
 * ========================================================================== */

int cdo_register_subcommands(CliCmdRegistry* reg) {
    if (!reg) return -1;

    int rc = 0;

    /* --- Merge global options into each subcommand's arg array --- */

    s_deps_add_arg_count = merge_args(s_deps_add_local, 2, s_deps_add_args, MAX_MERGED_ARGS);
    s_deps_remove_arg_count = merge_args(NULL, 0, s_deps_remove_args, MAX_MERGED_ARGS);
    s_deps_list_arg_count = merge_args(NULL, 0, s_deps_list_args, MAX_MERGED_ARGS);

    s_catalog_list_arg_count = merge_args(s_catalog_list_local, 2, s_catalog_list_args, MAX_MERGED_ARGS);
    s_catalog_search_arg_count = merge_args(NULL, 0, s_catalog_search_args, MAX_MERGED_ARGS);

    s_cache_stats_arg_count = merge_args(NULL, 0, s_cache_stats_args, MAX_MERGED_ARGS);
    s_cache_clear_arg_count = merge_args(NULL, 0, s_cache_clear_args, MAX_MERGED_ARGS);

    s_tool_install_arg_count = merge_args(NULL, 0, s_tool_install_args, MAX_MERGED_ARGS);
    s_tool_list_arg_count = merge_args(NULL, 0, s_tool_list_args, MAX_MERGED_ARGS);
    s_tool_remove_arg_count = merge_args(NULL, 0, s_tool_remove_args, MAX_MERGED_ARGS);

    /* --- deps subcommands --- */

    CliCmdSpec deps_add = {
        .name             = "add",
        .description      = "Add a dependency to the current crate",
        .handler          = NULL, /* Handler wired in Phase 3 */
        .handler_ctx      = NULL,
        .args             = s_deps_add_args,
        .arg_count        = s_deps_add_arg_count,
        .positionals      = s_deps_add_positionals,
        .positional_count = 1,
    };
    rc = cli_cmd_register_sub(reg, "deps", &deps_add);
    if (rc != 0) return rc;

    CliCmdSpec deps_remove = {
        .name             = "remove",
        .description      = "Remove a dependency from the current crate",
        .handler          = NULL,
        .handler_ctx      = NULL,
        .args             = s_deps_remove_args,
        .arg_count        = s_deps_remove_arg_count,
        .positionals      = s_deps_remove_positionals,
        .positional_count = 1,
    };
    rc = cli_cmd_register_sub(reg, "deps", &deps_remove);
    if (rc != 0) return rc;

    CliCmdSpec deps_list = {
        .name             = "list",
        .description      = "List all dependencies of the current crate",
        .handler          = NULL,
        .handler_ctx      = NULL,
        .args             = s_deps_list_args,
        .arg_count        = s_deps_list_arg_count,
        .positionals      = NULL,
        .positional_count = 0,
    };
    rc = cli_cmd_register_sub(reg, "deps", &deps_list);
    if (rc != 0) return rc;

    /* --- catalog subcommands --- */

    CliCmdSpec catalog_list = {
        .name             = "list",
        .description      = "List available catalog entries",
        .handler          = NULL,
        .handler_ctx      = NULL,
        .args             = s_catalog_list_args,
        .arg_count        = s_catalog_list_arg_count,
        .positionals      = NULL,
        .positional_count = 0,
    };
    rc = cli_cmd_register_sub(reg, "catalog", &catalog_list);
    if (rc != 0) return rc;

    CliCmdSpec catalog_search = {
        .name             = "search",
        .description      = "Search catalog entries by name or description",
        .handler          = NULL,
        .handler_ctx      = NULL,
        .args             = s_catalog_search_args,
        .arg_count        = s_catalog_search_arg_count,
        .positionals      = s_catalog_search_positionals,
        .positional_count = 1,
    };
    rc = cli_cmd_register_sub(reg, "catalog", &catalog_search);
    if (rc != 0) return rc;

    /* --- cache subcommands --- */

    CliCmdSpec cache_stats = {
        .name             = "stats",
        .description      = "Display cache statistics (size, entries, hit rate)",
        .handler          = NULL,
        .handler_ctx      = NULL,
        .args             = s_cache_stats_args,
        .arg_count        = s_cache_stats_arg_count,
        .positionals      = NULL,
        .positional_count = 0,
    };
    rc = cli_cmd_register_sub(reg, "cache", &cache_stats);
    if (rc != 0) return rc;

    CliCmdSpec cache_clear = {
        .name             = "clear",
        .description      = "Clear all cached build artifacts",
        .handler          = NULL,
        .handler_ctx      = NULL,
        .args             = s_cache_clear_args,
        .arg_count        = s_cache_clear_arg_count,
        .positionals      = NULL,
        .positional_count = 0,
    };
    rc = cli_cmd_register_sub(reg, "cache", &cache_clear);
    if (rc != 0) return rc;

    /* --- tool subcommands --- */

    CliCmdSpec tool_install = {
        .name             = "install",
        .description      = "Download and install a tool from the catalog",
        .handler          = NULL,
        .handler_ctx      = NULL,
        .args             = s_tool_install_args,
        .arg_count        = s_tool_install_arg_count,
        .positionals      = s_tool_install_positionals,
        .positional_count = 1,
    };
    rc = cli_cmd_register_sub(reg, "tool", &tool_install);
    if (rc != 0) return rc;

    CliCmdSpec tool_list = {
        .name             = "list",
        .description      = "List currently installed tools",
        .handler          = NULL,
        .handler_ctx      = NULL,
        .args             = s_tool_list_args,
        .arg_count        = s_tool_list_arg_count,
        .positionals      = NULL,
        .positional_count = 0,
    };
    rc = cli_cmd_register_sub(reg, "tool", &tool_list);
    if (rc != 0) return rc;

    CliCmdSpec tool_remove = {
        .name             = "remove",
        .description      = "Remove an installed tool",
        .handler          = NULL,
        .handler_ctx      = NULL,
        .args             = s_tool_remove_args,
        .arg_count        = s_tool_remove_arg_count,
        .positionals      = s_tool_remove_positionals,
        .positional_count = 1,
    };
    rc = cli_cmd_register_sub(reg, "tool", &tool_remove);
    if (rc != 0) return rc;

    return 0;
}
