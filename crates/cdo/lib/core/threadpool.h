#ifndef CDO_THREADPOOL_H
#define CDO_THREADPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TaskFunc)(void* arg);

typedef struct ThreadPool ThreadPool;

/// Create a thread pool with n worker threads.
/// If n == 0, uses the number of logical CPU cores (via pal_cpu_count).
/// Returns NULL on failure.
ThreadPool* threadpool_create(int n);

/// Submit a task to the pool. Returns 0 on success, non-zero on failure.
int threadpool_submit(ThreadPool* pool, TaskFunc func, void* arg);

/// Wait for all submitted tasks to complete. Returns 0 if all succeeded.
int threadpool_wait(ThreadPool* pool);

/// Destroy the pool: signals all workers to exit, joins them, frees resources.
void threadpool_destroy(ThreadPool* pool);

#ifdef __cplusplus
}
#endif

#endif // CDO_THREADPOOL_H
