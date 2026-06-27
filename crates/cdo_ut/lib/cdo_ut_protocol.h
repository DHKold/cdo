/*
 * cdo_ut_protocol.h — Internal structured protocol emission for cdo_ut.
 *
 * Provides functions to emit JSON Lines messages to stdout for the
 * test runner to consume. Each function writes exactly one JSON object
 * per line and flushes stdout.
 *
 * This is an internal header — not part of the public API.
 */

#ifndef CDO_UT_PROTOCOL_H
#define CDO_UT_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Emit suite_start message indicating total number of tests to run.
 * Output: {"type":"suite_start","total":N}
 */
void cdo_ut_emit_suite_start(int total);

/*
 * Emit test_start message indicating a test is beginning.
 * Output: {"type":"test_start","name":"...","id":N}
 */
void cdo_ut_emit_test_start(const char *name, int id);

/*
 * Emit test_end message indicating a test has completed.
 *
 * Parameters:
 *   name        - test name
 *   id          - test identifier
 *   status      - "pass", "fail", or "skip"
 *   duration_ms - elapsed time in milliseconds
 *   file        - source file of failure (NULL if pass/skip)
 *   line        - line number of failure (0 if pass/skip)
 *   expr        - assertion expression (NULL if pass/skip)
 *   actual      - actual value string (NULL if pass/skip or not applicable)
 *   expected    - expected value string (NULL if pass/skip or not applicable)
 *
 * Output (pass/skip): {"type":"test_end","name":"...","id":N,"status":"...","duration_ms":X.XX}
 * Output (fail):      {"type":"test_end","name":"...","id":N,"status":"fail","duration_ms":X.XX,
 *                       "failure":{"file":"...","line":N,"expr":"...","actual":"...","expected":"..."}}
 */
void cdo_ut_emit_test_end(const char *name, int id, const char *status,
                          double duration_ms,
                          const char *file, int line, const char *expr,
                          const char *actual, const char *expected);

/*
 * Emit suite_end message with final totals.
 * Output: {"type":"suite_end","total":N,"passed":N,"failed":N,"skipped":N,"duration_ms":X.XX}
 */
void cdo_ut_emit_suite_end(int total, int passed, int failed, int skipped,
                           double duration_ms);

/*
 * Emit error message for fatal initialization errors.
 * Output: {"type":"error","message":"..."}
 */
void cdo_ut_emit_error(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* CDO_UT_PROTOCOL_H */
