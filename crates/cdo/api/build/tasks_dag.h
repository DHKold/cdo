/**
 * tasks_dag.h - Directed acyclic graph of build tasks.
 *
 * Defines the TasksDag class that manages build tasks as nodes and dependency
 * relationships as directed edges. Provides thread-safe dispatch of ready tasks
 * (those with all dependencies satisfied) and failure propagation.
 *
 * The main build loop uses this class as:
 *   while (dag.hasActiveTask()) {
 *       Task* task = dag.waitNextTask();
 *       Runner& runner = pool.waitFreeRunner();
 *       runner.run(*task);
 *   }
 *
 * Thread safety: all public methods are safe to call from multiple threads.
 * Internally uses mutex + condition_variable for synchronization.
 *
 * Part of the cdo::build pipeline refactor.
 */
#ifndef CDO_BUILD_TASKS_DAG_H
#define CDO_BUILD_TASKS_DAG_H

#include <memory>

namespace cdo::build {

// Forward declaration — full definition in task.h
class Task;

/// Directed acyclic graph of build tasks with dependency-aware dispatch.
///
/// Tasks are added via addTask(), dependency edges via addDependency(), then
/// finalize() validates the graph and seeds the ready set. The dispatch loop
/// uses waitNextTask() and hasActiveTask() to consume tasks in dependency order.
///
/// After a task completes execution, the caller must invoke markCompleted() or
/// markFailed() to update the graph state and unblock dependents.
class TasksDag {
public:
    TasksDag();
    ~TasksDag();

    // Non-copyable, non-movable (owns thread-sync primitives)
    TasksDag(const TasksDag&) = delete;
    TasksDag& operator=(const TasksDag&) = delete;
    TasksDag(TasksDag&&) = delete;
    TasksDag& operator=(TasksDag&&) = delete;

    /// Add a task to the DAG. Returns the assigned task ID (sequential, starting at 0).
    /// The task's id is set via Task::setId(). Must be called before finalize().
    /// Thread safety: NOT thread-safe — call only during single-threaded construction.
    int addTask(std::unique_ptr<Task> task);

    /// Add a dependency edge: task `dependent_id` depends on task `dependency_id`.
    /// Both IDs must have been returned by prior addTask() calls.
    /// Must be called before finalize().
    /// Thread safety: NOT thread-safe — call only during single-threaded construction.
    void addDependency(int dependent_id, int dependency_id);

    /// Finalize the DAG: compute reverse edges, validate acyclicity, seed ready set.
    /// Must be called after all tasks and edges are added, before the dispatch loop.
    /// Returns 0 on success, non-zero if a cycle is detected.
    /// Thread safety: NOT thread-safe — call only during single-threaded construction.
    int finalize();

    /// Block until a ready task (all dependencies satisfied) is available, then return it.
    /// Returns nullptr if the DAG is terminated (all tasks done, or a failure occurred).
    /// Thread safety: safe to call from any thread.
    Task* waitNextTask();

    /// Returns true while tasks are still pending or in-flight (running).
    /// Returns false when all tasks are completed/skipped, or when a task has failed.
    /// Thread safety: safe to call from any thread.
    bool hasActiveTask() const;

    /// Mark a task as completed successfully. Decrements the remaining-dependency
    /// counter of all tasks that depend on this one; those reaching zero are moved
    /// to the ready set and the condition variable is signaled.
    /// Thread safety: safe to call from any thread.
    void markCompleted(int task_id);

    /// Mark a task as failed. Signals termination so that hasActiveTask() returns
    /// false and waitNextTask() unblocks and returns nullptr.
    /// Thread safety: safe to call from any thread.
    void markFailed(int task_id);

    /// Returns the total number of tasks in the DAG.
    int totalCount() const;

    /// Returns the number of tasks that have completed successfully.
    int completedCount() const;

    /// Returns the number of tasks that were skipped (condition said up-to-date).
    int skippedCount() const;

    /// Returns the number of tasks that have failed.
    int failedCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cdo::build

#endif // CDO_BUILD_TASKS_DAG_H
