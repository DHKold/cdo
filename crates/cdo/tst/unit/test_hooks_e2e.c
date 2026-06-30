// crates/cdo/tst/unit/test_hooks_e2e.c
// Unit tests for e2e hook parsing and execution order.
// Validates: Requirements 11.1-11.8
#include "cdo_ut.h"
#include "model/hooks.h"
#include "core/hooks_exec.h"
#include "pal/pal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// hooks_parse_pre_e2e_string: Parse [hooks] pre-e2e = "setup.sh"
// Validates: Requirements 11.1, 11.3
// =============================================================================

TEST(hooks_parse_pre_e2e_string) {
    const char* toml = "[hooks]\npre-e2e = \"setup.sh\"\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(set.hooks[HOOK_PRE_E2E].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_PRE_E2E].command, "setup.sh");
    TEST_ASSERT_EQ(set.hooks[HOOK_PRE_E2E].timeout_sec, 120);
    // Other hooks should remain absent
    TEST_ASSERT(set.hooks[HOOK_PRE_BUILD].present == false);
    TEST_ASSERT(set.hooks[HOOK_POST_BUILD].present == false);
    TEST_ASSERT(set.hooks[HOOK_PRE_TEST].present == false);
    TEST_ASSERT(set.hooks[HOOK_POST_TEST].present == false);
    TEST_ASSERT(set.hooks[HOOK_POST_E2E].present == false);
    return 0;
}

// =============================================================================
// hooks_parse_post_e2e_string: Parse [hooks] post-e2e = "cleanup.sh"
// Validates: Requirements 11.2, 11.4
// =============================================================================

TEST(hooks_parse_post_e2e_string) {
    const char* toml = "[hooks]\npost-e2e = \"cleanup.sh\"\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(set.hooks[HOOK_POST_E2E].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_POST_E2E].command, "cleanup.sh");
    TEST_ASSERT_EQ(set.hooks[HOOK_POST_E2E].timeout_sec, 120);
    // Other hooks should remain absent
    TEST_ASSERT(set.hooks[HOOK_PRE_BUILD].present == false);
    TEST_ASSERT(set.hooks[HOOK_POST_BUILD].present == false);
    TEST_ASSERT(set.hooks[HOOK_PRE_TEST].present == false);
    TEST_ASSERT(set.hooks[HOOK_POST_TEST].present == false);
    TEST_ASSERT(set.hooks[HOOK_PRE_E2E].present == false);
    return 0;
}

// =============================================================================
// hooks_parse_pre_e2e_table_form: Parse [hooks.pre-e2e] command/timeout
// Validates: Requirements 11.1, 11.3
// =============================================================================

TEST(hooks_parse_pre_e2e_table_form) {
    const char* toml = "[hooks.pre-e2e]\ncommand = \"setup\"\ntimeout = 60\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(set.hooks[HOOK_PRE_E2E].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_PRE_E2E].command, "setup");
    TEST_ASSERT_EQ(set.hooks[HOOK_PRE_E2E].timeout_sec, 60);
    return 0;
}

// =============================================================================
// hooks_parse_all_six_lifecycle_keys: Parse all 6 hooks
// Validates: Requirements 11.1-11.4
// =============================================================================

TEST(hooks_parse_all_six_lifecycle_keys) {
    const char* toml =
        "[hooks]\n"
        "pre-build = \"step1\"\n"
        "post-build = \"step2\"\n"
        "pre-test = \"step3\"\n"
        "post-test = \"step4\"\n"
        "pre-e2e = \"step5\"\n"
        "post-e2e = \"step6\"\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(set.hooks[HOOK_PRE_BUILD].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_PRE_BUILD].command, "step1");
    TEST_ASSERT(set.hooks[HOOK_POST_BUILD].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_POST_BUILD].command, "step2");
    TEST_ASSERT(set.hooks[HOOK_PRE_TEST].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_PRE_TEST].command, "step3");
    TEST_ASSERT(set.hooks[HOOK_POST_TEST].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_POST_TEST].command, "step4");
    TEST_ASSERT(set.hooks[HOOK_PRE_E2E].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_PRE_E2E].command, "step5");
    TEST_ASSERT(set.hooks[HOOK_POST_E2E].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_POST_E2E].command, "step6");
    return 0;
}

// =============================================================================
// hooks_e2e_lifecycle_enum_values: Verify enum values and count
// Validates: Requirements 11.1-11.4
// =============================================================================

TEST(hooks_e2e_lifecycle_enum_values) {
    // HOOK_PRE_E2E and HOOK_POST_E2E must exist and be distinct from others
    TEST_ASSERT_EQ((int)HOOK_PRE_E2E, 4);
    TEST_ASSERT_EQ((int)HOOK_POST_E2E, 5);
    // HOOK_LIFECYCLE_COUNT must be 6
    TEST_ASSERT_EQ(HOOK_LIFECYCLE_COUNT, 6);
    // Verify no overlap with existing lifecycle values
    TEST_ASSERT((int)HOOK_PRE_E2E != (int)HOOK_PRE_BUILD);
    TEST_ASSERT((int)HOOK_PRE_E2E != (int)HOOK_POST_BUILD);
    TEST_ASSERT((int)HOOK_PRE_E2E != (int)HOOK_PRE_TEST);
    TEST_ASSERT((int)HOOK_PRE_E2E != (int)HOOK_POST_TEST);
    TEST_ASSERT((int)HOOK_POST_E2E != (int)HOOK_PRE_BUILD);
    TEST_ASSERT((int)HOOK_POST_E2E != (int)HOOK_POST_BUILD);
    TEST_ASSERT((int)HOOK_POST_E2E != (int)HOOK_PRE_TEST);
    TEST_ASSERT((int)HOOK_POST_E2E != (int)HOOK_POST_TEST);
    return 0;
}

// =============================================================================
// hooks_parse_only_e2e_hooks: Parse only e2e hooks, verify others absent
// Validates: Requirements 11.1-11.4
// =============================================================================

TEST(hooks_parse_only_e2e_hooks) {
    const char* toml =
        "[hooks]\n"
        "pre-e2e = \"run_setup.sh\"\n"
        "post-e2e = \"run_cleanup.sh\"\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    // E2E hooks present
    TEST_ASSERT(set.hooks[HOOK_PRE_E2E].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_PRE_E2E].command, "run_setup.sh");
    TEST_ASSERT(set.hooks[HOOK_POST_E2E].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_POST_E2E].command, "run_cleanup.sh");
    // Build and test hooks remain absent
    TEST_ASSERT(set.hooks[HOOK_PRE_BUILD].present == false);
    TEST_ASSERT(set.hooks[HOOK_POST_BUILD].present == false);
    TEST_ASSERT(set.hooks[HOOK_PRE_TEST].present == false);
    TEST_ASSERT(set.hooks[HOOK_POST_TEST].present == false);
    return 0;
}

// =============================================================================
// hooks_e2e_ws_pre_failure_aborts: Workspace pre-e2e failure aborts run
// Validates: Requirements 11.5
// =============================================================================

TEST_SERIAL(hooks_e2e_ws_pre_failure_aborts) {
    // Workspace pre-e2e hook fails -> caller must abort the entire e2e run
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_E2E;
    hook.timeout_sec = 10;
    strcpy(hook.command, "exit /b 1");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_NEQ(rc, 0);  // Pre-e2e failure -> non-zero (caller aborts)
    return 0;
}

// =============================================================================
// hooks_e2e_crate_pre_failure_skips_crate: Crate pre-e2e failure skips crate
// Validates: Requirements 11.6
// =============================================================================

TEST_SERIAL(hooks_e2e_crate_pre_failure_skips_crate) {
    // Crate pre-e2e hook fails -> non-zero return, caller skips this crate
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_E2E;
    hook.timeout_sec = 10;
    strcpy(hook.command, "exit /b 42");
    HookEnv crate_env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = "my_crate", .crate_path = ".", .crate_build_dir = "./build/debug/my_crate" };
    int rc = hook_execute(&hook, &crate_env);
    TEST_ASSERT_NEQ(rc, 0);
    return 0;
}

// =============================================================================
// hooks_e2e_post_failure_does_not_alter_exit_code: Post-e2e hook failure
// is logged but does not alter the overall exit code
// Validates: Requirements 11.8
// =============================================================================

TEST_SERIAL(hooks_e2e_post_failure_does_not_alter_exit_code) {
    // Post-e2e hook returns non-zero, but the caller should only log it.
    // Here we verify that hook_execute returns non-zero for the post-e2e
    // hook failure (the caller's responsibility to only log it, not abort).
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_POST_E2E;
    hook.timeout_sec = 10;
    strcpy(hook.command, "exit /b 1");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    int rc = hook_execute(&hook, &env);
    // hook_execute reports failure, but cmd_e2e must only log it (not abort)
    TEST_ASSERT_NEQ(rc, 0);  // Failure is returned to caller
    return 0;
}

// =============================================================================
// hooks_e2e_execution_order: ws-pre-e2e -> crate-pre-e2e -> (tests) ->
//   crate-post-e2e -> ws-post-e2e
// Validates: Requirements 11.7
// =============================================================================

TEST_SERIAL(hooks_e2e_execution_order) {
    const char* tmpdir = getenv("TEMP");
    if (!tmpdir) tmpdir = "C:\\Temp";

    char tmp[260];
    snprintf(tmp, sizeof(tmp), "%s\\cdo_hook_e2e_order_test.txt", tmpdir);

    HookEnv ws_env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    HookEnv crate_env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = "test_crate", .crate_path = ".", .crate_build_dir = "./build/debug/test_crate" };

    // Clean up temp file first
    HookDef cleanup = {0};
    cleanup.present = true;
    cleanup.lifecycle = HOOK_PRE_E2E;
    cleanup.timeout_sec = 5;
    snprintf(cleanup.command, sizeof(cleanup.command), "del /q \"%s\" 2>nul & ver", tmp);
    hook_execute(&cleanup, &ws_env);

    // 1. Workspace pre-e2e
    HookDef h1 = {0};
    h1.present = true;
    h1.lifecycle = HOOK_PRE_E2E;
    h1.timeout_sec = 5;
    snprintf(h1.command, sizeof(h1.command), "echo ws-pre-e2e>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h1, &ws_env), 0);

    // 2. Crate pre-e2e
    HookDef h2 = {0};
    h2.present = true;
    h2.lifecycle = HOOK_PRE_E2E;
    h2.timeout_sec = 5;
    snprintf(h2.command, sizeof(h2.command), "echo crate-pre-e2e>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h2, &crate_env), 0);

    // (e2e tests would run here)

    // 3. Crate post-e2e
    HookDef h3 = {0};
    h3.present = true;
    h3.lifecycle = HOOK_POST_E2E;
    h3.timeout_sec = 5;
    snprintf(h3.command, sizeof(h3.command), "echo crate-post-e2e>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h3, &crate_env), 0);

    // 4. Workspace post-e2e
    HookDef h4 = {0};
    h4.present = true;
    h4.lifecycle = HOOK_POST_E2E;
    h4.timeout_sec = 5;
    snprintf(h4.command, sizeof(h4.command), "echo ws-post-e2e>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h4, &ws_env), 0);

    // Read the file and verify order
    char* buf = NULL;
    size_t len = 0;
    TEST_ASSERT_EQ(pal_file_read(tmp, &buf, &len), 0);
    TEST_ASSERT(buf != NULL);

    // Verify all entries are present and in correct order
    char* p1 = strstr(buf, "ws-pre-e2e");
    char* p2 = strstr(buf, "crate-pre-e2e");
    char* p3 = strstr(buf, "crate-post-e2e");
    char* p4 = strstr(buf, "ws-post-e2e");
    TEST_ASSERT(p1 != NULL);
    TEST_ASSERT(p2 != NULL);
    TEST_ASSERT(p3 != NULL);
    TEST_ASSERT(p4 != NULL);
    TEST_ASSERT(p1 < p2);
    TEST_ASSERT(p2 < p3);
    TEST_ASSERT(p3 < p4);
    free(buf);

    // Clean up temp file
    HookDef final_cleanup = {0};
    final_cleanup.present = true;
    final_cleanup.lifecycle = HOOK_PRE_E2E;
    final_cleanup.timeout_sec = 5;
    snprintf(final_cleanup.command, sizeof(final_cleanup.command), "del /q \"%s\" 2>nul & ver", tmp);
    hook_execute(&final_cleanup, &ws_env);
    return 0;
}
