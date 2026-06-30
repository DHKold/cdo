// test_runner.cpp — Unit tests for ThreadRunner and RunnerPool.
// Validates: Requirements 2.1, 2.2, 2.3, 2.4, 2.5
//
// Uses a SlowTask that sleeps for a short duration to verify async dispatch:
//   - run() returns immediately (caller thread continues)
//   - Task executes on a different thread (compare thread IDs)
//   - isIdle() transitions: true initially → false after run() → true after wait()
//   - lastResult() returns the task's return code
//   - RunnerPool(N) creates exactly N runners
//   - waitFreeRunner() blocks when all runners busy, returns when one finishes

#include "cdo_ut.h"
#include "build/runner.h"
#include "build/task.h"
#include "build/artifact.h"
#include "build/condition.h"
#include "core/log.h"

#include <thread>
#include <chrono>
#include <atomic>

using namespace cdo::build;

// ---------------------------------------------------------------------------
// AlwaysBuildCondition: returns Build so execute() always runs
// ---------------------------------------------------------------------------

class AlwaysBuildCondition : public TaskCondition {
public:
    ConditionResult evaluate(const std::vector<const Artifact*>& /*inputs*/, const Artifact& /*primary_output*/) const override {
        return { ConditionResult::Build, "test" };
    }
};

// ---------------------------------------------------------------------------
// SlowTask: sleeps for a given duration, records which thread executed it
// ---------------------------------------------------------------------------

class SlowTask : public Task {
public:
    explicit SlowTask(int sleep_ms, int return_code = 0)
        : output_("fake/slow_output.o", ArtifactType::Object)
        , sleep_ms_(sleep_ms)
        , return_code_(return_code)
        , executed_thread_id_(std::thread::id{}) {
        outputs_cache_.push_back(&output_);
    }

    const std::vector<const Artifact*>& inputs() const override { return inputs_; }
    const std::vector<const Artifact*>& outputs() const override { return outputs_cache_; }
    const Artifact& primaryOutput() const override { return output_; }
    const TaskCondition& condition() const override { return condition_; }

    std::thread::id executedThreadId() const { return executed_thread_id_; }

protected:
    int execute() override {
        executed_thread_id_ = std::this_thread::get_id();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms_));
        return return_code_;
    }

private:
    AlwaysBuildCondition condition_;
    FileArtifact output_;
    std::vector<const Artifact*> inputs_;
    std::vector<const Artifact*> outputs_cache_;
    int sleep_ms_;
    int return_code_;
    std::thread::id executed_thread_id_;
};

// ---------------------------------------------------------------------------
// Test: ThreadRunner run() returns before task completes (async dispatch)
// ---------------------------------------------------------------------------

TEST_SERIAL(runner_run_returns_before_task_completes) {
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);

    ThreadRunner runner;
    SlowTask task(100);  // 100ms sleep
    task.setId(1);

    auto start = std::chrono::steady_clock::now();
    runner.run(task);
    auto after_run = std::chrono::steady_clock::now();

    // run() should return almost immediately (well before the 100ms task)
    auto dispatch_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(after_run - start).count();
    TEST_ASSERT(dispatch_time_ms < 50);

    runner.wait();  // Clean up
    return 0;
}

// ---------------------------------------------------------------------------
// Test: ThreadRunner executes task on a different thread than caller
// ---------------------------------------------------------------------------

TEST_SERIAL(runner_executes_on_different_thread) {
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);

    ThreadRunner runner;
    SlowTask task(10);
    task.setId(2);

    std::thread::id caller_thread = std::this_thread::get_id();
    runner.run(task);
    runner.wait();

    TEST_ASSERT(task.executedThreadId() != std::thread::id{});
    TEST_ASSERT(task.executedThreadId() != caller_thread);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: isIdle() is false during execution, true after wait()
// ---------------------------------------------------------------------------

TEST_SERIAL(runner_idle_transitions) {
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);

    ThreadRunner runner;

    // Initially idle
    TEST_ASSERT(runner.isIdle());

    SlowTask task(80);  // 80ms sleep
    task.setId(3);
    runner.run(task);

    // Give the worker a moment to pick up the task — but run() should have
    // already set idle=false before returning.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    TEST_ASSERT(!runner.isIdle());

    runner.wait();

    // After wait(), should be idle again
    TEST_ASSERT(runner.isIdle());
    return 0;
}

// ---------------------------------------------------------------------------
// Test: lastResult() reflects task return code after completion
// ---------------------------------------------------------------------------

TEST_SERIAL(runner_last_result_reflects_return_code) {
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);

    ThreadRunner runner;

    // Run a task that returns 0
    SlowTask task_ok(10, 0);
    task_ok.setId(4);
    runner.run(task_ok);
    runner.wait();
    TEST_ASSERT_EQ(runner.lastResult(), 0);
    TEST_ASSERT_EQ(runner.lastTaskId(), 4);

    // Run a task that returns non-zero
    SlowTask task_fail(10, 7);
    task_fail.setId(5);
    runner.run(task_fail);
    runner.wait();
    TEST_ASSERT_EQ(runner.lastResult(), 7);
    TEST_ASSERT_EQ(runner.lastTaskId(), 5);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: RunnerPool(N) creates exactly N runners
// ---------------------------------------------------------------------------

TEST_SERIAL(runner_pool_creates_n_runners) {
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);

    RunnerPool pool1(1);
    TEST_ASSERT_EQ(pool1.size(), 1);

    RunnerPool pool4(4);
    TEST_ASSERT_EQ(pool4.size(), 4);

    RunnerPool pool8(8);
    TEST_ASSERT_EQ(pool8.size(), 8);

    return 0;
}

// ---------------------------------------------------------------------------
// Test: waitFreeRunner() blocks when all runners busy, returns when one finishes
// ---------------------------------------------------------------------------

TEST_SERIAL(runner_pool_wait_free_runner_blocks_and_returns) {
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);

    RunnerPool pool(2);  // 2 runners

    // Fill both runners with slow tasks
    SlowTask task1(100);
    task1.setId(10);
    SlowTask task2(100);
    task2.setId(11);

    Runner& r1 = pool.waitFreeRunner();
    r1.run(task1);

    Runner& r2 = pool.waitFreeRunner();
    r2.run(task2);

    // Both runners are now busy. waitFreeRunner() should block until one finishes.
    auto start = std::chrono::steady_clock::now();
    Runner& r3 = pool.waitFreeRunner();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    // It should have waited for at least some time (tasks sleep 100ms)
    TEST_ASSERT(elapsed_ms >= 30);

    // The returned runner should be idle
    TEST_ASSERT(r3.isIdle());

    // Run one more task to verify the runner works after being freed
    SlowTask task3(10);
    task3.setId(12);
    r3.run(task3);
    r3.wait();
    TEST_ASSERT_EQ(r3.lastResult(), 0);

    // Wait for all tasks to complete
    r1.wait();
    r2.wait();

    return 0;
}

// ---------------------------------------------------------------------------
// Test: lastTaskId() is -1 before any task runs
// ---------------------------------------------------------------------------

TEST_SERIAL(runner_last_task_id_initial_value) {
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);

    ThreadRunner runner;
    TEST_ASSERT_EQ(runner.lastTaskId(), -1);
    return 0;
}

// ---------------------------------------------------------------------------
// MSVC registration block
// ---------------------------------------------------------------------------

#ifdef _MSC_VER
#include <windows.h>

static void __cdecl register_runner_tests(void) {
    REGISTER_TEST_SERIAL(runner_run_returns_before_task_completes);
    REGISTER_TEST_SERIAL(runner_executes_on_different_thread);
    REGISTER_TEST_SERIAL(runner_idle_transitions);
    REGISTER_TEST_SERIAL(runner_last_result_reflects_return_code);
    REGISTER_TEST_SERIAL(runner_pool_creates_n_runners);
    REGISTER_TEST_SERIAL(runner_pool_wait_free_runner_blocks_and_returns);
    REGISTER_TEST_SERIAL(runner_last_task_id_initial_value);
}

#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void(__cdecl* _reg_runner)(void) = register_runner_tests;
#endif
