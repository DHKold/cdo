/**
 * test_cdo_ut.c — Self-tests for the cdo_ut test framework.
 *
 * Exercises:
 *   - Test registration (TEST macro auto-registers)
 *   - Assertion macros: TEST_ASSERT, TEST_ASSERT_EQ, TEST_ASSERT_NEQ,
 *     TEST_ASSERT_STR_EQ, TEST_ASSERT_NULL
 *   - --list output (verified externally by running with --list)
 *
 * Requirements validated: 1.1, 1.2, 1.3, 1.4, 9.1
 */

#include "cdo_ut.h"

// =============================================================================
// TEST_ASSERT — passes for true conditions
// =============================================================================

TEST(test_assert_pass) {
    TEST_ASSERT(1 == 1);
    TEST_ASSERT(42 > 0);
    TEST_ASSERT(100 != 99);
    return 0;
}

// =============================================================================
// TEST_ASSERT_EQ — passes for equal integer values
// =============================================================================

TEST(test_assert_eq_pass) {
    int a = 5;
    TEST_ASSERT_EQ(a, 5);
    TEST_ASSERT_EQ(0, 0);
    TEST_ASSERT_EQ(-1, -1);
    return 0;
}

// =============================================================================
// TEST_ASSERT_NEQ — passes for unequal integer values
// =============================================================================

TEST(test_assert_neq_pass) {
    TEST_ASSERT_NEQ(1, 2);
    TEST_ASSERT_NEQ(0, -1);
    TEST_ASSERT_NEQ(100, 200);
    return 0;
}

// =============================================================================
// TEST_ASSERT_STR_EQ — passes for equal strings
// =============================================================================

TEST(test_assert_str_eq_pass) {
    TEST_ASSERT_STR_EQ("hello", "hello");
    TEST_ASSERT_STR_EQ("", "");
    TEST_ASSERT_STR_EQ("cdo_ut", "cdo_ut");
    return 0;
}

// =============================================================================
// TEST_ASSERT_NULL — passes for NULL pointers
// =============================================================================

TEST(test_assert_null_pass) {
    TEST_ASSERT_NULL(NULL);
    void *ptr = NULL;
    TEST_ASSERT_NULL(ptr);
    return 0;
}

// =============================================================================
// Multiple assertions in one test — verifies combination works
// =============================================================================

TEST(test_multiple_assertions) {
    TEST_ASSERT(1);
    TEST_ASSERT_EQ(10, 10);
    TEST_ASSERT_NEQ(10, 20);
    TEST_ASSERT_STR_EQ("abc", "abc");
    TEST_ASSERT_NULL(NULL);
    return 0;
}
