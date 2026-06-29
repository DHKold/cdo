/**
 * test_term_ansi.c - Unit tests for cli_term_enable_ansi().
 *
 * Validates Requirements 9.2, 9.3:
 *   - POSIX: no-op, returns CLI_OK (0)
 *   - Windows: returns CLI_OK when VT processing is enabled successfully
 *   - Windows: returns CLI_ERR_PLATFORM when SetConsoleMode fails (requires mocked impl, see task 3.4)
 */
#include "cdo_ut.h"
#include "../api/term/cli_term.h"
#include "../api/cli_errors.h"

/* ==========================================================================
 * Test: cli_term_enable_ansi returns CLI_OK on current platform.
 *
 * On POSIX this is a no-op (always returns 0).
 * On Windows with VT support (modern terminals) it should also succeed.
 * The current stub implementation returns CLI_OK unconditionally.
 *
 * Validates: Requirement 9.3 (no-op on POSIX), 9.2 (success on Windows)
 * ========================================================================== */
TEST(term_ansi_enable_returns_ok) {
    int rc = cli_term_enable_ansi();
    TEST_ASSERT_EQ(rc, CLI_OK);
    return 0;
}

/* ==========================================================================
 * Test: cli_term_enable_ansi is idempotent - calling multiple times is safe.
 *
 * Regardless of platform, repeated calls should all return CLI_OK.
 * Validates: Requirement 9.2, 9.3
 * ========================================================================== */
TEST(term_ansi_enable_idempotent) {
    int rc1 = cli_term_enable_ansi();
    int rc2 = cli_term_enable_ansi();
    int rc3 = cli_term_enable_ansi();

    TEST_ASSERT_EQ(rc1, CLI_OK);
    TEST_ASSERT_EQ(rc2, CLI_OK);
    TEST_ASSERT_EQ(rc3, CLI_OK);
    return 0;
}

/* ==========================================================================
 * Test: Verify CLI_ERR_PLATFORM constant is available and distinct from CLI_OK.
 *
 * This validates that the error code we expect from failure paths is properly
 * defined. Full failure-path testing (SetConsoleMode fails) requires the
 * mocked implementation from task 3.4.
 *
 * Validates: Requirement 9.2 (error code on failure)
 * ========================================================================== */
TEST(term_ansi_error_code_defined) {
    TEST_ASSERT_NEQ(CLI_ERR_PLATFORM, CLI_OK);
    TEST_ASSERT_EQ(CLI_ERR_PLATFORM, 7);
    return 0;
}
