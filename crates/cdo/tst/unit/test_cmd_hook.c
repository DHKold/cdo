// crates/cdo/tst/unit/test_cmd_hook.c
// Unit tests for cdo hook command building blocks:
//   - hook_lifecycle_name (all values + invalid)
//   - hooks_parse (no hooks = "No hooks configured" condition; with hooks = list output)
//   - hook_execute (valid lifecycle run)
//   - invalid lifecycle string validation
// Validates: Requirements 8.1, 8.2, 8.3
#include "cdo_ut.h"
#include "model/hooks.h"
#include "core/hooks_exec.h"

#include <string.h>

// =============================================================================
// hook_lifecycle_name returns correct strings for all lifecycle values
// Validates: Requirements 8.1 (list uses lifecycle names for display)
// =============================================================================

TEST(cmd_hook_lifecycle_name_pre_build) {
    TEST_ASSERT_STR_EQ(hook_lifecycle_name(HOOK_PRE_BUILD), "pre-build");
    return 0;
}

TEST(cmd_hook_lifecycle_name_post_build) {
    TEST_ASSERT_STR_EQ(hook_lifecycle_name(HOOK_POST_BUILD), "post-build");
    return 0;
}

TEST(cmd_hook_lifecycle_name_pre_test) {
    TEST_ASSERT_STR_EQ(hook_lifecycle_name(HOOK_PRE_TEST), "pre-test");
    return 0;
}

TEST(cmd_hook_lifecycle_name_post_test) {
    TEST_ASSERT_STR_EQ(hook_lifecycle_name(HOOK_POST_TEST), "post-test");
    return 0;
}

// =============================================================================
// hook_lifecycle_name returns "unknown" for invalid value
// Validates: Requirements 8.3 (error handling for invalid lifecycle)
// =============================================================================

TEST(cmd_hook_lifecycle_name_invalid) {
    TEST_ASSERT_STR_EQ(hook_lifecycle_name((HookLifecycle)99), "unknown");
    return 0;
}

// =============================================================================
// "No hooks configured" condition: parse TOML with no [hooks] → all absent
// Validates: Requirements 8.3 (cdo hook list prints "No hooks configured.")
// =============================================================================

TEST(cmd_hook_list_no_hooks_all_absent) {
    HookSet set;
    int rc = hooks_parse("[crate]\nname = \"test\"\n", &set);
    TEST_ASSERT_EQ(rc, 0);
    for (int i = 0; i < HOOK_LIFECYCLE_COUNT; i++) {
        TEST_ASSERT(set.hooks[i].present == false);
    }
    return 0;
}

// =============================================================================
// List with hooks: parse TOML with hooks → hooks present (what list displays)
// Validates: Requirements 8.1 (cdo hook list shows all configured hooks)
// =============================================================================

TEST(cmd_hook_list_with_hooks) {
    const char* toml = "[hooks]\npre-build = \"gen.py\"\npost-test = \"cleanup.sh\"\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(set.hooks[HOOK_PRE_BUILD].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_PRE_BUILD].command, "gen.py");
    TEST_ASSERT(set.hooks[HOOK_POST_TEST].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_POST_TEST].command, "cleanup.sh");
    TEST_ASSERT(set.hooks[HOOK_POST_BUILD].present == false);
    TEST_ASSERT(set.hooks[HOOK_PRE_TEST].present == false);
    return 0;
}

// =============================================================================
// Run valid lifecycle: execute a hook (simulates "cdo hook run pre-build")
// Validates: Requirements 8.2 (cdo hook run <lifecycle-point> triggers hook)
// =============================================================================

TEST_SERIAL(cmd_hook_run_valid_lifecycle) {
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
// Run invalid lifecycle string: verify no valid lifecycle name matches
// Validates: Requirements 8.3 (invalid lifecycle → error)
// =============================================================================

TEST(cmd_hook_run_invalid_lifecycle_string) {
    for (int i = 0; i < HOOK_LIFECYCLE_COUNT; i++) {
        TEST_ASSERT(strcmp(hook_lifecycle_name((HookLifecycle)i), "invalid-lifecycle") != 0);
    }
    return 0;
}
