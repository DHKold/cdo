// crates/cdo_pbt/src/unit/test_cli.c
// Unit tests for CLI argument parsing and command suggestion
#include "cdo_ut.h"
#include "core/cli.h"

// --- cdo_cli_parse ---

TEST(cli_parse_build) {
    char* argv[] = {"cdo", "build"};
    CdoOptions opts = {0};
    int rc = cdo_cli_parse(2, argv, &opts);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(opts.command, CDO_CMD_BUILD);
    return 0;
}

TEST(cli_parse_build_release) {
    char* argv[] = {"cdo", "build", "--release"};
    CdoOptions opts = {0};
    int rc = cdo_cli_parse(3, argv, &opts);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(opts.release == true);
    return 0;
}

TEST(cli_parse_verbose_test) {
    char* argv[] = {"cdo", "--verbose", "test"};
    CdoOptions opts = {0};
    int rc = cdo_cli_parse(3, argv, &opts);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(opts.verbose == true);
    TEST_ASSERT_EQ(opts.command, CDO_CMD_TEST);
    return 0;
}

TEST(cli_parse_double_dash) {
    char* argv[] = {"cdo", "build", "--", "extra"};
    CdoOptions opts = {0};
    int rc = cdo_cli_parse(4, argv, &opts);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(opts.argc_rest, 1);
    TEST_ASSERT_STR_EQ(opts.argv_rest[0], "extra");
    return 0;
}

TEST(cli_parse_coverage) {
    char* argv[] = {"cdo", "build", "--coverage"};
    CdoOptions opts = {0};
    int rc = cdo_cli_parse(3, argv, &opts);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(opts.coverage == true);
    return 0;
}

TEST(cli_parse_cache_flag) {
    char* argv[] = {"cdo", "clean", "--cache"};
    CdoOptions opts = {0};
    int rc = cdo_cli_parse(3, argv, &opts);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(opts.command, CDO_CMD_CLEAN);
    TEST_ASSERT(opts.cache == true);
    return 0;
}

TEST(cli_parse_clean_without_cache_flag) {
    char* argv[] = {"cdo", "clean"};
    CdoOptions opts = {0};
    int rc = cdo_cli_parse(2, argv, &opts);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(opts.command, CDO_CMD_CLEAN);
    TEST_ASSERT(opts.cache == false);
    return 0;
}

// --- cdo_cli_suggest ---

TEST(cli_suggest_typo) {
    char suggestions[8][32] = {0};
    int count = cdo_cli_suggest("buidl", suggestions, 8);
    TEST_ASSERT(count >= 1);
    // At least one suggestion should contain "build"
    int found_build = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(suggestions[i], "build") == 0) {
            found_build = 1;
            break;
        }
    }
    TEST_ASSERT(found_build);
    return 0;
}

TEST(cli_suggest_no_match) {
    char suggestions[8][32] = {0};
    int count = cdo_cli_suggest("zzzzzzxqwvbnm", suggestions, 8);
    TEST_ASSERT_EQ(count, 0);
    return 0;
}
