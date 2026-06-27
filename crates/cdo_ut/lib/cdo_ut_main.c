/**
 * cdo_ut_main.c — Test framework main entry point.
 *
 * Provides:
 *   - Test registration (cdo_ut_register)
 *   - Failure recording (cdo_ut_record_failure)
 *   - main() with --list, --filter, --jobs argument parsing
 *   - Sequential test execution loop with structured protocol emission
 */

#include "cdo_ut.h"
#include "cdo_ut_filter.h"
#include "cdo_ut_protocol.h"
#include "cdo_ut_parallel.h"

#include <windows.h>

// =============================================================================
// Constants
// =============================================================================

#define CDO_UT_MAX_TESTS 1024

// =============================================================================
// Test Registry
// =============================================================================

typedef struct {
    const char *name;
    int (*func)(void);
    bool serial;
} CdoUtTestEntry;

static CdoUtTestEntry g_ut_tests[CDO_UT_MAX_TESTS];
static int g_ut_test_count = 0;

// =============================================================================
// Failure Recording
// =============================================================================

typedef struct {
    const char *file;
    int         line;
    const char *expr;
    const char *actual;
    const char *expected;
    bool        has_failure;
} CdoUtFailure;

static CdoUtFailure g_ut_current_failure = {0};

// =============================================================================
// cdo_ut_register — Add a test to the global registry.
// =============================================================================

void cdo_ut_register(const char *name, int (*func)(void), bool serial)
{
    if (g_ut_test_count >= CDO_UT_MAX_TESTS) {
        fprintf(stderr,
            "{\"type\": \"error\", \"message\": \"Test registry overflow: "
            "exceeded maximum of %d tests\"}\n", CDO_UT_MAX_TESTS);
        exit(1);
    }

    g_ut_tests[g_ut_test_count].name   = name;
    g_ut_tests[g_ut_test_count].func   = func;
    g_ut_tests[g_ut_test_count].serial = serial;
    g_ut_test_count++;
}

// =============================================================================
// cdo_ut_record_failure — Store failure details for the currently running test.
//
// When running in parallel mode, delegates to the parallel failure hook
// which uses thread-local storage instead of the global struct.
// =============================================================================

/* Defined in cdo_ut_parallel.c — non-NULL during parallel execution */
extern void (*g_ut_parallel_record_failure)(const char *file, int line,
                                            const char *expr,
                                            const char *actual,
                                            const char *expected);

void cdo_ut_record_failure(const char *file, int line,
                           const char *expr,
                           const char *actual, const char *expected)
{
    if (g_ut_parallel_record_failure != NULL) {
        g_ut_parallel_record_failure(file, line, expr, actual, expected);
        return;
    }

    g_ut_current_failure.file        = file;
    g_ut_current_failure.line        = line;
    g_ut_current_failure.expr        = expr;
    g_ut_current_failure.actual      = actual;
    g_ut_current_failure.expected    = expected;
    g_ut_current_failure.has_failure = true;
}

// =============================================================================
// Argument Parsing Helpers
// =============================================================================

typedef struct {
    bool        list_mode;
    const char *filter;
    int         jobs;
} CdoUtArgs;

static CdoUtArgs parse_args(int argc, char *argv[])
{
    CdoUtArgs args = {0};
    args.jobs = 1; // default: sequential

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) {
            args.list_mode = true;
        } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            args.filter = argv[++i];
        } else if (strcmp(argv[i], "--jobs") == 0 && i + 1 < argc) {
            args.jobs = atoi(argv[++i]);
            if (args.jobs < 1) args.jobs = 1;
        }
    }

    return args;
}

// =============================================================================
// main — Entry point for test binaries linked against cdo_ut.
// =============================================================================

int main(int argc, char *argv[])
{
    CdoUtArgs args = parse_args(argc, argv);

    // --list mode: print test names and exit
    if (args.list_mode) {
        for (int i = 0; i < g_ut_test_count; i++) {
            // If filter is set, only print matching tests
            if (args.filter) {
                if (!cdo_ut_filter_matches(g_ut_tests[i].name, args.filter)) {
                    continue;
                }
            }
            printf("%s\n", g_ut_tests[i].name);
        }
        return 0;
    }

    // Sequential execution loop
    int passed  = 0;
    int failed  = 0;
    int skipped = 0;
    int total   = 0;

    // Count matching tests first for protocol purposes
    for (int i = 0; i < g_ut_test_count; i++) {
        if (args.filter) {
            if (!cdo_ut_filter_matches(g_ut_tests[i].name, args.filter)) {
                continue;
            }
        }
        total++;
    }

    // When no tests match the filter, emit suite with total: 0 and exit 0
    if (total == 0) {
        cdo_ut_emit_suite_start(0);
        cdo_ut_emit_suite_end(0, 0, 0, 0, 0.0);
        return 0;
    }

    // =========================================================================
    // Parallel execution path (--jobs > 1)
    // =========================================================================
    if (args.jobs > 1) {
        // Separate tests into parallel and serial arrays
        CdoUtParallelEntry *par_tests = (CdoUtParallelEntry *)calloc(
            (size_t)total, sizeof(CdoUtParallelEntry));
        CdoUtParallelEntry *ser_tests = (CdoUtParallelEntry *)calloc(
            (size_t)total, sizeof(CdoUtParallelEntry));

        if (par_tests == NULL || ser_tests == NULL) {
            fprintf(stderr, "cdo_ut: failed to allocate test arrays\n");
            free(par_tests);
            free(ser_tests);
            return 2;
        }

        int par_count = 0;
        int ser_count = 0;

        for (int i = 0; i < g_ut_test_count; i++) {
            if (args.filter) {
                if (!cdo_ut_filter_matches(g_ut_tests[i].name, args.filter)) {
                    continue;
                }
            }
            CdoUtParallelEntry entry;
            entry.name   = g_ut_tests[i].name;
            entry.func   = g_ut_tests[i].func;
            entry.serial = g_ut_tests[i].serial;

            if (g_ut_tests[i].serial) {
                ser_tests[ser_count++] = entry;
            } else {
                par_tests[par_count++] = entry;
            }
        }

        // High-resolution timing for suite duration
        LARGE_INTEGER freq_par, suite_start_par, suite_end_par;
        QueryPerformanceFrequency(&freq_par);

        cdo_ut_emit_suite_start(total);
        QueryPerformanceCounter(&suite_start_par);

        int total_failed = cdo_ut_run_parallel(
            args.jobs, par_tests, par_count, ser_tests, ser_count);

        QueryPerformanceCounter(&suite_end_par);
        double total_dur = (double)(suite_end_par.QuadPart - suite_start_par.QuadPart)
                           * 1000.0 / (double)freq_par.QuadPart;

        int par_passed = total - total_failed;
        cdo_ut_emit_suite_end(total, par_passed, total_failed, 0, total_dur);

        free(par_tests);
        free(ser_tests);

        return (total_failed > 0) ? 1 : 0;
    }

    // =========================================================================
    // Sequential execution path (default, --jobs 1)
    // =========================================================================

    // High-resolution timing setup
    LARGE_INTEGER freq, suite_start_time, suite_end_time;
    LARGE_INTEGER test_start_time, test_end_time;
    QueryPerformanceFrequency(&freq);

    // Emit suite_start after discovery
    cdo_ut_emit_suite_start(total);
    QueryPerformanceCounter(&suite_start_time);

    int test_id = 0;

    for (int i = 0; i < g_ut_test_count; i++) {
        // If filter is set, skip non-matching tests
        if (args.filter) {
            if (!cdo_ut_filter_matches(g_ut_tests[i].name, args.filter)) {
                continue;
            }
        }

        // Emit test_start before execution
        cdo_ut_emit_test_start(g_ut_tests[i].name, test_id);

        // Clear failure state before running
        g_ut_current_failure.has_failure = false;

        // Time the test execution
        QueryPerformanceCounter(&test_start_time);

        // Execute the test function (0 = pass, non-zero = fail)
        int result = g_ut_tests[i].func();

        QueryPerformanceCounter(&test_end_time);

        // Compute duration in milliseconds
        double duration_ms = (double)(test_end_time.QuadPart - test_start_time.QuadPart)
                             * 1000.0 / (double)freq.QuadPart;

        // Emit test_end with results
        if (result == 0) {
            passed++;
            cdo_ut_emit_test_end(g_ut_tests[i].name, test_id, "pass",
                                 duration_ms, NULL, 0, NULL, NULL, NULL);
        } else {
            failed++;
            cdo_ut_emit_test_end(g_ut_tests[i].name, test_id, "fail",
                                 duration_ms,
                                 g_ut_current_failure.file,
                                 g_ut_current_failure.line,
                                 g_ut_current_failure.expr,
                                 g_ut_current_failure.actual,
                                 g_ut_current_failure.expected);
        }

        test_id++;
    }

    // Compute total suite duration
    QueryPerformanceCounter(&suite_end_time);
    double total_duration_ms = (double)(suite_end_time.QuadPart - suite_start_time.QuadPart)
                               * 1000.0 / (double)freq.QuadPart;

    // Emit suite_end — invariant: passed + failed + skipped == total
    cdo_ut_emit_suite_end(total, passed, failed, skipped, total_duration_ms);

    // Return non-zero if any test failed
    return (failed > 0) ? 1 : 0;
}
