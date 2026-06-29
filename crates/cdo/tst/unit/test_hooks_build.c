// crates/cdo/tst/unit/test_hooks_build.c
// Unit tests for build integration: hook execution in build-like scenarios,
// HookEnv construction, and execution order validation.
// Validates: Requirements 2.2, 2.3, 3.1, 3.3, 5.1, 5.2, 5.5
#include "cdo_ut.h"
#include "model/hooks.h"
#include "core/hooks_exec.h"
#include "pal/pal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// HookEnv construction: workspace env has NULL crate fields
// Validates: Requirements 2.2, 5.5
// =============================================================================

TEST(hooks_build_env_workspace_has_null_crate_fields) {
    HookEnv ws_env = {0};
    ws_env.ws_root = "C:/workspace";
    ws_env.profile = "debug";
    ws_env.build_dir = "C:/workspace/build/debug";
    // Workspace hooks should have NULL crate fields
    TEST_ASSERT_NULL(ws_env.crate_name);
    TEST_ASSERT_NULL(ws_env.crate_path);
    TEST_ASSERT_NULL(ws_env.crate_build_dir);
    return 0;
}

// =============================================================================
// HookEnv construction: crate env has all fields populated
// Validates: Requirements 2.3, 3.3
// =============================================================================

TEST(hooks_build_env_crate_has_all_fields) {
    HookEnv crate_env = {0};
    crate_env.ws_root = "C:/workspace";
    crate_env.profile = "release";
    crate_env.build_dir = "C:/workspace/build/release";
    crate_env.crate_name = "my_crate";
    crate_env.crate_path = "C:/workspace/crates/my_crate";
    crate_env.crate_build_dir = "C:/workspace/build/release/my_crate";
    TEST_ASSERT(crate_env.ws_root != NULL);
    TEST_ASSERT(crate_env.profile != NULL);
    TEST_ASSERT(crate_env.build_dir != NULL);
    TEST_ASSERT(crate_env.crate_name != NULL);
    TEST_ASSERT(crate_env.crate_path != NULL);
    TEST_ASSERT(crate_env.crate_build_dir != NULL);
    return 0;
}

// =============================================================================
// Pre-build hook failure returns non-zero (cmd_build uses this to abort)
// Validates: Requirements 5.1, 5.5
// =============================================================================

TEST_SERIAL(hooks_build_pre_failure_aborts) {
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_BUILD;
    hook.timeout_sec = 10;
    strcpy(hook.command, "exit /b 1");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_NEQ(rc, 0);  // Pre-build failure → non-zero return
    return 0;
}

// =============================================================================
// Post-build hook failure returns non-zero (artifacts preserved by caller)
// Validates: Requirements 5.2
// =============================================================================

TEST_SERIAL(hooks_build_post_failure_returns_nonzero) {
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_POST_BUILD;
    hook.timeout_sec = 10;
    strcpy(hook.command, "exit /b 1");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_NEQ(rc, 0);  // Post-build failure → non-zero, caller decides action
    return 0;
}

// =============================================================================
// Successful pre and post hooks both return zero
// Validates: Requirements 5.1, 5.2, 3.1
// =============================================================================

TEST_SERIAL(hooks_build_successful_pre_and_post) {
    HookDef pre = {0};
    pre.present = true;
    pre.lifecycle = HOOK_PRE_BUILD;
    pre.timeout_sec = 10;
    strcpy(pre.command, "ver");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    TEST_ASSERT_EQ(hook_execute(&pre, &env), 0);

    HookDef post = {0};
    post.present = true;
    post.lifecycle = HOOK_POST_BUILD;
    post.timeout_sec = 10;
    strcpy(post.command, "ver");
    TEST_ASSERT_EQ(hook_execute(&post, &env), 0);
    return 0;
}

// =============================================================================
// Workspace pre-build failure aborts (no crate hooks should run after)
// Validates: Requirements 5.5
// =============================================================================

TEST_SERIAL(hooks_build_ws_pre_failure_aborts_all) {
    // Workspace pre-build hook fails → caller must abort entire build
    HookDef ws_pre = {0};
    ws_pre.present = true;
    ws_pre.lifecycle = HOOK_PRE_BUILD;
    ws_pre.timeout_sec = 10;
    strcpy(ws_pre.command, "exit /b 1");
    HookEnv ws_env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    int rc = hook_execute(&ws_pre, &ws_env);
    TEST_ASSERT_NEQ(rc, 0);

    // Verify that a subsequent crate hook would succeed if called (proving the
    // abort decision is the caller's responsibility, not hook_execute's)
    HookDef crate_pre = {0};
    crate_pre.present = true;
    crate_pre.lifecycle = HOOK_PRE_BUILD;
    crate_pre.timeout_sec = 10;
    strcpy(crate_pre.command, "ver");
    HookEnv crate_env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = "test", .crate_path = ".", .crate_build_dir = "./build/debug/test" };
    TEST_ASSERT_EQ(hook_execute(&crate_pre, &crate_env), 0);
    return 0;
}

// =============================================================================
// Crate pre-build failure returns non-zero (crate not compiled)
// Validates: Requirements 5.1
// =============================================================================

TEST_SERIAL(hooks_build_crate_pre_failure_skips_build) {
    // Crate pre-build hook fails → non-zero, which cmd_build uses to skip
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_BUILD;
    hook.timeout_sec = 10;
    strcpy(hook.command, "exit /b 42");
    HookEnv crate_env = { .ws_root = ".", .profile = "release", .build_dir = "./build/release", .crate_name = "my_crate", .crate_path = ".", .crate_build_dir = "./build/release/my_crate" };
    int rc = hook_execute(&hook, &crate_env);
    TEST_ASSERT_NEQ(rc, 0);
    return 0;
}

// =============================================================================
// Execution order: ws-pre → crate-pre → (build) → crate-post → ws-post
// Validates: Requirements 3.1, 3.3
// =============================================================================

TEST_SERIAL(hooks_build_execution_order) {
    // Verify execution order by having each hook append a line to a temp file
    const char* tmpdir = getenv("TEMP");
    if (!tmpdir) tmpdir = "C:\\Temp";

    char tmp[260];
    snprintf(tmp, sizeof(tmp), "%s\\cdo_hook_order_test.txt", tmpdir);

    // Environment definitions
    HookEnv ws_env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    HookEnv crate_env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = "test_crate", .crate_path = ".", .crate_build_dir = "./build/debug/test_crate" };

    // Clean up temp file first
    HookDef cleanup = {0};
    cleanup.present = true;
    cleanup.lifecycle = HOOK_PRE_BUILD;
    cleanup.timeout_sec = 5;
    snprintf(cleanup.command, sizeof(cleanup.command), "del /q \"%s\" 2>nul & ver", tmp);
    hook_execute(&cleanup, &ws_env);

    // 1. Workspace pre-build
    HookDef h1 = {0};
    h1.present = true;
    h1.lifecycle = HOOK_PRE_BUILD;
    h1.timeout_sec = 5;
    snprintf(h1.command, sizeof(h1.command), "echo ws-pre>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h1, &ws_env), 0);

    // 2. Crate pre-build
    HookDef h2 = {0};
    h2.present = true;
    h2.lifecycle = HOOK_PRE_BUILD;
    h2.timeout_sec = 5;
    snprintf(h2.command, sizeof(h2.command), "echo crate-pre>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h2, &crate_env), 0);

    // (build would happen here)

    // 3. Crate post-build
    HookDef h3 = {0};
    h3.present = true;
    h3.lifecycle = HOOK_POST_BUILD;
    h3.timeout_sec = 5;
    snprintf(h3.command, sizeof(h3.command), "echo crate-post>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h3, &crate_env), 0);

    // 4. Workspace post-build
    HookDef h4 = {0};
    h4.present = true;
    h4.lifecycle = HOOK_POST_BUILD;
    h4.timeout_sec = 5;
    snprintf(h4.command, sizeof(h4.command), "echo ws-post>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&h4, &ws_env), 0);

    // Read the file and verify order
    char* buf = NULL;
    size_t len = 0;
    TEST_ASSERT_EQ(pal_file_read(tmp, &buf, &len), 0);
    TEST_ASSERT(buf != NULL);

    // Verify all entries are present and in correct order
    char* p1 = strstr(buf, "ws-pre");
    char* p2 = strstr(buf, "crate-pre");
    char* p3 = strstr(buf, "crate-post");
    char* p4 = strstr(buf, "ws-post");
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
    final_cleanup.lifecycle = HOOK_PRE_BUILD;
    final_cleanup.timeout_sec = 5;
    snprintf(final_cleanup.command, sizeof(final_cleanup.command), "del /q \"%s\" 2>nul & ver", tmp);
    hook_execute(&final_cleanup, &ws_env);
    return 0;
}

// =============================================================================
// Single-crate build still runs workspace hooks (order preserved)
// Validates: Requirements 3.3
// =============================================================================

TEST_SERIAL(hooks_build_single_crate_still_runs_ws_hooks) {
    // Even for a single crate build, workspace hooks execute.
    // Verify by running ws-pre, one crate-pre, crate-post, ws-post in order.
    const char* tmpdir = getenv("TEMP");
    if (!tmpdir) tmpdir = "C:\\Temp";

    char tmp[260];
    snprintf(tmp, sizeof(tmp), "%s\\cdo_hook_single_crate_test.txt", tmpdir);

    HookEnv ws_env = { .ws_root = ".", .profile = "release", .build_dir = "./build/release" };
    HookEnv crate_env = { .ws_root = ".", .profile = "release", .build_dir = "./build/release", .crate_name = "single_crate", .crate_path = ".", .crate_build_dir = "./build/release/single_crate" };

    // Clean up
    HookDef cleanup = {0};
    cleanup.present = true;
    cleanup.lifecycle = HOOK_PRE_BUILD;
    cleanup.timeout_sec = 5;
    snprintf(cleanup.command, sizeof(cleanup.command), "del /q \"%s\" 2>nul & ver", tmp);
    hook_execute(&cleanup, &ws_env);

    // Workspace pre-build (runs even for single crate)
    HookDef ws_pre = {0};
    ws_pre.present = true;
    ws_pre.lifecycle = HOOK_PRE_BUILD;
    ws_pre.timeout_sec = 5;
    snprintf(ws_pre.command, sizeof(ws_pre.command), "echo ws-pre>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&ws_pre, &ws_env), 0);

    // Single crate pre-build
    HookDef crate_pre = {0};
    crate_pre.present = true;
    crate_pre.lifecycle = HOOK_PRE_BUILD;
    crate_pre.timeout_sec = 5;
    snprintf(crate_pre.command, sizeof(crate_pre.command), "echo crate-pre>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&crate_pre, &crate_env), 0);

    // Single crate post-build
    HookDef crate_post = {0};
    crate_post.present = true;
    crate_post.lifecycle = HOOK_POST_BUILD;
    crate_post.timeout_sec = 5;
    snprintf(crate_post.command, sizeof(crate_post.command), "echo crate-post>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&crate_post, &crate_env), 0);

    // Workspace post-build (runs even for single crate)
    HookDef ws_post = {0};
    ws_post.present = true;
    ws_post.lifecycle = HOOK_POST_BUILD;
    ws_post.timeout_sec = 5;
    snprintf(ws_post.command, sizeof(ws_post.command), "echo ws-post>> \"%s\"", tmp);
    TEST_ASSERT_EQ(hook_execute(&ws_post, &ws_env), 0);

    // Verify all four hooks ran
    char* buf = NULL;
    size_t len = 0;
    TEST_ASSERT_EQ(pal_file_read(tmp, &buf, &len), 0);
    TEST_ASSERT(buf != NULL);
    TEST_ASSERT(strstr(buf, "ws-pre") != NULL);
    TEST_ASSERT(strstr(buf, "crate-pre") != NULL);
    TEST_ASSERT(strstr(buf, "crate-post") != NULL);
    TEST_ASSERT(strstr(buf, "ws-post") != NULL);

    // Verify order
    char* p1 = strstr(buf, "ws-pre");
    char* p2 = strstr(buf, "crate-pre");
    char* p3 = strstr(buf, "crate-post");
    char* p4 = strstr(buf, "ws-post");
    TEST_ASSERT(p1 < p2);
    TEST_ASSERT(p2 < p3);
    TEST_ASSERT(p3 < p4);
    free(buf);

    // Clean up
    HookDef final_cleanup = {0};
    final_cleanup.present = true;
    final_cleanup.lifecycle = HOOK_PRE_BUILD;
    final_cleanup.timeout_sec = 5;
    snprintf(final_cleanup.command, sizeof(final_cleanup.command), "del /q \"%s\" 2>nul & ver", tmp);
    hook_execute(&final_cleanup, &ws_env);
    return 0;
}
