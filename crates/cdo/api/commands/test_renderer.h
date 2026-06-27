#ifndef CDO_COMMANDS_TEST_RENDERER_H
#define CDO_COMMANDS_TEST_RENDERER_H

#include "commands/test_protocol.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Render a single test result line to stdout.
/// Pass: green checkmark prefix + test name + duration
/// Fail: red cross prefix + test name + duration, failure details indented below
/// Skip: yellow dash prefix + test name
/// When use_color is false, ANSI escape codes are omitted.
void test_renderer_result(const TestProtocolMsg *msg, bool use_color);

/// Render the final summary line to stdout.
/// Format: Results: X passed, Y failed, Z skipped (total: N, duration: 123.45ms)
/// If coverage_pct >= 0, appends coverage percentage.
/// Green when all pass, red when any fail.
void test_renderer_summary(int total, int passed, int failed, int skipped,
                           double duration_ms, double coverage_pct,
                           bool use_color);

/// Render the consolidated failure section to stdout.
/// Lists each failure with index, name, file, line, and assertion details.
/// Only prints if count > 0.
void test_renderer_failures(const TestProtocolMsg *failures, int count,
                            bool use_color);

/// Render progress indicator to stdout.
/// Format: [N/M] — just the fraction, printed inline.
/// When use_color is true, wraps in bold.
void test_renderer_progress(int completed, int total, bool use_color);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_TEST_RENDERER_H
