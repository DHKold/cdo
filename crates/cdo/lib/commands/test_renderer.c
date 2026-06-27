#include "commands/test_renderer.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// ANSI color codes
// ---------------------------------------------------------------------------

#define ANSI_GREEN  "\033[32m"
#define ANSI_RED    "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_RESET  "\033[0m"

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void test_renderer_result(const TestProtocolMsg *msg, bool use_color) {
    if (!msg) return;

    switch (msg->status) {
    case TEST_STATUS_PASS:
        if (use_color) {
            printf("  %sPASS%s %s (%.2fms)\n",
                   ANSI_GREEN, ANSI_RESET, msg->name, msg->duration_ms);
        } else {
            printf("  PASS %s (%.2fms)\n",
                   msg->name, msg->duration_ms);
        }
        break;

    case TEST_STATUS_FAIL:
        if (use_color) {
            printf("  %sFAIL%s %s (%.2fms)\n",
                   ANSI_RED, ANSI_RESET, msg->name, msg->duration_ms);
        } else {
            printf("  FAIL %s (%.2fms)\n",
                   msg->name, msg->duration_ms);
        }
        // Print failure details indented below
        if (msg->failure.file[0] != '\0') {
            printf("    at %s:%d: %s",
                   msg->failure.file, msg->failure.line, msg->failure.expr);
            if (msg->failure.actual[0] != '\0' || msg->failure.expected[0] != '\0') {
                printf(" (actual: %s, expected: %s)", msg->failure.actual, msg->failure.expected);
            }
            printf("\n");
        }
        break;

    case TEST_STATUS_SKIP:
        if (use_color) {
            printf("  %sSKIP%s %s\n", ANSI_YELLOW, ANSI_RESET, msg->name);
        } else {
            printf("  SKIP %s\n", msg->name);
        }
        break;
    }
}

void test_renderer_summary(int total, int passed, int failed, int skipped,
                           double duration_ms, double coverage_pct,
                           bool use_color) {
    const char *color_start = "";
    const char *color_end   = "";

    if (use_color) {
        color_start = (failed > 0) ? ANSI_RED : ANSI_GREEN;
        color_end   = ANSI_RESET;
    }

    printf("\n%sResults: %d passed, %d failed, %d skipped (total: %d, duration: %.2fms)",
           color_start, passed, failed, skipped, total, duration_ms);

    if (coverage_pct >= 0.0) {
        printf(", coverage: %.1f%%", coverage_pct);
    }

    printf("%s\n", color_end);
}

void test_renderer_failures(const TestProtocolMsg *failures, int count,
                            bool use_color) {
    if (!failures || count <= 0) return;

    (void)use_color; // Color not applied to failure section per spec

    printf("\nFailures:\n");

    for (int i = 0; i < count; i++) {
        const TestProtocolMsg *f = &failures[i];
        printf("  %d) %s\n", i + 1, f->name);
        if (f->failure.file[0] != '\0') {
            printf("     at %s:%d: %s", f->failure.file, f->failure.line, f->failure.expr);
            if (f->failure.actual[0] != '\0' || f->failure.expected[0] != '\0') {
                printf(" (actual: %s, expected: %s)", f->failure.actual, f->failure.expected);
            }
            printf("\n");
        }
    }
}

void test_renderer_progress(int completed, int total, bool use_color) {
    if (use_color) {
        printf("%s[%d/%d]%s", ANSI_BOLD, completed, total, ANSI_RESET);
    } else {
        printf("[%d/%d]", completed, total);
    }
}
