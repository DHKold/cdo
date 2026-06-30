// crates/cdo/tst/unit/build/test_cli_arguments.cpp
// Unit tests for cli::Arguments conversion from CliParseResult.
// Validates: Requirements 13.1, 13.2
#include "cdo_ut.h"
#include "build/cli_arguments.h"
#include "pal/pal.h"

#include <cstring>

using namespace cdo::build::cli;

// =============================================================================
// Helper: Build a CliParseResult with named args and positional values.
// This mirrors how the cdo_cli framework produces a parse result.
// =============================================================================

/// Maximum args and positionals for test fixtures
static const int MAX_TEST_ARGS = 16;
static const int MAX_TEST_POSITIONALS = 8;

struct TestParseResult {
    CliParseResult result;
    CliArgValue arg_values[MAX_TEST_ARGS];
    const char* positional_values[MAX_TEST_POSITIONALS];
    int arg_count;
    int pos_count;

    TestParseResult() {
        std::memset(&result, 0, sizeof(result));
        std::memset(arg_values, 0, sizeof(arg_values));
        std::memset(positional_values, 0, sizeof(positional_values));
        arg_count = 0;
        pos_count = 0;
        result.arg_values = arg_values;
        result.positional_values = positional_values;
    }

    void addBool(const char* name, bool value, bool present = true) {
        arg_values[arg_count].name = name;
        arg_values[arg_count].type = CLI_ARG_BOOL;
        arg_values[arg_count].value.bool_val = value;
        arg_values[arg_count].present = present;
        arg_count++;
        result.arg_value_count = arg_count;
    }

    void addInt(const char* name, int value, bool present = true) {
        arg_values[arg_count].name = name;
        arg_values[arg_count].type = CLI_ARG_INT;
        arg_values[arg_count].value.int_val = value;
        arg_values[arg_count].present = present;
        arg_count++;
        result.arg_value_count = arg_count;
    }

    void addStr(const char* name, const char* value, bool present = true) {
        arg_values[arg_count].name = name;
        arg_values[arg_count].type = CLI_ARG_STRING;
        arg_values[arg_count].value.str_val = value;
        arg_values[arg_count].present = present;
        arg_count++;
        result.arg_value_count = arg_count;
    }

    void addPositional(const char* value) {
        positional_values[pos_count] = value;
        pos_count++;
        result.positional_count = pos_count;
    }

    const CliParseResult* get() const { return &result; }
};

// =============================================================================
// Test: Valid CliParseResult converts correctly (default values)
// =============================================================================

TEST(cli_arguments_default_values) {
    // Tests run from workspace root where cdo.toml exists, so workspace discovery succeeds
    TestParseResult pr;

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT_STR_EQ(args.profile().c_str(), "debug");
    TEST_ASSERT_EQ(args.jobs(), 0);
    TEST_ASSERT(args.force() == false);
    TEST_ASSERT(args.clean() == false);
    TEST_ASSERT(args.cacheEnabled() == true);
    TEST_ASSERT_EQ(args.verbosity(), 2);
    TEST_ASSERT_EQ((int)args.crateFilter().size(), 0);
    // workspaceRoot should be non-empty since we're running from workspace root
    TEST_ASSERT(args.workspaceRoot().size() > 0);
    return 0;
}

// =============================================================================
// Test: --release sets profile to "release"
// =============================================================================

TEST(cli_arguments_release_flag_sets_profile) {
    TestParseResult pr;
    pr.addBool("release", true);

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT_STR_EQ(args.profile().c_str(), "release");
    return 0;
}

// =============================================================================
// Test: --profile explicitly overrides --release
// =============================================================================

TEST(cli_arguments_explicit_profile_overrides_release) {
    TestParseResult pr;
    pr.addBool("release", true);
    pr.addStr("profile", "relwithdebinfo");

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT_STR_EQ(args.profile().c_str(), "relwithdebinfo");
    return 0;
}

// =============================================================================
// Test: --no-cache sets cacheEnabled to false
// =============================================================================

TEST(cli_arguments_no_cache_disables_cache) {
    TestParseResult pr;
    pr.addBool("no-cache", true);

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT(args.cacheEnabled() == false);
    return 0;
}

// =============================================================================
// Test: jobs=0 is accepted (resolved later to cpu count)
// =============================================================================

TEST(cli_arguments_jobs_zero_accepted) {
    TestParseResult pr;
    pr.addInt("jobs", 0);

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT_EQ(args.jobs(), 0);
    return 0;
}

// =============================================================================
// Test: jobs=4 passes through correctly
// =============================================================================

TEST(cli_arguments_jobs_positive_value) {
    TestParseResult pr;
    pr.addInt("jobs", 4);

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT_EQ(args.jobs(), 4);
    return 0;
}

// =============================================================================
// Test: --force flag
// =============================================================================

TEST(cli_arguments_force_flag) {
    TestParseResult pr;
    pr.addBool("force", true);

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT(args.force() == true);
    return 0;
}

// =============================================================================
// Test: --clean flag
// =============================================================================

TEST(cli_arguments_clean_flag) {
    TestParseResult pr;
    pr.addBool("clean", true);

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT(args.clean() == true);
    return 0;
}

// =============================================================================
// Test: --verbose sets verbosity to 3
// =============================================================================

TEST(cli_arguments_verbose_flag) {
    TestParseResult pr;
    pr.addBool("verbose", true);

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT_EQ(args.verbosity(), 3);
    return 0;
}

// =============================================================================
// Test: positional values map to crateFilter
// =============================================================================

TEST(cli_arguments_positional_values_to_crate_filter) {
    TestParseResult pr;
    pr.addPositional("cdo");
    pr.addPositional("cdo_cli");

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT_EQ((int)args.crateFilter().size(), 2);
    TEST_ASSERT_STR_EQ(args.crateFilter()[0].c_str(), "cdo");
    TEST_ASSERT_STR_EQ(args.crateFilter()[1].c_str(), "cdo_cli");
    return 0;
}

// =============================================================================
// Test: single positional value
// =============================================================================

TEST(cli_arguments_single_positional) {
    TestParseResult pr;
    pr.addPositional("my_crate");

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT_EQ((int)args.crateFilter().size(), 1);
    TEST_ASSERT_STR_EQ(args.crateFilter()[0].c_str(), "my_crate");
    return 0;
}

// =============================================================================
// Test: null CliParseResult → isValid()=false, lastError() populated
// =============================================================================

TEST(cli_arguments_null_result_invalid) {
    Arguments args(nullptr);
    TEST_ASSERT(args.isValid() == false);
    TEST_ASSERT(args.lastError().size() > 0);
    return 0;
}

// =============================================================================
// Test: combined flags parse correctly
// =============================================================================

TEST(cli_arguments_combined_flags) {
    TestParseResult pr;
    pr.addBool("release", true);
    pr.addBool("force", true);
    pr.addBool("clean", true);
    pr.addBool("no-cache", true);
    pr.addBool("verbose", true);
    pr.addInt("jobs", 8);
    pr.addPositional("my_lib");

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    TEST_ASSERT_STR_EQ(args.profile().c_str(), "release");
    TEST_ASSERT(args.force() == true);
    TEST_ASSERT(args.clean() == true);
    TEST_ASSERT(args.cacheEnabled() == false);
    TEST_ASSERT_EQ(args.verbosity(), 3);
    TEST_ASSERT_EQ(args.jobs(), 8);
    TEST_ASSERT_EQ((int)args.crateFilter().size(), 1);
    TEST_ASSERT_STR_EQ(args.crateFilter()[0].c_str(), "my_lib");
    return 0;
}

// =============================================================================
// Test: workspace root is discovered (tests run from workspace root)
// =============================================================================

TEST(cli_arguments_workspace_root_discovered) {
    TestParseResult pr;

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    // The workspace root should contain "cdo" since we're in the cdo workspace
    TEST_ASSERT(args.workspaceRoot().size() > 0);
    // Verify cdo.toml exists at the discovered root
    char manifest[520];
    int rc = pal_path_join(manifest, sizeof(manifest), args.workspaceRoot().c_str(), "cdo.toml");
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(pal_path_exists(manifest), PAL_OK);
    return 0;
}

// =============================================================================
// Test: non-present args use defaults
// =============================================================================

TEST(cli_arguments_non_present_args_use_defaults) {
    TestParseResult pr;
    // Add args that are NOT present (present=false)
    pr.addBool("release", true, false);
    pr.addBool("force", true, false);
    pr.addBool("verbose", true, false);
    pr.addInt("jobs", 99, false);

    Arguments args(pr.get());
    TEST_ASSERT(args.isValid() == true);
    // Non-present args should result in defaults
    TEST_ASSERT_STR_EQ(args.profile().c_str(), "debug");
    TEST_ASSERT(args.force() == false);
    TEST_ASSERT_EQ(args.verbosity(), 2);
    TEST_ASSERT_EQ(args.jobs(), 0);
    return 0;
}

// =============================================================================
// MSVC Registration
// =============================================================================

#ifdef _MSC_VER
void register_test_cli_arguments_tests(void) {
    REGISTER_TEST(cli_arguments_default_values);
    REGISTER_TEST(cli_arguments_release_flag_sets_profile);
    REGISTER_TEST(cli_arguments_explicit_profile_overrides_release);
    REGISTER_TEST(cli_arguments_no_cache_disables_cache);
    REGISTER_TEST(cli_arguments_jobs_zero_accepted);
    REGISTER_TEST(cli_arguments_jobs_positive_value);
    REGISTER_TEST(cli_arguments_force_flag);
    REGISTER_TEST(cli_arguments_clean_flag);
    REGISTER_TEST(cli_arguments_verbose_flag);
    REGISTER_TEST(cli_arguments_positional_values_to_crate_filter);
    REGISTER_TEST(cli_arguments_single_positional);
    REGISTER_TEST(cli_arguments_null_result_invalid);
    REGISTER_TEST(cli_arguments_combined_flags);
    REGISTER_TEST(cli_arguments_workspace_root_discovered);
    REGISTER_TEST(cli_arguments_non_present_args_use_defaults);
}
#endif
