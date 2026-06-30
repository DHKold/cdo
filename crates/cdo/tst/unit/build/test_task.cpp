// test_task.cpp — Unit tests for Task::run() base logic (template method pattern).
// Validates: Requirements 3.2, 9.1, 9.2
//
// Uses a minimal MockTask with configurable condition and execute() behavior
// to verify that:
//   - Condition Skip → execute() not called, wasSkipped()=true, run() returns 0
//   - Condition Build → execute() called exactly once
//   - execute() non-zero → run() returns that non-zero code
//   - INFO log emitted on Build, DEBUG log emitted on Skip

#include "cdo_ut.h"
#include "build/task.h"
#include "build/artifact.h"
#include "build/condition.h"
#include "core/log.h"

#include <string>
#include <vector>

using namespace cdo::build;

// ---------------------------------------------------------------------------
// MockCondition: always returns a pre-configured ConditionResult
// ---------------------------------------------------------------------------

class MockCondition : public TaskCondition {
public:
    MockCondition(ConditionResult::Decision decision, std::string reason)
        : result_{ decision, std::move(reason) } {}

    ConditionResult evaluate(const std::vector<const Artifact*>& /*inputs*/, const Artifact& /*primary_output*/) const override {
        return result_;
    }

private:
    ConditionResult result_;
};

// ---------------------------------------------------------------------------
// MockTask: minimal concrete Task that tracks execute() calls
// ---------------------------------------------------------------------------

class MockTask : public Task {
public:
    MockTask(ConditionResult::Decision decision, std::string reason, int execute_return_code = 0)
        : condition_(decision, std::move(reason))
        , output_("fake/output.o", ArtifactType::Object)
        , execute_return_code_(execute_return_code)
        , execute_call_count_(0) {
        outputs_cache_.push_back(&output_);
    }

    const std::vector<const Artifact*>& inputs() const override { return inputs_; }
    const std::vector<const Artifact*>& outputs() const override { return outputs_cache_; }
    const Artifact& primaryOutput() const override { return output_; }
    const TaskCondition& condition() const override { return condition_; }

    int executeCallCount() const { return execute_call_count_; }

protected:
    int execute() override {
        execute_call_count_++;
        return execute_return_code_;
    }

private:
    MockCondition condition_;
    FileArtifact output_;
    std::vector<const Artifact*> inputs_;
    std::vector<const Artifact*> outputs_cache_;
    int execute_return_code_;
    int execute_call_count_;
};

// ---------------------------------------------------------------------------
// Test: condition returns Skip → execute() not called, wasSkipped()=true, run()=0
// ---------------------------------------------------------------------------

TEST(task_run_skip_does_not_call_execute) {
    MockTask task(ConditionResult::Skip, "up-to-date");

    int rc = task.run();

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(task.executeCallCount(), 0);
    TEST_ASSERT(task.wasSkipped());
    return 0;
}

// ---------------------------------------------------------------------------
// Test: condition returns Build → execute() called exactly once
// ---------------------------------------------------------------------------

TEST(task_run_build_calls_execute_once) {
    MockTask task(ConditionResult::Build, "does not exist");

    int rc = task.run();

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(task.executeCallCount(), 1);
    TEST_ASSERT(!task.wasSkipped());
    return 0;
}

// ---------------------------------------------------------------------------
// Test: execute() returns non-zero → run() returns that non-zero code
// ---------------------------------------------------------------------------

TEST(task_run_execute_failure_propagates_return_code) {
    MockTask task(ConditionResult::Build, "outdated", 42);

    int rc = task.run();

    TEST_ASSERT_EQ(rc, 42);
    TEST_ASSERT_EQ(task.executeCallCount(), 1);
    TEST_ASSERT(!task.wasSkipped());
    return 0;
}

// ---------------------------------------------------------------------------
// Test: INFO log emitted on Build (emit count increments at INFO level)
// ---------------------------------------------------------------------------

TEST_SERIAL(task_run_build_emits_info_log) {
    cdo_log_init_test(CDO_LOG_LEVEL_INFO, false, false);
    cdo_log_test_reset_emit_count();

    MockTask task(ConditionResult::Build, "does not exist");
    task.run();

    // At INFO level, the "Building: ..." message should be emitted
    TEST_ASSERT(cdo_log_test_get_emit_count() >= 1);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: DEBUG log emitted on Skip (emit count increments at DEBUG level)
// ---------------------------------------------------------------------------

TEST_SERIAL(task_run_skip_emits_debug_log) {
    cdo_log_init_test(CDO_LOG_LEVEL_DEBUG, false, false);
    cdo_log_test_reset_emit_count();

    MockTask task(ConditionResult::Skip, "up-to-date");
    task.run();

    // At DEBUG level, the "Up-to-date: ..." message should be emitted
    TEST_ASSERT(cdo_log_test_get_emit_count() >= 1);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: Skip log is suppressed at INFO level (only DEBUG and above see it)
// ---------------------------------------------------------------------------

TEST_SERIAL(task_run_skip_log_suppressed_at_info_level) {
    cdo_log_init_test(CDO_LOG_LEVEL_INFO, false, false);
    cdo_log_test_reset_emit_count();

    MockTask task(ConditionResult::Skip, "up-to-date");
    task.run();

    // At INFO level, the DEBUG "Up-to-date" message should NOT be emitted
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: id() and setId() accessors
// ---------------------------------------------------------------------------

TEST(task_id_default_is_minus_one) {
    MockTask task(ConditionResult::Skip, "up-to-date");
    TEST_ASSERT_EQ(task.id(), -1);
    return 0;
}

TEST(task_set_id_updates_value) {
    MockTask task(ConditionResult::Skip, "up-to-date");
    task.setId(7);
    TEST_ASSERT_EQ(task.id(), 7);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: wasSkipped() is false before run() is called
// ---------------------------------------------------------------------------

TEST(task_was_skipped_false_before_run) {
    MockTask task(ConditionResult::Skip, "up-to-date");
    TEST_ASSERT(!task.wasSkipped());
    return 0;
}

// ---------------------------------------------------------------------------
// MSVC registration block
// ---------------------------------------------------------------------------

#ifdef _MSC_VER
#include <windows.h>

static void __cdecl register_task_tests(void) {
    REGISTER_TEST(task_run_skip_does_not_call_execute);
    REGISTER_TEST(task_run_build_calls_execute_once);
    REGISTER_TEST(task_run_execute_failure_propagates_return_code);
    REGISTER_TEST_SERIAL(task_run_build_emits_info_log);
    REGISTER_TEST_SERIAL(task_run_skip_emits_debug_log);
    REGISTER_TEST_SERIAL(task_run_skip_log_suppressed_at_info_level);
    REGISTER_TEST(task_id_default_is_minus_one);
    REGISTER_TEST(task_set_id_updates_value);
    REGISTER_TEST(task_was_skipped_false_before_run);
}

#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void(__cdecl* _reg_task)(void) = register_task_tests;
#endif
