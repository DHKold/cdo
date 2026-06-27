# Implementation Plan: Crate Modules

## Overview

This plan implements the crate-modules feature in 8 logical groups matching the 8 requirements: resource module type, inter-crate module dependencies, enhanced run command with staging folder, shader module type, standalone shader command removal, PAL path_exists fix, progress bar global accuracy, and coverage source filtering. Each task is self-contained and builds successfully on its own. The approach follows TDD: interfaces/headers first, then unit tests, then implementation.

## Tasks

- [x] 1. PAL path_exists return code fix
  - [x] 1.1 Fix `pal_path_exists` implementation in `crates/cdo/lib/pal/pal_fs.c`
    - Windows: return `PAL_OK` (0) when `GetFileAttributesW` succeeds, `PAL_ERR_NOT_FOUND` (9) when `INVALID_FILE_ATTRIBUTES`
    - Linux: return `PAL_OK` (0) when `stat()` succeeds, `PAL_ERR_NOT_FOUND` (9) when `stat` fails
    - Both: return `PAL_ERR_NOT_FOUND` for NULL or empty-string input
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 6.7, 6.8_

  - [x] 1.2 Update all callers of `pal_path_exists` to use corrected convention
    - Search codebase for all usages of `pal_path_exists`
    - Change checks from truthy/boolean semantics to `== 0` for exists, `!= 0` for absent
    - Verify no caller relies on the old broken return values
    - _Requirements: 6.6_

  - [x] 1.3 Write unit tests for `pal_path_exists` fix in `crates/cdo/tst/unit/test_pal_path_exists.c`
    - Test NULL input returns `PAL_ERR_NOT_FOUND`
    - Test empty string returns `PAL_ERR_NOT_FOUND`
    - Test existing file returns `PAL_OK`
    - Test existing directory returns `PAL_OK`
    - Test non-existent path returns `PAL_ERR_NOT_FOUND`
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5_

- [x] 2. Checkpoint - Ensure PAL fix builds and tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 3. Extend module system with `res` and `shd` kinds
  - [x] 3.1 Extend `ModuleKind` enum and `MODULE_KIND_COUNT` in `crates/cdo/api/core/module.h`
    - Add `MODULE_RES` (6) and `MODULE_SHD` (7) to the enum
    - Update `MODULE_KIND_COUNT` from 5 to 7
    - Update `module_kind_to_string` to handle new kinds
    - _Requirements: 1.2, 4.2_

  - [x] 3.2 Expand `Crate` struct in `crates/cdo/api/core/workspace.h`
    - Change `modules[5]` to `modules[7]`
    - Add `bool has_res` and `bool has_shd` shortcut fields
    - _Requirements: 1.1, 4.1_

  - [x] 3.3 Update `scanner_scan_modules` in `crates/cdo/lib/core/scanner.c` to detect `res/` and `shd/`
    - Add `res` and `shd` to the directory name → ModuleKind mapping
    - For `MODULE_RES`: scan all files (not just `.c/.cpp`) recursively
    - For `MODULE_SHD`: scan only `.hlsl` files recursively
    - Set `crate->has_res` and `crate->has_shd` shortcuts
    - _Requirements: 1.1, 4.1_

  - [x] 3.4 Write unit tests for module detection in `crates/cdo/tst/unit/test_scanner_modules.c`
    - Test `res/` directory is detected as `MODULE_RES`
    - Test `shd/` directory is detected as `MODULE_SHD`
    - Test `res/` scanner finds all file types (not just .c/.cpp)
    - Test `shd/` scanner finds only `.hlsl` files
    - Test empty `res/` and `shd/` directories are detected but have empty file lists
    - _Requirements: 1.1, 1.2, 4.1, 4.2_

- [x] 4. Implement resource module build
  - [x] 4.1 Add `build_resource_module` declaration to `crates/cdo/lib/commands/cmd_build_internal.h`
    - Declare the function signature per design: takes `ws`, `crate`, `profile`, `progress`, `completed_units`
    - _Requirements: 1.3_

  - [x] 4.2 Implement `build_resource_module` in new file `crates/cdo/lib/commands/cmd_build_res.c`
    - Walk `res/` dir recursively for all files
    - For each file: compute destination at `build/<profile>/<crate>/res/<relative_path>`
    - Incremental copy: skip if dest mtime >= source mtime
    - Remove stale files: walk dest dir, remove files not present in source
    - Log copied/skipped counts at debug level
    - Return non-zero on filesystem error with descriptive message
    - Return 0 for empty `res/` directory
    - _Requirements: 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9_

  - [x] 4.3 Integrate `build_resource_module` into `build_crate_modules` in `crates/cdo/lib/commands/cmd_build.c`
    - Call after `shd/` and before `exe/dyn/tst` per design order: lib → shd → res → exe → dyn → tst
    - Only call when `crate->has_res` is true
    - _Requirements: 1.4_

  - [x] 4.4 Write unit tests for resource module in `crates/cdo/tst/unit/test_build_resource.c`
    - Test incremental copy: newer source → copied
    - Test incremental copy: older source → skipped
    - Test incremental copy: missing dest → copied
    - Test stale file removal: dest file not in source → removed
    - Test empty `res/` dir → success with zero counts
    - Test filesystem error handling (permission error simulation)
    - Test nested subdirectory structure preservation
    - _Requirements: 1.4, 1.5, 1.6, 1.7, 1.8, 1.9_

- [x] 5. Implement shader module build
  - [x] 5.1 Add `build_shader_module` declaration to `crates/cdo/lib/commands/cmd_build_internal.h`
    - Declare per design: takes `ws`, `crate`, `profile`, `build_prof`, `force`, `progress`, `completed_units`
    - _Requirements: 4.3_

  - [x] 5.2 Implement `build_shader_module` in new file `crates/cdo/lib/commands/cmd_build_shd.c`
    - Check DXC exists at `.cdo/tools/dxc/bin/dxc.exe`; if missing, error with install suggestion
    - Walk `shd/` for `.hlsl` files recursively
    - For each: compute output at `build/<profile>/<crate>/shd/<relative_path>.dxil`
    - Incremental: skip if output mtime >= source mtime (unless `force` is true)
    - Invoke DXC via `pal_spawn` per shader file
    - On failure: log DXC stderr, continue remaining shaders, track error count
    - Report compiled/skipped counts at info verbosity
    - Return non-zero if any shader failed
    - Handle empty `shd/` → zero compiled, zero skipped, return 0
    - _Requirements: 4.3, 4.4, 4.5, 4.6, 4.7, 4.8, 4.9, 4.10_

  - [x] 5.3 Integrate `build_shader_module` into `build_crate_modules` in `crates/cdo/lib/commands/cmd_build.c`
    - Call after `lib/` and before `res/` in the build order
    - Only call when `crate->has_shd` is true
    - Pass `opts->release` or profile-based force flag
    - _Requirements: 4.3_

  - [x] 5.4 Write unit tests for shader module in `crates/cdo/tst/unit/test_build_shader.c`
    - Test DXC missing → error message + non-zero return
    - Test incremental: newer source → compiled
    - Test incremental: older source → skipped
    - Test force flag → all compiled regardless of mtime
    - Test single shader failure → continues, returns non-zero
    - Test empty `shd/` → zero counts, returns 0
    - Test nested directory structure preservation in output
    - _Requirements: 4.3, 4.4, 4.5, 4.6, 4.7, 4.8, 4.9, 4.10_

- [x] 6. Checkpoint - Ensure res/shd module builds pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Implement inter-crate module dependency propagation
  - [x] 7.1 Add `propagate_dep_modules` declaration to `crates/cdo/lib/commands/cmd_build_internal.h`
    - Declare per design: takes `ws`, `crate`, `profile`; returns 0 on success
    - _Requirements: 2.3, 2.4, 2.5_

  - [x] 7.2 Implement `propagate_dep_modules` in new file `crates/cdo/lib/commands/cmd_build_deps.c`
    - For each resolved dependency (from `dep_indices`, respecting transitive BFS order):
      - If dep has `res`: copy `build/<profile>/<dep>/res/` → `build/<profile>/<crate>/res/` (incremental)
      - If dep has `shd`: copy `build/<profile>/<dep>/shd/` → `build/<profile>/<crate>/shd/` (incremental)
      - If dep has `dyn`: copy DLL/SO to `build/<profile>/<crate>/` adjacent to exe
    - Detect conflicts: if two deps provide same relative path in res/ or shd/, error with both source crates
    - Exclude `exe` and `tst` modules from dependency resolution
    - _Requirements: 2.3, 2.4, 2.5, 2.6, 2.7, 2.8_

  - [x] 7.3 Integrate `propagate_dep_modules` into `build_crate_modules` after all modules are built
    - Call after individual module builds complete for the crate
    - _Requirements: 2.1, 2.2_

  - [x] 7.4 Write unit tests for dependency propagation in `crates/cdo/tst/unit/test_dep_propagation.c`
    - Test res propagation: dep's built res files copied to dependent's res dir
    - Test shd propagation: dep's compiled shaders copied to dependent's shd dir
    - Test dyn propagation: dep's DLL/SO copied adjacent to dependent's exe
    - Test transitive: A→B→C, A receives C's res/shd/dyn
    - Test conflict detection: two deps with same relative path → error
    - Test exe/tst modules excluded from propagation
    - _Requirements: 2.3, 2.4, 2.5, 2.6, 2.7, 2.8_

- [x] 8. Implement enhanced run command with staging folder
  - [x] 8.1 Refactor `cmd_run` in `crates/cdo/lib/commands/cmd_run.c` to use staging folder
    - After successful build, create staging folder at `.cdo/<crate>/run/`
    - If staging folder exists from prior run, remove contents first
    - Copy executable from `build/<profile>/<crate>/<exe>` into staging
    - Copy DLL/SO files from dependencies into staging root
    - Copy `build/<profile>/<crate>/res/` → `staging/res/` preserving structure
    - Copy `build/<profile>/<crate>/shd/` → `staging/shd/` preserving structure
    - Spawn executable with `cwd` set to staging folder
    - Forward `argv_rest` to spawned process
    - Return spawned process exit code
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10, 3.11_

  - [x] 8.2 Add error handling for run command edge cases
    - If crate has no `exe/` module → error "crate has no executable target"
    - If build fails → return non-zero without creating staging
    - Update `select_run_crate` to check `modules[MODULE_EXE].present` instead of legacy `CRATE_EXECUTABLE`
    - _Requirements: 3.12, 3.13_

  - [x] 8.3 Write unit tests for run staging in `crates/cdo/tst/unit/test_run_staging.c`
    - Test staging folder creation at `.cdo/<crate>/run/`
    - Test exe copy into staging
    - Test DLL copy into staging
    - Test res/ subdirectory copy preserving structure
    - Test shd/ subdirectory copy preserving structure
    - Test prior run cleanup (staging exists → cleared first)
    - Test non-exe crate → error
    - Test build failure → no staging created
    - Test argv forwarding
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10, 3.11, 3.12, 3.13_

- [x] 9. Checkpoint - Ensure run command and deps work
  - Ensure all tests pass, ask the user if questions arise.

- [x] 10. Remove standalone shader command
  - [x] 10.1 Replace shader command dispatch with deprecation message
    - In the main command dispatch (likely `main.c` or command router), replace `CDO_CMD_SHADER` case with an error message: "The 'shader' command has been removed. Use a shd/ module in your crate instead."
    - Return exit code 1 from the shader command handler
    - _Requirements: 5.1_

  - [x] 10.2 Remove shader command from help output and CLI tables
    - Remove `{ "shader", CDO_CMD_SHADER }` from command table in `crates/cdo/lib/core/cli_parse.c`
    - Remove shader case from `cdo_cli_print_help` in `crates/cdo/lib/core/cli_parse.c`
    - Remove from `cli_suggest.c` command table
    - Keep the `CDO_CMD_SHADER` enum value for backward compat but add a comment marking it deprecated
    - _Requirements: 5.2_

  - [x] 10.3 Remove standalone shader source files from build
    - Delete `crates/cdo/lib/commands/cmd_shader.c` (or equivalent) from compilation
    - Remove `cmd_shader.h` header if it exists
    - The existing `shader.h` / `shader.c` (compilation logic) stays — it's reused by `build_shader_module`
    - Update old shader test files (`test_shader.c`, `test_shader_build.c`, `test_shader_list.c`, `test_shader_test.c`) to test the deprecation message instead
    - _Requirements: 5.3_

  - [x] 10.4 Write unit test for shader deprecation in `crates/cdo/tst/unit/test_shader_removal.c`
    - Test `cdo shader` prints deprecation error and returns exit code 1
    - Test `cdo shader build` prints deprecation error and returns exit code 1
    - Test help output does not contain "shader" command
    - _Requirements: 5.1, 5.2_

- [x] 11. Fix build progress bar global accuracy
  - [x] 11.1 Refactor progress bar counting in `cmd_build` to be globally accurate
    - Move the existing total_units pre-counting loop to count files per-module (lib, exe, dyn, tst only)
    - Exclude `res/` and `shd/` files from the compilable count
    - When targeting specific crates, count only those crates + their transitive deps
    - When a crate is fully up-to-date, add its file count to completed_units immediately
    - If total_units is 0, skip progress bar creation entirely
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 7.7_

  - [x] 11.2 Ensure progress bar finalizes correctly on error
    - On compilation error, call `progress_finish` before reporting error
    - Progress bar shows current completed count at time of error
    - _Requirements: 7.8_

  - [x] 11.3 Write unit tests for progress bar accuracy in `crates/cdo/tst/unit/test_progress_global.c`
    - Test global count across multiple crates matches expected file count
    - Test single-crate targeting counts only that crate + deps
    - Test up-to-date skip adds count immediately
    - Test zero sources → no progress bar
    - Test mid-build error → progress finalizes at current count
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 7.7, 7.8_

- [x] 12. Implement coverage source filtering
  - [x] 12.1 Add `coverage_run_gcov_filtered` declaration to `crates/cdo/api/commands/test_coverage.h`
    - Declare per design: takes `build_dir`, `ws_root`, `out`, `max_files`
    - _Requirements: 8.1_

  - [x] 12.2 Implement `coverage_run_gcov_filtered` in `crates/cdo/lib/commands/test_coverage.c`
    - Call existing `coverage_run_gcov` to get raw results
    - For each result: resolve file path to absolute using `ws_root` as base
    - Normalize path separators (backslash → forward slash)
    - Include only files whose absolute path starts with `<ws_root>/crates/`
    - Case-sensitive comparison on Linux, case-insensitive on Windows
    - Compact results array in-place, return filtered count
    - If all files filtered → return 0 (caller handles 0% display)
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5_

  - [x] 12.3 Update coverage callers to use `coverage_run_gcov_filtered` instead of `coverage_run_gcov`
    - Find all call sites of `coverage_run_gcov` and replace with filtered version
    - Pass workspace root path to the filtered function
    - _Requirements: 8.1_

  - [x] 12.4 Write unit tests for coverage filtering in `crates/cdo/tst/unit/test_coverage_filter.c`
    - Test workspace-local file (under `crates/`) → included
    - Test external file (system header) → excluded
    - Test path normalization (backslash vs forward slash)
    - Test all files excluded → returns 0, 0% coverage
    - Test case sensitivity behavior per platform
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5_

- [x] 13. Final checkpoint - Full build and test verification
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- The PAL fix (task 1) is done first because other tasks may depend on correct `pal_path_exists` behavior
- Module enum extension (task 3) must precede resource and shader build tasks
- The existing `shader.h` / `shader.c` (DXC compilation logic) is reused by `build_shader_module` — only the CLI command dispatch is removed
- Inter-crate dependency propagation (task 7) depends on both res and shd module builds being functional
- The run command (task 8) depends on dependency propagation being complete
- E2E test workspaces can be created later as follow-up work in `e2e/` directory
- Each checkpoint verifies the build compiles and tests pass before proceeding

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1", "3.1"] },
    { "id": 1, "tasks": ["1.2", "1.3", "3.2"] },
    { "id": 2, "tasks": ["3.3", "4.1", "5.1"] },
    { "id": 3, "tasks": ["3.4", "4.2", "5.2"] },
    { "id": 4, "tasks": ["4.3", "4.4", "5.3", "5.4"] },
    { "id": 5, "tasks": ["7.1", "11.1"] },
    { "id": 6, "tasks": ["7.2", "11.2", "12.1"] },
    { "id": 7, "tasks": ["7.3", "7.4", "11.3", "12.2"] },
    { "id": 8, "tasks": ["8.1", "12.3"] },
    { "id": 9, "tasks": ["8.2", "8.3", "12.4"] },
    { "id": 10, "tasks": ["10.1", "10.2"] },
    { "id": 11, "tasks": ["10.3", "10.4"] }
  ]
}
```
