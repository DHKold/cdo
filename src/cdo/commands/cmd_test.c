#include "commands/cmd_test.h"
#include "commands/cmd_build.h"
#include "core/output.h"
#include "core/workspace.h"
#include "pal/pal.h"
#include <string.h>

#define BUILD_DIR "build"

int cmd_test(const CdoOptions* opts) {
    if (opts->help) {
        cdo_info("Usage: cdo test [crate_name...]");
        cdo_info("  Build and run test crates.");
        cdo_info("  If no name is given, all test crates are built and run.");
        return 0;
    }

    // Load workspace
    Workspace ws;
    memset(&ws, 0, sizeof(ws));

    int rc = workspace_load(".", &ws);
    if (rc != 0) {
        cdo_error("Failed to load workspace");
        return 1;
    }

    // Collect test crates (filtered by positional args if given)
    const Crate* test_crates[64];
    int test_count = 0;

    for (int i = 0; i < ws.crate_count; i++) {
        if (ws.crates[i].type != CRATE_TEST) {
            continue;
        }

        if (opts->positional_count > 0) {
            // Only include test crates whose name matches one of the args
            bool matched = false;
            for (int j = 0; j < opts->positional_count; j++) {
                if (strcmp(ws.crates[i].name, opts->positional_args[j]) == 0) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                continue;
            }
        }

        if (test_count < 64) {
            test_crates[test_count++] = &ws.crates[i];
        }
    }

    if (test_count == 0) {
        if (opts->positional_count > 0) {
            cdo_error("No matching test crates found");
        } else {
            cdo_error("No test crates found in workspace");
        }
        workspace_free(&ws);
        return 1;
    }

    int passed = 0;
    int failed = 0;

    for (int i = 0; i < test_count; i++) {
        const Crate* crate = test_crates[i];

        // Build the test crate via cmd_build
        const char* build_args[1];
        build_args[0] = crate->name;

        CdoOptions build_opts = *opts;
        build_opts.command = CDO_CMD_BUILD;
        build_opts.positional_args = build_args;
        build_opts.positional_count = 1;
        build_opts.argv_rest = NULL;
        build_opts.argc_rest = 0;

        rc = cmd_build(&build_opts);
        if (rc != 0) {
            cdo_error("Build failed for test crate '%s'", crate->name);
            failed++;
            continue;
        }

        // Construct path to the built test executable:
        // build/<profile>/<crate_name>/<crate_name>[.exe on Windows]
        const char* profile = opts->release ? "release" :
                              (opts->profile && opts->profile[0]) ? opts->profile : "debug";

        char exe_path[1024];
        char profile_dir[512];
        char crate_dir[512];

        rc = pal_path_join(profile_dir, sizeof(profile_dir), BUILD_DIR, profile);
        if (rc != 0) {
            cdo_error("Path too long for profile");
            failed++;
            continue;
        }

        rc = pal_path_join(crate_dir, sizeof(crate_dir), profile_dir, crate->name);
        if (rc != 0) {
            cdo_error("Path too long for test crate '%s'", crate->name);
            failed++;
            continue;
        }

#ifdef _WIN32
        char exe_name[128];
        snprintf(exe_name, sizeof(exe_name), "%s.exe", crate->name);
#else
        const char* exe_name = crate->name;
#endif

        rc = pal_path_join(exe_path, sizeof(exe_path), crate_dir, exe_name);
        if (rc != 0) {
            cdo_error("Path too long for test executable '%s'", crate->name);
            failed++;
            continue;
        }

        // Check that the executable exists
        if (pal_path_exists(exe_path) != 1) {
            cdo_error("Test executable not found: %s", exe_path);
            failed++;
            continue;
        }

        // Spawn the test executable
        PalSpawnOpts spawn_opts;
        memset(&spawn_opts, 0, sizeof(spawn_opts));
        spawn_opts.program = exe_path;
        spawn_opts.args = NULL;
        spawn_opts.arg_count = 0;
        spawn_opts.capture_output = false;

        PalSpawnResult spawn_result;
        memset(&spawn_result, 0, sizeof(spawn_result));

        cdo_info("Running test '%s'", crate->name);

        rc = pal_spawn(&spawn_opts, &spawn_result);
        if (rc != 0) {
            cdo_error("Failed to execute test '%s'", exe_path);
            failed++;
            pal_spawn_result_free(&spawn_result);
            continue;
        }

        if (spawn_result.exit_code != 0) {
            cdo_error("Test '%s' failed (exit code %d)", crate->name, spawn_result.exit_code);
            failed++;
        } else {
            cdo_info("Test '%s' passed", crate->name);
            passed++;
        }

        pal_spawn_result_free(&spawn_result);
    }

    // Print summary
    cdo_info("Tests: %d passed, %d failed", passed, failed);

    workspace_free(&ws);
    return (failed > 0) ? 1 : 0;
}
