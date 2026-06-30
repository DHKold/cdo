/*
 * cmd_catalog.c â€” Catalog listing and search subcommand handlers.
 *
 * Subcommands:
 *   list [--tools|--packages]  - Display available catalog entries
 *   search <query>             - Search entries by name/description
 */

#include "commands/cmd_catalog.h"
#include "core/catalog.h"
#include "core/cli_arg_access.h"
#include "core/handler_ctx.h"
#include "core/log.h"

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

int cmd_catalog_list(const CliParseResult* result, void* ctx) {
    (void)ctx;

    bool tools_only = cli_arg_get_bool(result, "tools");
    bool packages_only = cli_arg_get_bool(result, "packages");

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

int cmd_catalog_search(const CliParseResult* result, void* ctx) {
    (void)ctx;

    /* The query is the first positional arg */
    if (result->positional_count < 1 || result->positional_values[0][0] == '\0') {
        cdo_log_error("usage: cdo catalog search <query>");
        return 1;
    }

    const char* query = result->positional_values[0];

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

    catalog_search(&cat, query, false, false, tool_indices, &tool_match_count, pkg_indices, &pkg_match_count);

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

/* -------------------------------------------------------------------------- */
// End of cmd_catalog.c
