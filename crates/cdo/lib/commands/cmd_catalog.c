/*
 * cmd_catalog.c — Catalog listing and search command.
 *
 * Subcommands:
 *   list [--tools|--packages]  - Display available catalog entries
 *   search <query>             - Search entries by name/description
 */

#include "commands/cmd_catalog.h"
#include "core/catalog.h"
#include "core/output.h"
#include "core/cli.h"

#include <stdio.h>
#include <string.h>

/* Maximum number of search result indices we support */
#define MAX_SEARCH_RESULTS 512

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

/// Display a single tool entry formatted as: "  {name} {version}  {description}"
static void print_tool_entry(const CatalogToolEntry* tool) {
    if (tool->description[0] != '\0') {
        printf("  %s  %s  %s\n", tool->name, tool->version, tool->description);
    } else {
        printf("  %s  %s\n", tool->name, tool->version);
    }
}

/// Display a single package entry formatted as: "  {name} {version}  {description}"
static void print_package_entry(const CatalogPackageEntry* pkg) {
    if (pkg->description[0] != '\0') {
        printf("  %s  %s  %s\n", pkg->name, pkg->version, pkg->description);
    } else {
        printf("  %s  %s\n", pkg->name, pkg->version);
    }
}

/* --------------------------------------------------------------------------
 * catalog list
 * -------------------------------------------------------------------------- */

static int catalog_list(const CdoOptions* opts) {
    bool tools_only = false;
    bool packages_only = false;

    /* Check for --tools or --packages flags in positional args */
    for (int i = 1; i < opts->positional_count; i++) {
        const char* arg = opts->positional_args[i];
        if (strcmp(arg, "--tools") == 0) {
            tools_only = true;
        } else if (strcmp(arg, "--packages") == 0) {
            packages_only = true;
        }
    }

    /* Load the catalog */
    Catalog cat;
    int rc = catalog_load(&cat, ".");
    if (rc != 0) {
        return 1;
    }

    /* Check if catalog is empty */
    bool show_tools = !packages_only;
    bool show_packages = !tools_only;

    int visible_tools = show_tools ? cat.tool_count : 0;
    int visible_packages = show_packages ? cat.package_count : 0;

    if (visible_tools == 0 && visible_packages == 0) {
        printf("catalog is empty\n");
        catalog_free(&cat);
        return 0;
    }

    /* Display tool entries */
    if (show_tools && cat.tool_count > 0) {
        printf("tools:\n");
        for (int i = 0; i < cat.tool_count; i++) {
            print_tool_entry(&cat.tools[i]);
        }
        if (show_packages && cat.package_count > 0) {
            printf("\n");
        }
    }

    /* Display package entries */
    if (show_packages && cat.package_count > 0) {
        printf("packages:\n");
        for (int i = 0; i < cat.package_count; i++) {
            print_package_entry(&cat.packages[i]);
        }
    }

    catalog_free(&cat);
    return 0;
}

/* --------------------------------------------------------------------------
 * catalog search
 * -------------------------------------------------------------------------- */

static int catalog_search_cmd(const CdoOptions* opts) {
    /* The query is the next positional arg after "search" */
    const char* query = NULL;

    for (int i = 1; i < opts->positional_count; i++) {
        const char* arg = opts->positional_args[i];
        /* Skip flags */
        if (arg[0] == '-') continue;
        query = arg;
        break;
    }

    if (!query || query[0] == '\0') {
        cdo_error("usage: cdo catalog search <query>");
        return 1;
    }

    /* Load the catalog */
    Catalog cat;
    int rc = catalog_load(&cat, ".");
    if (rc != 0) {
        return 1;
    }

    /* Perform search */
    int tool_indices[MAX_SEARCH_RESULTS];
    int pkg_indices[MAX_SEARCH_RESULTS];
    int tool_match_count = 0;
    int pkg_match_count = 0;

    catalog_search(&cat, query, false, false,
                   tool_indices, &tool_match_count,
                   pkg_indices, &pkg_match_count);

    /* Check for zero results */
    if (tool_match_count == 0 && pkg_match_count == 0) {
        printf("no entries matched '%s'\n", query);
        catalog_free(&cat);
        return 0;
    }

    /* Display matching tools */
    if (tool_match_count > 0) {
        printf("tools:\n");
        for (int i = 0; i < tool_match_count && i < MAX_SEARCH_RESULTS; i++) {
            print_tool_entry(&cat.tools[tool_indices[i]]);
        }
        if (pkg_match_count > 0) {
            printf("\n");
        }
    }

    /* Display matching packages */
    if (pkg_match_count > 0) {
        printf("packages:\n");
        for (int i = 0; i < pkg_match_count && i < MAX_SEARCH_RESULTS; i++) {
            print_package_entry(&cat.packages[pkg_indices[i]]);
        }
    }

    catalog_free(&cat);
    return 0;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int cmd_catalog(const CdoOptions* opts) {
    if (opts->help || opts->positional_count < 1) {
        cdo_cli_print_help(CDO_CMD_CATALOG, stdout);
        return opts->help ? 0 : 1;
    }

    const char* subcommand = opts->positional_args[0];

    if (strcmp(subcommand, "list") == 0) {
        return catalog_list(opts);
    } else if (strcmp(subcommand, "search") == 0) {
        return catalog_search_cmd(opts);
    }

    /* Unrecognized subcommand */
    cdo_error("Unknown subcommand '%s'", subcommand);
    cdo_info("Available subcommands: list, search");
    cdo_info("Run 'cdo catalog --help' for usage information.");
    return 1;
}
