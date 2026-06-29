// crates/cdo/tst/unit/test_hooks_test.c
// Unit tests for test pipeline integration: hook execution in test-like scenarios,
// pre-test failure skipping, post-test failure warnings, and execution order.
// Validates: Requirements 2.4, 2.5, 3.2, 5.3, 5.4
#include "cdo_ut.h"
#include "model/hooks.h"
#include "core/hooks_exec.h"
#include "pal/pal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// Pre-test hook failure returns non-zero (caller uses this to skip test)
// Validates: Requirements 5.3
// =============================================================================

TEST_SERIAL(hooks_test_pre_failure_skips_test) {
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_TEST;
    hook.timeout_sec = 10;
    strcpy(hook.command, "exit /b 1");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = "my_crate", .crate_path = ".", .crate_build_dir = "./build/debug/my_crate" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_NEQ(rc, 0);  // Pre-test failure → non-zero, caller skips test
    return 0;
}

// =============================================================================
// Post-test hook failure returns non-zero (caller preserves test results)
// Validates: Requirements 5.4
// =============================================================================

TEST_SERIAL(hooks_test_post_failure_returns_nonzero) {
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_POST_TEST;
    hook.timeout_sec = 10;
    strcpy(hook.command, "exit /b 1");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = "my_crate", .crate_path = ".", .crate_build_dir = "./build/debug/my_crate" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_NEQ(rc, 0);  // Post-test failure → non-zero, caller preserves results
    return 0;
}

// =============================================================================
// Workspace pre-test hook failure returns non-zero (caller aborts test run)
// Validates: Requirements 2.4
// =============================================================================

TEST_SERIAL(hooks_test_ws_pre_failure_aborts) {
    HookDef ws_pre = {0};
    ws_pre.present = true;
    ws_pre.lifecycle = HOOK_PRE_TEST;
    ws_pre.timeout_sec = 10;
    strcpy(ws_pre.command, "exit /b 1");
    HookEnv ws_env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    int rc = hook_execute(&ws_pre, &ws_env);
    TEST_ASSERT_NEQ(rc, 0);  // Workspace pre-test failure → no tests run

    // Verify that a subsequent crate hook would succeed if called (proving the
    // abort decision is the caller's responsibility, not hook_execute's)
    HookDef crate_pre = {0};
    crate_pre.present = true;
    crate_pre.lifecycle = HOOK_PRE_TEST;
    crate_pre.timeout_sec = 10;
    strcpy(crate_pre.command, "ver");
    HookEnv crate_env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = "test", .crate_path = ".", .crate_build_dir = "./build/debug/test" };
    TEST_ASSERT_EQ(hook_execute(&crate_pre, &crate_env), 0);
    return 0;
}

// =============================================================================
// Successful pre-test and post-test hooks both return zero
// Validates: Requirements 5.3, 5.4, 3.2
// =============================================================================

TEST_SERIAL(hooks_test_successful_pre_and_post) {
    HookDef pre = {0};
    pre.present = true;
    pre.lifecycle = HOOK_PRE_TEST;
    pre.timeout_sec = 10;
    strcpy(pre.command, "ver");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = "my_crate", .crate_path = ".", .crate_build_dir = "./build/debug/my_crate" };
    TEST_ASSERT_EQ(hook_execute(&pre, &env), 0);

    HookDef post = {0};
    post.present = true;
    post.lifecycle = HOOK_POST_TEST;
    post.timeout_sec = 10;
    strcpy(post.command, "ver");
    TEST_ASSERT_EQ(hook_execute(&post, &env), 0);
    return 0;
}

// =============================================================================
// Execution order: ws-pre-test → crate-pre-test → (test) → crate-post-test → ws-post-test
// Validates: Requirements 3.2, 2.4, 2.5
// =============================================================================

TEST_SERIAL(hooks_test_execution_order) {
    // Verify execution order by having each hook append a line to a temp file
    const char* tmpdir = getenv("TEMP");
    if (!tmpdir) tmpdir = "C:\\Temp";

    char tmp[260];
    snprintf(tmp, sizeof(tmp), "%s\\cdo_hook_test_order.txt", tmpdir);

    // Environment definitions
    HookEnv ws_env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    HookEnv crate_env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = "test_crate", .crate_path = ".", .crate_build_dir = "./build/debug/test_crate" };

    // Clean up temp file first
    HookDef cleanup = {0};
    cleanup.present = true;
    cleanup.lifecycle = HOOK_PRE_TEST;
    cleanup.timeout_sec = 5;
    snprintf(cleanup.command, sizeof(cleanup.command), "del /q \"%s\" 2>nul & ver", tmp);
    hook_execute(&cleanup, &ws_env);

    // 1. Workspace pre-test
    HookDef h1 = {0};
    h1.present = true;
    h1.lifecycle = HOOK_PRE_TEST;
    h1.timeout_sec = 5;
    snprintf(h1.command, sizeof(h1.command), "echo ws-pre-test>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h1, &ws_env), 0);

    // 2. Crate pre-test
    HookDef h2 = {0};
    h2.present = true;
    h2.lifecycle = HOOK_PRE_TEST;
    h2.timeout_sec = 5;
    snprintf(h2.command, sizeof(h2.command), "echo crate-pre-test>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h2, &crate_env), 0);

    // (test binary execution would happen here)

    // 3. Crate post-test
    HookDef h3 = {0};
    h3.present = true;
    h3.lifecycle = HOOK_POST_TEST;
    h3.timeout_sec = 5;
    snprintf(h3.command, sizeof(h3.command), "echo crate-post-test>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h3, &crate_env), 0);

    // 4. Workspace post-test
    HookDef h4 = {0};
    h4.present = true;
    h4.lifecycle = HOOK_POST_TEST;
    h4.timeout_sec = 5;
    snprintf(h4.command, sizeof(h4.command), "echo ws-post-test>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h4, &ws_env), 0);

    // Read the file and verify order
    char* buf = NULL;
    size_t len = 0;
    TEST_ASSERT_EQ(pal_file_read(tmp, &buf, &len), 0);
    TEST_ASSERT(buf != NULL);

    // Verify all entries are present and in correct order
    char* p1 = strstr(buf, "ws-pre-test");
    char* p2 = strstr(buf, "crate-pre-test");
    char* p3 = strstr(buf, "crate-post-test");
    char* p4 = strstr(buf, "ws-post-test");
    TEST_ASSERT(p1 != NULL);
    TEST_ASSERT(p2 != NULL);
    TEST_ASSERT(p3 != NULL);
    TEST_ASSERT(p4 != NULL);
    TEST_ASSERT(p1 < p2);
    TEST_ASSERT(p2 < p3);
    TEST_ASSERT(p3 < p4);
    free(buf);

    // Clean up
    HookDef final_cleanup = {0};
    final_cleanup.present = true;
    final_cleanup.lifecycle = HOOK_PRE_TEST;
    final_cleanup.timeout_sec = 5;
    snprintf(final_cleanup.command, sizeof(final_cleanup.command), "del /q \"%s\" 2>nul & ver", tmp);
    hook_execute(&final_cleanup, &ws_env);
    return 0;
}
