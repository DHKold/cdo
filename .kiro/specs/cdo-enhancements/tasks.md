# Implementation Plan: CDo Enhancements

## Overview

This plan implements three CDo enhancements: (1) comprehensive README documentation, (2) virtual environment (`--venv`) support for `cdo init`, and (3) file-locking for `cdo build` and `cdo test`. Implementation proceeds bottom-up: PAL primitives first, then higher-level components, then CLI integration, and finally documentation.

## Tasks

- [x] 1. Implement PAL extensions
  - [x] 1.1 Add new PAL function declarations to `pal.h`
    - Add `pal_get_executable_path`, `pal_file_copy`, `pal_file_lock_exclusive`, `pal_file_lock_release` declarations
    - Add `PalFileLock` opaque typedef
    - Add `PAL_ERR_TIMEOUT` error code if not already defined
    - _Requirements: 9.1, 9.2, 9.5_

  - [x] 1.2 Implement `pal_file_lock.c` with cross-platform file locking
    - Windows: `CreateFileW` + `LockFileEx` with `LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY` in a retry loop (50ms sleep between retries)
    - Unix: `open` + `flock(LOCK_EX | LOCK_NB)` in a retry loop (50ms nanosleep between retries)
    - Implement `pal_file_lock_exclusive` with timeout parameter (milliseconds)
    - Implement `pal_file_lock_release` that closes the file handle/descriptor and frees memory
    - Return `PAL_ERR_TIMEOUT` when timeout expires, `PAL_ERR_IO` on I/O errors
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5_

  - [x] 1.3 Implement `pal_get_executable_path` and `pal_file_copy` in appropriate PAL source file
    - Windows: `GetModuleFileNameW(NULL, ...)` for exe path; `CopyFileW` for file copy
    - Unix: read `/proc/self/exe` (Linux) or `_NSGetExecutablePath` (macOS) for exe path; `open`/`read`/`write` loop with `fchmod` for file copy preserving executable bit
    - _Requirements: 3.2_

- [x] 2. Implement Build Lock Manager
  - [x] 2.1 Create `build_lock.h` header with public API
    - Define `BuildLock` opaque typedef
    - Declare `build_lock_acquire(workspace_root, timeout_sec, lock_out)` and `build_lock_release(lock)`
    - _Requirements: 7.1, 7.2, 8.1_

  - [x] 2.2 Implement `build_lock.c`
    - `build_lock_acquire`: construct `.cdo/build.lock` path, ensure `.cdo` dir exists, call `pal_file_lock_exclusive` with `timeout_sec * 1000` ms
    - Write diagnostic JSON (PID + timestamp + command) into lock file after acquisition
    - Check `CDO_BUILD_LOCK_HELD` env var for re-entrancy — if set, skip acquisition and return success with NULL lock
    - `build_lock_release`: call `pal_file_lock_release`, free BuildLock struct
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 8.1, 8.2, 8.3_

  - [ ]* 2.3 Write property tests for build lock timeout behavior
    - **Property 7: Lock timeout causes deterministic failure**
    - **Validates: Requirements 7.3, 7.5, 9.5, 10.1, 10.2**

  - [ ]* 2.4 Write property tests for lock file diagnostic metadata
    - **Property 9: Lock file contains valid diagnostic metadata**
    - **Validates: Requirements 8.3**

- [x] 3. Integrate build lock into CLI commands
  - [x] 3.1 Add `lock_timeout` field to `CdoOptions` in `cli.h`
    - Add `int lock_timeout;` field (default sentinel -1 meaning use 30s default)
    - Parse `--lock-timeout <N>` in `cdo_cli_parse`
    - _Requirements: 10.1, 10.2, 10.3_

  - [x] 3.2 Integrate build lock into `cmd_build.c`
    - Acquire lock after workspace load, before compilation
    - Set `CDO_BUILD_LOCK_HELD=1` env var after acquisition
    - Release lock on all exit paths (success and failure)
    - Print error with diagnostic info on timeout
    - _Requirements: 7.1, 7.4, 7.5, 8.1, 8.2, 10.1, 10.3_

  - [x] 3.3 Integrate build lock into `cmd_test.c`
    - Acquire lock before calling internal build step
    - Set `CDO_BUILD_LOCK_HELD=1` env var so nested `cmd_build` calls skip lock acquisition
    - Clear env var and release lock after test completes (all exit paths)
    - _Requirements: 7.2, 7.4, 7.5, 8.1, 8.2, 10.1, 10.3_

  - [ ]* 3.4 Write property tests for lock acquire-before-compile and release-after-completion
    - **Property 6: Build lock is acquired before compilation**
    - **Property 8: Lock is always released after command completion**
    - **Validates: Requirements 7.1, 7.2, 8.1, 8.2**

- [x] 4. Checkpoint - Verify build lock functionality
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Implement Virtual Environment feature
  - [x] 5.1 Implement `venv_init` function in `cmd_new.c`
    - Create `.cdo` directory (preserve existing `tools/` and `cache/`)
    - Call `pal_get_executable_path` to get current binary path
    - Call `pal_file_copy` to copy binary into `.cdo/cdo.exe` (or `.cdo/cdo` on Unix)
    - Call script generators for all three platforms
    - _Requirements: 3.1, 3.2, 3.3, 3.5_

  - [x] 5.2 Implement activation script generators
    - `venv_generate_activate_bat`: writes `activate.bat` with `set`/`%VAR%` syntax, `doskey deactivate` command
    - `venv_generate_activate_ps1`: writes `activate.ps1` with `$env:VAR` syntax, `deactivate` function
    - `venv_generate_activate_sh`: writes `activate.sh` with `export` syntax, `deactivate()` function
    - Each script: stores original PATH/prompt, prepends `.cdo` to PATH, sets `CDO_HOME`/`CDO_VENV`, modifies prompt with `(cdo)` prefix
    - _Requirements: 3.3, 3.4, 4.1, 4.2, 4.3, 4.4, 4.5, 5.1, 5.2, 5.3, 5.4, 6.1, 6.2, 6.3, 6.4, 6.5, 6.6_

  - [x] 5.3 Integrate `--venv` flag into `cmd_init` dispatch
    - Parse `--venv` flag in options
    - Call `venv_init(workspace_root)` when flag is present, after existing template logic
    - _Requirements: 3.1_

  - [ ]* 5.4 Write property tests for venv initialization structure
    - **Property 1: Venv initialization creates required structure**
    - **Validates: Requirements 3.1, 3.3, 3.4**

  - [ ]* 5.5 Write property tests for binary copy fidelity
    - **Property 2: Venv binary copy is faithful**
    - **Validates: Requirements 3.2**

  - [ ]* 5.6 Write property tests for content preservation
    - **Property 3: Venv preserves existing content**
    - **Validates: Requirements 3.5**

  - [ ]* 5.7 Write property tests for activation/deactivation round-trip
    - **Property 4: Activation/deactivation round-trip restores environment**
    - **Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5, 5.1, 5.2, 5.3, 5.4**

  - [ ]* 5.8 Write property tests for platform-appropriate script syntax
    - **Property 5: Generated scripts use platform-appropriate syntax**
    - **Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5, 6.6**

- [x] 6. Checkpoint - Verify venv functionality
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Write comprehensive README
  - [x] 7.1 Write README with full feature documentation and quick-start guide
    - Overview/Introduction section
    - Installation & Prerequisites section
    - Quick Start: `cdo init` → `cdo build` → `cdo run` workflow with example shell commands
    - Commands Reference: build, run, test, clean, new, init (including `--venv`), deps, catalog, tool, doctor, shader
    - Configuration: `cdo.toml`, `crate.toml`, build profiles (debug, release, relwithdebinfo)
    - Virtual Environment (`--venv`) section
    - Build Locking section (including `--lock-timeout`)
    - Contributing / License section
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 2.1, 2.2, 2.3, 2.4_

- [x] 8. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from the design document
- The implementation language is C (matching the existing codebase)
- Build with `.\cdo.exe build` and test with `.\cdo.exe test cdo_pbt`
- Re-entrancy for `cmd_test` → `cmd_build` is handled via `CDO_BUILD_LOCK_HELD` env var

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["1.2", "1.3"] },
    { "id": 2, "tasks": ["2.1"] },
    { "id": 3, "tasks": ["2.2", "3.1"] },
    { "id": 4, "tasks": ["2.3", "2.4", "3.2", "3.3"] },
    { "id": 5, "tasks": ["3.4", "5.1"] },
    { "id": 6, "tasks": ["5.2"] },
    { "id": 7, "tasks": ["5.3", "5.4", "5.5", "5.6"] },
    { "id": 8, "tasks": ["5.7", "5.8", "7.1"] }
  ]
}
```
