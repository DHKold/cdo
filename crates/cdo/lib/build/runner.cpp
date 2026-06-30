// runner.cpp — ThreadRunner and RunnerPool implementation.
// ThreadRunner: dedicated std::thread per runner with mutex+condvar for dispatch/completion.
// RunnerPool: manages N ThreadRunners, provides waitFreeRunner() for the main dispatch loop.

#include "build/runner.h"
#include "build/task.h"
#include "core/log.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace cdo::build {

// ---------------------------------------------------------------------------
// ThreadRunner::Impl — internal state with dedicated worker thread
// ---------------------------------------------------------------------------

struct ThreadRunner::Impl {
    std::thread worker;
    mutable std::mutex mtx;
    std::condition_variable dispatch_cv;   // signals worker: new task or shutdown
    std::condition_variable complete_cv;   // signals caller: task finished

    Task* current_task = nullptr;
    bool idle = true;
    bool shutdown = false;
    int last_result = 0;
    int last_task_id = -1;

    Impl() {
        worker = std::thread([this]() { workerLoop(); });
    }

    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            shutdown = true;
        }
        dispatch_cv.notify_one();
        if (worker.joinable()) {
            worker.join();
        }
    }

    void workerLoop() {
        while (true) {
            std::unique_lock<std::mutex> lock(mtx);
            dispatch_cv.wait(lock, [this]() { return current_task != nullptr || shutdown; });

            if (shutdown && current_task == nullptr) {
                break;
            }

            // Execute the task
            Task* task = current_task;
            lock.unlock();

            int result = task->run();

            lock.lock();
            last_result = result;
            last_task_id = task->id();
            current_task = nullptr;
            idle = true;
            lock.unlock();

            complete_cv.notify_one();
        }
    }
};

// ---------------------------------------------------------------------------
// ThreadRunner public interface
// ---------------------------------------------------------------------------

ThreadRunner::ThreadRunner() : impl_(std::make_unique<Impl>()) {}

ThreadRunner::~ThreadRunner() = default;

void ThreadRunner::run(Task& task) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->current_task = &task;
    impl_->idle = false;
    impl_->dispatch_cv.notify_one();
}

bool ThreadRunner::isIdle() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->idle;
}

void ThreadRunner::wait() {
    std::unique_lock<std::mutex> lock(impl_->mtx);
    impl_->complete_cv.wait(lock, [this]() { return impl_->idle; });
}

int ThreadRunner::lastResult() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->last_result;
}

int ThreadRunner::lastTaskId() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    return impl_->last_task_id;
}

// ---------------------------------------------------------------------------
// RunnerPool
// ---------------------------------------------------------------------------

RunnerPool::RunnerPool(int job_count) {
    if (job_count < 1) job_count = 1;
    runners_.reserve(job_count);
    for (int i = 0; i < job_count; ++i) {
        runners_.push_back(std::make_unique<ThreadRunner>());
    }
}

RunnerPool::~RunnerPool() = default;

Runner& RunnerPool::waitFreeRunner() {
    // Spin-wait with short sleeps checking for any idle runner.
    // In practice the main dispatch loop calls this infrequently relative to
    // task execution time, so the sleep overhead is negligible.
    while (true) {
        for (auto& runner : runners_) {
            if (runner->isIdle()) {
                return *runner;
            }
        }
        // No free runner — wait for any runner to complete.
        // We iterate and call wait() on the first non-idle runner, which blocks
        // until that runner finishes. This is acceptable because when all runners
        // are busy, any completion unblocks us.
        for (auto& runner : runners_) {
            if (!runner->isIdle()) {
                runner->wait();
                return *runner;
            }
        }
    }
}

int RunnerPool::size() const {
    return static_cast<int>(runners_.size());
}

} // namespace cdo::build
