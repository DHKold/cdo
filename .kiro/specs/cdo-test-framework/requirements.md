# Requirements Document

## Introduction

This document specifies the requirements for the CDo Test Framework — a comprehensive testing infrastructure for the CDo build system. The framework introduces a dedicated test library crate (`cdo_ut`) that provides test registration, assertion macros, and a structured reporting protocol. The `cdo test` runner consumes this structured output to produce visually rich, colored terminal reports with progress indication, detailed summaries, and consolidated failure lists. The framework also adds dry-run/list capabilities, test filtering, and coverage integration via GCC's gcov tooling.

## Glossary

- **Test_Runner**: The `cdo test` command that orchestrates building, spawning, and reporting on test binaries
- **Test_Binary**: The compiled executable produced from a crate's `tst/` module that discovers and executes individual tests
- **cdo_ut**: A dedicated CDo crate (`crates/cdo_ut`) providing the unit test framework library (registration macros, assertion macros, structured output protocol)
- **Test_Case**: A single named test function registered via the `TEST()` macro
- **Test_Suite**: A logical grouping of Test_Cases within a single crate
- **Structured_Protocol**: A machine-readable line-oriented output format (JSON Lines) emitted by the Test_Binary on stdout for the Test_Runner to parse
- **Progress_Bar**: A visual inline indicator showing test execution progress as a fraction of total tests
- **ANSI_Colors**: Terminal escape sequences used for colored output (green for pass, red for fail, yellow for skip)
- **Dry_Run**: A mode where the Test_Runner lists available tests without executing them
- **Test_Filter**: A glob or substring pattern used to select which Test_Cases to execute
- **Coverage_Report**: A summary of source lines exercised during test execution, produced via GCC `--coverage` and gcov
- **w64devkit**: The MinGW-w64 GCC toolchain distribution bundled with CDo for Windows builds

## Requirements

### Requirement 1: cdo_ut Crate Structure

**User Story:** As a CDo developer, I want a dedicated test framework crate, so that any crate in the workspace can depend on it for structured, consistent testing.

#### Acceptance Criteria

1. THE cdo_ut crate SHALL provide a `lib/` module containing test runner infrastructure, assertion macros, and structured output emission logic
2. THE cdo_ut crate SHALL provide an `api/` module exposing public headers (`cdo_ut.h`) for consumption by dependent crates
3. WHEN a crate's `tst/` module includes `cdo_ut.h`, THE cdo_ut crate SHALL supply the `TEST()` macro for auto-registering Test_Cases using GCC constructor attributes
4. THE cdo_ut crate SHALL supply assertion macros including `TEST_ASSERT`, `TEST_ASSERT_EQ`, `TEST_ASSERT_STR_EQ`, `TEST_ASSERT_NEQ`, and `TEST_ASSERT_NULL`
5. WHEN an assertion macro fails, THE cdo_ut crate SHALL record the file path, line number, assertion expression, and actual vs expected values in the test result

### Requirement 2: Structured Reporting Protocol

**User Story:** As a CDo developer, I want the test binary to emit machine-readable output, so that the test runner can parse results reliably and produce rich reports.

#### Acceptance Criteria

1. WHEN test execution begins, THE Test_Binary SHALL emit a JSON Lines message indicating the total number of discovered Test_Cases
2. WHEN a Test_Case starts, THE Test_Binary SHALL emit a JSON Lines message containing the test name
3. WHEN a Test_Case completes, THE Test_Binary SHALL emit a JSON Lines message containing the test name, pass/fail status, duration in milliseconds, and any failure details
4. WHEN all Test_Cases have completed, THE Test_Binary SHALL emit a JSON Lines summary message containing total count, pass count, fail count, skip count, and total duration
5. THE Structured_Protocol SHALL use one JSON object per line on stdout, with a `"type"` field distinguishing message kinds (`suite_start`, `test_start`, `test_end`, `suite_end`)
6. IF the Test_Binary encounters a fatal error during initialization, THEN THE Test_Binary SHALL emit a JSON Lines error message with type `error` and a descriptive message, then exit with a non-zero code

### Requirement 3: Colored and Structured Console Output

**User Story:** As a CDo developer, I want visually clear and colored test output in my terminal, so that I can quickly identify pass/fail status at a glance.

#### Acceptance Criteria

1. WHEN the Test_Runner displays a passing Test_Case result, THE Test_Runner SHALL render the test name prefixed with a green checkmark symbol
2. WHEN the Test_Runner displays a failing Test_Case result, THE Test_Runner SHALL render the test name prefixed with a red cross symbol, followed by the failure details indented below
3. WHILE tests are executing, THE Test_Runner SHALL display a Progress_Bar showing the count of completed tests out of the total (e.g., `[12/47]`)
4. WHEN the terminal does not support ANSI escape sequences, THE Test_Runner SHALL omit color codes and render plain text output
5. THE Test_Runner SHALL group test results by crate name with a header line for each crate

### Requirement 4: Detailed Summary

**User Story:** As a CDo developer, I want a detailed summary after test execution, so that I can see an at-a-glance overview of the entire test run.

#### Acceptance Criteria

1. WHEN all tests across all crates have completed, THE Test_Runner SHALL display a summary section containing: total test count, passed count, failed count, skipped count, and total wall-clock duration
2. WHEN coverage data is available, THE Test_Runner SHALL include line coverage percentage in the summary section
3. WHEN at least one test fails, THE Test_Runner SHALL display the summary with a red background or red-colored totals
4. WHEN all tests pass, THE Test_Runner SHALL display the summary with a green-colored totals line

### Requirement 5: Consolidated Failure List

**User Story:** As a CDo developer, I want all failures listed together at the end of the run, so that I do not have to scroll through passing output to find what broke.

#### Acceptance Criteria

1. WHEN at least one Test_Case fails across any crate, THE Test_Runner SHALL display a "Failures:" section after the summary listing each failed test
2. THE Test_Runner SHALL display each failed test in the "Failures:" section with the crate name, test name, file path, and line number of the first assertion failure
3. WHEN all tests pass, THE Test_Runner SHALL omit the "Failures:" section entirely

### Requirement 6: Dry-Run and Test Listing

**User Story:** As a CDo developer, I want to list available tests without running them, so that I can discover test names for filtering and verify registration.

#### Acceptance Criteria

1. WHEN the user invokes `cdo test --list`, THE Test_Runner SHALL build the test binary and instruct it to emit only discovered test names without executing them
2. THE Test_Binary SHALL support a `--list` argument that causes it to print all registered Test_Case names (one per line) and exit with code 0
3. WHEN `--list` is combined with `--filter`, THE Test_Binary SHALL print only test names matching the filter pattern
4. THE Test_Runner SHALL display listed tests grouped by crate name

### Requirement 7: Test Filtering

**User Story:** As a CDo developer, I want to run only a subset of tests by name pattern, so that I can iterate quickly on specific functionality.

#### Acceptance Criteria

1. WHEN the user provides a `--filter <pattern>` argument to `cdo test`, THE Test_Runner SHALL pass the pattern to each Test_Binary
2. THE Test_Binary SHALL execute only Test_Cases whose names contain the filter pattern as a substring
3. WHEN a filter pattern contains a `*` character, THE Test_Binary SHALL treat it as a glob wildcard matching zero or more characters
4. WHEN no Test_Cases match the filter, THE Test_Binary SHALL emit the suite summary with zero tests run and exit with code 0

### Requirement 8: Coverage Integration

**User Story:** As a CDo developer, I want to measure code coverage of my tests, so that I can identify untested code paths.

#### Acceptance Criteria

1. WHEN the user invokes `cdo test --coverage`, THE Test_Runner SHALL compile the test binary and its dependencies with GCC flags `--coverage -fprofile-arcs -ftest-coverage`
2. WHEN a test run with `--coverage` completes, THE Test_Runner SHALL invoke `gcov` on the generated `.gcda` files to produce coverage data
3. THE Test_Runner SHALL parse gcov output and display per-file line coverage percentages
4. THE Test_Runner SHALL display an aggregate line coverage percentage in the test summary
5. IF gcov is not found in the toolchain path, THEN THE Test_Runner SHALL display an error message indicating gcov is required for coverage and exit with a non-zero code

### Requirement 9: Portability and Platform Support

**User Story:** As a CDo developer, I want the test framework to work on Windows with w64devkit and be portable to other GCC platforms, so that tests run consistently across environments.

#### Acceptance Criteria

1. THE cdo_ut crate SHALL compile and function correctly with GCC 13.x on Windows via w64devkit
2. THE cdo_ut crate SHALL use only C17-compliant standard library functions and GCC extensions (`__attribute__((constructor))`) for test registration
3. WHEN compiled on a platform without GCC constructor attribute support, THE cdo_ut crate SHALL provide a fallback manual registration mechanism
4. THE Structured_Protocol SHALL use only ASCII-safe JSON encoding to avoid character encoding issues across platforms
5. THE Test_Runner SHALL detect terminal capabilities (ANSI support) at runtime rather than assuming a specific terminal type

### Requirement 10: Parallel Test Execution

**User Story:** As a CDo developer, I want tests within a crate to run in parallel, so that the test suite completes faster on multi-core machines.

#### Acceptance Criteria

1. WHEN the user invokes `cdo test --jobs <N>`, THE Test_Runner SHALL instruct the Test_Binary to execute up to N Test_Cases concurrently
2. THE Test_Binary SHALL default to sequential execution when no `--jobs` argument is provided
3. WHILE executing tests in parallel, THE Test_Binary SHALL isolate stdout/stderr capture per Test_Case to prevent interleaved output
4. WHEN parallel execution is active, THE Structured_Protocol messages SHALL include a test identifier to allow the Test_Runner to correlate results correctly
5. IF a Test_Case is not safe for parallel execution, THEN THE cdo_ut crate SHALL provide a `TEST_SERIAL()` macro to mark it for sequential-only execution
