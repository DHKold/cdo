/*
 * cdo_ut_parallel.h — Internal header for parallel test execution.
 *
 * Declares the parallel executor that runs tests concurrently using
 * Windows threads (_beginthreadex), with per-test output isolation
 * and mutex-protected protocol emission.
 */

#ifndef CDO_UT_PARALLEL_H
#define CDO_UT_PARALLEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* =========================================================================
 * Types
 * ========================================================================= */

/**
 * Entry describing a single test to execute.
 * Mirrors CdoUtTestEntry from cdo_ut_main.c.
 */
typedef struct {
    const char *name;
    int (*func)(void);
    bool serial;
} CdoUtParallelEntry;

/**
 * Result of a single test execution (populated by worker threads).
 */
typedef struct {
    const char *name;
    int         test_id;
    int         result;        /* 0 = pass, non-zero = fail */
    double      duration_ms;
    /* Failure info (valid only when result != 0) */
    const char *fail_file;
    int         fail_line;
    const char *fail_expr;
    const char *fail_actual;
    const char *fail_expected;
} CdoUtTestResult;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * Execute tests in parallel using Windows threads.
 *
 * Parallel tests (those not marked serial) are dispatched to a pool of
 * `jobs` worker threads using a shared work queue. Serial tests run
 * sequentially after all parallel tests complete.
 *
 * Parameters:
 *   jobs         - maximum number of concurrent worker threads
 *   tests        - array of parallel test entries
 *   count        - number of parallel tests
 *   serial_tests - array of serial test entries (run after parallel)
 *   serial_count - number of serial tests
 *
 * Returns the total number of failed tests (parallel + serial).
 */
int cdo_ut_run_parallel(int jobs, CdoUtParallelEntry *tests, int count,
                        CdoUtParallelEntry *serial_tests, int serial_count);

#ifdef __cplusplus
}
#endif

#endif /* CDO_UT_PARALLEL_H */
