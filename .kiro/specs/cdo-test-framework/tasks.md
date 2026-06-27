# Implementation Plan: CDo Test Framework

## Overview

This plan implements a comprehensive testing infrastructure for the CDo build system. It introduces the `cdo_ut` crate (test library), a JSON Lines reporting protocol, colored console output, dry-run/list capabilities, test filtering, coverage integration via gcov, and parallel test execution. The implementation proceeds bottom-up: first the test library crate, then the protocol layer, then the runner enhancements, and finally integration wiring.

## Tasks

- [x] 1. Create `cdo_ut` crate structure and core registration
  - [x] 1.1 Create `crates/cdo_ut/crate.toml` and directory layout
    - Create `crates/cdo_ut/crate.toml` with `name = "cdo_ut"`, `c-standard = 17`
    - Create directories: `crates/cdo_ut/api/`, `crates/cdo_ut/lib/`, `crates/cdo_ut/tst/`
    - _Requirements: 1.1, 1.2_

  - [x] 1.2 Implement the public header `crates/cdo_ut/api/cdo_ut.h`
    - Define `TEST()` macro using `__attribute__((constructor))` for auto-registration
    - Define `TEST_SERIAL()` macro for sequential-only tests
    - Define assertion macros: `TEST_ASSERT`, `TEST_ASSERT_EQ`, `TEST_ASSERT_NEQ`, `TEST_ASSERT_STR_EQ`, `TEST_ASSERT_NULL`
    - Provide MSVC fallback registration mechanism (`REGISTER_TEST`, `REGISTER_TEST_SERIAL`)
    - Declare `cdo_ut_register()` and `cdo_ut_record_failure()` externs
    - _Requirements: 1.2, 1.3, 1.4, 1.5, 9.2, 9.3_

  - [x] 1.3 Implement test registration in `crates/cdo_ut/lib/cdo_ut_main.c`
    - Implement `cdo_ut_register()` to populate global `g_ut_tests[]` registry (max 1024)
    - Implement `cdo_ut_record_failure()` to store file, line, expression, actual, expected
    - Emit `{"type": "error", ...}` and `exit(1)` on registry overflow
    - Implement `main()` entry point with `--list`, `--filter`, `--jobs` argument parsing
    - Sequential test execution loop: iterate registered tests, call each, collect results
    - _Requirements: 1.1, 1.3, 2.6, 6.2, 7.1_

  - [ ]* 1.4 Write property test for test registration discovery (Property 1)
    - **Property 1: Test Registration Discovery**
    - **Validates: Requirements 1.3, 6.2**

  - [ ]* 1.5 Write property test for assertion correctness (Property 2)
    - **Property 2: Assertion Correctness**
    - **Validates: Requirements 1.4**

  - [ ]* 1.6 Write property test for failure metadata recording (Property 3)
    - **Property 3: Failure Metadata Recording**
    - **Validates: Requirements 1.5**

- [x] 2. Implement JSON Lines protocol emitter
  - [x] 2.1 Implement `crates/cdo_ut/lib/cdo_ut_protocol.c`
    - Implement `cdo_ut_emit_suite_start(int total)` — emits `{"type":"suite_start","total":N}`
    - Implement `cdo_ut_emit_test_start(const char *name, int id)` — emits `{"type":"test_start",...}`
    - Implement `cdo_ut_emit_test_end(...)` — emits `{"type":"test_end",...}` with status, duration, optional failure details
    - Implement `cdo_ut_emit_suite_end(...)` — emits `{"type":"suite_end",...}` with totals and duration
    - Implement `cdo_ut_emit_error(const char *message)` — emits `{"type":"error",...}`
    - All functions write one JSON object per line to stdout, ASCII-safe encoding only
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 9.4_

  - [x] 2.2 Wire protocol emitter into `cdo_ut_main.c` execution loop
    - Call `cdo_ut_emit_suite_start` after discovery
    - Call `cdo_ut_emit_test_start` before each test
    - Call `cdo_ut_emit_test_end` after each test with timing and failure info
    - Call `cdo_ut_emit_suite_end` after all tests complete
    - Ensure `passed + failed + skipped == total` invariant holds
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5_

  - [ ]* 2.3 Write property test for protocol message validity (Property 4)
    - **Property 4: Protocol Message Validity**
    - **Validates: Requirements 2.5, 9.4**

  - [ ]* 2.4 Write property test for protocol count consistency (Property 5)
    - **Property 5: Protocol Count Consistency**
    - **Validates: Requirements 2.4**

- [x] 3. Implement filter engine
  - [x] 3.1 Implement `crates/cdo_ut/lib/cdo_ut_filter.c`
    - Implement `cdo_ut_filter_matches(const char *test_name, const char *pattern)` — returns bool
    - If pattern contains no `*`: use `strstr(test_name, pattern) != NULL` (substring match)
    - If pattern contains `*`: implement recursive glob match where `*` matches zero or more characters
    - Wire filter into `cdo_ut_main.c`: apply filter before execution, skip non-matching tests
    - When `--list` combined with `--filter`, print only matching names
    - When no tests match, emit `suite_end` with `total: 0` and exit 0
    - _Requirements: 7.2, 7.3, 7.4, 6.3_

  - [ ]* 3.2 Write property test for substring filter correctness (Property 12)
    - **Property 12: Substring Filter Correctness**
    - **Validates: Requirements 7.2**

  - [ ]* 3.3 Write property test for glob filter correctness (Property 13)
    - **Property 13: Glob Filter Correctness**
    - **Validates: Requirements 7.3**

  - [ ]* 3.4 Write property test for list-with-filter intersection (Property 14)
    - **Property 14: List With Filter Intersection**
    - **Validates: Requirements 6.3**

- [x] 4. Checkpoint - Verify `cdo_ut` crate builds and basic protocol works
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Implement parallel test executor
  - [x] 5.1 Implement `crates/cdo_ut/lib/cdo_ut_parallel.c`
    - Implement `cdo_ut_run_parallel(int jobs, ...)` using Windows threads (`_beginthreadex`)
    - Each parallel test redirects stdout/stderr to a per-test buffer (pipe or temp file)
    - Store results and captured output per test
    - Emit protocol messages in serialized manner (mutex-protected writes to stdout)
    - Serial tests (`TEST_SERIAL`) run sequentially after all parallel tests complete
    - Fall back to sequential execution if thread creation fails (log warning)
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5_

  - [x] 5.2 Implement `crates/cdo_ut/lib/cdo_ut_capture.c`
    - Implement per-test stdout/stderr capture using pipe redirection
    - Provide `cdo_ut_capture_start()` / `cdo_ut_capture_end()` to bracket each test
    - Store captured output associated with the test result for later protocol emission
    - Emit protocol error and `exit(1)` on pipe creation failure
    - _Requirements: 10.3_

  - [x] 5.3 Wire parallel executor into `cdo_ut_main.c`
    - When `--jobs N` provided (N > 1), invoke `cdo_ut_run_parallel` instead of sequential loop
    - Default to sequential when no `--jobs` argument
    - _Requirements: 10.1, 10.2_

  - [ ]* 5.4 Write property test for parallel output isolation (Property 17)
    - **Property 17: Parallel Output Isolation**
    - **Validates: Requirements 10.3**

  - [ ]* 5.5 Write property test for serial test exclusivity (Property 18)
    - **Property 18: Serial Test Exclusivity**
    - **Validates: Requirements 10.5**

- [x] 6. Implement test runner protocol parser and renderer
  - [x] 6.1 Implement `crates/cdo/lib/commands/test_protocol.c`
    - Implement `test_protocol_parse_line(const char *line, TestResult *out)` — parse one JSON line
    - Handle all message types: `suite_start`, `test_start`, `test_end`, `suite_end`, `error`
    - Use existing `json.h` API for JSON deserialization
    - Return error code for invalid/malformed lines (skip and continue)
    - _Requirements: 2.5, 3.5_

  - [x] 6.2 Implement colored renderer in `crates/cdo/lib/commands/test_renderer.c`
    - Implement `test_renderer_result(const TestResult *result, bool use_color)`:
      - Pass: green checkmark (✓) prefix + test name
      - Fail: red cross (✗) prefix + test name, failure details indented below
      - Skip: yellow dash (-) prefix + test name
    - Implement `test_renderer_summary(...)` — total/passed/failed/skipped/duration/coverage
      - Green totals when all pass, red totals when any fail
    - Implement `test_renderer_failures(...)` — consolidated failure list with crate, name, file, line
    - Implement `test_renderer_progress(int completed, int total, bool use_color)` — `[N/M]` display
    - Omit ANSI escape codes when `use_color` is false
    - Use existing `output.h` infrastructure for terminal capability detection
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 4.1, 4.2, 4.3, 4.4, 5.1, 5.2, 5.3, 9.5_

  - [ ]* 6.3 Write property test for result rendering correctness (Property 6)
    - **Property 6: Result Rendering Correctness**
    - **Validates: Requirements 3.1, 3.2**

  - [ ]* 6.4 Write property test for progress display accuracy (Property 7)
    - **Property 7: Progress Display Accuracy**
    - **Validates: Requirements 3.3**

  - [ ]* 6.5 Write property test for ANSI-free output in non-color mode (Property 8)
    - **Property 8: ANSI-Free Output in Non-Color Mode**
    - **Validates: Requirements 3.4**

  - [ ]* 6.6 Write property test for summary color reflects status (Property 9)
    - **Property 9: Summary Color Reflects Status**
    - **Validates: Requirements 4.3, 4.4**

  - [ ]* 6.7 Write property test for failures section presence (Property 10)
    - **Property 10: Failures Section Presence**
    - **Validates: Requirements 5.1, 5.3**

  - [ ]* 6.8 Write property test for failure listing completeness (Property 11)
    - **Property 11: Failure Listing Completeness**
    - **Validates: Requirements 5.2**

- [x] 7. Checkpoint - Verify protocol parser and renderer build correctly
  - Ensure all tests pass, ask the user if questions arise.

- [x] 8. Enhance `cdo test` runner command
  - [x] 8.1 Refactor `crates/cdo/lib/commands/cmd_test.c` for new CLI options
    - Add `--filter <pattern>` option parsing and forwarding to test binaries
    - Add `--list` option to instruct binaries to emit test names only
    - Add `--jobs <N>` option to forward parallel execution hint to binaries
    - Add `--coverage` option flag for coverage mode
    - Pass parsed options to spawned test binary as command-line arguments
    - _Requirements: 6.1, 7.1, 8.1, 10.1_

  - [x] 8.2 Implement piped stdout reading and protocol-driven output
    - Change `spawn_opts.capture_output` to pipe stdout line-by-line
    - Read each line from spawned test binary, call `test_protocol_parse_line`
    - On `test_start`: optionally update progress display
    - On `test_end`: call `test_renderer_result` for colored output
    - On `suite_end`: accumulate summary totals across crates
    - On `error`: display error message and mark crate as failed
    - Handle binary crash (non-zero exit without `suite_end`): report unfinished tests as failed
    - _Requirements: 3.1, 3.2, 3.3, 3.5, 4.1_

  - [x] 8.3 Implement summary and consolidated failure output
    - After all crate binaries complete, call `test_renderer_summary` with aggregated totals
    - If any failures, call `test_renderer_failures` with all failed test details
    - Display crate grouping headers between results from different crates
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 5.1, 5.2, 5.3, 3.5_

  - [x] 8.4 Implement `--list` mode in the runner
    - When `--list` specified, spawn binaries with `--list` (and `--filter` if given)
    - Display listed tests grouped by crate name with header lines
    - Exit without running tests
    - _Requirements: 6.1, 6.3, 6.4_

- [x] 9. Implement coverage integration
  - [x] 9.1 Implement coverage build flags and gcov invocation
    - When `--coverage` is active, add `--coverage -fprofile-arcs -ftest-coverage` to GCC compile/link flags
    - After test execution completes, invoke `gcov` on generated `.gcda` files
    - Parse gcov output: extract per-file `File 'path'` and `Lines executed:XX.XX% of N` lines
    - Compute aggregate coverage: `sum(lines_hit) / sum(lines_total) * 100`
    - Display per-file percentages and aggregate percentage in summary
    - If gcov not found in PATH, display error and exit with code 2
    - Handle `sum(lines_total) == 0` case: report 0% coverage
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5_

  - [ ]* 9.2 Write property test for gcov output parsing (Property 15)
    - **Property 15: gcov Output Parsing**
    - **Validates: Requirements 8.3**

  - [ ]* 9.3 Write property test for aggregate coverage computation (Property 16)
    - **Property 16: Aggregate Coverage Computation**
    - **Validates: Requirements 8.4**

- [x] 10. Integration wiring and self-tests
  - [x] 10.1 Create `cdo_ut` self-test in `crates/cdo_ut/tst/`
    - Write a small set of tests using `cdo_ut.h` that exercise the framework itself
    - Tests should verify: registration works, assertions pass/fail correctly, `--list` outputs names
    - Build with `.\cdo.exe build cdo_ut` to verify the crate integrates with the build system
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 9.1_

  - [x] 10.2 Update `cdo` crate to depend on new `test_protocol.c` and `test_renderer.c`
    - Ensure `test_protocol.c` and `test_renderer.c` are included in the `cdo` crate build
    - Add appropriate header files under `crates/cdo/api/commands/`
    - Verify `.\cdo.exe build cdo` compiles cleanly
    - _Requirements: 3.1, 4.1, 5.1_

  - [x] 10.3 Wire exit codes consistently
    - Test binary: exit 0 (all pass or zero tests matched), exit 1 (any failure), exit 2 (infrastructure error)
    - Runner `cdo test`: exit 0 (all pass), exit 1 (any failure), exit 2 (build/infra error)
    - _Requirements: 7.4, 8.5_

- [x] 11. Final checkpoint - Full integration verification
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from the design document
- Unit tests validate specific examples and edge cases
- Build with `.\cdo.exe build` to compile; property tests use vendored `theft` library at `crates/cdo/tst/vendor/theft.h`
- The `cdo_ut` crate will need its own `crate.toml` to be discovered by the CDo build system

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["1.2"] },
    { "id": 2, "tasks": ["1.3", "3.1"] },
    { "id": 3, "tasks": ["1.4", "1.5", "1.6", "2.1"] },
    { "id": 4, "tasks": ["2.2", "3.2", "3.3", "3.4"] },
    { "id": 5, "tasks": ["2.3", "2.4", "5.1", "5.2"] },
    { "id": 6, "tasks": ["5.3", "5.4", "5.5", "6.1"] },
    { "id": 7, "tasks": ["6.2"] },
    { "id": 8, "tasks": ["6.3", "6.4", "6.5", "6.6", "6.7", "6.8", "8.1"] },
    { "id": 9, "tasks": ["8.2", "8.4"] },
    { "id": 10, "tasks": ["8.3", "9.1"] },
    { "id": 11, "tasks": ["9.2", "9.3", "10.1", "10.2"] },
    { "id": 12, "tasks": ["10.3"] }
  ]
}
```
