// crates/cdo/lib/core/cache_hash_parallel.c
// Parallel hash dispatch implementation.
// When a thread pool is available (pool != NULL), dispatches cache key computation
// across worker threads. Falls back to serial computation on the calling thread
// when pool is NULL or threadpool_submit fails.
#include "core/cache_hash_parallel.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Worker function — executed on a thread pool worker for each hash job
// ---------------------------------------------------------------------------

/// Thread pool task: compute a single cache key and store result in the job's slot.
/// Each worker reads only filesystem files and writes to its own pre-allocated slot.
static void hash_worker(void* arg) {
    HashJobCtx* job = (HashJobCtx*)arg;
    HashResultSlot* slot = job->result;

    int rc = cache_compute_key(&job->inputs, slot->key);
    slot->valid = (rc == 0);
}

// ---------------------------------------------------------------------------
// Allocation helpers
// ---------------------------------------------------------------------------

HashResultSlot* cache_hash_results_alloc(int count) {
    if (count <= 0) return NULL;
    HashResultSlot* slots = (HashResultSlot*)calloc((size_t)count, sizeof(HashResultSlot));
    return slots;
}

HashJobCtx* cache_hash_jobs_alloc(int count) {
    if (count <= 0) return NULL;
    HashJobCtx* jobs = (HashJobCtx*)calloc((size_t)count, sizeof(HashJobCtx));
    return jobs;
}

// ---------------------------------------------------------------------------
// Parallel hash dispatch
// ---------------------------------------------------------------------------

int cache_parallel_hash_dispatch(ThreadPool* pool, HashJobCtx* jobs, int job_count, HashResultSlot* results) {
    if (!jobs || !results || job_count <= 0) return -1;

    // Serial fallback: when pool is NULL (jobs=1 mode or pool creation failed)
    if (pool == NULL) {
        for (int i = 0; i < job_count; i++) {
            HashResultSlot* slot = &results[i];
            // Skip files already resolved (fast-path hits have valid == true)
            if (slot->valid) continue;

            int rc = cache_compute_key(&jobs[i].inputs, slot->key);
            slot->valid = (rc == 0);
        }
        return 0;
    }

    // Parallel path: submit hash jobs for files not already resolved by fast-path
    int submit_count = 0;
    bool submit_failed = false;

    for (int i = 0; i < job_count; i++) {
        HashResultSlot* slot = &results[i];
        // Skip files already resolved by mtime fast-path (Requirement 8.6)
        if (slot->valid) continue;

        int rc = threadpool_submit(pool, hash_worker, &jobs[i]);
        if (rc != 0) {
            cdo_log_warn("Failed to submit hash job %d to thread pool — falling back to serial", i);
            submit_failed = true;
            break;
        }
        submit_count++;
    }

    if (submit_failed) {
        // Some submissions failed — wait for already-submitted tasks to finish,
        // then fall back to serial for remaining unresolved slots.
        if (submit_count > 0) {
            threadpool_wait(pool);
        }

        // Serial fallback for any remaining unresolved slots
        for (int i = 0; i < job_count; i++) {
            HashResultSlot* slot = &results[i];
            if (slot->valid) continue;

            int rc = cache_compute_key(&jobs[i].inputs, slot->key);
            slot->valid = (rc == 0);
        }
        return 0;
    }

    // All jobs submitted successfully — wait for completion
    threadpool_wait(pool);
    return 0;
}
