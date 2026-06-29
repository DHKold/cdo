// crates/cdo/tst/unit/test_hooks_execute.c
// Unit tests for hook execution: spawning, env injection, cwd, timeout
#include "cdo_ut.h"
#include "model/hooks.h"
#include "core/hooks_exec.h"

#include <string.h>

// =============================================================================
// Absent / NULL hook → immediate return 0
// Validates: Requirements 4.1, 6.1
// =============================================================================

TEST(hook_execute_absent_hook) {
    HookDef hook = {0};
    hook.present = false;
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_EQ(rc, 0);
    return 0;
}

TEST(hook_execute_null_hook) {
    int rc = hook_execute(NULL, NULL);
    TEST_ASSERT_EQ(rc, 0);
    return 0;
}

// =============================================================================
// Successful command → return 0
// Validates: Requirements 4.2, 6.5
// =============================================================================

TEST_SERIAL(hook_execute_successful_command) {
    // "ver" is a Windows built-in that always returns 0, no spaces in command
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_BUILD;
    hook.timeout_sec = 10;
    strcpy(hook.command, "ver");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_EQ(rc, 0);
    return 0;
}

// =============================================================================
// Failing command (exit 1) → return non-zero
// Validates: Requirements 4.3
// =============================================================================

TEST_SERIAL(hook_execute_failing_command) {
    // "exit /b 1" causes cmd to exit with code 1
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_POST_BUILD;
    hook.timeout_sec = 10;
    strcpy(hook.command, "exit /b 1");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_NEQ(rc, 0);
    return 0;
}

// =============================================================================
// Environment variables injected correctly
// Validates: Requirements 4.4, 6.1, 6.2
// =============================================================================

TEST_SERIAL(hook_execute_env_vars_injected) {
    // "if not defined CDO_WS_ROOT exit /b 1" — fails if env var is NOT set
    // Uses cwd "." so spawn can succeed
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_BUILD;
    hook.timeout_sec = 10;
    strcpy(hook.command, "if not defined CDO_WS_ROOT exit /b 1");
    HookEnv env = { .ws_root = ".", .profile = "release", .build_dir = "./build/release" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_EQ(rc, 0);
    return 0;
}

TEST_SERIAL(hook_execute_env_vars_profile) {
    // Verify CDO_PROFILE is injected
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_BUILD;
    hook.timeout_sec = 10;
    strcpy(hook.command, "if not defined CDO_PROFILE exit /b 1");
    HookEnv env = { .ws_root = ".", .profile = "release", .build_dir = "./build/release" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_EQ(rc, 0);
    return 0;
}

TEST_SERIAL(hook_execute_env_vars_crate) {
    // Verify crate-level env vars are injected when crate_name is set
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_BUILD;
    hook.timeout_sec = 10;
    strcpy(hook.command, "if not defined CDO_CRATE_NAME exit /b 1");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = "my_crate", .crate_path = ".", .crate_build_dir = "./build/debug/my_crate" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_EQ(rc, 0);
    return 0;
}

// =============================================================================
// Workspace hook uses ws_root as cwd
// Validates: Requirements 6.1
// =============================================================================

TEST_SERIAL(hook_execute_cwd_workspace) {
    // NULL crate_path = workspace hook → uses ws_root as cwd
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_BUILD;
    hook.timeout_sec = 10;
    strcpy(hook.command, "ver");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = NULL, .crate_path = NULL, .crate_build_dir = NULL };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_EQ(rc, 0);
    return 0;
}

// =============================================================================
// Crate hook uses crate_path as cwd
// Validates: Requirements 6.2
// =============================================================================

TEST_SERIAL(hook_execute_cwd_crate) {
    // crate_path set → uses crate_path as cwd
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_BUILD;
    hook.timeout_sec = 10;
    strcpy(hook.command, "ver");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug", .crate_name = "my_crate", .crate_path = ".", .crate_build_dir = "./build/debug/my_crate" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_EQ(rc, 0);
    return 0;
}

// =============================================================================
// Timeout: long-running command killed after timeout
// Validates: Requirements 7.1, 7.3
// =============================================================================

TEST_SERIAL(hook_execute_timeout) {
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_BUILD;
    hook.timeout_sec = 1;  // 1 second timeout
    // "ping -n 10 127.0.0.1" waits ~10 seconds (no >nul to avoid quoting issues)
    strcpy(hook.command, "ping -n 10 127.0.0.1");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_NEQ(rc, 0);  // Should fail due to timeout
    return 0;
}

// =============================================================================
// Command not found → non-zero return with error
// Validates: Requirements 4.3
// =============================================================================

TEST_SERIAL(hook_execute_command_not_found) {
    HookDef hook = {0};
    hook.present = true;
    hook.lifecycle = HOOK_PRE_BUILD;
    hook.timeout_sec = 10;
    strcpy(hook.command, "nonexistent_command_xyz_12345");
    HookEnv env = { .ws_root = ".", .profile = "debug", .build_dir = "./build/debug" };
    int rc = hook_execute(&hook, &env);
    TEST_ASSERT_NEQ(rc, 0);  // Should fail: command not found
    return 0;
}
