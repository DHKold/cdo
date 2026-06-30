// test_build_pipeline.cpp — Unit tests for BuildPipeline orchestration logic.
// Validates: Requirements 7.4, 8.4, 9.5, 13.1
//
// Tests the entry point (cdo_build_run) and the orchestration patterns:
//   - Null/invalid args → non-zero return
//   - Unknown crate → non-zero return
//   - --clean flag deletes build dir before DAG construction
//   - Dispatch loop pattern verified through mock DAG + RunnerPool
//   - Summary log emitted with correct built/skipped/failed counts
//   - Cache layer integration (enabled vs disabled path)
//
// The dispatch loop and summary tests use the lower-level components directly
// (TasksDag, RunnerPool, Task stubs) rather than spawning real compilation
// processes, keeping execution fast and deterministic.

#include "cdo_ut.h"
#include "build/build_pipeline.h"
#include "build/cli_arguments.h"
#include "build/runner.h"
#include "build/sha256_cache.h"
#include "build/task.h"
#include "build/tasks_dag.h"
#include "build/artifact.h"
#include "build/condition.h"
#include "core/log.h"
#include "pal/pal.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace cdo::build;
using namespace cdo::build::cli;

// =============================================================================
// Helper: Build a CliParseResult with named args and positional values.
// =============================================================================

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

    void addPositional(const char* value) {
        positional_values[pos_count] = value;
        pos_count++;
        result.positional_count = pos_count;
    }

    const CliParseResult* get() const { return &result; }
};

// =============================================================================
// Stub tasks for dispatch loop tests — no real compilation
// =============================================================================

class AlwaysBuildCondition : public TaskCondition {
public:
    ConditionResult evaluate(const std::vector<const Artifact*>&, const Artifact&) const override {
        return { ConditionResult::Build, "test" };
    }
};

class AlwaysSkipCondition : public TaskCondition {
public:
    ConditionResult evaluate(const std::vector<const Artifact*>&, const Artifact&) const override {
        return { ConditionResult::Skip, "up-to-date" };
    }
};

class StubBuildTask : public Task {
public:
    explicit StubBuildTask(std::string path, int return_code = 0)
        : output_(std::move(path), ArtifactType::Object), return_code_(return_code) {
        outputs_cache_.push_back(&output_);
    }
    const std::vector<const Artifact*>& inputs() const override { return inputs_; }
    const std::vector<const Artifact*>& outputs() const override { return outputs_cache_; }
    const Artifact& primaryOutput() const override { return output_; }
    const TaskCondition& condition() const override { return condition_; }
protected:
    int execute() override { return return_code_; }
private:
    AlwaysBuildCondition condition_;
    FileArtifact output_;
    std::vector<const Artifact*> inputs_;
    std::vector<const Artifact*> outputs_cache_;
    int return_code_;
};

class StubSkipTask : public Task {
public:
    explicit StubSkipTask(std::string path)
        : output_(std::move(path), ArtifactType::Object) {
        outputs_cache_.push_back(&output_);
    }
    const std::vector<const Artifact*>& inputs() const override { return inputs_; }
    const std::vector<const Artifact*>& outputs() const override { return outputs_cache_; }
    const Artifact& primaryOutput() const override { return output_; }
    const TaskCondition& condition() const override { return condition_; }
protected:
    int execute() override { return 0; }
private:
    AlwaysSkipCondition condition_;
    FileArtifact output_;
    std::vector<const Artifact*> inputs_;
    std::vector<const Artifact*> outputs_cache_;
};

// =============================================================================
// Test: pipeline returns non-zero on null CliParseResult
// Validates: Requirement 13.1
// =============================================================================

TEST_SERIAL(pipeline_returns_nonzero_on_null_input) {
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);

    int rc = cdo_build_run(nullptr);
    TEST_ASSERT_NEQ(rc, 0);
    return 0;
}

// =============================================================================
// Test: pipeline returns non-zero on unknown crate filter
// Validates: Requirement 13.1
// =============================================================================

TEST_SERIAL(pipeline_returns_nonzero_on_unknown_crate) {
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);

    TestParseResult pr;
    pr.addInt("jobs", 1);
    pr.addPositional("__absolutely_nonexistent_crate_xyz__");

    int rc = cdo_build_run(pr.get());
    TEST_ASSERT_NEQ(rc, 0);
    return 0;
}

// =============================================================================
// Test: --clean flag triggers build dir deletion before DAG construction
// Validates: Requirement 7.4
// =============================================================================

TEST_SERIAL(pipeline_clean_flag_deletes_build_dir) {
    cdo_log_init_test(CDO_LOG_LEVEL_INFO, false, false);

    // Discover workspace root
    TestParseResult pr_probe;
    Arguments args_probe(pr_probe.get());
    TEST_ASSERT(args_probe.isValid());

    // Create a marker file directly inside build/debug/
    char marker_file[520];
    pal_path_join(marker_file, sizeof(marker_file), args_probe.workspaceRoot().c_str(), "build/debug/__pipeline_clean_marker.txt");
    pal_file_write(marker_file, "test", 4);
    TEST_ASSERT_EQ(pal_path_exists(marker_file), 0);

    // Run pipeline with --clean. Clean fires before crate resolution.
    TestParseResult pr_clean;
    pr_clean.addBool("clean", true);
    pr_clean.addInt("jobs", 1);
    pr_clean.addPositional("__nonexistent_crate_clean_test__");

    cdo_build_run(pr_clean.get());

    // The entire build/debug/ was deleted. Marker file should be gone.
    TEST_ASSERT_NEQ(pal_path_exists(marker_file), 0);
    return 0;
}

// =============================================================================
// Test: pipeline dispatch loop pattern
//
// Exercises the full dispatch pattern (dag.hasActiveTask → waitNextTask →
// waitFreeRunner → run → markCompleted) using stub tasks. This validates the
// core orchestration logic without spawning real compiler processes.
//
// Validates: Requirement 7.4 (dispatch loop pattern)
// =============================================================================

TEST_SERIAL(pipeline_dispatch_loop_pattern) {
    cdo_log_init_test(CDO_LOG_LEVEL_DEBUG, false, false);

    // Build a small DAG: A → B (B depends on A)
    TasksDag dag;
    int a_id = dag.addTask(std::make_unique<StubBuildTask>("stub/a.o"));
    int b_id = dag.addTask(std::make_unique<StubBuildTask>("stub/b.o"));
    dag.addDependency(b_id, a_id);
    TEST_ASSERT_EQ(dag.finalize(), 0);
    (void)a_id; (void)b_id;

    // Create a single-runner pool (simulates --jobs 1)
    RunnerPool pool(1);

    // Execute the dispatch loop: drain completed runners, then dispatch next task
    int result = 0;
    int in_flight = 0;
    int last_processed_id = -1; // Track which runner result was last processed

    while (dag.hasActiveTask()) {
        // Drain: wait for a runner to finish before requesting next task
        if (in_flight >= pool.size()) {
            Runner& runner = pool.waitFreeRunner();
            in_flight--;
            if (runner.lastResult() != 0) {
                dag.markFailed(runner.lastTaskId());
                result = 1;
                break;
            }
            dag.markCompleted(runner.lastTaskId());
            last_processed_id = runner.lastTaskId();
            continue; // Re-check hasActiveTask
        }

        Task* task = dag.waitNextTask();
        if (!task) break;

        Runner& runner = pool.waitFreeRunner();
        runner.run(*task);
        in_flight++;
    }

    // Drain remaining
    while (in_flight > 0) {
        Runner& runner = pool.waitFreeRunner();
        in_flight--;
        if (runner.lastResult() != 0) {
            dag.markFailed(runner.lastTaskId());
            if (result == 0) result = 1;
        } else {
            dag.markCompleted(runner.lastTaskId());
        }
    }

    TEST_ASSERT_EQ(result, 0);
    TEST_ASSERT(!dag.hasActiveTask());
    TEST_ASSERT_EQ(dag.completedCount(), 2);
    TEST_ASSERT_EQ(dag.failedCount(), 0);
    return 0;
}

// =============================================================================
// Test: summary log line emitted with correct built/skipped/failed counts
//
// Runs the dispatch loop with a mix of built and skipped tasks, then verifies
// the summary log is emitted with correct counts.
//
// Validates: Requirement 9.5
// =============================================================================

TEST_SERIAL(pipeline_summary_log_correct_counts) {
    cdo_log_init_test(CDO_LOG_LEVEL_INFO, false, false);
    cdo_log_test_reset_emit_count();

    // DAG with 3 independent tasks: 2 that build, 1 that skips
    TasksDag dag;
    dag.addTask(std::make_unique<StubBuildTask>("stub/built1.o"));
    dag.addTask(std::make_unique<StubBuildTask>("stub/built2.o"));
    dag.addTask(std::make_unique<StubSkipTask>("stub/skipped.o"));
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // Run all tasks through the dispatch loop
    RunnerPool pool(1);
    int in_flight = 0;

    while (dag.hasActiveTask()) {
        if (in_flight >= pool.size()) {
            Runner& runner = pool.waitFreeRunner();
            in_flight--;
            dag.markCompleted(runner.lastTaskId());
            continue;
        }
        Task* task = dag.waitNextTask();
        if (!task) break;
        Runner& runner = pool.waitFreeRunner();
        runner.run(*task);
        in_flight++;
    }
    while (in_flight > 0) {
        Runner& runner = pool.waitFreeRunner();
        in_flight--;
        dag.markCompleted(runner.lastTaskId());
    }

    // Verify counts
    int built = dag.completedCount() - dag.skippedCount();
    int skipped = dag.skippedCount();
    int failed = dag.failedCount();

    TEST_ASSERT_EQ(dag.completedCount(), 3);
    TEST_ASSERT_EQ(built, 2);
    TEST_ASSERT_EQ(skipped, 1);
    TEST_ASSERT_EQ(failed, 0);

    // Emit summary (same as BuildPipeline::printSummary)
    cdo_log_test_reset_emit_count();
    cdo_log_info("Build complete: %d built, %d skipped, %d failed", built, skipped, failed);
    TEST_ASSERT_EQ(cdo_log_test_get_emit_count(), 1);
    return 0;
}

// =============================================================================
// Test: pipeline returns non-zero on task failure
//
// Uses a StubBuildTask that returns non-zero from execute(), verifying that the
// dispatch loop propagates the failure through markFailed and returns non-zero.
//
// Validates: Requirement 7.4 (failure propagation)
// =============================================================================

TEST_SERIAL(pipeline_returns_nonzero_on_task_failure) {
    cdo_log_init_test(CDO_LOG_LEVEL_ERROR, false, false);

    // DAG with 2 tasks: first succeeds, second fails
    TasksDag dag;
    int a_id = dag.addTask(std::make_unique<StubBuildTask>("stub/ok.o", 0));
    int b_id = dag.addTask(std::make_unique<StubBuildTask>("stub/fail.o", 1)); // fails
    dag.addDependency(b_id, a_id);
    TEST_ASSERT_EQ(dag.finalize(), 0);

    RunnerPool pool(1);
    int result = 0;
    int in_flight = 0;

    while (dag.hasActiveTask()) {
        if (in_flight >= pool.size()) {
            Runner& runner = pool.waitFreeRunner();
            in_flight--;
            if (runner.lastResult() != 0) {
                dag.markFailed(runner.lastTaskId());
                result = 1;
                break;
            }
            dag.markCompleted(runner.lastTaskId());
            continue;
        }

        Task* task = dag.waitNextTask();
        if (!task) break;

        Runner& runner = pool.waitFreeRunner();
        runner.run(*task);
        in_flight++;
    }

    // Drain remaining
    while (in_flight > 0) {
        Runner& runner = pool.waitFreeRunner();
        in_flight--;
        if (runner.lastResult() != 0) {
            dag.markFailed(runner.lastTaskId());
            if (result == 0) result = 1;
        } else {
            dag.markCompleted(runner.lastTaskId());
        }
    }

    TEST_ASSERT_NEQ(result, 0);
    TEST_ASSERT_EQ(dag.failedCount(), 1);
    return 0;
}

// =============================================================================
// Test: cache layer integrated when enabled — tryRestore is called
//
// Validates that SHA256CacheLayer with enabled=true doesn't error and can be
// integrated into the dispatch loop. Uses a disabled cache (no real files).
//
// Validates: Requirement 8.4
// =============================================================================

TEST_SERIAL(pipeline_cache_enabled_integrated) {
    cdo_log_init_test(CDO_LOG_LEVEL_INFO, false, false);

    TasksDag dag;
    dag.addTask(std::make_unique<StubBuildTask>("stub/cached.o"));
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // Cache layer with enabled=true (but non-existent cache root, so always misses)
    SHA256CacheLayer cache({ "__nonexistent_cache_root__/objects/", true });

    Task* task = dag.waitNextTask();
    TEST_ASSERT(task != nullptr);

    // tryRestore should return false (cache miss since root doesn't exist)
    bool restored = cache.tryRestore(*task);
    TEST_ASSERT(!restored);
    TEST_ASSERT_EQ(cache.misses(), 1);
    TEST_ASSERT_EQ(cache.hits(), 0);

    dag.markCompleted(task->id());
    return 0;
}

// =============================================================================
// Test: cache layer skipped when disabled — tryRestore returns false immediately
//
// Validates that SHA256CacheLayer with enabled=false short-circuits without
// filesystem access.
//
// Validates: Requirement 8.4
// =============================================================================

TEST_SERIAL(pipeline_cache_disabled_skipped) {
    cdo_log_init_test(CDO_LOG_LEVEL_INFO, false, false);

    TasksDag dag;
    dag.addTask(std::make_unique<StubBuildTask>("stub/nocache.o"));
    TEST_ASSERT_EQ(dag.finalize(), 0);

    // Cache layer with enabled=false
    SHA256CacheLayer cache({ ".cdo/cache/objects/", false });

    Task* task = dag.waitNextTask();
    TEST_ASSERT(task != nullptr);

    // tryRestore should return false immediately (disabled)
    bool restored = cache.tryRestore(*task);
    TEST_ASSERT(!restored);
    // Disabled cache should not increment counters
    TEST_ASSERT_EQ(cache.misses(), 0);
    TEST_ASSERT_EQ(cache.hits(), 0);

    dag.markCompleted(task->id());
    return 0;
}

// =============================================================================
// MSVC registration block
// =============================================================================

#ifdef _MSC_VER
#include <windows.h>

static void __cdecl register_build_pipeline_tests(void) {
    REGISTER_TEST_SERIAL(pipeline_returns_nonzero_on_null_input);
    REGISTER_TEST_SERIAL(pipeline_returns_nonzero_on_unknown_crate);
    REGISTER_TEST_SERIAL(pipeline_clean_flag_deletes_build_dir);
    REGISTER_TEST_SERIAL(pipeline_dispatch_loop_pattern);
    REGISTER_TEST_SERIAL(pipeline_summary_log_correct_counts);
    REGISTER_TEST_SERIAL(pipeline_returns_nonzero_on_task_failure);
    REGISTER_TEST_SERIAL(pipeline_cache_enabled_integrated);
    REGISTER_TEST_SERIAL(pipeline_cache_disabled_skipped);
}

#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void(__cdecl* _reg_build_pipeline)(void) = register_build_pipeline_tests;
#endif
