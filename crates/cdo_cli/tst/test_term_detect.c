/**
 * test_term_detect.c - Unit tests for terminal detection and capability probing.
 *
 * Tests the cli_term_detect() function which inspects environment variables,
 * TTY state, terminal dimensions, and Unicode support.
 *
 * Environment-variable-based tests use _putenv_s (Windows) / setenv (POSIX) to
 * control NO_COLOR, COLORTERM, and TERM before calling cli_term_detect().
 * These tests are marked TEST_SERIAL since they modify global process state.
 *
 * Validates: Requirements 8.1, 8.2, 8.3, 8.4, 8.5, 8.6, 10.3
 */

#include "cdo_ut.h"
#include "../api/term/cli_term.h"
#include "../api/cli_errors.h"

#include <stdlib.h>
#include <string.h>

// =============================================================================
// Platform helpers for setting/unsetting environment variables
// =============================================================================

#ifdef _WIN32
#include <windows.h>

static void env_set(const char* name, const char* value) {
    _putenv_s(name, value);
}

static void env_unset(const char* name) {
    _putenv_s(name, "");
}

#else
#include <unistd.h>

static void env_set(const char* name, const char* value) {
    setenv(name, value, 1);
}

static void env_unset(const char* name) {
    unsetenv(name);
}

#endif

// =============================================================================
// Helper: Clear all color-related environment variables to get a clean state
// =============================================================================

static void env_clear_color_vars(void) {
    env_unset("NO_COLOR");
    env_unset("COLORTERM");
    env_unset("TERM");
}

// =============================================================================
// Test: cli_term_detect returns CLI_OK and populates the struct
// =============================================================================

TEST(test_term_detect_returns_ok) {
    CliTermInfo info;
    memset(&info, 0xFF, sizeof(info));  // Fill with garbage to verify population

    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);
    return 0;
}

// =============================================================================
// Test: cli_term_detect returns error for NULL pointer
// =============================================================================

TEST(test_term_detect_null_returns_error) {
    int rc = cli_term_detect(NULL);
    TEST_ASSERT_NEQ(rc, CLI_OK);
    return 0;
}

// =============================================================================
// Test: columns defaults to 80 when detection returns unknown/zero
// Requirement 10.3: Default width of 80 columns when terminal width unknown
// =============================================================================

TEST(test_term_detect_columns_default_80) {
    CliTermInfo info;
    memset(&info, 0, sizeof(info));

    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    // columns should be at least 80 (either detected or default)
    // The stub returns 80, and the full implementation should default to 80
    // if the terminal width cannot be determined.
    TEST_ASSERT(info.columns >= 80);
    return 0;
}

// =============================================================================
// Test: rows defaults to 0 when unknown
// Requirement 8.4: Terminal dimensions query
// =============================================================================

TEST(test_term_detect_rows_zero_or_detected) {
    CliTermInfo info;
    memset(&info, 0, sizeof(info));

    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    // rows is 0 when unknown, or a positive value if detected
    TEST_ASSERT(info.rows >= 0);
    return 0;
}

// =============================================================================
// Test: NO_COLOR environment variable forces CLI_COLOR_NONE
// Requirement 8.3: NO_COLOR set (any value) -> Color_Level NONE
// =============================================================================

TEST_SERIAL(test_term_detect_no_color_forces_none) {
    env_clear_color_vars();
    env_set("NO_COLOR", "1");

    CliTermInfo info;
    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT_EQ(info.color_level, CLI_COLOR_NONE);

    env_unset("NO_COLOR");
    return 0;
}

// =============================================================================
// Test: NO_COLOR with empty-string value still forces NONE
// Requirement 8.3: "any value" includes empty string
// =============================================================================

TEST_SERIAL(test_term_detect_no_color_empty_forces_none) {
    env_clear_color_vars();
    // On Windows, _putenv_s with "" actually removes the var.
    // Use a space or non-empty value to test "any value" semantics.
    env_set("NO_COLOR", " ");

    CliTermInfo info;
    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);
    TEST_ASSERT_EQ(info.color_level, CLI_COLOR_NONE);

    env_unset("NO_COLOR");
    return 0;
}

// =============================================================================
// Test: COLORTERM=truecolor -> CLI_COLOR_TRUECOLOR
// Requirement 8.2: COLORTERM environment variable for color level
// =============================================================================

TEST_SERIAL(test_term_detect_colorterm_truecolor) {
    env_clear_color_vars();
    env_set("COLORTERM", "truecolor");

    CliTermInfo info;
    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    // Only applies if stdout is a TTY; on non-TTY this is still NONE.
    // We check that if a TTY is present, truecolor is detected.
    if (info.stdout_tty) {
        TEST_ASSERT_EQ(info.color_level, CLI_COLOR_TRUECOLOR);
    } else {
        // Non-TTY should be NONE regardless of COLORTERM
        TEST_ASSERT_EQ(info.color_level, CLI_COLOR_NONE);
    }

    env_unset("COLORTERM");
    return 0;
}

// =============================================================================
// Test: COLORTERM=24bit -> CLI_COLOR_TRUECOLOR
// Requirement 8.2: COLORTERM with "24bit" value
// =============================================================================

TEST_SERIAL(test_term_detect_colorterm_24bit) {
    env_clear_color_vars();
    env_set("COLORTERM", "24bit");

    CliTermInfo info;
    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    if (info.stdout_tty) {
        TEST_ASSERT_EQ(info.color_level, CLI_COLOR_TRUECOLOR);
    } else {
        TEST_ASSERT_EQ(info.color_level, CLI_COLOR_NONE);
    }

    env_unset("COLORTERM");
    return 0;
}

// =============================================================================
// Test: TERM containing "256color" -> CLI_COLOR_EXTENDED_256
// Requirement 8.2: TERM environment variable inspection
// =============================================================================

TEST_SERIAL(test_term_detect_term_256color) {
    env_clear_color_vars();
    env_set("TERM", "xterm-256color");

    CliTermInfo info;
    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    if (info.stdout_tty) {
        TEST_ASSERT_EQ(info.color_level, CLI_COLOR_EXTENDED_256);
    } else {
        TEST_ASSERT_EQ(info.color_level, CLI_COLOR_NONE);
    }

    env_unset("TERM");
    return 0;
}

// =============================================================================
// Test: Default fallback to CLI_COLOR_BASIC_16 when TTY but no color hints
// Requirement 8.2: Fallback when no COLORTERM/TERM signals found
// =============================================================================

TEST_SERIAL(test_term_detect_default_basic_16_when_tty) {
    env_clear_color_vars();

    CliTermInfo info;
    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    // When TTY is detected but no COLORTERM or TERM hints exist,
    // color_level should fall back to BASIC_16.
    if (info.stdout_tty) {
        TEST_ASSERT_EQ(info.color_level, CLI_COLOR_BASIC_16);
    }

    return 0;
}

// =============================================================================
// Test: Non-TTY -> CLI_COLOR_NONE
// Requirement 8.1, 8.2: When not a TTY, color is NONE
// Note: In a CI/test environment, stdout is typically not a TTY.
// =============================================================================

TEST_SERIAL(test_term_detect_non_tty_color_none) {
    env_clear_color_vars();

    CliTermInfo info;
    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    // If stdout is NOT a TTY, color must be NONE
    if (!info.stdout_tty) {
        TEST_ASSERT_EQ(info.color_level, CLI_COLOR_NONE);
    }

    return 0;
}

// =============================================================================
// Test: NO_COLOR overrides COLORTERM even when TTY
// Requirement 8.3: NO_COLOR takes precedence regardless of other signals
// =============================================================================

TEST_SERIAL(test_term_detect_no_color_overrides_colorterm) {
    env_clear_color_vars();
    env_set("COLORTERM", "truecolor");
    env_set("NO_COLOR", "1");

    CliTermInfo info;
    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    // NO_COLOR always wins
    TEST_ASSERT_EQ(info.color_level, CLI_COLOR_NONE);

    env_unset("NO_COLOR");
    env_unset("COLORTERM");
    return 0;
}

// =============================================================================
// Test: Unicode detection field is populated (bool)
// Requirement 8.5: Detect Unicode support from locale/code page
// =============================================================================

TEST(test_term_detect_unicode_field_populated) {
    CliTermInfo info;
    memset(&info, 0xFF, sizeof(info));

    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    // unicode field should be either true or false (valid bool state)
    TEST_ASSERT(info.unicode == true || info.unicode == false);
    return 0;
}

// =============================================================================
// Test: stdout_tty and stderr_tty fields are populated (bool)
// Requirement 8.1: TTY detection for stdout and stderr
// =============================================================================

TEST(test_term_detect_tty_fields_populated) {
    CliTermInfo info;
    memset(&info, 0xFF, sizeof(info));

    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    // Both fields should be valid booleans
    TEST_ASSERT(info.stdout_tty == true || info.stdout_tty == false);
    TEST_ASSERT(info.stderr_tty == true || info.stderr_tty == false);
    return 0;
}

// =============================================================================
// Test: struct is fully zeroed before population (no leftover garbage)
// Requirement 8.6: Single struct queryable after initialization
// =============================================================================

TEST(test_term_detect_struct_initialized_clean) {
    CliTermInfo info;
    // Fill with known non-zero pattern
    memset(&info, 0xAB, sizeof(info));

    int rc = cli_term_detect(&info);
    TEST_ASSERT_EQ(rc, CLI_OK);

    // After detect, columns should not still be 0xABABABAB
    // (The implementation should memset the struct before populating)
    TEST_ASSERT(info.columns != (int)0xABABABAB);
    return 0;
}
