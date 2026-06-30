/**
 * runner.h - Task execution abstraction for the build pipeline.
 *
 * Defines the Runner abstract base class, ThreadRunner concrete implementation,
 * and RunnerPool for managing a configurable number of runner instances.
 *
 * Runners execute tasks asynchronously: the run() method dispatches a task and
 * returns immediately while the task executes on the runner's execution context.
 * The main thread is responsible for dispatching tasks to runners via the pool;
 * runners never poll or fetch tasks from the DAG themselves.
 *
 * The Runner interface is designed so that alternative implementations (e.g., a
 * future RemoteRunner) can be added without modifying the pool or task logic.
 *
 * Part of the cdo::build pipeline refactor.
 */
#ifndef CDO_BUILD_RUNNER_H
#define CDO_BUILD_RUNNER_H

#include <memory>
#include <vector>

namespace cdo::build {

// Forward declaration — full definition in task.h
class Task;

/// Abstract base class representing an executor capable of running Tasks asynchronously.
/// The main thread dispatches tasks to Runners; Runners do not poll or fetch tasks themselves.
/// Designed so that alternative implementations (e.g., RemoteRunner) can be added without
/// modifying the pool or task logic.
class Runner {
public:
    virtual ~Runner() = default;

    /// Dispatch a task for asynchronous execution. Returns immediately.
    /// The task executes on the runner's execution context (e.g., a dedicated thread).
    /// Behavior is undefined if called while the runner is not idle.
    virtual void run(Task& task) = 0;

    /// Returns true if the runner has finished its current task and is available
    /// for a new dispatch. Returns true if no task has been dispatched yet.
    virtual bool isIdle() const = 0;

    /// Block until the runner finishes its current task.
    /// Returns immediately if the runner is already idle.
    virtual void wait() = 0;

    /// Get the result of the last completed task.
    /// Returns 0 on success, non-zero on failure. Undefined if no task has completed.
    virtual int lastResult() const = 0;

    /// Get the task ID of the last completed task.
    /// Returns -1 if no task has completed yet.
    virtual int lastTaskId() const = 0;
};

/// Concrete Runner implementation that executes a dispatched Task on a dedicated worker thread.
/// Uses the pimpl pattern to encapsulate threading internals (mutex, condvar, thread).
/// The run() method is asynchronous — it returns immediately and the task executes on
/// the runner's dedicated thread.
class ThreadRunner : public Runner {
public:
    ThreadRunner();
    ~ThreadRunner() override;

    // Non-copyable, non-movable (owns a thread)
    ThreadRunner(const ThreadRunner&) = delete;
    ThreadRunner& operator=(const ThreadRunner&) = delete;
    ThreadRunner(ThreadRunner&&) = delete;
    ThreadRunner& operator=(ThreadRunner&&) = delete;

    void run(Task& task) override;
    bool isIdle() const override;
    void wait() override;
    int lastResult() const override;
    int lastTaskId() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Pool of Runner instances managed by the main thread.
/// The main thread dispatches tasks to free runners via waitFreeRunner().
/// The pool size is set at construction (typically from the --jobs flag).
class RunnerPool {
public:
    /// Construct a pool with the given number of ThreadRunner instances.
    /// job_count must be >= 1.
    explicit RunnerPool(int job_count);
    ~RunnerPool();

    // Non-copyable, non-movable (owns runners)
    RunnerPool(const RunnerPool&) = delete;
    RunnerPool& operator=(const RunnerPool&) = delete;
    RunnerPool(RunnerPool&&) = delete;
    RunnerPool& operator=(RunnerPool&&) = delete;

    /// Block until any runner finishes its task and becomes free.
    /// Returns a reference to the free runner. If multiple runners are free,
    /// returns the first one found.
    Runner& waitFreeRunner();

    /// Get the number of runners in the pool.
    int size() const;

private:
    std::vector<std::unique_ptr<ThreadRunner>> runners_;
};

} // namespace cdo::build

#endif // CDO_BUILD_RUNNER_H
