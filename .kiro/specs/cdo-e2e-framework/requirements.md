# Requirements Document

## Introduction

This document specifies requirements for a dedicated end-to-end (e2e) testing framework for CDo. The framework provides a generic, powerful library (`cdo_e2e`) that enables developers to test any executable crate within a deterministic, isolated environment. Tests define the filesystem layout, environment variables, and command invocations, then assert on exit codes, stdout/stderr content, and resulting filesystem state. CDo's build system gains a new `e2e/` module type and a `cdo e2e` command to discover and run these tests. As a first consumer, the CDo project itself uses this framework to test `cdo.exe` against realistic workspace scenarios — but the framework contains no cdo-specific logic.

## Glossary

- **CDo**: The C/C++ build tool that manages workspaces containing crates with modules.
- **Workspace**: A directory containing a `cdo.toml` file with `[workspace] members = [...]` listing crate paths.
- **Crate**: A named unit of compilation within a workspace, defined by a `crate.toml` file.
- **Module**: A subdirectory within a crate representing a compilation target type (lib/, exe/, tst/, dyn/, shd/, res/, api/).
- **E2E_Module**: A new module type represented by the `e2e/` directory within a crate, containing end-to-end test source files.
- **E2E_Framework**: The `cdo_e2e` library crate providing generic utilities for writing end-to-end tests (environment setup, subprocess execution, assertions, fixtures).
- **E2E_Runner**: The component of CDo responsible for discovering, building, and executing e2e test executables produced from E2E_Module sources.
- **Test_Environment**: An isolated temporary directory prepared by the E2E_Framework for a single test, containing a defined filesystem layout and environment variable set.
- **Fixture**: A predefined directory tree (template) that a test copies into its Test_Environment before execution.
- **Spawn_Result**: A structure holding the captured stdout, stderr, and exit code from a subprocess invocation.
- **Test_Protocol**: The structured line-based output format used by CDo test executables to communicate results (suite start/end, test start/end, pass/fail/skip, duration).
- **PAL**: Platform Abstraction Layer, the cross-platform utility layer used by CDo (return 0 = success, non-zero = error code).

## Requirements

### Requirement 1: E2E Module Type Recognition

**User Story:** As a CDo user, I want to add an `e2e/` directory to my crate, so that CDo recognizes it as containing end-to-end tests separate from unit tests.

#### Acceptance Criteria

1. WHEN a crate directory contains an `e2e/` subdirectory with at least one `.c` or `.cpp` source file outside of the `e2e/fixtures/` subdirectory, THE CDo SHALL recognize the E2E_Module as present for that crate.
2. THE CDo SHALL treat the E2E_Module as a distinct module kind separate from MODULE_TST, MODULE_LIB, MODULE_EXE, MODULE_DYN, MODULE_SHD, and MODULE_RES.
3. WHEN scanning a crate for E2E_Module sources, THE CDo SHALL discover `.c` and `.cpp` source files within the `e2e/` directory recursively, excluding any files within the `e2e/fixtures/` subdirectory tree.
4. THE CDo SHALL compile the E2E_Module into an executable artifact named `<crate_name>_e2e.exe` on Windows and `<crate_name>_e2e` on other platforms.
5. WHEN the user invokes `cdo build` without specifying the E2E_Module, THE CDo SHALL NOT build the E2E_Module.
6. WHEN the user invokes `cdo build` and explicitly targets the E2E_Module (via `cdo build <crate> --e2e` or `cdo e2e <crate>`), THE CDo SHALL build the E2E_Module for the specified crate.
7. IF the `e2e/` subdirectory exists but contains no `.c` or `.cpp` source files outside of `e2e/fixtures/`, THEN THE CDo SHALL NOT recognize an E2E_Module for that crate.

### Requirement 2: E2E Command Discovery and Execution

**User Story:** As a CDo user, I want a `cdo e2e` command that discovers and runs all e2e tests in my workspace, so that I can validate my project in realistic end-to-end scenarios.

#### Acceptance Criteria

1. WHEN the user invokes `cdo e2e`, THE E2E_Runner SHALL discover all crates in the workspace that have a present E2E_Module and execute their e2e tests in workspace member order.
2. WHEN the user invokes `cdo e2e <crate_name>`, THE E2E_Runner SHALL run e2e tests only for the specified crate.
3. WHEN a crate has a present E2E_Module, THE E2E_Runner SHALL acquire the build lock (with a default timeout of 30 seconds) and build the E2E_Module executable using the debug profile before executing it.
4. WHEN the E2E_Module build fails for a crate, THE E2E_Runner SHALL log an error message identifying the crate name and continue to the next crate.
5. WHEN the E2E_Module executable is built successfully, THE E2E_Runner SHALL execute it as a subprocess and capture the Test_Protocol output from stdout.
6. WHEN all e2e test crates have been executed, THE E2E_Runner SHALL display an aggregate summary showing total tests, passed, failed, skipped, and wall-clock duration in milliseconds.
7. THE E2E_Runner SHALL return exit code 0 when all e2e tests pass, exit code 1 when any test fails, and exit code 2 when an infrastructure error occurs without test failures.
8. IF the user invokes `cdo e2e <crate_name>` and the specified crate does not exist in the workspace or does not have a present E2E_Module, THEN THE E2E_Runner SHALL log an error message identifying the crate name and return exit code 2.
9. IF no crates with a present E2E_Module are found in the workspace, THEN THE E2E_Runner SHALL log an error message and return exit code 2.
10. IF the E2E_Module executable exits with a non-zero code without producing a suite_end Test_Protocol message, THEN THE E2E_Runner SHALL treat the crate as an infrastructure error, log the crate name and exit code, and continue to the next crate.

### Requirement 3: E2E Command Options

**User Story:** As a CDo user, I want `cdo e2e` to support filtering, listing, profile selection, and job control, so that I can control e2e test execution the same way I control unit tests.

#### Acceptance Criteria

1. WHEN the user passes `--filter <pattern>` to `cdo e2e`, THE E2E_Runner SHALL forward the filter pattern to the e2e test executable as a `--filter` argument for substring-based test name matching.
2. WHEN the user passes `--list` to `cdo e2e`, THE E2E_Runner SHALL pass `--list` to each discovered e2e test executable and display the test names printed to stdout without executing any tests.
3. WHEN the user passes `--release` to `cdo e2e` without `--profile`, THE E2E_Runner SHALL build and run e2e tests using the "release" profile.
4. WHEN the user passes `--profile <name>` to `cdo e2e`, THE E2E_Runner SHALL build and run e2e tests using the specified build profile, overriding `--release` if both are present.
5. IF neither `--release` nor `--profile` is passed to `cdo e2e`, THEN THE E2E_Runner SHALL build and run e2e tests using the "debug" profile.
6. WHEN the user passes `--jobs <N>` to `cdo e2e` where N is an integer greater than or equal to 1, THE E2E_Runner SHALL forward the job count to the test executable as a `--jobs` argument for parallel test execution.
7. WHEN the user passes `--verbose` to `cdo e2e`, THE E2E_Runner SHALL enable debug-level log output during build and execution.
8. WHEN the user passes `--timeout <seconds>` to `cdo e2e` where seconds is a positive integer, THE E2E_Runner SHALL terminate any individual e2e test that exceeds the specified duration and report the test as failed with an indication that it was terminated due to timeout.
9. IF neither `--timeout` is specified, THEN THE E2E_Runner SHALL run e2e tests with no time limit per test.

### Requirement 4: E2E Framework Library — Test Environment Setup

**User Story:** As a developer writing e2e tests, I want utility functions to create isolated temporary directories with a defined filesystem layout, so that each test runs in a deterministic, clean environment.

#### Acceptance Criteria

1. THE E2E_Framework SHALL provide a function to create an isolated temporary directory for a test under the system temporary directory, returning the absolute path to the created directory.
2. THE E2E_Framework SHALL generate unique temporary directory names that include the test name (truncated to at most 64 characters) and a random or sequential suffix to prevent collisions between parallel test runs.
3. THE E2E_Framework SHALL provide a function to create files within the Test_Environment with a specified relative path, a pointer to content data, and a content length in bytes, creating any intermediate parent directories that do not yet exist.
4. THE E2E_Framework SHALL provide a function to create directories within the Test_Environment at specified relative paths, creating any intermediate parent directories that do not yet exist.
5. IF temporary directory creation fails, THEN THE E2E_Framework SHALL return a non-zero error code and log the failure reason.
6. IF file or directory creation within the Test_Environment fails, THEN THE E2E_Framework SHALL return a non-zero error code and log the failure reason including the target relative path.
7. IF a relative path passed to a file or directory creation function resolves to a location outside the Test_Environment root (e.g., contains `..` components that escape), THEN THE E2E_Framework SHALL reject the operation and return a non-zero error code without creating any file or directory.
8. THE E2E_Framework SHALL provide a function to set environment variables for subprocesses spawned within the Test_Environment, storing overrides in a structure passed to subprocess spawn calls without modifying the host process environment.
9. THE E2E_Framework SHALL provide a function to destroy the Test_Environment directory and all its contents recursively after a test completes.

### Requirement 5: E2E Framework Library — Subprocess Execution

**User Story:** As a developer writing e2e tests, I want utilities to spawn any executable as a subprocess with captured output and configurable environment, so that I can verify program behavior end-to-end.

#### Acceptance Criteria

1. THE E2E_Framework SHALL provide a function to spawn a subprocess that accepts a Test_Environment context, an executable path, an argument list of up to 128 arguments, and optional environment variable overrides that are merged with the Test_Environment's configured environment variables.
2. THE E2E_Framework SHALL capture the subprocess stdout, stderr (each up to 16 MB), and exit code into a Spawn_Result structure.
3. WHEN the caller specifies a timeout value greater than zero in milliseconds, THE E2E_Framework SHALL terminate the subprocess after the timeout elapses, set a timeout flag in the Spawn_Result, and retain any stdout and stderr output captured before termination.
4. IF the caller does not specify a timeout, THEN THE E2E_Framework SHALL apply a default timeout of 120000 milliseconds (2 minutes).
5. WHEN the subprocess cannot be started, THE E2E_Framework SHALL return a non-zero error code and set an error description in the Spawn_Result indicating the reason for failure.
6. THE E2E_Framework SHALL use the Test_Environment's configured working directory as the subprocess working directory unless the caller provides an explicit working directory override.
7. THE E2E_Framework SHALL provide a function to free resources associated with a Spawn_Result (captured buffers).

### Requirement 6: E2E Framework Library — Assertions

**User Story:** As a developer writing e2e tests, I want e2e-specific assertion macros that check exit codes, output content, and filesystem state, so that I can write expressive e2e test validations.

#### Acceptance Criteria

1. THE E2E_Framework SHALL provide an assertion macro to verify that a Spawn_Result exit code equals an expected integer value, reporting the actual and expected values on failure and causing the calling test function to return 1.
2. THE E2E_Framework SHALL provide an assertion macro to verify that a Spawn_Result stdout contains a specified substring using case-sensitive matching, reporting the expected substring and actual stdout on failure and causing the calling test function to return 1.
3. THE E2E_Framework SHALL provide an assertion macro to verify that a Spawn_Result stderr contains a specified substring using case-sensitive matching, reporting the expected substring and actual stderr on failure and causing the calling test function to return 1.
4. THE E2E_Framework SHALL provide an assertion macro to verify that a file exists at a specified path, reporting the expected path on failure and causing the calling test function to return 1.
5. THE E2E_Framework SHALL provide an assertion macro to verify that a file does not exist at a specified path, reporting the path on failure and causing the calling test function to return 1.
6. THE E2E_Framework SHALL provide an assertion macro to verify that a file's content contains a specified substring using case-sensitive matching, reporting the expected substring and file path on failure and causing the calling test function to return 1.
7. WHEN any e2e assertion fails, THE E2E_Framework SHALL record the failure with file name, line number, and descriptive message by calling cdo_ut_record_failure, matching the pattern used by TEST_ASSERT and TEST_ASSERT_EQ in cdo_ut.
8. IF a stdout or stderr assertion macro is invoked on a Spawn_Result whose corresponding buffer is NULL, THEN THE E2E_Framework SHALL treat the assertion as failed, reporting that the output buffer was not captured.
9. IF the file content assertion macro cannot read the file at the specified path, THEN THE E2E_Framework SHALL treat the assertion as failed, reporting the file path and the reason the file could not be read.

### Requirement 7: E2E Framework Library — Fixture Management

**User Story:** As a developer writing e2e tests, I want a structured approach to managing test fixtures (template directory trees), so that I can reuse environment templates across multiple tests.

#### Acceptance Criteria

1. THE E2E_Framework SHALL support fixtures stored as directories within the crate's `e2e/fixtures/` subdirectory.
2. WHEN a test requests a fixture by name, THE E2E_Framework SHALL locate the fixture directory at `<crate_path>/e2e/fixtures/<fixture_name>/`, where fixture names consist of alphanumeric characters, hyphens, and underscores with a maximum length of 64 characters.
3. THE E2E_Framework SHALL provide a function to recursively copy a fixture into a Test_Environment, reproducing all subdirectories (including empty ones) and file contents with identical relative paths to the fixture root.
4. IF a requested fixture directory does not exist at the resolved path, THEN THE E2E_Framework SHALL return a non-zero error code and log a message identifying the missing fixture name and expected path.
5. THE E2E_Framework SHALL support fixtures containing directory trees up to 16 levels deep and up to 10,000 files total.
6. THE E2E_Framework SHALL provide a function to set the crate root path used for fixture resolution, which must be called before any fixture copy operations.
7. WHEN the CDo module scanner discovers source files in a crate's `e2e/` directory, THE CDo SHALL exclude the `e2e/fixtures/` subdirectory and its contents from compilation source file discovery.
8. IF a fixture copy operation fails for any file or directory within the fixture, THEN THE E2E_Framework SHALL return a non-zero error code and log a message identifying the file path that failed to copy.

### Requirement 8: E2E Test Isolation

**User Story:** As a developer, I want each e2e test to run in complete isolation, so that tests do not interfere with each other or with the host environment.

#### Acceptance Criteria

1. THE E2E_Framework SHALL create a unique temporary directory for each e2e test invocation, using a naming scheme that includes a monotonic counter or PID combined with a timestamp to prevent filesystem conflicts between concurrent tests.
2. THE E2E_Framework SHALL NOT modify the original fixture directory or any directory outside the Test_Environment during test execution.
3. WHEN the E2E_Runner executes e2e tests without a `--jobs` option, THE E2E_Runner SHALL run each e2e test function serially to avoid subprocess contention.
4. WHEN the user passes `--jobs <N>` with N in the range 2 to 64 to `cdo e2e`, THE E2E_Runner SHALL execute up to N e2e tests in parallel, each in its own separate temporary directory.
5. IF the user passes `--jobs <N>` with N less than 1 or greater than 64 to `cdo e2e`, THEN THE E2E_Runner SHALL reject the value with an error message indicating the valid range and exit with a non-zero exit code without running tests.
6. WHEN a test completes (pass or fail) and `--keep-temps` has not been passed, THE E2E_Framework SHALL delete the temporary directory and all its contents.
7. IF deletion of the temporary directory fails, THEN THE E2E_Framework SHALL log a warning identifying the directory path and continue execution without failing the test.
8. WHEN the user passes `--keep-temps` to `cdo e2e`, THE E2E_Framework SHALL preserve the temporary directory after test completion to allow debugging of test artifacts.

### Requirement 9: E2E Test Registration and Protocol

**User Story:** As a developer, I want e2e tests to use the same registration and protocol mechanisms as unit tests, so that test infrastructure is consistent and tooling works across both.

#### Acceptance Criteria

1. THE E2E_Framework SHALL use the same TEST and TEST_SERIAL macros from cdo_ut for e2e test function registration, linking against the cdo_ut library which provides the main() entry point and protocol emission.
2. THE E2E_Module executable SHALL produce JSON Lines output on stdout where each line is a JSON object with a "type" field matching one of: "suite_start", "test_start", "test_end", "suite_end", or "error", parseable by `test_protocol_parse_line` without returning -1.
3. THE E2E_Runner SHALL parse the Test_Protocol output from e2e executables using the same `test_protocol_parse_line` function used by `cdo test`.
4. WHEN an e2e test function returns 0, THE E2E_Framework SHALL emit a "test_end" JSON line with status "pass", the test name, test id, and duration in milliseconds.
5. WHEN an e2e test function returns non-zero, THE E2E_Framework SHALL emit a "test_end" JSON line with status "fail", the test name, test id, duration in milliseconds, and a "failure" object containing the file, line, expression, actual value, and expected value recorded by assertion macros.
6. WHEN all e2e tests in a module have executed, THE E2E_Module executable SHALL exit with code 0 if no tests failed, or exit with code 1 if one or more tests failed.
7. IF an E2E_Module executable exits with a non-zero code that does not match the protocol-reported failure count, THEN THE E2E_Runner SHALL treat the execution as a crash and report all un-reported tests as failed.

### Requirement 10: E2E Module Build Dependencies

**User Story:** As a developer, I want the E2E_Module to automatically link against cdo_ut and cdo_e2e libraries, so that I can use both test macros and e2e utilities without manual configuration.

#### Acceptance Criteria

1. WHEN building an E2E_Module, THE CDo SHALL automatically add `cdo_ut` as an implicit dependency providing the test runner and assertion infrastructure.
2. WHEN building an E2E_Module, THE CDo SHALL automatically add `cdo_e2e` as an implicit dependency providing e2e-specific utilities.
3. WHEN building an E2E_Module, THE CDo SHALL include the api/ directories (and their immediate subdirectories) of `cdo_ut` and `cdo_e2e` in the include search path.
4. WHEN the crate declares additional dependencies in `crate.toml`, THE CDo SHALL link those dependencies into the E2E_Module executable in addition to the implicit ones, deduplicating any dependency that matches an implicit one so that no library is linked twice.
5. IF `cdo_ut` or `cdo_e2e` is not present as a workspace member when building an E2E_Module, THEN THE CDo SHALL fail the build and report an error message identifying which implicit dependency is missing and the workspace path searched.

### Requirement 11: E2E Hooks Support

**User Story:** As a CDo user, I want pre-e2e and post-e2e hooks at both workspace and crate levels, so that I can run setup or cleanup scripts around e2e test execution.

#### Acceptance Criteria

1. WHEN a workspace `cdo.toml` defines a `[hooks.pre-e2e]` entry, THE E2E_Runner SHALL execute the hook command before running any e2e tests in the workspace, passing the environment variables CDO_WS_ROOT, CDO_PROFILE, and CDO_BUILD_DIR to the hook process.
2. WHEN a workspace `cdo.toml` defines a `[hooks.post-e2e]` entry, THE E2E_Runner SHALL execute the hook command after all e2e tests in the workspace have completed regardless of test pass or fail outcomes, passing the environment variables CDO_WS_ROOT, CDO_PROFILE, and CDO_BUILD_DIR to the hook process.
3. WHEN a crate `crate.toml` defines a `[hooks.pre-e2e]` entry, THE E2E_Runner SHALL execute the hook command before running the e2e tests for that crate, passing the environment variables CDO_WS_ROOT, CDO_PROFILE, CDO_BUILD_DIR, CDO_CRATE_NAME, CDO_CRATE_PATH, and CDO_CRATE_BUILD_DIR to the hook process.
4. WHEN a crate `crate.toml` defines a `[hooks.post-e2e]` entry, THE E2E_Runner SHALL execute the hook command after the e2e tests for that crate have completed regardless of test pass or fail outcomes, passing the environment variables CDO_WS_ROOT, CDO_PROFILE, CDO_BUILD_DIR, CDO_CRATE_NAME, CDO_CRATE_PATH, and CDO_CRATE_BUILD_DIR to the hook process.
5. IF a pre-e2e hook at workspace level fails (returns non-zero exit code), THEN THE E2E_Runner SHALL abort the entire e2e run without executing the workspace post-e2e hook and return exit code 2.
6. IF a pre-e2e hook at crate level fails (returns non-zero exit code), THEN THE E2E_Runner SHALL skip the e2e tests for that crate, skip the crate post-e2e hook, and continue to the next crate.
7. WHEN both workspace-level and crate-level hooks are defined, THE E2E_Runner SHALL execute them in the order: workspace pre-e2e, then for each crate (crate pre-e2e, crate e2e tests, crate post-e2e), then workspace post-e2e.
8. IF a post-e2e hook at workspace or crate level fails (returns non-zero exit code), THEN THE E2E_Runner SHALL log the failure but preserve the test results and not alter the overall exit code determined by test outcomes.
