/**
 * cdo hook — List or manually run lifecycle hooks.
 *
 * Subcommands:
 *   list              — List all configured hooks (workspace + crates)
 *   run <point> [<crate>] — Manually trigger a hook
 */

#include "commands/cmd_hook.h"
#include "model/hooks.h"
#include "core/hooks_exec.h"
#include "core/output.h"
#include "model/workspace.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

/// Print usage for `cdo hook`.
static void print_hook_usage(void) {
    printf("Usage: cdo hook <subcommand>\n"
           "\n"
           "Subcommands:\n"
           "  list              List all configured hooks\n"
           "  run <point> [<crate>]  Manually trigger a hook\n"
           "\n"
           "Lifecycle points: pre-build, post-build, pre-test, post-test\n");
}

/// Parse lifecycle name to enum. Returns -1 on invalid.
static int parse_lifecycle(const char* name) {
    if (strcmp(name, "pre-build") == 0) return HOOK_PRE_BUILD;
    if (strcmp(name, "post-build") == 0) return HOOK_POST_BUILD;
    if (strcmp(name, "pre-test") == 0) return HOOK_PRE_TEST;
    if (strcmp(name, "post-test") == 0) return HOOK_POST_TEST;
    return -1;
}

/// List all hooks configured in workspace and crates.
static int hook_list(const CdoOptions* opts) {
    (void)opts;

    char cwd[260];
#ifdef _WIN32
    if (_getcwd(cwd, sizeof(cwd)) == NULL) {
#else
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
#endif
        cdo_error("failed to determine current working directory");
        return 1;
    }

    Workspace ws = {0};
    if (workspace_load(cwd, &ws) != 0) {
        cdo_error("Failed to load workspace.");
        workspace_free(&ws);
        return 1;
    }

    int found = 0;

    // Workspace hooks
    for (int i = 0; i < HOOK_LIFECYCLE_COUNT; i++) {
        if (ws.ws_hooks.hooks[i].present) {
            if (!found) {
                printf("Workspace hooks:\n");
            }
            printf("  %-12s %s\n", hook_lifecycle_name((HookLifecycle)i), ws.ws_hooks.hooks[i].command);
            found++;
        }
    }

    // Crate hooks
    for (int c = 0; c < ws.crate_count; c++) {
        int crate_found = 0;
        for (int i = 0; i < HOOK_LIFECYCLE_COUNT; i++) {
            if (ws.crates[c].hooks.hooks[i].present) {
                if (!crate_found) {
                    printf("\nCrate '%s' hooks:\n", ws.crates[c].name);
                    crate_found = 1;
                }
                printf("  %-12s %s\n", hook_lifecycle_name((HookLifecycle)i), ws.crates[c].hooks.hooks[i].command);
                found++;
            }
        }
    }

    if (!found) {
        printf("No hooks configured.\n");
    }

    workspace_free(&ws);
    return 0;
}

/// Run a specific hook manually.
static int hook_run(const CdoOptions* opts) {
    // positional_args[0] = "run", [1] = <lifecycle>, [2] = [<crate>]
    if (opts->positional_count < 2) {
        cdo_error("Missing lifecycle point. Usage: cdo hook run <point> [<crate>]");
        return 1;
    }

    const char* point_str = opts->positional_args[1];
    int lifecycle = parse_lifecycle(point_str);
    if (lifecycle < 0) {
        cdo_error("Invalid lifecycle point: '%s'. Valid: pre-build, post-build, pre-test, post-test", point_str);
        return 1;
    }

    char cwd[260];
#ifdef _WIN32
    if (_getcwd(cwd, sizeof(cwd)) == NULL) {
#else
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
#endif
        cdo_error("failed to determine current working directory");
        return 1;
    }

    Workspace ws = {0};
    if (workspace_load(cwd, &ws) != 0) {
        cdo_error("Failed to load workspace.");
        workspace_free(&ws);
        return 1;
    }

    const HookDef* hook = NULL;
    HookEnv env = {0};
    env.ws_root = ws.root_path;
    env.profile = opts->profile ? opts->profile : "debug";
    env.build_dir = NULL; // Not readily available; hook can use CDO_WS_ROOT

    if (opts->positional_count >= 3) {
        // Crate-specific hook
        const char* crate_name = opts->positional_args[2];
        int found = 0;
        for (int c = 0; c < ws.crate_count; c++) {
            if (strcmp(ws.crates[c].name, crate_name) == 0) {
                hook = &ws.crates[c].hooks.hooks[lifecycle];
                env.crate_name = ws.crates[c].name;
                env.crate_path = ws.crates[c].path;
                env.crate_build_dir = NULL;
                found = 1;
                break;
            }
        }
        if (!found) {
            cdo_error("Crate '%s' not found.", crate_name);
            workspace_free(&ws);
            return 1;
        }
    } else {
        // Workspace hook
        hook = &ws.ws_hooks.hooks[lifecycle];
    }

    if (!hook->present) {
        const char* scope = (opts->positional_count >= 3) ? opts->positional_args[2] : "workspace";
        cdo_error("No %s hook configured for %s.", hook_lifecycle_name((HookLifecycle)lifecycle), scope);
        workspace_free(&ws);
        return 1;
    }

    cdo_info("Running %s hook: %s", hook_lifecycle_name((HookLifecycle)lifecycle), hook->command);
    int result = hook_execute(hook, &env);

    workspace_free(&ws);
    return result;
}

int cmd_hook(const CdoOptions* opts) {
    if (!opts) return 1;

    if (opts->positional_count < 1) {
        print_hook_usage();
        return 0;
    }

    const char* subcmd = opts->positional_args[0];

    if (strcmp(subcmd, "list") == 0) {
        return hook_list(opts);
    } else if (strcmp(subcmd, "run") == 0) {
        return hook_run(opts);
    } else if (strcmp(subcmd, "--help") == 0 || strcmp(subcmd, "-h") == 0) {
        print_hook_usage();
        return 0;
    } else {
        cdo_error("Unknown subcommand '%s'. Run 'cdo hook --help' for usage.", subcmd);
        return 1;
    }
}
