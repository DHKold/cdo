#ifndef CDO_CORE_CACHE_HASH_PARALLEL_H
#define CDO_CORE_CACHE_HASH_PARALLEL_H

#include "core/cache.h"
#include "model/cache_config.h"
#include "commons/threadpool.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Per-file hash result slot
// ---------------------------------------------------------------------------

/// Result of a single cache key computation (one per file submitted to the pool).
/// Each thread writes ONLY to its own slot — no contention by design.
typedef struct {
    char    key[CACHE_KEY_HEX_LEN + 1]; // Computed cache key (64 hex + null)
    bool    valid;                       // true if key computation succeeded
    int     job_index;                   // Original index into the CompileJob array
} HashResultSlot;

// ---------------------------------------------------------------------------
// Hash job context (passed to each worker thread)
// ---------------------------------------------------------------------------

/// Context for a single hash job submitted to the thread pool.
/// Contains the inputs required for cache_compute_key and a pointer to
/// the pre-allocated result slot where the computed key should be stored.
typedef struct {
    CacheKeyInputs  inputs;             // Inputs for SHA-256 cache key computation
    HashResultSlot* result;             // Pointer to this job's pre-allocated result slot
} HashJobCtx;

// ---------------------------------------------------------------------------
// Parallel hash dispatch interface
// ---------------------------------------------------------------------------

/// Dispatch cache key computation for multiple files in parallel using a thread pool.
///
/// This function submits hash jobs for all files that were NOT resolved by the
/// fast-path (mtime check) and NOT skipped by the filesize threshold. Each job
/// computes a SHA-256 cache key from the CacheKeyInputs.
///
/// Thread safety is guaranteed by:
///   - Each worker reads only filesystem files (no shared mutable state)
///   - Each worker writes to its own pre-allocated HashResultSlot (no contention)
///
/// When parallelism == 1 or the pool is NULL, computation falls back to serial
/// execution on the calling thread.
///
/// @param pool         Thread pool to dispatch to (NULL for serial fallback)
/// @param jobs         Array of hash job contexts (one per file to hash)
/// @param job_count    Number of hash jobs to dispatch
/// @param results      Pre-allocated array of result slots (one per file, same indexing as jobs)
/// @return 0 on success (all jobs dispatched and completed),
///         non-zero if thread pool submission fails (partial results may exist in slots)
int cache_parallel_hash_dispatch(ThreadPool* pool, HashJobCtx* jobs, int job_count, HashResultSlot* results);

/// Allocate and initialize hash result slots for a batch.
/// All slots are zero-initialized (valid = false, key = empty).
///
/// @param count    Number of slots to allocate
/// @return Heap-allocated array of HashResultSlot, or NULL on allocation failure.
///         Caller must free() the returned pointer.
HashResultSlot* cache_hash_results_alloc(int count);

/// Allocate and initialize hash job contexts for a batch.
/// All contexts are zero-initialized.
///
/// @param count    Number of job contexts to allocate
/// @return Heap-allocated array of HashJobCtx, or NULL on allocation failure.
///         Caller must free() the returned pointer.
HashJobCtx* cache_hash_jobs_alloc(int count);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_CACHE_HASH_PARALLEL_H
