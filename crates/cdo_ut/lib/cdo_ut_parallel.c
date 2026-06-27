/**
 * cdo_ut_parallel.c — Parallel test execution using Windows threads.
 *
 * Implements a work-queue pattern where N worker threads each pick the
 * next test from a shared queue protected by a CRITICAL_SECTION. Each
 * test's failure state is stored in a per-test result struct (avoiding
 * the global g_ut_current_failure which is not thread-safe).
 *
 * Protocol messages (test_start, test_end) are emitted under a mutex
 * to prevent interleaved JSON output on stdout.
 *
 * Serial tests (TEST_SERIAL) run sequentially after all parallel tests.
 *
 * Falls back to sequential execution if thread creation fails.
 */

#include "cdo_ut_parallel.h"
#include "cdo_ut_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <process.h>  /* _beginthreadex */

/* =========================================================================
 * Thread-local failure recording
 * ========================================================================= */

/**
 * Per-thread failure state. Each worker thread uses __declspec(thread) to
 * store the failure details for the currently executing test, which is then
 * copied into the results array after the test completes.
 */
static __declspec(thread) const char *tls_fail_file     = NULL;
static __declspec(thread) int         tls_fail_line     = 0;
static __declspec(thread) const char *tls_fail_expr     = NULL;
static __declspec(thread) const char *tls_fail_actual   = NULL;
static __declspec(thread) const char *tls_fail_expected = NULL;
static __declspec(thread) bool        tls_has_failure   = false;

/**
 * Thread-safe failure recorder that workers call instead of the global
 * cdo_ut_record_failure. We override the global symbol when running in
 * parallel mode via a function pointer.
 */
static void parallel_record_failure(const char *file, int line,
                                    const char *expr,
                                    const char *actual, const char *expected)
{
    tls_fail_file     = file;
    tls_fail_line     = line;
    tls_fail_expr     = expr;
    tls_fail_actual   = actual;
    tls_fail_expected = expected;
    tls_has_failure   = true;
}

static void tls_clear_failure(void)
{
    tls_fail_file     = NULL;
    tls_fail_line     = 0;
    tls_fail_expr     = NULL;
    tls_fail_actual   = NULL;
    tls_fail_expected = NULL;
    tls_has_failure   = false;
}

/* =========================================================================
 * Work Queue
 * ========================================================================= */

typedef struct {
    CdoUtParallelEntry *tests;
    CdoUtTestResult    *results;
    int                 count;
    int                 next_index;    /* Next test to dequeue */
    int                 base_test_id;  /* Starting test_id for protocol */
    CRITICAL_SECTION    queue_lock;
    CRITICAL_SECTION    output_lock;
} WorkQueue;

/* =========================================================================
 * Failure recording hook
 *
 * We use a global function pointer that the parallel executor swaps to
 * point to parallel_record_failure during parallel execution. The default
 * implementation (in cdo_ut_main.c) uses a global struct. During parallel
 * mode, assertion macros call through this pointer instead.
 *
 * NOTE: Since cdo_ut_record_failure is the symbol the macros call, and
 * it's defined in cdo_ut_main.c, we call it directly for serial tests.
 * For parallel tests, we intercept via the TLS approach: the worker
 * installs a per-thread hook before running each test.
 * ========================================================================= */

/*
 * We declare the extern to the existing cdo_ut_record_failure so we can
 * swap it. However, since it's not a function pointer in the current design,
 * we use a different approach: we provide a global function pointer that
 * assertion macros can optionally call. For now, we use the TLS directly
 * by having workers call cdo_ut_record_failure which writes to TLS.
 *
 * Actually, the simplest approach: we make cdo_ut_record_failure_parallel
 * a global function pointer. The parallel executor sets it before spawning
 * threads. When set, cdo_ut_record_failure (in cdo_ut_main.c) should
 * delegate to it. But we can't modify cdo_ut_main.c's function directly.
 *
 * RESOLUTION: We expose a global function pointer that the parallel executor
 * sets. The main module's cdo_ut_record_failure checks this pointer.
 * This is the cleanest way without modifying the assertion macros.
 */

/**
 * Global hook for parallel failure recording. When non-NULL, the
 * cdo_ut_record_failure function should delegate to this.
 * Defined here, declared extern in cdo_ut_parallel.h isn't ideal,
 * so we just make it a global symbol the linker resolves.
 */
void (*g_ut_parallel_record_failure)(const char *file, int line,
                                     const char *expr,
                                     const char *actual,
                                     const char *expected) = NULL;

/* =========================================================================
 * Worker Thread
 * ========================================================================= */

static unsigned __stdcall worker_thread_func(void *arg)
{
    WorkQueue *queue = (WorkQueue *)arg;
    LARGE_INTEGER freq, start_time, end_time;
    QueryPerformanceFrequency(&freq);

    for (;;) {
        /* Dequeue next test */
        int idx;
        EnterCriticalSection(&queue->queue_lock);
        idx = queue->next_index;
        if (idx < queue->count) {
            queue->next_index++;
        }
        LeaveCriticalSection(&queue->queue_lock);

        if (idx >= queue->count) {
            break;  /* No more work */
        }

        CdoUtParallelEntry *test = &queue->tests[idx];
        CdoUtTestResult *result = &queue->results[idx];
        int test_id = queue->base_test_id + idx;

        result->name    = test->name;
        result->test_id = test_id;

        /* Emit test_start (mutex-protected) */
        EnterCriticalSection(&queue->output_lock);
        cdo_ut_emit_test_start(test->name, test_id);
        LeaveCriticalSection(&queue->output_lock);

        /* Clear TLS failure state */
        tls_clear_failure();

        /* Execute the test with timing */
        QueryPerformanceCounter(&start_time);
        int ret = test->func();
        QueryPerformanceCounter(&end_time);

        double duration_ms = (double)(end_time.QuadPart - start_time.QuadPart)
                             * 1000.0 / (double)freq.QuadPart;

        /* Store result */
        result->result      = ret;
        result->duration_ms = duration_ms;

        if (ret != 0 && tls_has_failure) {
            result->fail_file     = tls_fail_file;
            result->fail_line     = tls_fail_line;
            result->fail_expr     = tls_fail_expr;
            result->fail_actual   = tls_fail_actual;
            result->fail_expected = tls_fail_expected;
        } else {
            result->fail_file     = NULL;
            result->fail_line     = 0;
            result->fail_expr     = NULL;
            result->fail_actual   = NULL;
            result->fail_expected = NULL;
        }

        /* Emit test_end (mutex-protected) */
        EnterCriticalSection(&queue->output_lock);
        if (ret == 0) {
            cdo_ut_emit_test_end(test->name, test_id, "pass",
                                 duration_ms, NULL, 0, NULL, NULL, NULL);
        } else {
            cdo_ut_emit_test_end(test->name, test_id, "fail",
                                 duration_ms,
                                 result->fail_file,
                                 result->fail_line,
                                 result->fail_expr,
                                 result->fail_actual,
                                 result->fail_expected);
        }
        LeaveCriticalSection(&queue->output_lock);
    }

    return 0;
}

/* =========================================================================
 * Sequential fallback (used when thread creation fails)
 * ========================================================================= */

static int run_sequential(CdoUtParallelEntry *tests, int count,
                          int base_test_id, CRITICAL_SECTION *output_lock)
{
    int failed = 0;
    LARGE_INTEGER freq, start_time, end_time;
    QueryPerformanceFrequency(&freq);

    for (int i = 0; i < count; i++) {
        int test_id = base_test_id + i;

        /* Emit test_start */
        if (output_lock) {
            EnterCriticalSection(output_lock);
        }
        cdo_ut_emit_test_start(tests[i].name, test_id);
        if (output_lock) {
            LeaveCriticalSection(output_lock);
        }

        /* Clear TLS failure state */
        tls_clear_failure();

        /* Execute test */
        QueryPerformanceCounter(&start_time);
        int ret = tests[i].func();
        QueryPerformanceCounter(&end_time);

        double duration_ms = (double)(end_time.QuadPart - start_time.QuadPart)
                             * 1000.0 / (double)freq.QuadPart;

        /* Emit test_end */
        if (output_lock) {
            EnterCriticalSection(output_lock);
        }
        if (ret == 0) {
            cdo_ut_emit_test_end(tests[i].name, test_id, "pass",
                                 duration_ms, NULL, 0, NULL, NULL, NULL);
        } else {
            failed++;
            cdo_ut_emit_test_end(tests[i].name, test_id, "fail",
                                 duration_ms,
                                 tls_has_failure ? tls_fail_file : NULL,
                                 tls_has_failure ? tls_fail_line : 0,
                                 tls_has_failure ? tls_fail_expr : NULL,
                                 tls_has_failure ? tls_fail_actual : NULL,
                                 tls_has_failure ? tls_fail_expected : NULL);
        }
        if (output_lock) {
            LeaveCriticalSection(output_lock);
        }
    }

    return failed;
}

/* =========================================================================
 * cdo_ut_run_parallel — Main entry point for parallel execution.
 * ========================================================================= */

int cdo_ut_run_parallel(int jobs, CdoUtParallelEntry *tests, int count,
                        CdoUtParallelEntry *serial_tests, int serial_count)
{
    int total_failed = 0;

    /* Install parallel failure recording hook */
    g_ut_parallel_record_failure = parallel_record_failure;

    /* Cap jobs to number of parallel tests */
    if (jobs > count) {
        jobs = count;
    }
    if (jobs < 1) {
        jobs = 1;
    }

    /* Allocate results array for parallel tests */
    CdoUtTestResult *results = NULL;
    if (count > 0) {
        results = (CdoUtTestResult *)calloc((size_t)count,
                                            sizeof(CdoUtTestResult));
        if (results == NULL) {
            fprintf(stderr, "cdo_ut: failed to allocate results array\n");
            /* Fall back to sequential */
            g_ut_parallel_record_failure = parallel_record_failure;
            total_failed += run_sequential(tests, count, 0, NULL);
            total_failed += run_sequential(serial_tests, serial_count,
                                           count, NULL);
            g_ut_parallel_record_failure = NULL;
            return total_failed;
        }
    }

    /* Initialize synchronization primitives */
    WorkQueue queue;
    queue.tests        = tests;
    queue.results      = results;
    queue.count        = count;
    queue.next_index   = 0;
    queue.base_test_id = 0;
    InitializeCriticalSection(&queue.queue_lock);
    InitializeCriticalSection(&queue.output_lock);

    /* Create worker threads */
    HANDLE *threads = NULL;
    int threads_created = 0;
    bool fallback_sequential = false;

    if (count > 0 && jobs > 0) {
        threads = (HANDLE *)calloc((size_t)jobs, sizeof(HANDLE));
        if (threads == NULL) {
            fprintf(stderr,
                "cdo_ut: warning: failed to allocate thread handles, "
                "falling back to sequential execution\n");
            fallback_sequential = true;
        }
    }

    if (!fallback_sequential && count > 0) {
        for (int i = 0; i < jobs; i++) {
            threads[i] = (HANDLE)_beginthreadex(
                NULL,               /* security */
                0,                  /* stack size (default) */
                worker_thread_func, /* start routine */
                &queue,             /* argument */
                0,                  /* creation flags (start immediately) */
                NULL                /* thread id (not needed) */
            );

            if (threads[i] == 0) {
                fprintf(stderr,
                    "cdo_ut: warning: failed to create thread %d, "
                    "falling back to sequential execution\n", i);
                /* Close threads we already created */
                for (int j = 0; j < i; j++) {
                    /* Wait for already-running threads to finish their
                     * current work before falling back */
                    WaitForSingleObject(threads[j], INFINITE);
                    CloseHandle(threads[j]);
                }
                fallback_sequential = true;
                break;
            }
            threads_created++;
        }
    }

    if (fallback_sequential) {
        /* Run all remaining parallel tests sequentially */
        /* Some may have already been consumed by the threads that did start.
         * Run from where the queue left off. */
        int already_done = queue.next_index;
        for (int i = 0; i < already_done; i++) {
            if (results[i].result != 0) {
                total_failed++;
            }
        }
        /* Run the rest sequentially */
        total_failed += run_sequential(
            tests + already_done, count - already_done,
            already_done, &queue.output_lock);
    } else if (count > 0) {
        /* Wait for all worker threads to complete */
        WaitForMultipleObjects((DWORD)threads_created, threads, TRUE, INFINITE);

        /* Close thread handles */
        for (int i = 0; i < threads_created; i++) {
            CloseHandle(threads[i]);
        }

        /* Count failures from parallel tests */
        for (int i = 0; i < count; i++) {
            if (results[i].result != 0) {
                total_failed++;
            }
        }
    }

    /* Cleanup */
    DeleteCriticalSection(&queue.queue_lock);
    DeleteCriticalSection(&queue.output_lock);
    free(threads);
    free(results);

    /* Run serial tests sequentially (after all parallel tests complete) */
    if (serial_count > 0) {
        total_failed += run_sequential(serial_tests, serial_count, count, NULL);
    }

    /* Remove parallel failure hook */
    g_ut_parallel_record_failure = NULL;

    return total_failed;
}
