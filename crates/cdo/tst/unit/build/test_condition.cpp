// test_condition.cpp — Unit tests for FreshnessCondition.
// Validates: Requirements 5.2, 5.3, 5.6
//
// Uses real temp files with explicit mtime values to create known freshness scenarios.
// FreshnessCondition logic:
//   1. If primary_output.exists() is false → Build("does not exist")
//   2. If forced_=true and output exists → Build("forced")
//   3. If any input.mtime() > primary_output.mtime() → Build("outdated")
//   4. Otherwise → Skip("up-to-date")

#include "cdo_ut.h"
#include "build/artifact.h"
#include "build/condition.h"
#include "pal/pal.h"

#include <string>
#include <vector>

using namespace cdo::build;

// ---------------------------------------------------------------------------
// Helpers: create temp files with known mtimes
// ---------------------------------------------------------------------------

#define TEST_DIR "build/test_tmp_condition"

/// Create a temp file at the given path and set its mtime to the given value (nanoseconds).
/// Returns 0 on success.
static int create_file_with_mtime(const char* path, uint64_t mtime_ns) {
    const char content[] = "x";
    int rc = pal_file_write(path, content, 1);
    if (rc != 0) return rc;
    return pal_file_set_mtime(path, mtime_ns);
}

/// Ensure our test temp directory exists.
static int ensure_test_dir() {
    return pal_mkdir_p(TEST_DIR);
}

// ---------------------------------------------------------------------------
// Test: output does not exist → Build("does not exist")
// ---------------------------------------------------------------------------

TEST(freshness_output_not_exists_returns_build) {
    FreshnessCondition cond(false);
    FileArtifact output("__nonexistent_path_freshness_test_xyz__.o", ArtifactType::Object);

    // No inputs needed for this case
    std::vector<const Artifact*> inputs;

    ConditionResult result = cond.evaluate(inputs, output);

    TEST_ASSERT_EQ(result.decision, ConditionResult::Build);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "does not exist");
    return 0;
}

// ---------------------------------------------------------------------------
// Test: forced=true and output up-to-date → Build("forced")
// ---------------------------------------------------------------------------

TEST_SERIAL(freshness_forced_returns_build_even_when_up_to_date) {
    TEST_ASSERT_EQ(ensure_test_dir(), 0);

    const char* input_path = TEST_DIR "/forced_input.c";
    const char* output_path = TEST_DIR "/forced_output.o";

    // Create input with mtime 1000000000 ns (1 second since epoch)
    TEST_ASSERT_EQ(create_file_with_mtime(input_path, 1000000000ULL), 0);
    // Create output with mtime 2000000000 ns (2 seconds since epoch) — newer than input
    TEST_ASSERT_EQ(create_file_with_mtime(output_path, 2000000000ULL), 0);

    FreshnessCondition cond(true); // forced = true
    FileArtifact input_art(input_path, ArtifactType::Source);
    FileArtifact output_art(output_path, ArtifactType::Object);

    std::vector<const Artifact*> inputs = { &input_art };

    ConditionResult result = cond.evaluate(inputs, output_art);

    TEST_ASSERT_EQ(result.decision, ConditionResult::Build);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "forced");
    return 0;
}

// ---------------------------------------------------------------------------
// Test: any input mtime > output mtime → Build("outdated")
// ---------------------------------------------------------------------------

TEST_SERIAL(freshness_input_newer_than_output_returns_build_outdated) {
    TEST_ASSERT_EQ(ensure_test_dir(), 0);

    const char* input_path = TEST_DIR "/outdated_input.c";
    const char* output_path = TEST_DIR "/outdated_output.o";

    // Create output with mtime 1000000000 ns
    TEST_ASSERT_EQ(create_file_with_mtime(output_path, 1000000000ULL), 0);
    // Create input with mtime 2000000000 ns — newer than output
    TEST_ASSERT_EQ(create_file_with_mtime(input_path, 2000000000ULL), 0);

    FreshnessCondition cond(false);
    FileArtifact input_art(input_path, ArtifactType::Source);
    FileArtifact output_art(output_path, ArtifactType::Object);

    std::vector<const Artifact*> inputs = { &input_art };

    ConditionResult result = cond.evaluate(inputs, output_art);

    TEST_ASSERT_EQ(result.decision, ConditionResult::Build);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "outdated");
    return 0;
}

// ---------------------------------------------------------------------------
// Test: multiple inputs, one is newer → Build("outdated")
// ---------------------------------------------------------------------------

TEST_SERIAL(freshness_one_of_many_inputs_newer_returns_outdated) {
    TEST_ASSERT_EQ(ensure_test_dir(), 0);

    const char* input1_path = TEST_DIR "/multi_input1.c";
    const char* input2_path = TEST_DIR "/multi_input2.h";
    const char* input3_path = TEST_DIR "/multi_input3.h";
    const char* output_path = TEST_DIR "/multi_output.o";

    // Output at mtime 5000000000 ns
    TEST_ASSERT_EQ(create_file_with_mtime(output_path, 5000000000ULL), 0);
    // Inputs: two older, one newer
    TEST_ASSERT_EQ(create_file_with_mtime(input1_path, 3000000000ULL), 0);
    TEST_ASSERT_EQ(create_file_with_mtime(input2_path, 4000000000ULL), 0);
    TEST_ASSERT_EQ(create_file_with_mtime(input3_path, 6000000000ULL), 0); // newer than output

    FreshnessCondition cond(false);
    FileArtifact in1(input1_path, ArtifactType::Source);
    FileArtifact in2(input2_path, ArtifactType::Header);
    FileArtifact in3(input3_path, ArtifactType::Header);
    FileArtifact out(output_path, ArtifactType::Object);

    std::vector<const Artifact*> inputs = { &in1, &in2, &in3 };

    ConditionResult result = cond.evaluate(inputs, out);

    TEST_ASSERT_EQ(result.decision, ConditionResult::Build);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "outdated");
    return 0;
}

// ---------------------------------------------------------------------------
// Test: all inputs older or equal → Skip("up-to-date")
// ---------------------------------------------------------------------------

TEST_SERIAL(freshness_all_inputs_older_returns_skip) {
    TEST_ASSERT_EQ(ensure_test_dir(), 0);

    const char* input_path = TEST_DIR "/uptodate_input.c";
    const char* output_path = TEST_DIR "/uptodate_output.o";

    // Create input with mtime 1000000000 ns
    TEST_ASSERT_EQ(create_file_with_mtime(input_path, 1000000000ULL), 0);
    // Create output with mtime 2000000000 ns — newer than input
    TEST_ASSERT_EQ(create_file_with_mtime(output_path, 2000000000ULL), 0);

    FreshnessCondition cond(false);
    FileArtifact input_art(input_path, ArtifactType::Source);
    FileArtifact output_art(output_path, ArtifactType::Object);

    std::vector<const Artifact*> inputs = { &input_art };

    ConditionResult result = cond.evaluate(inputs, output_art);

    TEST_ASSERT_EQ(result.decision, ConditionResult::Skip);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "up-to-date");
    return 0;
}

// ---------------------------------------------------------------------------
// Test: empty inputs list with existing output → Skip("up-to-date")
// ---------------------------------------------------------------------------

TEST_SERIAL(freshness_empty_inputs_existing_output_returns_skip) {
    TEST_ASSERT_EQ(ensure_test_dir(), 0);

    const char* output_path = TEST_DIR "/empty_inputs_output.o";

    // Create output with some mtime
    TEST_ASSERT_EQ(create_file_with_mtime(output_path, 5000000000ULL), 0);

    FreshnessCondition cond(false);
    FileArtifact output_art(output_path, ArtifactType::Object);

    std::vector<const Artifact*> inputs; // empty

    ConditionResult result = cond.evaluate(inputs, output_art);

    TEST_ASSERT_EQ(result.decision, ConditionResult::Skip);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "up-to-date");
    return 0;
}

// ---------------------------------------------------------------------------
// Test: identical mtimes (input == output) → Skip("up-to-date")
// The condition is strictly "input mtime > output mtime" triggers Build,
// so equal mtimes should result in Skip.
// ---------------------------------------------------------------------------

TEST_SERIAL(freshness_identical_mtimes_returns_skip) {
    TEST_ASSERT_EQ(ensure_test_dir(), 0);

    const char* input_path = TEST_DIR "/same_mtime_input.c";
    const char* output_path = TEST_DIR "/same_mtime_output.o";

    uint64_t same_mtime = 3000000000ULL;
    TEST_ASSERT_EQ(create_file_with_mtime(input_path, same_mtime), 0);
    TEST_ASSERT_EQ(create_file_with_mtime(output_path, same_mtime), 0);

    FreshnessCondition cond(false);
    FileArtifact input_art(input_path, ArtifactType::Source);
    FileArtifact output_art(output_path, ArtifactType::Object);

    std::vector<const Artifact*> inputs = { &input_art };

    ConditionResult result = cond.evaluate(inputs, output_art);

    TEST_ASSERT_EQ(result.decision, ConditionResult::Skip);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "up-to-date");
    return 0;
}

// ---------------------------------------------------------------------------
// Test: forced=true but output does not exist → Build("does not exist")
// The "does not exist" check takes priority over the forced flag.
// ---------------------------------------------------------------------------

TEST(freshness_forced_output_missing_returns_does_not_exist) {
    FreshnessCondition cond(true); // forced = true
    FileArtifact output("__nonexistent_forced_test_xyz__.o", ArtifactType::Object);

    std::vector<const Artifact*> inputs;

    ConditionResult result = cond.evaluate(inputs, output);

    // "does not exist" should take priority over "forced"
    TEST_ASSERT_EQ(result.decision, ConditionResult::Build);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "does not exist");
    return 0;
}

// ---------------------------------------------------------------------------
// Test: forced=false, multiple inputs all older → Skip("up-to-date")
// ---------------------------------------------------------------------------

TEST_SERIAL(freshness_multiple_inputs_all_older_returns_skip) {
    TEST_ASSERT_EQ(ensure_test_dir(), 0);

    const char* input1_path = TEST_DIR "/all_older_in1.c";
    const char* input2_path = TEST_DIR "/all_older_in2.h";
    const char* input3_path = TEST_DIR "/all_older_in3.h";
    const char* output_path = TEST_DIR "/all_older_out.o";

    // Output is the newest
    TEST_ASSERT_EQ(create_file_with_mtime(output_path, 9000000000ULL), 0);
    TEST_ASSERT_EQ(create_file_with_mtime(input1_path, 1000000000ULL), 0);
    TEST_ASSERT_EQ(create_file_with_mtime(input2_path, 5000000000ULL), 0);
    TEST_ASSERT_EQ(create_file_with_mtime(input3_path, 8000000000ULL), 0);

    FreshnessCondition cond(false);
    FileArtifact in1(input1_path, ArtifactType::Source);
    FileArtifact in2(input2_path, ArtifactType::Header);
    FileArtifact in3(input3_path, ArtifactType::Header);
    FileArtifact out(output_path, ArtifactType::Object);

    std::vector<const Artifact*> inputs = { &in1, &in2, &in3 };

    ConditionResult result = cond.evaluate(inputs, out);

    TEST_ASSERT_EQ(result.decision, ConditionResult::Skip);
    TEST_ASSERT_STR_EQ(result.reason.c_str(), "up-to-date");
    return 0;
}

// ---------------------------------------------------------------------------
// MSVC registration block (required on Windows with MSVC compiler)
// ---------------------------------------------------------------------------

#ifdef _MSC_VER
#include <windows.h>

static void __cdecl register_condition_tests(void) {
    REGISTER_TEST(freshness_output_not_exists_returns_build);
    REGISTER_TEST_SERIAL(freshness_forced_returns_build_even_when_up_to_date);
    REGISTER_TEST_SERIAL(freshness_input_newer_than_output_returns_build_outdated);
    REGISTER_TEST_SERIAL(freshness_one_of_many_inputs_newer_returns_outdated);
    REGISTER_TEST_SERIAL(freshness_all_inputs_older_returns_skip);
    REGISTER_TEST_SERIAL(freshness_empty_inputs_existing_output_returns_skip);
    REGISTER_TEST_SERIAL(freshness_identical_mtimes_returns_skip);
    REGISTER_TEST(freshness_forced_output_missing_returns_does_not_exist);
    REGISTER_TEST_SERIAL(freshness_multiple_inputs_all_older_returns_skip);
}

#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void(__cdecl* _reg_condition)(void) = register_condition_tests;
#endif
