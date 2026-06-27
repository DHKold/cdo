/*
 * test_semver.c - Unit tests for semver parsing and comparison
 *
 * Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9
 */
#include "cdo_ut.h"
#include "commons/semver.h"

/*============================================================================
 * Semver parse tests
 *============================================================================*/

/* Requirement 3.1: parse "1.2.3" → major=1, minor=2, patch=3, empty prerelease */
TEST(semver_parse_basic) {
    Semver v = {0};
    int rc = semver_parse("1.2.3", &v);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(v.major, 1);
    TEST_ASSERT_EQ(v.minor, 2);
    TEST_ASSERT_EQ(v.patch, 3);
    TEST_ASSERT_STR_EQ(v.prerelease, "");
    return 0;
}

/* Requirement 3.2: parse "0.1.0-alpha" → prerelease="alpha" */
TEST(semver_parse_prerelease) {
    Semver v = {0};
    int rc = semver_parse("0.1.0-alpha", &v);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(v.major, 0);
    TEST_ASSERT_EQ(v.minor, 1);
    TEST_ASSERT_EQ(v.patch, 0);
    TEST_ASSERT_STR_EQ(v.prerelease, "alpha");
    return 0;
}

/* Requirement 3.3: parse "abc" returns non-zero */
TEST(semver_parse_invalid) {
    Semver v = {0};
    int rc = semver_parse("abc", &v);
    TEST_ASSERT(rc != 0);
    return 0;
}

/*============================================================================
 * Semver constraint tests
 *============================================================================*/

/* Requirement 3.4: "^1.2.3" → kind=SEMVER_CARET, version 1.2.3 */
TEST(semver_constraint_caret) {
    SemverConstraint c = {0};
    int rc = semver_constraint_parse("^1.2.3", &c);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(c.kind, SEMVER_CARET);
    TEST_ASSERT_EQ(c.version.major, 1);
    TEST_ASSERT_EQ(c.version.minor, 2);
    TEST_ASSERT_EQ(c.version.patch, 3);
    return 0;
}

/* Requirement 3.5: "*" → kind=SEMVER_WILDCARD */
TEST(semver_constraint_wildcard) {
    SemverConstraint c = {0};
    int rc = semver_constraint_parse("*", &c);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(c.kind, SEMVER_WILDCARD);
    return 0;
}

/*============================================================================
 * Semver satisfies tests
 *============================================================================*/

/* Requirement 3.6: version 1.3.0 satisfies "^1.2.0" */
TEST(semver_satisfies_true) {
    Semver v = {0};
    SemverConstraint c = {0};
    int rc = semver_parse("1.3.0", &v);
    TEST_ASSERT_EQ(rc, 0);
    rc = semver_constraint_parse("^1.2.0", &c);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(semver_satisfies(&v, &c) == true);
    return 0;
}

/* Requirement 3.7: version 2.0.0 does not satisfy "^1.2.0" */
TEST(semver_satisfies_false) {
    Semver v = {0};
    SemverConstraint c = {0};
    int rc = semver_parse("2.0.0", &v);
    TEST_ASSERT_EQ(rc, 0);
    rc = semver_constraint_parse("^1.2.0", &c);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(semver_satisfies(&v, &c) == false);
    return 0;
}

/*============================================================================
 * Semver compare tests
 *============================================================================*/

/* Requirement 3.8: compare "1.0.0" vs "1.0.1" → negative */
TEST(semver_compare_patch) {
    Semver a = {0}, b = {0};
    int rc = semver_parse("1.0.0", &a);
    TEST_ASSERT_EQ(rc, 0);
    rc = semver_parse("1.0.1", &b);
    TEST_ASSERT_EQ(rc, 0);
    int cmp = semver_compare(&a, &b);
    TEST_ASSERT(cmp < 0);
    return 0;
}

/* Requirement 3.9: pre-release ranks lower than release */
TEST(semver_compare_prerelease_lower) {
    Semver release = {0}, prerelease = {0};
    int rc = semver_parse("1.0.0", &release);
    TEST_ASSERT_EQ(rc, 0);
    rc = semver_parse("1.0.0-beta", &prerelease);
    TEST_ASSERT_EQ(rc, 0);
    int cmp = semver_compare(&prerelease, &release);
    TEST_ASSERT(cmp < 0);
    return 0;
}
