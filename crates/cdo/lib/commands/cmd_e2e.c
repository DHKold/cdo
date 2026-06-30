/**
 * cmd_e2e.c - End-to-end test command implementation.
 *
 * Discovers crates with MODULE_E2E, builds and runs their e2e executables,
 * parses JSON Lines protocol output, and reports aggregate results.
 */
#include "commands/cmd_e2e.h"
#include "commands/build_lock.h"
#include "commands/test_protocol.h"
#include "commands/test_renderer.h"
#include "cmd_build_internal.h"
#include "core/cli_arg_access.h"
#include "core/compiler.h"
#include "core/cache.h"
#include "core/hooks_exec.h"
#include "core/log.h"
#include "model/hooks.h"
#include "model/workspace.h"
#include "model/module.h"
#include "pal/pal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// cmd_e2e - Entry point for `cdo e2e`
// ---------------------------------------------------------------------------

int cmd_e2e(const CliParseResult* result, void* ctx) {
    (void)ctx;
    if (!result) {
        cdo_log_error("internal error: NULL parse result passed to e2e command");
        return 2;
    }

    // -----------------------------------------------------------------------
    // Extract CLI arguments
    // -----------------------------------------------------------------------
    const char* filter = cli_arg_get_str(result, "filter");
    bool list = cli_arg_get_bool(result, "list");
    bool release = cli_arg_get_bool(result, "release");
    const char* profile_str = cli_arg_get_str(result, "profile");
    int jobs = cli_arg_get_int(result, "jobs", 0);
    bool verbose = cli_arg_get_bool(result, "verbose");
    int timeout = cli_arg_get_int(result, "timeout", 0);
    bool keep_temps = cli_arg_get_bool(result, "keep-temps");
    const char* crate_name = (result->positional_count > 0) ? result->positional_values[0] : NULL;

    (void)verbose;

    // -----------------------------------------------------------------------
    // Load workspace
    // -----------------------------------------------------------------------
    Workspace ws = {0};
    int rc = workspace_load(".", &ws);
    if (rc != 0) {
        cdo_log_error("failed to load workspace");
        return 2;
    }

    rc = workspace_resolve(&ws, NULL, 0);
    if (rc != 0) {
        cdo_log_error("failed to resolve workspace dependencies");
        workspace_free(&ws);
        return 2;
    }

    // -----------------------------------------------------------------------
    // Discover e2e crates
    // -----------------------------------------------------------------------
    const Crate* e2e_crates[64];
    int e2e_count = 0;

    if (crate_name != NULL) {
        // Targeted: find the specific crate
        bool found = false;
        for (int i = 0; i < ws.crate_count; i++) {
            if (strcmp(ws.crates[i].name, crate_name) == 0) {
                found = true;
                if (!ws.crates[i].modules[MODULE_E2E].present) {
                    cdo_log_error("crate '%s' does not have an e2e module", crate_name);
                    workspace_free(&ws);
                    return 2;
                }
                e2e_crates[e2e_count++] = &ws.crates[i];
                break;
            }
        }
        if (!found) {
            cdo_log_error("crate '%s' not found in workspace", crate_name);
            workspace_free(&ws);
            return 2;
        }
    } else {
        // All crates: iterate in workspace member order, collect those with MODULE_E2E
        for (int i = 0; i < ws.crate_count; i++) {
            if (ws.crates[i].modules[MODULE_E2E].present) {
                if (e2e_count < 64) {
                    e2e_crates[e2e_count++] = &ws.crates[i];
                }
            }
        }
        if (e2e_count == 0) {
            cdo_log_error("no crates with e2e module found in workspace");
            workspace_free(&ws);
            return 2;
        }
    }

    cdo_log_info("found %d crate(s) with e2e module", e2e_count);

    // -----------------------------------------------------------------------
    // Determine effective profile
    // -----------------------------------------------------------------------
    const char* profile = resolve_profile_raw(release, profile_str);

    // -----------------------------------------------------------------------
    // Detect compiler
    // -----------------------------------------------------------------------
    CompilerInfo compiler = {0};
    rc = compiler_detect(&compiler);
    if (rc != 0) {
        cdo_log_error("failed to detect compiler");
        workspace_free(&ws);
        return 2;
    }
    cdo_log_debug("detected compiler: %s (%s)", compiler.path, compiler.version);

    // -----------------------------------------------------------------------
    // Load build profile
    // -----------------------------------------------------------------------
    BuildProfile build_prof = {0};
    rc = build_profile_load(ws.root_path, profile, &build_prof);
    if (rc != 0) {
        cdo_log_warn("failed to load build profile '%s', using defaults", profile);
    }

    // -----------------------------------------------------------------------
    // Resolve parallelism
    // -----------------------------------------------------------------------
    int jobs_val = resolve_jobs_raw(jobs);

    // -----------------------------------------------------------------------
    // Initialize cache
    // -----------------------------------------------------------------------
    CacheConfig cache_cfg = ws.cache_config;
    CacheStats cache_stats = {0};
    if (cache_cfg.enabled && strcmp(cache_cfg.backend, "builtin") == 0) {
        if (cache_init(&cache_cfg, ws.root_path) != 0) {
            cdo_log_warn("cache initialization failed, continuing without cache");
            cache_cfg.enabled = false;
        }
    }

    // -----------------------------------------------------------------------
    // Acquire build lock
    // -----------------------------------------------------------------------
    BuildLock* lock = NULL;
    int lock_rc = build_lock_acquire(ws.root_path, 30, &lock);
    if (lock_rc != 0) {
        if (lock_rc == PAL_ERR_TIMEOUT) {
            cdo_log_error("could not acquire build lock within 30 seconds");
        } else {
            cdo_log_error("failed to acquire build lock");
        }
        build_profile_free(&build_prof);
        workspace_free(&ws);
        return 2;
    }

    // Set re-entrancy flag so nested build calls skip locking
#ifdef _WIN32
    _putenv_s("CDO_BUILD_LOCK_HELD", "1");
#else
    setenv("CDO_BUILD_LOCK_HELD", "1", 1);
#endif

    // -----------------------------------------------------------------------
    // Workspace pre-e2e hook
    // -----------------------------------------------------------------------
    {
        char ws_build_dir[260];
        snprintf(ws_build_dir, sizeof(ws_build_dir), "%s/build/%s", ws.root_path, profile);

        HookEnv ws_hook_env = {0};
        ws_hook_env.ws_root = ws.root_path;
        ws_hook_env.profile = profile;
        ws_hook_env.build_dir = ws_build_dir;

        if (hook_execute(&ws.ws_hooks.hooks[HOOK_PRE_E2E], &ws_hook_env) != 0) {
            cdo_log_error("workspace pre-e2e hook failed, aborting e2e run");
#ifdef _WIN32
            _putenv_s("CDO_BUILD_LOCK_HELD", "");
#else
            unsetenv("CDO_BUILD_LOCK_HELD");
#endif
            build_lock_release(lock);
            build_profile_free(&build_prof);
            workspace_free(&ws);
            return 2;
        }
    }

    // -----------------------------------------------------------------------
    // Build + Execution loop
    // -----------------------------------------------------------------------
    int total_tests = 0;
    int total_passed = 0;
    int total_failed = 0;
    int total_skipped = 0;
    double total_duration_ms = 0.0;
    TestProtocolMsg all_failures[256];
    int all_failures_count = 0;
    int crates_built = 0;
    int crates_build_failed = 0;
    int crates_infra_error = 0;

    bool use_color = cdo_log_use_color();

    for (int i = 0; i < e2e_count; i++) {
        const Crate* crate = e2e_crates[i];

        // --- Crate pre-e2e hook ---
        {
            char crate_abs[260];
            pal_path_join(crate_abs, sizeof(crate_abs), ws.root_path, crate->path);
            char crate_build[260];
            snprintf(crate_build, sizeof(crate_build), "%s/build/%s/%s", ws.root_path, profile, crate->name);
            char build_dir_buf[260];
            snprintf(build_dir_buf, sizeof(build_dir_buf), "%s/build/%s", ws.root_path, profile);

            HookEnv crate_hook_env = {0};
            crate_hook_env.ws_root = ws.root_path;
            crate_hook_env.profile = profile;
            crate_hook_env.build_dir = build_dir_buf;
            crate_hook_env.crate_name = crate->name;
            crate_hook_env.crate_path = crate_abs;
            crate_hook_env.crate_build_dir = crate_build;

            if (hook_execute(&crate->hooks.hooks[HOOK_PRE_E2E], &crate_hook_env) != 0) {
                cdo_log_warn("crate '%s' pre-e2e hook failed, skipping crate", crate->name);
                continue;
            }
        }

        // --- Build e2e module ---
        int completed = 0;
        rc = build_e2e_module(&ws, (Crate*)crate, &compiler, profile, &build_prof, jobs_val, &cache_cfg, &cache_stats, false, NULL, &completed);
        if (rc != 0) {
            cdo_log_error("e2e build failed for crate '%s'", crate->name);
            crates_build_failed++;
            continue;
        }
        crates_built++;

        // --- Determine executable path ---
        const char* exe_path = crate->modules[MODULE_E2E].artifact_path;
        if (!exe_path || exe_path[0] == '\0') {
            cdo_log_error("e2e artifact path not set for crate '%s'", crate->name);
            crates_build_failed++;
            continue;
        }

        // Verify the executable exists
        if (pal_path_exists(exe_path) != 0) {
            cdo_log_error("e2e executable not found: %s", exe_path);
            crates_build_failed++;
            continue;
        }

        // --- Build spawn args ---
        const char* spawn_args[16];
        int spawn_arg_count = 0;

        if (filter && filter[0]) {
            spawn_args[spawn_arg_count++] = "--filter";
            spawn_args[spawn_arg_count++] = filter;
        }

        if (jobs > 0) {
            static char jobs_buf[16];
            snprintf(jobs_buf, sizeof(jobs_buf), "%d", jobs);
            spawn_args[spawn_arg_count++] = "--jobs";
            spawn_args[spawn_arg_count++] = jobs_buf;
        }

        if (list) {
            spawn_args[spawn_arg_count++] = "--list";
        }

        if (timeout > 0) {
            static char timeout_buf[16];
            snprintf(timeout_buf, sizeof(timeout_buf), "%d", timeout);
            spawn_args[spawn_arg_count++] = "--timeout";
            spawn_args[spawn_arg_count++] = timeout_buf;
        }

        if (keep_temps) {
            spawn_args[spawn_arg_count++] = "--keep-temps";
        }

        // --- Spawn the e2e executable ---
        PalSpawnOpts spawn_opts = {0};
        spawn_opts.program = exe_path;
        spawn_opts.args = spawn_arg_count > 0 ? spawn_args : NULL;
        spawn_opts.arg_count = spawn_arg_count;
        spawn_opts.timeout_ms = -1; // no timeout at runner level; per-test timeout handled by executable

        // In --list mode, pass stdout through directly. Otherwise capture for protocol parsing.
        if (list) {
            spawn_opts.capture_output = false;
        } else {
            spawn_opts.capture_output = true;
        }

        PalSpawnResult spawn_result = {0};

        cdo_log_info("running e2e tests for '%s'", crate->name);

        rc = pal_spawn(&spawn_opts, &spawn_result);
        if (rc != 0) {
            cdo_log_error("failed to execute e2e binary: %s", exe_path);
            crates_infra_error++;
            pal_spawn_result_free(&spawn_result);
            continue;
        }

        // In --list mode, just check exit code and move on
        if (list) {
            if (spawn_result.exit_code != 0) {
                cdo_log_error("list failed for '%s' (exit code %d)", crate->name, spawn_result.exit_code);
                crates_infra_error++;
            }
            pal_spawn_result_free(&spawn_result);
            continue;
        }

        // --- Parse captured stdout line-by-line using the test protocol ---
        bool received_suite_end = false;
        int crate_total = 0;
        int crate_completed = 0;

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
                                cdo_log_debug("e2e suite_start for '%s': %d tests", crate->name, crate_total);
                                break;

                            case TEST_MSG_TEST_START:
                                break;

                            case TEST_MSG_TEST_END:
                                crate_completed++;
                                if (crate_total > 0) {
                                    test_renderer_progress(crate_completed, crate_total, use_color);
                                }
                                test_renderer_result(&msg, use_color);
                                if (msg.status == TEST_STATUS_FAIL && all_failures_count < 256) {
                                    all_failures[all_failures_count++] = msg;
                                }
                                break;

                            case TEST_MSG_SUITE_END:
                                received_suite_end = true;
                                total_tests += msg.total;
                                total_passed += msg.passed;
                                total_failed += msg.failed;
                                total_skipped += msg.skipped;
                                total_duration_ms += msg.duration_ms;
                                cdo_log_debug("e2e suite_end for '%s': %d total, %d passed, %d failed, %d skipped, %.2fms",
                                              crate->name, msg.total, msg.passed, msg.failed, msg.skipped, msg.duration_ms);
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

        // --- Print stderr if present ---
        if (spawn_result.stderr_buf && spawn_result.stderr_buf[0]) {
            fprintf(stderr, "%s", spawn_result.stderr_buf);
        }

        // Handle executable crash: non-zero exit without suite_end
        if (spawn_result.exit_code != 0 && !received_suite_end) {
            cdo_log_error("e2e binary crashed for '%s' (exit code %d)", crate->name, spawn_result.exit_code);
            crates_infra_error++;
        } else if (spawn_result.exit_code != 0) {
            cdo_log_error("e2e tests failed for '%s' (exit code %d)", crate->name, spawn_result.exit_code);
        } else {
            cdo_log_info("e2e tests passed for '%s'", crate->name);
        }

        pal_spawn_result_free(&spawn_result);

        // --- Crate post-e2e hook ---
        {
            char crate_abs[260];
            pal_path_join(crate_abs, sizeof(crate_abs), ws.root_path, crate->path);
            char crate_build[260];
            snprintf(crate_build, sizeof(crate_build), "%s/build/%s/%s", ws.root_path, profile, crate->name);
            char build_dir_buf[260];
            snprintf(build_dir_buf, sizeof(build_dir_buf), "%s/build/%s", ws.root_path, profile);

            HookEnv crate_hook_env = {0};
            crate_hook_env.ws_root = ws.root_path;
            crate_hook_env.profile = profile;
            crate_hook_env.build_dir = build_dir_buf;
            crate_hook_env.crate_name = crate->name;
            crate_hook_env.crate_path = crate_abs;
            crate_hook_env.crate_build_dir = crate_build;

            if (hook_execute(&crate->hooks.hooks[HOOK_POST_E2E], &crate_hook_env) != 0) {
                cdo_log_warn("crate '%s' post-e2e hook failed (test results unaffected)", crate->name);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Workspace post-e2e hook
    // -----------------------------------------------------------------------
    {
        char ws_build_dir[260];
        snprintf(ws_build_dir, sizeof(ws_build_dir), "%s/build/%s", ws.root_path, profile);

        HookEnv ws_hook_env = {0};
        ws_hook_env.ws_root = ws.root_path;
        ws_hook_env.profile = profile;
        ws_hook_env.build_dir = ws_build_dir;

        if (hook_execute(&ws.ws_hooks.hooks[HOOK_POST_E2E], &ws_hook_env) != 0) {
            cdo_log_warn("workspace post-e2e hook failed (test results unaffected)");
        }
    }

    // -----------------------------------------------------------------------
    // Release build lock
    // -----------------------------------------------------------------------
#ifdef _WIN32
    _putenv_s("CDO_BUILD_LOCK_HELD", "");
#else
    unsetenv("CDO_BUILD_LOCK_HELD");
#endif
    build_lock_release(lock);

    // -----------------------------------------------------------------------
    // Summary display
    // -----------------------------------------------------------------------
    build_profile_free(&build_prof);

    if (crates_build_failed > 0) {
        cdo_log_warn("%d crate(s) failed to build", crates_build_failed);
    }

    // Render consolidated failures section
    if (!list && all_failures_count > 0) {
        test_renderer_failures(all_failures, all_failures_count, use_color);
    }

    // Render summary line (no coverage for e2e, pass -1.0)
    if (!list) {
        test_renderer_summary(total_tests, total_passed, total_failed, total_skipped, total_duration_ms, -1.0, use_color);
    }

    workspace_free(&ws);

    // -----------------------------------------------------------------------
    // Exit code logic
    // -----------------------------------------------------------------------
    // 1 = at least one test failed
    // 2 = infrastructure error only (build failure, binary crash, no test failures)
    // 0 = all tests passed
    if (total_failed > 0) return 1;
    if (crates_infra_error > 0 || crates_build_failed > 0) return 2;
    return 0;
}
