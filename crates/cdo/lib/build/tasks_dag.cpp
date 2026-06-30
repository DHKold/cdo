// tasks_dag.cpp — TasksDag implementation.
// Thread-safe directed acyclic graph for build task scheduling.
// Uses mutex + condition_variable for ready-set management.

#include "build/tasks_dag.h"
#include "build/task.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

namespace cdo::build {

// ---------------------------------------------------------------------------
// TasksDag::Impl — internal state
// ---------------------------------------------------------------------------

enum class TaskState {
    Pending,
    Ready,
    Running,
    Done,
    Failed,
};

struct TasksDag::Impl {
    // All tasks owned by the DAG
    std::vector<std::unique_ptr<Task>> tasks;

    // Forward adjacency: forward_edges[A] = list of tasks that depend on A
    // i.e., A must complete before each dependent can run.
    std::vector<std::vector<int>> forward_edges;

    // Reverse edges: reverse_edges[B] = list of tasks that B depends on
    // Used only during finalize for documentation. Actually we need:
    // forward_edges[dependency] contains dependents.
    // So if B depends on A, then forward_edges[A] contains B.

    // Per-task state
    std::vector<TaskState> state;

    // Remaining dependency count per task
    std::vector<int> remaining_deps;

    // Ready queue (tasks with remaining_deps == 0 and state == Ready)
    std::queue<int> ready_queue;

    // Synchronization
    mutable std::mutex mtx;
    std::condition_variable cv;

    // Termination flag (set on failure)
    bool terminated = false;

    // Counters
    int completed_count = 0;
    int skipped_count = 0;
    int failed_count = 0;
};

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

TasksDag::TasksDag() : impl_(std::make_unique<Impl>()) {}

TasksDag::~TasksDag() {
    // Ensure any thread blocked in waitNextTask() is unblocked before destruction.
    if (impl_) {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->terminated = true;
        impl_->cv.notify_all();
    }
}

// ---------------------------------------------------------------------------
// addTask
// ---------------------------------------------------------------------------

int TasksDag::addTask(std::unique_ptr<Task> task) {
    int id = static_cast<int>(impl_->tasks.size());
    task->setId(id);
    impl_->tasks.push_back(std::move(task));
    impl_->forward_edges.emplace_back();
    impl_->state.push_back(TaskState::Pending);
    impl_->remaining_deps.push_back(0);
    return id;
}

// ---------------------------------------------------------------------------
// addDependency
// ---------------------------------------------------------------------------

void TasksDag::addDependency(int dependent_id, int dependency_id) {
    // dependent_id depends on dependency_id
    // When dependency_id completes, it unblocks dependent_id
    // So forward_edges[dependency_id] -> dependent_id
    impl_->forward_edges[dependency_id].push_back(dependent_id);
    impl_->remaining_deps[dependent_id]++;
}

// ---------------------------------------------------------------------------
// finalize — validate acyclicity via Kahn's algorithm, seed ready set
// ---------------------------------------------------------------------------

int TasksDag::finalize() {
    int n = static_cast<int>(impl_->tasks.size());
    if (n == 0) {
        impl_->terminated = true;
        return 0;
    }

    // Kahn's algorithm for cycle detection using a copy of remaining_deps
    std::vector<int> in_degree(impl_->remaining_deps);
    std::queue<int> topo_queue;

    for (int i = 0; i < n; i++) {
        if (in_degree[i] == 0) {
            topo_queue.push(i);
        }
    }

    int processed = 0;
    while (!topo_queue.empty()) {
        int node = topo_queue.front();
        topo_queue.pop();
        processed++;

        for (int dep : impl_->forward_edges[node]) {
            in_degree[dep]--;
            if (in_degree[dep] == 0) {
                topo_queue.push(dep);
            }
        }
    }

    if (processed != n) {
        // Cycle detected
        return 1;
    }

    // Seed ready set with zero-dep tasks
    for (int i = 0; i < n; i++) {
        if (impl_->remaining_deps[i] == 0) {
            impl_->state[i] = TaskState::Ready;
            impl_->ready_queue.push(i);
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// waitNextTask — block until a ready task is available or terminated
// ---------------------------------------------------------------------------

Task* TasksDag::waitNextTask() {
    std::unique_lock<std::mutex> lock(impl_->mtx);

    impl_->cv.wait(lock, [this]() {
        return !impl_->ready_queue.empty() || impl_->terminated;
    });

    if (impl_->terminated && impl_->ready_queue.empty()) {
        return nullptr;
    }

    int id = impl_->ready_queue.front();
    impl_->ready_queue.pop();
    impl_->state[id] = TaskState::Running;

    return impl_->tasks[id].get();
}

// ---------------------------------------------------------------------------
// hasActiveTask — true if tasks are still pending or running
// ---------------------------------------------------------------------------

bool TasksDag::hasActiveTask() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    if (impl_->terminated) {
        return false;
    }

    int n = static_cast<int>(impl_->tasks.size());
    for (int i = 0; i < n; i++) {
        if (impl_->state[i] == TaskState::Pending ||
            impl_->state[i] == TaskState::Ready ||
            impl_->state[i] == TaskState::Running) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// markCompleted — update state, unblock dependents, signal condvar
// ---------------------------------------------------------------------------

void TasksDag::markCompleted(int task_id) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    impl_->state[task_id] = TaskState::Done;
    impl_->completed_count++;

    // Check if task was skipped
    if (impl_->tasks[task_id]->wasSkipped()) {
        impl_->skipped_count++;
    }

    // Decrement remaining_deps for all dependents
    for (int dependent : impl_->forward_edges[task_id]) {
        impl_->remaining_deps[dependent]--;
        if (impl_->remaining_deps[dependent] == 0) {
            impl_->state[dependent] = TaskState::Ready;
            impl_->ready_queue.push(dependent);
        }
    }

    // Check if all tasks are done
    bool all_done = true;
    int n = static_cast<int>(impl_->tasks.size());
    for (int i = 0; i < n; i++) {
        if (impl_->state[i] != TaskState::Done && impl_->state[i] != TaskState::Failed) {
            all_done = false;
            break;
        }
    }
    if (all_done) {
        impl_->terminated = true;
    }

    impl_->cv.notify_all();
}

// ---------------------------------------------------------------------------
// markFailed — set terminated, signal condvar
// ---------------------------------------------------------------------------

void TasksDag::markFailed(int task_id) {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    impl_->state[task_id] = TaskState::Failed;
    impl_->failed_count++;
    impl_->terminated = true;

    impl_->cv.notify_all();
}

// ---------------------------------------------------------------------------
// Count accessors
// ---------------------------------------------------------------------------

int TasksDag::totalCount() const {
    return static_cast<int>(impl_->tasks.size());
}

int TasksDag::completedCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->completed_count;
}

int TasksDag::skippedCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->skipped_count;
}

int TasksDag::failedCount() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->failed_count;
}

} // namespace cdo::build
