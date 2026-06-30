# Implementation Plan: CDo E2E Framework

## Overview

This plan implements the CDo E2E testing framework in dependency order: first the `cdo_e2e` library crate (no dependencies on the command), then the module system extension (`MODULE_E2E`), then the E2E module builder, then the `cdo e2e` command, and finally the hook system extension. Each component follows TDD: interfaces first, then unit tests, then implementation.

## Tasks

- [x] 1. Create the `cdo_e2e` library crate structure and public API
  - [x] 1.1 Create crate directory structure and `crate.toml`
    - Create `crates/cdo_e2e/crate.toml` with name `cdo_e2e`, c-standard 17, dependency on `cdo_ut`
    - Create directory structure: `api/`, `lib/env/`, `lib/spawn/`, `lib/assert/`, `lib/fixture/`, `tst/`
    - Add `cdo_e2e` to workspace `cdo.toml` members list
    - _Requirements: 10.1, 10.2_

  - [x] 1.2 Define the public API header (`api/cdo_e2e.h`)
    - Write the umbrella header with all type definitions (`E2eEnv`, `E2eSpawnResult`, `E2eSpawnOpts`, `E2eEnvVar`)
    - Define error code constants (`E2E_OK`, `E2E_ERR_IO`, `E2E_ERR_TIMEOUT`, `E2E_ERR_INVALID`, `E2E_ERR_NOT_FOUND`, `E2E_ERR_LIMIT`, `E2E_ERR_SPAWN`)
    - Declare all public function signatures: `e2e_env_create`, `e2e_env_write_file`, `e2e_env_mkdir`, `e2e_env_setvar`, `e2e_env_destroy`, `e2e_env_set_crate_path`, `e2e_spawn`, `e2e_spawn_result_free`, `e2e_fixture_copy`
    - Define all assertion macros: `E2E_ASSERT_EXIT_CODE`, `E2E_ASSERT_STDOUT_CONTAINS`, `E2E_ASSERT_STDERR_CONTAINS`, `E2E_ASSERT_FILE_EXISTS`, `E2E_ASSERT_FILE_NOT_EXISTS`, `E2E_ASSERT_FILE_CONTAINS`
    - _Requirements: 4.1, 4.3, 4.4, 4.8, 4.9, 5.1, 5.2, 5.7, 6.1–6.9, 7.3, 7.6_

  - [x] 1.3 Verify crate compiles with empty stubs
    - Create minimal `.c` stub files in each `lib/` subdirectory so the crate compiles
    - Run `.\cdo.exe build cdo_e2e` to confirm the crate structure is recognized
    - _Requirements: 10.1, 10.2_

- [x] 2. Implement Test Environment module (`env/`)
  - [x] 2.1 Write unit tests for `e2e_env` functions
    - Create `tst/test_e2e_env.c` with tests for: `e2e_env_create` (unique naming, path length), `e2e_env_write_file` (basic file, nested dirs, path escape rejection), `e2e_env_mkdir` (basic, nested, escape rejection), `e2e_env_setvar` (set/overwrite, limit reached), `e2e_env_destroy` (cleanup, keep_temps), `e2e_env_set_crate_path` (valid, NULL, too long)
    - Cover edge cases: empty test name, 64-char truncation, `..` escape attempts, max env vars
    - _Requirements: 4.1–4.9, 8.1, 8.2, 8.6, 8.7, 8.8_

  - [x] 2.2 Implement `e2e_env_create`
    - Generate unique temp dir name with sanitized test name (truncated to 64 chars) + monotonic counter or PID+timestamp suffix
    - Create the directory under the system temp path using PAL functions
    - Initialize `E2eEnv` struct fields
    - Return `E2E_ERR_IO` on failure with logging
    - _Requirements: 4.1, 4.2, 4.5, 8.1_

  - [x] 2.3 Implement `e2e_env_write_file` and `e2e_env_mkdir`
    - Resolve relative path against `env->root_path`
    - Validate resolved path is a descendant of root (reject `..` escapes with `E2E_ERR_INVALID`)
    - Create intermediate parent directories as needed
    - Write file content for `write_file`, create directory for `mkdir`
    - Return appropriate error codes on failure with logging
    - _Requirements: 4.3, 4.4, 4.6, 4.7_

  - [x] 2.4 Implement `e2e_env_setvar`, `e2e_env_set_crate_path`, and `e2e_env_destroy`
    - `e2e_env_setvar`: Store key-value pair in `env->env_vars`, return `E2E_ERR_LIMIT` if full
    - `e2e_env_set_crate_path`: Validate and copy path into `env->crate_path`
    - `e2e_env_destroy`: Recursively delete temp dir (skip if `keep_temps`), log warning on failure
    - _Requirements: 4.8, 4.9, 7.6, 8.6, 8.7, 8.8_

  - [x] 2.5 Run env unit tests and verify all pass
    - Run `.\cdo.exe test cdo_e2e` and confirm all env tests pass
    - _Requirements: 4.1–4.9, 8.1, 8.2_

- [x] 3. Implement Subprocess Execution module (`spawn/`)
  - [x] 3.1 Write unit tests for `e2e_spawn` functions
    - Create `tst/test_e2e_spawn.c` with tests for: successful spawn with captured stdout/stderr, exit code capture, timeout enforcement, env var merging (env-level + extra), working directory override, spawn failure (invalid executable), `e2e_spawn_result_free` cleanup, NULL buffer handling, default timeout behavior
    - Use small helper executables or scripts as spawn targets
    - _Requirements: 5.1–5.7_

  - [x] 3.2 Implement `e2e_spawn`
    - Accept `E2eEnv` context, `E2eSpawnOpts` configuration
    - Merge environment variables from `env->env_vars` and `opts->extra_env`
    - Set working directory to `env->root_path` or `opts->working_dir` override
    - Spawn subprocess using PAL process APIs
    - Capture stdout and stderr into heap-allocated buffers (up to 16 MB each)
    - Apply timeout (default 120000ms): terminate process and set `timed_out` flag
    - On spawn failure: set `error_desc` and return `E2E_ERR_SPAWN`
    - _Requirements: 5.1–5.6_

  - [x] 3.3 Implement `e2e_spawn_result_free`
    - Free `stdout_buf` and `stderr_buf` if non-NULL
    - Zero out the struct
    - _Requirements: 5.7_

  - [x] 3.4 Run spawn unit tests and verify all pass
    - Run `.\cdo.exe test cdo_e2e` and confirm all spawn tests pass
    - _Requirements: 5.1–5.7_

- [x] 4. Implement Assertions module (`assert/`)
  - [x] 4.1 Write unit tests for assertion macros
    - Create `tst/test_e2e_assert.c` with tests for: `E2E_ASSERT_EXIT_CODE` (pass and fail cases), `E2E_ASSERT_STDOUT_CONTAINS` (pass, fail, NULL buffer), `E2E_ASSERT_STDERR_CONTAINS` (pass, fail, NULL buffer), `E2E_ASSERT_FILE_EXISTS` (exists, not exists), `E2E_ASSERT_FILE_NOT_EXISTS` (exists, not exists), `E2E_ASSERT_FILE_CONTAINS` (contains, not contains, unreadable file)
    - Verify failure recording calls `cdo_ut_record_failure` with correct file/line/message
    - _Requirements: 6.1–6.9_

  - [x] 4.2 Implement assertion helper functions (if any non-macro logic needed)
    - The assertion macros are defined in the header; implement any supporting C functions needed (e.g., file reading helper for `E2E_ASSERT_FILE_CONTAINS`)
    - Ensure all macros use `cdo_ut_record_failure` for failure reporting
    - _Requirements: 6.7, 6.8, 6.9_

  - [x] 4.3 Run assertion unit tests and verify all pass
    - Run `.\cdo.exe test cdo_e2e` and confirm all assertion tests pass
    - _Requirements: 6.1–6.9_

- [x] 5. Implement Fixture Management module (`fixture/`)
  - [x] 5.1 Write unit tests for `e2e_fixture_copy`
    - Create `tst/test_e2e_fixture.c` with tests for: successful copy of a multi-level fixture, empty directory preservation, fixture not found error, fixture name validation (max 64 chars, valid characters), deep nesting (up to 16 levels), crate path not set error, copy failure on individual file
    - Create test fixture directories under a temp location for tests
    - _Requirements: 7.1–7.8_

  - [x] 5.2 Implement `e2e_fixture_copy`
    - Validate fixture name (alphanumeric, hyphens, underscores, max 64 chars)
    - Resolve fixture path as `<crate_path>/e2e/fixtures/<fixture_name>/`
    - Verify fixture directory exists (return `E2E_ERR_NOT_FOUND` if missing)
    - Recursively copy all files and subdirectories (including empty dirs) into `env->root_path`
    - Handle up to 16 levels deep and 10,000 files
    - Return `E2E_ERR_IO` on individual file copy failure with logging
    - _Requirements: 7.1–7.5, 7.8_

  - [x] 5.3 Run fixture unit tests and verify all pass
    - Run `.\cdo.exe test cdo_e2e` and confirm all fixture tests pass
    - _Requirements: 7.1–7.8_

- [x] 6. Checkpoint — cdo_e2e library complete
  - Ensure all tests pass with `.\cdo.exe test cdo_e2e`, ask the user if questions arise.

- [x] 7. Extend Module System with `MODULE_E2E`
  - [x] 7.1 Write unit tests for MODULE_E2E scanner and artifact naming
    - Add tests in `crates/cdo/tst/` for: `MODULE_E2E` enum value exists, scanner detects `e2e/` directory, scanner excludes `e2e/fixtures/` from sources, scanner ignores empty `e2e/` (no sources outside fixtures), `module_artifact_name` returns `<crate>_e2e.exe` on Windows / `<crate>_e2e` on other platforms
    - _Requirements: 1.1–1.4, 1.7, 7.7_

  - [x] 7.2 Add `MODULE_E2E` to the `ModuleKind` enum
    - Add `MODULE_E2E` variant to `ModuleKind` in `crates/cdo/api/model/module.h`
    - Update `MODULE_KIND_COUNT` to 8
    - _Requirements: 1.2_

  - [x] 7.3 Update module scanner to detect `e2e/` directory
    - Modify `scanner_scan_modules` to detect the `e2e/` subdirectory
    - Modify `scanner_scan_module_sources` for `MODULE_E2E` to recursively discover `.c`/`.cpp` files excluding the `e2e/fixtures/` subtree
    - _Requirements: 1.1, 1.3, 1.7, 7.7_

  - [x] 7.4 Update `module_artifact_name` for `MODULE_E2E`
    - Return `<crate_name>_e2e.exe` on Windows, `<crate_name>_e2e` on other platforms
    - _Requirements: 1.4_

  - [x] 7.5 Update `build_crate` to skip `MODULE_E2E` during normal builds
    - Ensure `cdo build` without explicit e2e targeting does not build `MODULE_E2E`
    - _Requirements: 1.5, 1.6_

  - [x] 7.6 Run cdo unit tests and verify module system changes pass
    - Run `.\cdo.exe test cdo` and confirm all tests pass
    - _Requirements: 1.1–1.7_

- [x] 8. Implement E2E Module Builder (`build_e2e_module`)
  - [x] 8.1 Write unit tests for `build_e2e_module`
    - Add tests for: successful build with implicit deps (`cdo_ut`, `cdo_e2e`), fixture exclusion during source scan, deduplication of implicit deps with declared deps, include path setup for `cdo_ut` and `cdo_e2e` api/ directories, `CDO_TESTING` define is set, missing implicit dep error
    - _Requirements: 10.1–10.5_

  - [x] 8.2 Implement `build_e2e_module` in `crates/cdo/lib/commands/cmd_build_e2e.c`
    - Follow the same pattern as `build_test_module` in `cmd_build_test.c`
    - Compile all `.c`/`.cpp` files in `e2e/` excluding `e2e/fixtures/`
    - Automatically add `cdo_ut` and `cdo_e2e` as implicit dependencies
    - Include `api/` directories of implicit deps in include search path
    - Add `CDO_TESTING` define
    - Deduplicate any dependency already declared in `crate.toml`
    - Report error if `cdo_ut` or `cdo_e2e` not found in workspace
    - _Requirements: 10.1–10.5, 1.6_

  - [x] 8.3 Run builder unit tests and verify all pass
    - Run `.\cdo.exe test cdo` and confirm build_e2e tests pass
    - _Requirements: 10.1–10.5_

- [x] 9. Checkpoint — Module system and builder complete
  - Ensure all tests pass with `.\cdo.exe test cdo` and `.\cdo.exe test cdo_e2e`, ask the user if questions arise.

- [x] 10. Implement `cdo e2e` command
  - [x] 10.1 Write unit tests for `cmd_e2e` argument parsing and option extraction
    - Add tests for: `--filter`, `--list`, `--release`, `--profile`, `--jobs` (valid range, invalid range), `--verbose`, `--timeout`, `--keep-temps`, positional crate name, default profile (debug)
    - _Requirements: 3.1–3.9_

  - [x] 10.2 Define `cmd_e2e.h` and register the command in the CLI dispatcher
    - Create `crates/cdo/api/commands/cmd_e2e.h` with `cmd_e2e` function declaration
    - Register `cdo e2e` in the CLI command table with all option definitions
    - _Requirements: 2.1, 3.1–3.9_

  - [x] 10.3 Implement crate discovery logic in `cmd_e2e`
    - Discover all crates with `MODULE_E2E` present in workspace member order
    - Handle positional crate name filter (single crate targeting)
    - Return exit code 2 with error if specified crate not found or has no e2e module
    - Return exit code 2 with error if no e2e crates found in workspace
    - _Requirements: 2.1, 2.2, 2.8, 2.9_

  - [x] 10.4 Implement build + execution loop in `cmd_e2e`
    - For each discovered crate: acquire build lock (30s timeout), build e2e module with selected profile, release lock
    - On build failure: log error with crate name, continue to next crate
    - On build success: spawn the e2e executable with forwarded args (`--filter`, `--jobs`, `--list`, `--timeout`, `--keep-temps`)
    - Capture stdout for protocol parsing
    - _Requirements: 2.3, 2.4, 2.5, 3.1–3.9_

  - [x] 10.5 Implement Test Protocol parsing and result aggregation
    - Parse JSON Lines output via `test_protocol_parse_line`
    - Accumulate per-crate and aggregate results (total, passed, failed, skipped, duration)
    - Handle executable crash (non-zero exit without `suite_end` message)
    - _Requirements: 2.5, 2.6, 2.10, 9.3, 9.7_

  - [x] 10.6 Implement summary display and exit code logic
    - Display aggregate summary: total tests, passed, failed, skipped, wall-clock duration (ms)
    - Exit code 0: all passed; exit code 1: any failed; exit code 2: infrastructure error without test failures
    - _Requirements: 2.6, 2.7_

  - [x] 10.7 Run cmd_e2e unit tests and verify all pass
    - Run `.\cdo.exe test cdo` and confirm command tests pass
    - _Requirements: 2.1–2.10, 3.1–3.9_

- [x] 11. Checkpoint — cdo e2e command complete
  - Ensure all tests pass with `.\cdo.exe test cdo`, ask the user if questions arise.

- [x] 12. Extend Hook System for E2E lifecycle
  - [x] 12.1 Write unit tests for hook parsing and execution order
    - Add tests for: `hooks_parse_table` recognizes `"pre-e2e"` and `"post-e2e"` keys, `HookLifecycle` enum includes `HOOK_PRE_E2E` and `HOOK_POST_E2E`, workspace pre-e2e hook failure aborts run, crate pre-e2e hook failure skips crate, post-e2e hook failure is logged but doesn't alter exit code, execution order validation
    - _Requirements: 11.1–11.8_

  - [x] 12.2 Add `HOOK_PRE_E2E` and `HOOK_POST_E2E` to `HookLifecycle` enum
    - Update `crates/cdo/api/model/hooks.h` with new enum variants
    - Update `HOOK_LIFECYCLE_COUNT` to 6
    - Update `hooks_parse_table` to recognize `"pre-e2e"` and `"post-e2e"` TOML keys
    - _Requirements: 11.1–11.4_

  - [x] 12.3 Integrate hook execution into `cmd_e2e`
    - Execute workspace `pre-e2e` hook before any crate processing (abort on failure with exit 2)
    - Execute crate `pre-e2e` hook before each crate's tests (skip crate on failure)
    - Execute crate `post-e2e` hook after each crate's tests (log failure, don't alter results)
    - Execute workspace `post-e2e` hook after all crates (log failure, don't alter results)
    - Pass correct environment variables to each hook level
    - _Requirements: 11.1–11.8_

  - [x] 12.4 Run hook unit tests and verify all pass
    - Run `.\cdo.exe test cdo` and confirm hook tests pass
    - _Requirements: 11.1–11.8_

- [x] 13. Checkpoint — Hook system complete
  - Ensure all tests pass with `.\cdo.exe test cdo`, ask the user if questions arise.

- [x] 14. Self-hosting E2E tests for CDo
  - [x] 14.1 Create CDo crate's `e2e/` module with a basic e2e test
    - Create `crates/cdo/e2e/` directory with a test file that uses `cdo_e2e` to spawn `cdo.exe` against a fixture workspace
    - Create `crates/cdo/e2e/fixtures/` with a minimal workspace fixture (cdo.toml + simple crate)
    - Verify `cdo e2e cdo` discovers, builds, and runs the test successfully
    - _Requirements: 1.1, 1.3, 2.1, 2.2, 9.1, 9.2_

  - [x] 14.2 Add E2E tests exercising the `cdo e2e` command itself
    - Write tests that validate: `cdo e2e` with no e2e crates reports error, `cdo e2e` with a passing test exits 0, `cdo e2e` with a failing test exits 1, `--filter` correctly limits test execution, `--list` prints test names without executing
    - _Requirements: 2.1–2.10, 3.1, 3.2_

  - [x] 14.3 Run self-hosting e2e tests and verify all pass
    - Run `.\cdo.exe e2e cdo` and confirm self-hosting tests pass
    - _Requirements: 2.1, 9.1–9.7_

- [x] 15. Final checkpoint — All tests pass
  - Ensure all tests pass with `.\cdo.exe test cdo_e2e`, `.\cdo.exe test cdo`, and `.\cdo.exe e2e cdo`. Ask the user if questions arise.

## Notes

- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation between major components
- The `cdo_e2e` library is built first since it has no dependency on the command infrastructure
- TDD approach: interfaces/headers → unit tests → implementation for each component
- All unit tests run via `.\cdo.exe test <crate>`, e2e tests via `.\cdo.exe e2e <crate>`
- No property-based testing per project conventions; extensive unit testing instead

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["1.2"] },
    { "id": 2, "tasks": ["1.3"] },
    { "id": 3, "tasks": ["2.1", "3.1", "4.1", "5.1"] },
    { "id": 4, "tasks": ["2.2", "3.2", "4.2", "5.2"] },
    { "id": 5, "tasks": ["2.3", "3.3"] },
    { "id": 6, "tasks": ["2.4"] },
    { "id": 7, "tasks": ["2.5", "3.4", "4.3", "5.3"] },
    { "id": 8, "tasks": ["7.1"] },
    { "id": 9, "tasks": ["7.2"] },
    { "id": 10, "tasks": ["7.3", "7.4"] },
    { "id": 11, "tasks": ["7.5"] },
    { "id": 12, "tasks": ["7.6"] },
    { "id": 13, "tasks": ["8.1"] },
    { "id": 14, "tasks": ["8.2"] },
    { "id": 15, "tasks": ["8.3"] },
    { "id": 16, "tasks": ["10.1", "12.1"] },
    { "id": 17, "tasks": ["10.2", "12.2"] },
    { "id": 18, "tasks": ["10.3"] },
    { "id": 19, "tasks": ["10.4"] },
    { "id": 20, "tasks": ["10.5"] },
    { "id": 21, "tasks": ["10.6"] },
    { "id": 22, "tasks": ["10.7"] },
    { "id": 23, "tasks": ["12.3"] },
    { "id": 24, "tasks": ["12.4"] },
    { "id": 25, "tasks": ["14.1"] },
    { "id": 26, "tasks": ["14.2"] },
    { "id": 27, "tasks": ["14.3"] }
  ]
}
```
