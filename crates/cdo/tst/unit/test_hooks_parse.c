// crates/cdo/tst/unit/test_hooks_parse.c
// Unit tests for hook TOML parsing (hooks_parse and hooks_parse_table)
#include "cdo_ut.h"
#include "model/hooks.h"

// --- hooks_parse: string shorthand ---

TEST(hooks_parse_string_shorthand) {
    const char* toml = "[hooks]\npre-build = \"echo hello\"\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(set.hooks[HOOK_PRE_BUILD].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_PRE_BUILD].command, "echo hello");
    TEST_ASSERT_EQ(set.hooks[HOOK_PRE_BUILD].timeout_sec, 120);
    // Other hooks should be absent
    TEST_ASSERT(set.hooks[HOOK_POST_BUILD].present == false);
    TEST_ASSERT(set.hooks[HOOK_PRE_TEST].present == false);
    TEST_ASSERT(set.hooks[HOOK_POST_TEST].present == false);
    return 0;
}

// --- hooks_parse: table form with custom timeout ---

TEST(hooks_parse_table_form_custom_timeout) {
    const char* toml = "[hooks.pre-build]\ncommand = \"python gen.py\"\ntimeout = 300\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(set.hooks[HOOK_PRE_BUILD].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_PRE_BUILD].command, "python gen.py");
    TEST_ASSERT_EQ(set.hooks[HOOK_PRE_BUILD].timeout_sec, 300);
    return 0;
}

// --- hooks_parse: table form with timeout = 0 (no timeout) ---

TEST(hooks_parse_timeout_zero) {
    const char* toml = "[hooks.post-build]\ncommand = \"deploy.sh\"\ntimeout = 0\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(set.hooks[HOOK_POST_BUILD].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_POST_BUILD].command, "deploy.sh");
    TEST_ASSERT_EQ(set.hooks[HOOK_POST_BUILD].timeout_sec, 0);
    return 0;
}

// --- hooks_parse: all four lifecycle keys ---

TEST(hooks_parse_all_four_keys) {
    const char* toml =
        "[hooks]\n"
        "pre-build = \"step1\"\n"
        "post-build = \"step2\"\n"
        "pre-test = \"step3\"\n"
        "post-test = \"step4\"\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(set.hooks[HOOK_PRE_BUILD].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_PRE_BUILD].command, "step1");
    TEST_ASSERT_EQ(set.hooks[HOOK_PRE_BUILD].timeout_sec, 120);
    TEST_ASSERT(set.hooks[HOOK_POST_BUILD].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_POST_BUILD].command, "step2");
    TEST_ASSERT_EQ(set.hooks[HOOK_POST_BUILD].timeout_sec, 120);
    TEST_ASSERT(set.hooks[HOOK_PRE_TEST].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_PRE_TEST].command, "step3");
    TEST_ASSERT_EQ(set.hooks[HOOK_PRE_TEST].timeout_sec, 120);
    TEST_ASSERT(set.hooks[HOOK_POST_TEST].present == true);
    TEST_ASSERT_STR_EQ(set.hooks[HOOK_POST_TEST].command, "step4");
    TEST_ASSERT_EQ(set.hooks[HOOK_POST_TEST].timeout_sec, 120);
    return 0;
}

// --- hooks_parse: no [hooks] section → all absent ---

TEST(hooks_parse_no_hooks_section) {
    const char* toml = "[package]\nname = \"my-crate\"\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    for (int i = 0; i < HOOK_LIFECYCLE_COUNT; i++) {
        TEST_ASSERT(set.hooks[i].present == false);
    }
    return 0;
}

// --- hooks_parse: NULL content → all absent ---

TEST(hooks_parse_null_content) {
    HookSet set;
    int rc = hooks_parse(NULL, &set);
    TEST_ASSERT_EQ(rc, 0);
    for (int i = 0; i < HOOK_LIFECYCLE_COUNT; i++) {
        TEST_ASSERT(set.hooks[i].present == false);
    }
    return 0;
}

// --- hooks_parse: empty string content → all absent ---

TEST(hooks_parse_empty_content) {
    HookSet set;
    int rc = hooks_parse("", &set);
    TEST_ASSERT_EQ(rc, 0);
    for (int i = 0; i < HOOK_LIFECYCLE_COUNT; i++) {
        TEST_ASSERT(set.hooks[i].present == false);
    }
    return 0;
}

// --- hooks_parse: invalid type (integer) → error ---

TEST(hooks_parse_invalid_type_integer) {
    const char* toml = "[hooks]\npre-build = 42\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_NEQ(rc, 0);
    return 0;
}

// --- hooks_parse: empty string value → treated as absent ---

TEST(hooks_parse_empty_string_absent) {
    const char* toml = "[hooks]\npre-build = \"\"\n";
    HookSet set;
    int rc = hooks_parse(toml, &set);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(set.hooks[HOOK_PRE_BUILD].present == false);
    return 0;
}

// --- hooks_parse_table: NULL table → all absent ---

TEST(hooks_parse_table_null) {
    HookSet set;
    int rc = hooks_parse_table(NULL, &set);
    TEST_ASSERT_EQ(rc, 0);
    for (int i = 0; i < HOOK_LIFECYCLE_COUNT; i++) {
        TEST_ASSERT(set.hooks[i].present == false);
    }
    return 0;
}
