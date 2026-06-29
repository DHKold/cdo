#include "commands/cmd_test.h"
#include "commands/cmd_build.h"
#include "commands/build_lock.h"
#include "commands/test_protocol.h"
#include "commands/test_renderer.h"
#include "commands/test_coverage.h"
#include "model/hooks.h"
#include "core/hooks_exec.h"
#include "model/module.h"
#include "core/output.h"
#include "model/workspace.h"
#include "pal/pal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int cmd_test(const CdoOptions* opts) {
    if (opts->help) {
        cdo_info("Usage: cdo test [OPTIONS] [crate_name...]");
        cdo_info("  Build and run tests for crates that have a tst/ module.");
        cdo_info("  If no name is given, all crates with tst/ are tested.");
        cdo_info("");
        cdo_info("Options:");
        cdo_info("  --filter <pattern>  Run only tests matching pattern (substring or glob)");
        cdo_info("  --list              List available test names without running them");
        cdo_info("  --jobs <N>          Number of parallel test execution jobs");
        cdo_info("  --coverage          Build with coverage instrumentation and report");
        return 0;
    }

    // Load workspace
    Workspace ws = {0};
    int rc = workspace_load(".", &ws);
    if (rc != 0) {
        cdo_error("Failed to load workspace");
        return 1;
    }

    // Collect crates that have a tst/ module (filtered by positional args if given)
    const Crate* test_crates[64];
    int test_count = 0;

    for (int i = 0; i < ws.crate_count; i++) {
        if (!ws.crates[i].modules[MODULE_TST].present) {
            continue;
        }

        if (opts->positional_count > 0) {
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
            cdo_error("No matching crates with tst/ module found");
        } else {
            cdo_error("No crates with tst/ module found in workspace");
        }
        workspace_free(&ws);
        return 1;
    }

    // Acquire build lock for the test session (Req 7.2, 7.4, 7.5, 10.1, 10.3)
    int lock_timeout = opts->lock_timeout;
    if (lock_timeout < 0) {
        lock_timeout = 30;  // Default 30s when unset (sentinel -1)
    }
    BuildLock* lock = NULL;
    int lock_rc = build_lock_acquire(ws.root_path, lock_timeout, &lock);
    if (lock_rc != 0) {
        if (lock_rc == PAL_ERR_TIMEOUT) {
            cdo_error("could not acquire build lock within %d seconds", lock_timeout);
        } else {
            cdo_error("failed to acquire build lock");
        }
        workspace_free(&ws);
        return 1;
    }

    // Set re-entrancy flag so nested cmd_build calls skip locking (Req 8.1, 8.2)
#ifdef _WIN32
    _putenv_s("CDO_BUILD_LOCK_HELD", "1");
#else
    setenv("CDO_BUILD_LOCK_HELD", "1", 1);
#endif

    // --- Workspace pre-test hook ---
    {
        const char* profile = opts->release ? "release" :
                              (opts->profile && opts->profile[0]) ? opts->profile : "debug";
        HookEnv ws_hook_env = {0};
        ws_hook_env.ws_root = ws.root_path;
        ws_hook_env.profile = profile;
        char ws_build_dir[260];
        snprintf(ws_build_dir, sizeof(ws_build_dir), "%s/build/%s", ws.root_path, profile);
        ws_hook_env.build_dir = ws_build_dir;

        if (hook_execute(&ws.ws_hooks.hooks[HOOK_PRE_TEST], &ws_hook_env) != 0) {
            cdo_error("Workspace pre-test hook failed, aborting test run");
            build_lock_release(lock);
            workspace_free(&ws);
            return 1;
        }
    }

    // Aggregate totals across all crates
    int total_total = 0, total_passed = 0, total_failed = 0, total_skipped = 0;
    double total_duration_ms = 0.0;
    TestProtocolMsg all_failures[256];
    int all_failures_count = 0;
    int crates_passed = 0;
    int crates_failed = 0;       // test failures (exit 1)
    int crates_infra_error = 0;  // build/infrastructure errors (exit 2)

    bool use_color = output_use_color();

    for (int i = 0; i < test_count; i++) {
        const Crate* crate = test_crates[i];

        // Build the crate (builds all modules including tst/)
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
            cdo_error("Build failed for crate '%s'", crate->name);
            crates_infra_error++;
            continue;
        }

        // Construct path to the test executable:
        // build/<profile>/<crate_name>/<crate_name>_test.exe (Windows)
        // build/<profile>/<crate_name>/<crate_name>_test (Unix)
        const char* profile = opts->release ? "release" :
                              (opts->profile && opts->profile[0]) ? opts->profile : "debug";

        char exe_path[260];
        char artifact_name[128];
        rc = module_artifact_name(crate->name, MODULE_TST, artifact_name, sizeof(artifact_name));
        if (rc != 0) {
            cdo_error("Failed to compute test artifact name for crate '%s'", crate->name);
            crates_infra_error++;
            continue;
        }

        char tmp1[260], tmp2[260];
        pal_path_join(tmp1, sizeof(tmp1), "build", profile);
        pal_path_join(tmp2, sizeof(tmp2), tmp1, crate->name);
        pal_path_join(exe_path, sizeof(exe_path), tmp2, artifact_name);

        // Check that the executable exists
        if (pal_path_exists(exe_path) != 0) {
            cdo_error("Test executable not found: %s", exe_path);
            crates_infra_error++;
            continue;
        }

        // Build dynamic args array for the test binary.
        // We forward: --filter <pattern>, --list, --jobs <N>
        // Note: --coverage is NOT forwarded — it's used by the runner for build flags/gcov.
        const char* binary_args[16];
        int binary_arg_count = 0;

        if (opts->filter && opts->filter[0]) {
            binary_args[binary_arg_count++] = "--filter";
            binary_args[binary_arg_count++] = opts->filter;
        }

        if (opts->list) {
            binary_args[binary_arg_count++] = "--list";
        }

        if (opts->jobs > 1) {
            // Format --jobs N as two separate args
            static char jobs_buf[16];
            snprintf(jobs_buf, sizeof(jobs_buf), "%d", opts->jobs);
            binary_args[binary_arg_count++] = "--jobs";
            binary_args[binary_arg_count++] = jobs_buf;
        }

        // Also append any extra args passed after -- (opts->argv_rest)
        for (int j = 0; j < opts->argc_rest && binary_arg_count < 16; j++) {
            binary_args[binary_arg_count++] = opts->argv_rest[j];
        }

        // Run the test executable
        PalSpawnOpts spawn_opts = {0};
        spawn_opts.program = exe_path;
        spawn_opts.args = binary_arg_count > 0 ? binary_args : NULL;
        spawn_opts.arg_count = binary_arg_count;
        spawn_opts.timeout_ms = -1; // no timeout for tests

        // In --list mode, pass stdout through directly. Otherwise capture for parsing.
        if (opts->list) {
            spawn_opts.capture_output = false;
        } else {
            spawn_opts.capture_output = true;
        }

        PalSpawnResult spawn_result = {0};

        // --- Crate pre-test hook ---
        {
            char crate_abs[260];
            pal_path_join(crate_abs, sizeof(crate_abs), ws.root_path, crate->path);
            char crate_build[260];
            snprintf(crate_build, sizeof(crate_build), "%s/build/%s/%s", ws.root_path, profile, crate->name);

            HookEnv crate_env = {0};
            crate_env.ws_root = ws.root_path;
            crate_env.profile = profile;
            char build_dir_buf[260];
            snprintf(build_dir_buf, sizeof(build_dir_buf), "%s/build/%s", ws.root_path, profile);
            crate_env.build_dir = build_dir_buf;
            crate_env.crate_name = crate->name;
            crate_env.crate_path = crate_abs;
            crate_env.crate_build_dir = crate_build;

            if (hook_execute(&crate->hooks.hooks[HOOK_PRE_TEST], &crate_env) != 0) {
                cdo_error("Crate '%s' pre-test hook failed, skipping tests", crate->name);
                crates_infra_error++;
                continue;
            }
        }

        if (opts->list) {
            cdo_info("Tests in '%s':", crate->name);
        } else {
            cdo_info("Running tests for '%s'", crate->name);
        }

        rc = pal_spawn(&spawn_opts, &spawn_result);
        if (rc != 0) {
            cdo_error("Failed to execute test binary: %s", exe_path);
            crates_infra_error++;
            pal_spawn_result_free(&spawn_result);
            continue;
        }

        // In --list mode, just check exit code and move on (output already passed through)
        if (opts->list) {
            if (spawn_result.exit_code != 0) {
                cdo_error("List failed for '%s' (exit code %d)", crate->name, spawn_result.exit_code);
                crates_infra_error++;
            } else {
                crates_passed++;
            }
            pal_spawn_result_free(&spawn_result);
            continue;
        }

        // Parse captured stdout line-by-line using the test protocol
        bool received_suite_end = false;
        int crate_total = 0;
        int completed = 0;

        if (spawn_result.stdout_buf != NULL) {
            // Parse line by line (split on '\n')
            char* buf = spawn_result.stdout_buf;
            char* line_start = buf;

            while (*line_start != '\0') {
                // Find end of line
                char* line_end = line_start;
                while (*line_end != '\0' && *line_end != '\n') {
                    line_end++;
                }

                // Null-terminate this line (save char to restore if needed)
                char saved = *line_end;
                *line_end = '\0';

                // Strip trailing \r if present (Windows line endings)
                size_t line_len = (size_t)(line_end - line_start);
                if (line_len > 0 && line_start[line_len - 1] == '\r') {
                    line_start[line_len - 1] = '\0';
                }

                // Skip empty lines
                if (line_start[0] != '\0') {
                    TestProtocolMsg msg = {0};
                    int parse_rc = test_protocol_parse_line(line_start, &msg);

                    if (parse_rc == 0) {
                        switch (msg.msg_type) {
                            case TEST_MSG_SUITE_START:
                                crate_total = msg.total;
                                break;

                            case TEST_MSG_TEST_START:
                                // Optionally update progress display
                                break;

                            case TEST_MSG_TEST_END:
                                completed++;
                                // Show progress indicator
                                if (crate_total > 0) {
                                    test_renderer_progress(completed, crate_total, use_color);
                                }
                                // Render the test result (pass/fail/skip)
                                test_renderer_result(&msg, use_color);
                                // Store failures for consolidated output
                                if (msg.status == TEST_STATUS_FAIL && all_failures_count < 256) {
                                    all_failures[all_failures_count++] = msg;
                                }
                                break;

                            case TEST_MSG_SUITE_END:
                                received_suite_end = true;
                                total_total += msg.total;
                                total_passed += msg.passed;
                                total_failed += msg.failed;
                                total_skipped += msg.skipped;
                                total_duration_ms += msg.duration_ms;
                                break;

                            case TEST_MSG_ERROR:
                                cdo_error("[%s] %s", crate->name, msg.message);
                                break;

                            case TEST_MSG_UNKNOWN:
                            default:
                                // Skip unrecognized lines
                                break;
                        }
                    }
                    // If parse fails, skip the line (malformed JSON)
                }

                // Move to next line
                if (saved == '\n') {
                    line_start = line_end + 1;
                } else {
                    break; // end of buffer
                }
            }
        }

        // Handle binary crash: non-zero exit without suite_end
        if (spawn_result.exit_code != 0 && !received_suite_end) {
            cdo_error("Test binary crashed for '%s' (exit code %d)", crate->name, spawn_result.exit_code);
            crates_infra_error++;
        } else if (spawn_result.exit_code != 0) {
            crates_failed++;
        } else {
            crates_passed++;
        }

        pal_spawn_result_free(&spawn_result);

        // --- Crate post-test hook ---
        {
            char crate_abs[260];
            pal_path_join(crate_abs, sizeof(crate_abs), ws.root_path, crate->path);
            char crate_build[260];
            snprintf(crate_build, sizeof(crate_build), "%s/build/%s/%s", ws.root_path, profile, crate->name);

            HookEnv crate_env = {0};
            crate_env.ws_root = ws.root_path;
            crate_env.profile = profile;
            char build_dir_buf[260];
            snprintf(build_dir_buf, sizeof(build_dir_buf), "%s/build/%s", ws.root_path, profile);
            crate_env.build_dir = build_dir_buf;
            crate_env.crate_name = crate->name;
            crate_env.crate_path = crate_abs;
            crate_env.crate_build_dir = crate_build;

            if (hook_execute(&crate->hooks.hooks[HOOK_POST_TEST], &crate_env) != 0) {
                cdo_warn("Crate '%s' post-test hook failed (test results unaffected)", crate->name);
            }
        }
    }

    // --- Workspace post-test hook ---
    {
        const char* profile = opts->release ? "release" :
                              (opts->profile && opts->profile[0]) ? opts->profile : "debug";
        HookEnv ws_hook_env = {0};
        ws_hook_env.ws_root = ws.root_path;
        ws_hook_env.profile = profile;
        char ws_build_dir[260];
        snprintf(ws_build_dir, sizeof(ws_build_dir), "%s/build/%s", ws.root_path, profile);
        ws_hook_env.build_dir = ws_build_dir;

        if (hook_execute(&ws.ws_hooks.hooks[HOOK_POST_TEST], &ws_hook_env) != 0) {
            cdo_warn("Workspace post-test hook failed");
        }
    }

    // Final summary (skip for --list mode)
    if (!opts->list) {
        // Render consolidated failures section
        if (all_failures_count > 0) {
            test_renderer_failures(all_failures, all_failures_count, use_color);
        }

        // Coverage: run gcov and report if --coverage was active
        double coverage_pct = -1.0;
        if (opts->coverage) {
            FileCoverage cov_files[COVERAGE_MAX_FILES];
            int cov_count = 0;

            // Run gcov on each crate's build directory
            const char* profile = opts->release ? "release" :
                                  (opts->profile && opts->profile[0]) ? opts->profile : "debug";

            for (int i = 0; i < test_count; i++) {
                char crate_build_dir[260];
                char tmp1[260], tmp2[260];
                pal_path_join(tmp1, sizeof(tmp1), "build", profile);
                pal_path_join(tmp2, sizeof(tmp2), tmp1, test_crates[i]->name);
                pal_path_join(crate_build_dir, sizeof(crate_build_dir), ws.root_path, tmp2);

                int result = coverage_run_gcov_filtered(crate_build_dir, ws.root_path,
                                              cov_files + cov_count,
                                              COVERAGE_MAX_FILES - cov_count);
                if (result == -2) {
                    // gcov not found — release lock and exit with code 2
#ifdef _WIN32
                    _putenv_s("CDO_BUILD_LOCK_HELD", "");
#else
                    unsetenv("CDO_BUILD_LOCK_HELD");
#endif
                    build_lock_release(lock);
                    workspace_free(&ws);
                    return 2;
                }
                if (result > 0) {
                    cov_count += result;
                }
            }

            if (cov_count > 0) {
                coverage_pct = coverage_aggregate(cov_files, cov_count);
                coverage_display(cov_files, cov_count, coverage_pct, use_color);
            } else {
                coverage_pct = 0.0;
                cdo_info("Coverage: 0%% (no instrumented files found)");
            }
        }

        // Render summary line
        test_renderer_summary(total_total, total_passed, total_failed, total_skipped,
                              total_duration_ms, coverage_pct, use_color);
    }

    // Clear re-entrancy flag and release build lock (all exit paths)
#ifdef _WIN32
    _putenv_s("CDO_BUILD_LOCK_HELD", "");
#else
    unsetenv("CDO_BUILD_LOCK_HELD");
#endif
    build_lock_release(lock);

    workspace_free(&ws);

    // Exit code logic:
    //   1 = at least one test failed
    //   2 = infrastructure error only (build failure, binary crash, no test failures)
    //   0 = all tests passed
    if (crates_failed > 0 || total_failed > 0) {
        return 1;
    }
    if (crates_infra_error > 0) {
        return 2;
    }
    return 0;
}
