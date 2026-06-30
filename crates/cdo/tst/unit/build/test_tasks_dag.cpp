// test_tasks_dag.cpp — Unit tests for TasksDag (dependency-aware task dispatch).
// Validates: Requirements 4.1, 4.2, 4.3, 4.6, 4.7
//
// Uses a StubTask with configurable output path and an AlwaysBuild condition
// so tests focus purely on DAG scheduling semantics without freshness concerns.
// Tests build small DAGs (2-5 tasks), call finalize(), then simulate dispatch.

#include "cdo_ut.h"
#include "build/tasks_dag.h"
#include "build/task.h"
#include "build/artifact.h"
#include "build/condition.h"

#include <memory>
#include <string>
#include <vector>

using namespace cdo::build;

// ---------------------------------------------------------------------------
// AlwaysBuildCondition: always returns Build so tests don't worry about freshness
// ---------------------------------------------------------------------------

class AlwaysBuildCondition : public TaskCondition {
public:
    ConditionResult evaluate(const std::vector<const Artifact*>& /*inputs*/, const Artifact& /*primary_output*/) const override {
        return { ConditionResult::Build, "test" };
    }
};

// ---------------------------------------------------------------------------
// StubTask: minimal Task implementation for DAG tests
// ---------------------------------------------------------------------------

class StubTask : public Task {
public:
    explicit StubTask(std::string output_path)
        : output_(std::move(output_path), ArtifactType::Object) {
        outputs_cache_.push_back(&output_);
    }

    const std::vector<const Artifact*>& inputs() const override { return inputs_; }
    const std::vector<const Artifact*>& outputs() const override { return outputs_cache_; }
    const Artifact& primaryOutput() const override { return output_; }
    const TaskCondition& condition() const override { return condition_; }

protected:
    int execute() override { return 0; }

private:
    AlwaysBuildCondition condition_;
    FileArtifact output_;
    std::vector<const Artifact*> inputs_;
    std::vector<const Artifact*> outputs_cache_;
};

// ---------------------------------------------------------------------------
// Helper: create a StubTask with a unique output path
// ---------------------------------------------------------------------------

static std::unique_ptr<Task> make_stub(const char* name) {
    return std::make_unique<StubTask>(std::string("stub/") + name + ".o");
}

// ---------------------------------------------------------------------------
// Test: linear chain A→B→C dispatches in order
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_linear_chain_dispatches_in_order) {
    TasksDag dag;

    int a = dag.addTask(make_stub("A"));
    int b = dag.addTask(make_stub("B"));
    int c = dag.addTask(make_stub("C"));

    dag.addDependency(b, a); // B depends on A
    dag.addDependency(c, b); // C depends on B

    TEST_ASSERT_EQ(dag.finalize(), 0);
    TEST_ASSERT(dag.hasActiveTask());

    // Only A should be ready first
    Task* t1 = dag.waitNextTask();
    TEST_ASSERT(t1 != nullptr);
    TEST_ASSERT_EQ(t1->id(), a);

    // B and C should not be available yet (no more ready tasks without completing A)
    // Complete A to unblock B
    dag.markCompleted(a);

    Task* t2 = dag.waitNextTask();
    TEST_ASSERT(t2 != nullptr);
    TEST_ASSERT_EQ(t2->id(), b);

    // Complete B to unblock C
    dag.markCompleted(b);

    Task* t3 = dag.waitNextTask();
    TEST_ASSERT(t3 != nullptr);
    TEST_ASSERT_EQ(t3->id(), c);

    dag.markCompleted(c);

    // All done
    TEST_ASSERT(!dag.hasActiveTask());
    return 0;
}

// ---------------------------------------------------------------------------
// Test: diamond dependency (A→B, A→C, B→D, C→D) allows B and C in parallel
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_diamond_allows_parallel_dispatch) {
    TasksDag dag;

    int a = dag.addTask(make_stub("A"));
    int b = dag.addTask(make_stub("B"));
    int c = dag.addTask(make_stub("C"));
    int d = dag.addTask(make_stub("D"));

    dag.addDependency(b, a); // B depends on A
    dag.addDependency(c, a); // C depends on A
    dag.addDependency(d, b); // D depends on B
    dag.addDependency(d, c); // D depends on C

    TEST_ASSERT_EQ(dag.finalize(), 0);

    // A is the only ready task
    Task* t1 = dag.waitNextTask();
    TEST_ASSERT(t1 != nullptr);
    TEST_ASSERT_EQ(t1->id(), a);

    // Complete A — B and C should both become ready
    dag.markCompleted(a);

    Task* t2 = dag.waitNextTask();
    TEST_ASSERT(t2 != nullptr);

    Task* t3 = dag.waitNextTask();
    TEST_ASSERT(t3 != nullptr);

    // t2 and t3 should be B and C (in some order)
    bool got_b = (t2->id() == b || t3->id() == b);
    bool got_c = (t2->id() == c || t3->id() == c);
    TEST_ASSERT(got_b);
    TEST_ASSERT(got_c);

    // D should not be available until both B and C complete
    dag.markCompleted(b);
    dag.markCompleted(c);

    Task* t4 = dag.waitNextTask();
    TEST_ASSERT(t4 != nullptr);
    TEST_ASSERT_EQ(t4->id(), d);

    dag.markCompleted(d);
    TEST_ASSERT(!dag.hasActiveTask());
    return 0;
}

// ---------------------------------------------------------------------------
// Test: finalize() detects cycle and returns non-zero
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_finalize_detects_cycle) {
    TasksDag dag;

    int a = dag.addTask(make_stub("A"));
    int b = dag.addTask(make_stub("B"));

    dag.addDependency(b, a); // B depends on A
    dag.addDependency(a, b); // A depends on B — cycle!

    int rc = dag.finalize();
    TEST_ASSERT_NEQ(rc, 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: finalize() detects 3-node cycle
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_finalize_detects_three_node_cycle) {
    TasksDag dag;

    int a = dag.addTask(make_stub("A"));
    int b = dag.addTask(make_stub("B"));
    int c = dag.addTask(make_stub("C"));

    dag.addDependency(b, a); // B depends on A
    dag.addDependency(c, b); // C depends on B
    dag.addDependency(a, c); // A depends on C — cycle!

    int rc = dag.finalize();
    TEST_ASSERT_NEQ(rc, 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: waitNextTask() only returns tasks with all deps completed
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_wait_next_task_respects_dependencies) {
    TasksDag dag;

    int a = dag.addTask(make_stub("A"));
    int b = dag.addTask(make_stub("B"));
    int c = dag.addTask(make_stub("C"));

    // C depends on both A and B
    dag.addDependency(c, a);
    dag.addDependency(c, b);

    TEST_ASSERT_EQ(dag.finalize(), 0);

    // A and B should both be ready (no deps)
    Task* t1 = dag.waitNextTask();
    Task* t2 = dag.waitNextTask();
    TEST_ASSERT(t1 != nullptr);
    TEST_ASSERT(t2 != nullptr);

    // t1 and t2 are A and B in some order
    bool got_a = (t1->id() == a || t2->id() == a);
    bool got_b = (t1->id() == b || t2->id() == b);
    TEST_ASSERT(got_a);
    TEST_ASSERT(got_b);

    // Complete only A — C should still not be ready (needs B too)
    dag.markCompleted(a);

    // Now complete B — C should become ready
    dag.markCompleted(b);

    Task* t3 = dag.waitNextTask();
    TEST_ASSERT(t3 != nullptr);
    TEST_ASSERT_EQ(t3->id(), c);

    dag.markCompleted(c);
    TEST_ASSERT(!dag.hasActiveTask());
    return 0;
}

// ---------------------------------------------------------------------------
// Test: markCompleted() unblocks dependent tasks
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_mark_completed_unblocks_dependents) {
    TasksDag dag;

    int a = dag.addTask(make_stub("A"));
    int b = dag.addTask(make_stub("B"));

    dag.addDependency(b, a); // B depends on A

    TEST_ASSERT_EQ(dag.finalize(), 0);

    Task* t1 = dag.waitNextTask();
    TEST_ASSERT(t1 != nullptr);
    TEST_ASSERT_EQ(t1->id(), a);

    // Before completing A, B should not be dispatchable
    // After completing A, B should become available
    dag.markCompleted(a);

    Task* t2 = dag.waitNextTask();
    TEST_ASSERT(t2 != nullptr);
    TEST_ASSERT_EQ(t2->id(), b);

    dag.markCompleted(b);
    TEST_ASSERT(!dag.hasActiveTask());
    return 0;
}

// ---------------------------------------------------------------------------
// Test: markFailed() causes hasActiveTask()=false, waitNextTask()=nullptr
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_mark_failed_terminates_dispatch) {
    TasksDag dag;

    int a = dag.addTask(make_stub("A"));
    int b = dag.addTask(make_stub("B"));

    dag.addDependency(b, a); // B depends on A

    TEST_ASSERT_EQ(dag.finalize(), 0);

    Task* t1 = dag.waitNextTask();
    TEST_ASSERT(t1 != nullptr);
    TEST_ASSERT_EQ(t1->id(), a);

    // Mark A as failed
    dag.markFailed(a);

    // hasActiveTask should now return false
    TEST_ASSERT(!dag.hasActiveTask());

    // waitNextTask should return nullptr
    Task* t2 = dag.waitNextTask();
    TEST_ASSERT_NULL(t2);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: hasActiveTask() returns true while tasks pending/running, false when all done
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_has_active_task_lifecycle) {
    TasksDag dag;

    int a = dag.addTask(make_stub("A"));
    int b = dag.addTask(make_stub("B"));

    dag.addDependency(b, a);

    TEST_ASSERT_EQ(dag.finalize(), 0);

    // Before any dispatch: pending tasks exist
    TEST_ASSERT(dag.hasActiveTask());

    // Dispatch A (now Running)
    Task* t1 = dag.waitNextTask();
    TEST_ASSERT(t1 != nullptr);
    TEST_ASSERT(dag.hasActiveTask()); // B is still pending, A is running

    // Complete A — B becomes ready
    dag.markCompleted(a);
    TEST_ASSERT(dag.hasActiveTask()); // B is ready

    // Dispatch B (now Running)
    Task* t2 = dag.waitNextTask();
    TEST_ASSERT(t2 != nullptr);
    TEST_ASSERT(dag.hasActiveTask()); // B is running

    // Complete B — all done
    dag.markCompleted(b);
    TEST_ASSERT(!dag.hasActiveTask());
    return 0;
}

// ---------------------------------------------------------------------------
// Test: count accessors match actual state
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_count_accessors_match_state) {
    TasksDag dag;

    int a = dag.addTask(make_stub("A"));
    int b = dag.addTask(make_stub("B"));
    int c = dag.addTask(make_stub("C"));

    dag.addDependency(b, a);
    dag.addDependency(c, a);

    TEST_ASSERT_EQ(dag.finalize(), 0);

    // Initial state
    TEST_ASSERT_EQ(dag.totalCount(), 3);
    TEST_ASSERT_EQ(dag.completedCount(), 0);
    TEST_ASSERT_EQ(dag.skippedCount(), 0);
    TEST_ASSERT_EQ(dag.failedCount(), 0);

    // Dispatch and complete A
    Task* t1 = dag.waitNextTask();
    TEST_ASSERT(t1 != nullptr);
    dag.markCompleted(a);
    TEST_ASSERT_EQ(dag.completedCount(), 1);

    // Dispatch B and mark it failed
    Task* t2 = dag.waitNextTask();
    TEST_ASSERT(t2 != nullptr);
    dag.markFailed(b);
    TEST_ASSERT_EQ(dag.failedCount(), 1);
    TEST_ASSERT_EQ(dag.completedCount(), 1);

    // Total should still be 3
    TEST_ASSERT_EQ(dag.totalCount(), 3);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: single task with no deps dispatches immediately
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_single_task_dispatches_immediately) {
    TasksDag dag;

    int a = dag.addTask(make_stub("A"));

    TEST_ASSERT_EQ(dag.finalize(), 0);
    TEST_ASSERT(dag.hasActiveTask());

    Task* t1 = dag.waitNextTask();
    TEST_ASSERT(t1 != nullptr);
    TEST_ASSERT_EQ(t1->id(), a);

    dag.markCompleted(a);
    TEST_ASSERT(!dag.hasActiveTask());
    return 0;
}

// ---------------------------------------------------------------------------
// Test: empty DAG — hasActiveTask returns false, finalize returns 0
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_empty_dag_has_no_active_tasks) {
    TasksDag dag;
    TEST_ASSERT_EQ(dag.finalize(), 0);
    TEST_ASSERT(!dag.hasActiveTask());
    TEST_ASSERT_EQ(dag.totalCount(), 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: addTask assigns sequential IDs starting from 0
// ---------------------------------------------------------------------------

TEST_SERIAL(dag_add_task_assigns_sequential_ids) {
    TasksDag dag;

    int a = dag.addTask(make_stub("A"));
    int b = dag.addTask(make_stub("B"));
    int c = dag.addTask(make_stub("C"));

    TEST_ASSERT_EQ(a, 0);
    TEST_ASSERT_EQ(b, 1);
    TEST_ASSERT_EQ(c, 2);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: skippedCount tracks tasks that were skipped
// Uses a SkipCondition stub task
// ---------------------------------------------------------------------------

class AlwaysSkipCondition : public TaskCondition {
public:
    ConditionResult evaluate(const std::vector<const Artifact*>& /*inputs*/, const Artifact& /*primary_output*/) const override {
        return { ConditionResult::Skip, "up-to-date" };
    }
};

class SkipStubTask : public Task {
public:
    explicit SkipStubTask(std::string output_path)
        : output_(std::move(output_path), ArtifactType::Object) {
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

TEST_SERIAL(dag_skipped_count_tracks_skipped_tasks) {
    TasksDag dag;

    auto skip_task = std::make_unique<SkipStubTask>("stub/skip.o");
    int a = dag.addTask(std::move(skip_task));

    TEST_ASSERT_EQ(dag.finalize(), 0);

    Task* t1 = dag.waitNextTask();
    TEST_ASSERT(t1 != nullptr);

    // Run the task — it will skip due to AlwaysSkipCondition
    int rc = t1->run();
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(t1->wasSkipped());

    // Mark completed — skippedCount should increment
    dag.markCompleted(a);
    TEST_ASSERT_EQ(dag.skippedCount(), 1);
    TEST_ASSERT_EQ(dag.completedCount(), 1);
    return 0;
}

// ---------------------------------------------------------------------------
// MSVC registration block
// ---------------------------------------------------------------------------

#ifdef _MSC_VER
#include <windows.h>

static void __cdecl register_tasks_dag_tests(void) {
    REGISTER_TEST_SERIAL(dag_linear_chain_dispatches_in_order);
    REGISTER_TEST_SERIAL(dag_diamond_allows_parallel_dispatch);
    REGISTER_TEST_SERIAL(dag_finalize_detects_cycle);
    REGISTER_TEST_SERIAL(dag_finalize_detects_three_node_cycle);
    REGISTER_TEST_SERIAL(dag_wait_next_task_respects_dependencies);
    REGISTER_TEST_SERIAL(dag_mark_completed_unblocks_dependents);
    REGISTER_TEST_SERIAL(dag_mark_failed_terminates_dispatch);
    REGISTER_TEST_SERIAL(dag_has_active_task_lifecycle);
    REGISTER_TEST_SERIAL(dag_count_accessors_match_state);
    REGISTER_TEST_SERIAL(dag_single_task_dispatches_immediately);
    REGISTER_TEST_SERIAL(dag_empty_dag_has_no_active_tasks);
    REGISTER_TEST_SERIAL(dag_add_task_assigns_sequential_ids);
    REGISTER_TEST_SERIAL(dag_skipped_count_tracks_skipped_tasks);
}

#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void(__cdecl* _reg_tasks_dag)(void) = register_tasks_dag_tests;
#endif
