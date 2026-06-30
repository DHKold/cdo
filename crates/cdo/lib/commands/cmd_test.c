#include "commands/cmd_test.h"
#include "commands/cmd_build.h"
#include "commands/build_lock.h"
#include "commands/test_protocol.h"
#include "commands/test_renderer.h"
#include "commands/test_coverage.h"
#include "core/handler_ctx.h"
#include "model/hooks.h"
#include "core/hooks_exec.h"
#include "model/module.h"
#include "core/log.h"
#include "model/workspace.h"
#include "pal/pal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Argument extraction helpers (match main_new.cpp pattern)
// ---------------------------------------------------------------------------

/// Find a named argument in the parse result. Returns NULL if not found.
static const CliArgValue* test_find_arg(const CliParseResult* result, const char* name) {
    for (int i = 0; i < result->arg_value_count; i++) {
        if (result->arg_values[i].name && strcmp(result->arg_values[i].name, name) == 0) {
            return &result->arg_values[i];
        }
    }
    return NULL;
}

/// Get a bool argument value. Returns false if not present.
static bool test_get_arg_bool(const CliParseResult* result, const char* name) {
    const CliArgValue* v = test_find_arg(result, name);
    return (v && v->present && v->type == CLI_ARG_BOOL) ? v->value.bool_val : false;
}

/// Get a string argument value. Returns NULL if not present.
static const char* test_get_arg_str(const CliParseResult* result, const char* name) {
    const CliArgValue* v = test_find_arg(result, name);
    return (v && v->present && (v->type == CLI_ARG_STRING || v->type == CLI_ARG_ENUM)) ? v->value.str_val : NULL;
}

/// Get an int argument value. Returns default_val if not present.
static int test_get_arg_int(const CliParseResult* result, const char* name, int default_val) {
    const CliArgValue* v = test_find_arg(result, name);
    return (v && v->present && v->type == CLI_ARG_INT) ? v->value.int_val : default_val;
}

// ---------------------------------------------------------------------------
// New CLI framework handler
// ---------------------------------------------------------------------------

int cmd_test(const CliParseResult* result, void* ctx) {
    (void)ctx; // CdoHandlerCtx* â€” not used directly yet (output goes through global)

    // Extract args from CliParseResult
    bool coverage = test_get_arg_bool(result, "coverage");
    bool list = test_get_arg_bool(result, "list");
    const char* filter = test_get_arg_str(result, "filter");
    bool release = test_get_arg_bool(result, "release");
    const char* profile_str = test_get_arg_str(result, "profile");
    int jobs = test_get_arg_int(result, "jobs", 0);
    int lock_timeout = test_get_arg_int(result, "lock-timeout", -1);
    bool verbose = test_get_arg_bool(result, "verbose");

    // Load workspace
    Workspace ws = {0};
    int rc = workspace_load(".", &ws);
    if (rc != 0) {
        cdo_log_error("Failed to load workspace");
        return 1;
    }

    // Collect crates that have a tst/ module (filtered by positional args if given)
    const Crate* test_crates[64];
    int test_count = 0;

    for (int i = 0; i < ws.crate_count; i++) {
        if (!ws.crates[i].modules[MODULE_TST].present) {
            continue;
        }

        if (result->positional_count > 0) {
            bool matched = false;
            for (int j = 0; j < result->positional_count; j++) {
                if (strcmp(ws.crates[i].name, result->positional_values[j]) == 0) {
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
        if (result->positional_count > 0) {
            cdo_log_error("No matching crates with tst/ module found");
        } else {
            cdo_log_error("No crates with tst/ module found in workspace");
        }
        workspace_free(&ws);
        return 1;
    }

    // Acquire build lock for the test session
    int effective_lock_timeout = lock_timeout;
    if (effective_lock_timeout < 0) {
        effective_lock_timeout = 30;  // Default 30s when unset (sentinel -1)
    }
    BuildLock* lock = NULL;
    int lock_rc = build_lock_acquire(ws.root_path, effective_lock_timeout, &lock);
    if (lock_rc != 0) {
        if (lock_rc == PAL_ERR_TIMEOUT) {
            cdo_log_error("could not acquire build lock within %d seconds", effective_lock_timeout);
        } else {
            cdo_log_error("failed to acquire build lock");
        }
        workspace_free(&ws);
        return 1;
    }

    // Set re-entrancy flag so nested cmd_build calls skip locking
#ifdef _WIN32
    _putenv_s("CDO_BUILD_LOCK_HELD", "1");
#else
    setenv("CDO_BUILD_LOCK_HELD", "1", 1);
#endif

    // --- Workspace pre-test hook ---
    {
        const char* prof = release ? "release" :
                           (profile_str && profile_str[0]) ? profile_str : "debug";
        HookEnv ws_hook_env = {0};
        ws_hook_env.ws_root = ws.root_path;
        ws_hook_env.profile = prof;
        char ws_build_dir[260];
        snprintf(ws_build_dir, sizeof(ws_build_dir), "%s/build/%s", ws.root_path, prof);
        ws_hook_env.build_dir = ws_build_dir;

        if (hook_execute(&ws.ws_hooks.hooks[HOOK_PRE_TEST], &ws_hook_env) != 0) {
            cdo_log_error("Workspace pre-test hook failed, aborting test run");
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

    bool use_color = cdo_log_use_color();

    for (int i = 0; i < test_count; i++) {
        const Crate* crate = test_crates[i];

        // Build the crate using cmd_build directly with a CliParseResult
        const char* build_args[1];
        build_args[0] = crate->name;

        CliArgValue build_arg_buf[6];
        int build_arg_count = 0;

        build_arg_buf[build_arg_count].name = "release";
        build_arg_buf[build_arg_count].type = CLI_ARG_BOOL;
        build_arg_buf[build_arg_count].value.bool_val = release;
        build_arg_buf[build_arg_count].present = release;
        build_arg_count++;

        build_arg_buf[build_arg_count].name = "profile";
        build_arg_buf[build_arg_count].type = CLI_ARG_STRING;
        build_arg_buf[build_arg_count].value.str_val = profile_str;
        build_arg_buf[build_arg_count].present = (profile_str != NULL && profile_str[0] != '\0');
        build_arg_count++;

        build_arg_buf[build_arg_count].name = "jobs";
        build_arg_buf[build_arg_count].type = CLI_ARG_INT;
        build_arg_buf[build_arg_count].value.int_val = jobs;
        build_arg_buf[build_arg_count].present = (jobs > 0);
        build_arg_count++;

        build_arg_buf[build_arg_count].name = "verbose";
        build_arg_buf[build_arg_count].type = CLI_ARG_BOOL;
        build_arg_buf[build_arg_count].value.bool_val = verbose;
        build_arg_buf[build_arg_count].present = verbose;
        build_arg_count++;

        build_arg_buf[build_arg_count].name = "coverage";
        build_arg_buf[build_arg_count].type = CLI_ARG_BOOL;
        build_arg_buf[build_arg_count].value.bool_val = coverage;
        build_arg_buf[build_arg_count].present = coverage;
        build_arg_count++;

        build_arg_buf[build_arg_count].name = "lock-timeout";
        build_arg_buf[build_arg_count].type = CLI_ARG_INT;
        build_arg_buf[build_arg_count].value.int_val = -1;
        build_arg_buf[build_arg_count].present = true;
        build_arg_count++;

        CliParseResult build_result = {0};
        build_result.arg_values = build_arg_buf;
        build_result.arg_value_count = build_arg_count;
        build_result.positional_values = build_args;
        build_result.positional_count = 1;
        build_result.rest_args = NULL;
        build_result.rest_count = 0;

        rc = cmd_build(&build_result, NULL);
        if (rc != 0) {
            cdo_log_error("Build failed for crate '%s'", crate->name);
            crates_infra_error++;
            continue;
        }

        // Construct path to the test executable
        const char* prof = release ? "release" :
                           (profile_str && profile_str[0]) ? profile_str : "debug";

        char exe_path[260];
        char artifact_name[128];
        rc = module_artifact_name(crate->name, MODULE_TST, artifact_name, sizeof(artifact_name));
        if (rc != 0) {
            cdo_log_error("Failed to compute test artifact name for crate '%s'", crate->name);
            crates_infra_error++;
            continue;
        }

        char tmp1[260], tmp2[260];
        pal_path_join(tmp1, sizeof(tmp1), "build", prof);
        pal_path_join(tmp2, sizeof(tmp2), tmp1, crate->name);
        pal_path_join(exe_path, sizeof(exe_path), tmp2, artifact_name);

        // Check that the executable exists
        if (pal_path_exists(exe_path) != 0) {
            cdo_log_error("Test executable not found: %s", exe_path);
            crates_infra_error++;
            continue;
        }

        // Build dynamic args array for the test binary.
        // We forward: --filter <pattern>, --list, --jobs <N>
        // Note: --coverage is NOT forwarded â€” it's used by the runner for build flags/gcov.
        const char* binary_args[16];
        int binary_arg_count = 0;

        if (filter && filter[0]) {
            binary_args[binary_arg_count++] = "--filter";
            binary_args[binary_arg_count++] = filter;
        }

        if (list) {
            binary_args[binary_arg_count++] = "--list";
        }

        if (jobs > 1) {
            // Format --jobs N as two separate args
            static char jobs_buf[16];
            snprintf(jobs_buf, sizeof(jobs_buf), "%d", jobs);
            binary_args[binary_arg_count++] = "--jobs";
            binary_args[binary_arg_count++] = jobs_buf;
        }

        // Also append any extra args passed after -- (rest_args)
        for (int j = 0; j < result->rest_count && binary_arg_count < 16; j++) {
            binary_args[binary_arg_count++] = result->rest_args[j];
        }

        // Run the test executable
        PalSpawnOpts spawn_opts = {0};
        spawn_opts.program = exe_path;
        spawn_opts.args = binary_arg_count > 0 ? binary_args : NULL;
        spawn_opts.arg_count = binary_arg_count;
        spawn_opts.timeout_ms = -1; // no timeout for tests

        // In --list mode, pass stdout through directly. Otherwise capture for parsing.
        if (list) {
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
            snprintf(crate_build, sizeof(crate_build), "%s/build/%s/%s", ws.root_path, prof, crate->name);

            HookEnv crate_env = {0};
            crate_env.ws_root = ws.root_path;
            crate_env.profile = prof;
            char build_dir_buf[260];
            snprintf(build_dir_buf, sizeof(build_dir_buf), "%s/build/%s", ws.root_path, prof);
            crate_env.build_dir = build_dir_buf;
            crate_env.crate_name = crate->name;
            crate_env.crate_path = crate_abs;
            crate_env.crate_build_dir = crate_build;

            if (hook_execute(&crate->hooks.hooks[HOOK_PRE_TEST], &crate_env) != 0) {
                cdo_log_error("Crate '%s' pre-test hook failed, skipping tests", crate->name);
                crates_infra_error++;
                continue;
            }
        }

        if (list) {
            cdo_log_info("Tests in '%s':", crate->name);
        } else {
            cdo_log_info("Running tests for '%s'", crate->name);
        }

        rc = pal_spawn(&spawn_opts, &spawn_result);
        if (rc != 0) {
            cdo_log_error("Failed to execute test binary: %s", exe_path);
            crates_infra_error++;
            pal_spawn_result_free(&spawn_result);
            continue;
        }

        // In --list mode, just check exit code and move on (output already passed through)
        if (list) {
            if (spawn_result.exit_code != 0) {
                cdo_log_error("List failed for '%s' (exit code %d)", crate->name, spawn_result.exit_code);
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
            char* buf = spawn_result.stdout_buf;
            char* line_start = buf;

            while (*line_start != '\0') {
                char* line_end = line_start;
                while (*line_end != '\0' && *line_end != '\n') {
                    line_end++;
                }

                char saved = *line_end;
                *line_end = '\0';

                // Strip trailing \r if present (Windows line endings)
                size_t line_len = (size_t)(line_end - line_start);
                if (line_len > 0 && line_start[line_len - 1] == '\r') {
                    line_start[line_len - 1] = '\0';
                }

                if (line_start[0] != '\0') {
                    TestProtocolMsg msg = {0};
                    int parse_rc = test_protocol_parse_line(line_start, &msg);

                    if (parse_rc == 0) {
                        switch (msg.msg_type) {
                            case TEST_MSG_SUITE_START:
                                crate_total = msg.total;
                                break;

                            case TEST_MSG_TEST_START:
                                break;

                            case TEST_MSG_TEST_END:
                                completed++;
                                if (crate_total > 0) {
                                    test_renderer_progress(completed, crate_total, use_color);
                                }
                                test_renderer_result(&msg, use_color);
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
                                cdo_log_error("[%s] %s", crate->name, msg.message);
                                break;

                            case TEST_MSG_UNKNOWN:
                            default:
                                break;
                        }
                    }
                }

                if (saved == '\n') {
                    line_start = line_end + 1;
                } else {
                    break;
                }
            }
        }

        // Handle binary crash: non-zero exit without suite_end
        if (spawn_result.exit_code != 0 && !received_suite_end) {
            cdo_log_error("Test binary crashed for '%s' (exit code %d)", crate->name, spawn_result.exit_code);
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
            snprintf(crate_build, sizeof(crate_build), "%s/build/%s/%s", ws.root_path, prof, crate->name);

            HookEnv crate_env = {0};
            crate_env.ws_root = ws.root_path;
            crate_env.profile = prof;
            char build_dir_buf[260];
            snprintf(build_dir_buf, sizeof(build_dir_buf), "%s/build/%s", ws.root_path, prof);
            crate_env.build_dir = build_dir_buf;
            crate_env.crate_name = crate->name;
            crate_env.crate_path = crate_abs;
            crate_env.crate_build_dir = crate_build;

            if (hook_execute(&crate->hooks.hooks[HOOK_POST_TEST], &crate_env) != 0) {
                cdo_log_warn("Crate '%s' post-test hook failed (test results unaffected)", crate->name);
            }
        }
    }

    // --- Workspace post-test hook ---
    {
        const char* prof = release ? "release" :
                           (profile_str && profile_str[0]) ? profile_str : "debug";
        HookEnv ws_hook_env = {0};
        ws_hook_env.ws_root = ws.root_path;
        ws_hook_env.profile = prof;
        char ws_build_dir[260];
        snprintf(ws_build_dir, sizeof(ws_build_dir), "%s/build/%s", ws.root_path, prof);
        ws_hook_env.build_dir = ws_build_dir;

        if (hook_execute(&ws.ws_hooks.hooks[HOOK_POST_TEST], &ws_hook_env) != 0) {
            cdo_log_warn("Workspace post-test hook failed");
        }
    }

    // Final summary (skip for --list mode)
    if (!list) {
        // Render consolidated failures section
        if (all_failures_count > 0) {
            test_renderer_failures(all_failures, all_failures_count, use_color);
        }

        // Coverage: run gcov and report if --coverage was active
        double coverage_pct = -1.0;
        if (coverage) {
            FileCoverage cov_files[COVERAGE_MAX_FILES];
            int cov_count = 0;

            const char* prof = release ? "release" :
                               (profile_str && profile_str[0]) ? profile_str : "debug";

            for (int i = 0; i < test_count; i++) {
                char crate_build_dir[260];
                char tmp1[260], tmp2[260];
                pal_path_join(tmp1, sizeof(tmp1), "build", prof);
                pal_path_join(tmp2, sizeof(tmp2), tmp1, test_crates[i]->name);
                pal_path_join(crate_build_dir, sizeof(crate_build_dir), ws.root_path, tmp2);

                int cov_result = coverage_run_gcov_filtered(crate_build_dir, ws.root_path,
                                                  cov_files + cov_count,
                                                  COVERAGE_MAX_FILES - cov_count);
                if (cov_result == -2) {
                    // gcov not found â€” release lock and exit with code 2
#ifdef _WIN32
                    _putenv_s("CDO_BUILD_LOCK_HELD", "");
#else
                    unsetenv("CDO_BUILD_LOCK_HELD");
#endif
                    build_lock_release(lock);
                    workspace_free(&ws);
                    return 2;
                }
                if (cov_result > 0) {
                    cov_count += cov_result;
                }
            }

            if (cov_count > 0) {
                coverage_pct = coverage_aggregate(cov_files, cov_count);
                coverage_display(cov_files, cov_count, coverage_pct, use_color);
            } else {
                coverage_pct = 0.0;
                cdo_log_info("Coverage: 0%% (no instrumented files found)");
            }
        }

        // Render summary line
        test_renderer_summary(total_total, total_passed, total_failed, total_skipped,
                              total_duration_ms, coverage_pct, use_color);
    }

    // Clear re-entrancy flag and release build lock
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

// ---------------------------------------------------------------------------
// End of cmd_test.c
