# Implementation Plan: Multi-Module Crates

## Overview

Extend CDo's build model from "one crate = one artifact" to "one crate = N modules, each producing a distinct artifact." This involves introducing a `ModuleKind` enum and `Module` struct, extending the `Crate` struct, updating the scanner to discover module subdirectories (`lib/`, `exe/`, `dyn/`, `tst/`, `api/`), modifying the compiler/linker pipeline to build modules in dependency order, and updating inter-crate resolution to target library modules.

## Tasks

- [x] 1. Define module data structures and update Crate struct
  - [x] 1.1 Create `module.h` with `ModuleKind` enum and `Module` struct
    - Define `ModuleKind` enum: `MODULE_LIB`, `MODULE_EXE`, `MODULE_DYN`, `MODULE_TST`, `MODULE_API`
    - Define `Module` struct with `kind`, `dir_path[260]`, `sources` (FileList), `artifact_path[260]`, `present` (bool)
    - Add helper function declarations: `module_kind_to_string()`, `module_artifact_extension()`
    - Place in `crates/cdo/src/core/module.h`
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6_

  - [x] 1.2 Update `Crate` struct in `workspace.h` to include module fields
    - Add `Module modules[5]` array indexed by `ModuleKind`
    - Add `int module_count` for number of present modules
    - Add `bool has_lib` and `bool has_api` shortcut flags
    - Keep existing `CrateType type` field for backward compatibility but mark it as deprecated/ignored
    - _Requirements: 9.1, 9.2_

  - [x] 1.3 Implement `module.c` with helper functions
    - Implement `module_kind_to_string()` returning human-readable names
    - Implement `module_artifact_extension()` returning platform-appropriate extensions
    - Implement `module_artifact_name()` computing full artifact filename from crate name and module kind
    - Place in `crates/cdo/src/core/module.c`
    - _Requirements: 11.2, 11.3, 11.4, 11.5_

- [x] 2. Extend the scanner for module directory discovery
  - [x] 2.1 Add `scanner_scan_modules()` to `scanner.h` and implement in `scanner.c`
    - Probe crate directory for well-known subdirectories: `lib/`, `exe/`, `dyn/`, `tst/`, `api/`
    - Populate `crate->modules[]` array, setting `present = true` for each found directory
    - Set `crate->module_count`, `crate->has_lib`, `crate->has_api`
    - Compute `dir_path` for each discovered module
    - Return error if no module directories are found
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8_

  - [x] 2.2 Add `scanner_scan_module_sources()` to scan individual module directories
    - Scan recursively for `.c` and `.cpp` files within a module directory
    - For `MODULE_API`, scan only for header files (`.h`, `.hpp`)
    - Apply exclude patterns relative to the module directory root
    - Populate the module's `FileList sources` field
    - _Requirements: 10.1, 10.2, 10.3, 10.4_

  - [x]* 2.3 Write unit tests for module scanning
    - Test discovery of all five module directory types
    - Test that missing directories are correctly marked `present = false`
    - Test error case when no module directories exist
    - Test that non-well-known directories (e.g., `src/`) are ignored
    - Test recursive source scanning within module directories
    - Test that `api/` only yields header files
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 10.1, 10.2, 10.3, 10.4_

- [x] 3. Update workspace loading and dependency resolution
  - [x] 3.1 Modify `workspace_load()` to invoke module scanning
    - After parsing `crate.toml`, call `scanner_scan_modules()` for each crate
    - Ignore the `type` field in `crate.toml` when module directories are found
    - Apply `c-standard`, `cpp-standard`, `[dependencies]`, and `[build]` settings to all modules
    - _Requirements: 9.1, 9.2, 9.3_

  - [x] 3.2 Implement `workspace_resolve_module_deps()` for inter-crate validation
    - When Crate A depends on Crate B, validate Crate B has a Library_Module (`has_lib == true`)
    - Report error if target crate has no Library_Module
    - Resolve transitive dependencies: if B depends on C, A links against C's library too
    - Detect and report cycles in the dependency graph
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6_

  - [x]* 3.3 Write unit tests for workspace module resolution
    - Test valid inter-crate dependency on a crate with lib/ module
    - Test error when depending on a crate without lib/ module
    - Test transitive dependency resolution
    - Test cycle detection
    - _Requirements: 7.1, 7.2, 7.3, 7.5, 7.6_

- [x] 4. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Implement module include path resolution
  - [x] 5.1 Implement `module_include_paths()` function
    - For `MODULE_LIB`: add `lib/` and `api/` (if present) to include paths
    - For `MODULE_EXE`: add `exe/`, `lib/`, and `api/` (if present) to include paths
    - For `MODULE_DYN`: add `dyn/`, `lib/`, and `api/` (if present) to include paths
    - For `MODULE_TST`: add `tst/`, `lib/`, and `api/` (if present) to include paths
    - For inter-crate deps: add target crate's `api/` directory, or fallback to `include/` if no `api/` exists
    - Place in `crates/cdo/src/core/module.c`
    - _Requirements: 2.3, 2.4, 3.2, 4.3, 5.4, 6.1, 6.2, 6.3_

  - [x]* 5.2 Write unit tests for include path resolution
    - Test each module kind gets correct include paths
    - Test api/ directory fallback to include/ for external crates
    - Test that lib/ headers are NOT exposed to external crates
    - _Requirements: 6.1, 6.2, 6.3_

- [x] 6. Implement module compilation pipeline
  - [x] 6.1 Implement Library_Module compilation in `cmd_build.c`
    - Compile all `.c`/`.cpp` files in `lib/` into object files placed in `build/<profile>/<crate_name>/lib/`
    - Archive objects into a static library artifact at `build/<profile>/<crate_name>/<crate_name>.lib` (Windows) or `build/<profile>/<crate_name>/lib<crate_name>.a` (Unix)
    - Final artifact lives at the crate level, NOT inside the `lib/` subfolder
    - Report error if `lib/` has no compilable source files
    - Ensure Library_Module compiles before all other modules in the same crate
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 8.1, 11.1, 11.2_

  - [x] 6.2 Implement Executable_Module compilation
    - Compile `exe/` sources into object files placed in `build/<profile>/<crate_name>/exe/`
    - Use include paths from `module_include_paths()`
    - Link against the Library_Module's static library artifact (if present)
    - If no Library_Module exists, compile as standalone with only `exe/` include path
    - Output artifact to `build/<profile>/<crate_name>/<crate_name>.exe` (crate level, not in exe/ subfolder)
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 11.1, 11.3_

  - [x] 6.3 Implement Shared_Library_Module compilation
    - Compile `dyn/` sources into object files placed in `build/<profile>/<crate_name>/dyn/`
    - Use position-independent code flags (`-fPIC` on GCC/Clang)
    - Link against the Library_Module's static library artifact
    - Report error if no Library_Module exists in the same crate
    - Output artifact to `build/<profile>/<crate_name>/<crate_name>.dll` (crate level, not in dyn/ subfolder)
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 11.1, 11.4_

  - [x] 6.4 Implement Test_Module compilation
    - Compile `tst/` sources into object files placed in `build/<profile>/<crate_name>/tst/`
    - Add `CDO_TESTING` define to compile jobs
    - Link against the Library_Module's static library artifact
    - Report error if no Library_Module exists in the same crate
    - Output artifact to `build/<profile>/<crate_name>/<crate_name>_test.exe` (crate level, not in tst/ subfolder)
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 11.1, 11.5_

  - [x]* 6.5 Write unit tests for module compilation pipeline
    - Test Library_Module compilation produces correct static library
    - Test Executable_Module links against library artifact
    - Test Shared_Library_Module uses PIC flags
    - Test Test_Module includes CDO_TESTING define
    - Test error when dyn/ or tst/ exists without lib/
    - _Requirements: 2.1, 3.1, 4.1, 4.4, 5.1, 5.5_

- [x] 7. Implement intra-crate build orchestration
  - [x] 7.1 Implement `build_crate_modules()` function
    - Build Library_Module first; on failure, skip all other modules and report error
    - After lib/ succeeds, build exe/, dyn/, tst/ (can be sequential for now)
    - Wire inter-crate library linking: link against all dependency crates' library artifacts
    - Handle transitive linking (if A depends on B depends on C, link A against C's artifact too)
    - Place in `crates/cdo/src/core/compiler.c` or a new `module_build.c`
    - _Requirements: 8.1, 8.2, 8.3, 7.2, 7.5_

  - [x] 7.2 Integrate `build_crate_modules()` into `cmd_build()` flow
    - When processing a crate in build order, call `build_crate_modules()` instead of the current single-artifact flow
    - Maintain existing workspace-level topological sort for inter-crate ordering
    - Ensure all modules of crate N complete before crate N+1 starts
    - _Requirements: 8.3_

- [x] 8. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 9. Artifact output path generation and backward compatibility
  - [x] 9.1 Implement artifact path computation for each module kind
    - Object file directories: `build/<profile>/<crate_name>/lib/`, `build/<profile>/<crate_name>/exe/`, `build/<profile>/<crate_name>/dyn/`, `build/<profile>/<crate_name>/tst/`
    - Final artifact path: `build/<profile>/<crate_name>/<artifact_filename>` (at crate level, not inside module subfolder)
    - Set `module.artifact_path` to the crate-level path during build based on profile and platform
    - Create both object-file subdirectories and the crate-level output directory as needed
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5_

  - [x] 9.2 Handle backward compatibility for `include/` fallback
    - When a dependent crate has no `api/` directory, check for `include/` and use it as public include path
    - When a crate has no module directories at all, report the error as specified
    - _Requirements: 6.3, 1.7_

  - [x]* 9.3 Write integration tests for end-to-end multi-module build
    - Create a test fixture crate with lib/, exe/, and tst/ directories
    - Verify object files are produced in module subfolders (`build/<profile>/<crate_name>/lib/`, `exe/`, `tst/`)
    - Verify final artifacts are produced at crate level (`build/<profile>/<crate_name>/<crate_name>.lib`, `<crate_name>.exe`, `<crate_name>_test.exe`)
    - Verify inter-crate dependency linking works end-to-end
    - Verify `include/` fallback works when no `api/` exists
    - _Requirements: 11.1, 11.2, 11.3, 11.5, 6.3_

- [x] 10. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- The implementation language is C (GCC via w64devkit), matching the existing codebase
- Build with `.\cdo.exe build`, tests with `.\cdo.exe build cdo_pbt` then `.\build\release\cdo_pbt\cdo_pbt.exe`
- The design does not include a Correctness Properties section, so property-based test tasks are not included
- Unit tests should be added to the `cdo_pbt` crate's source directory

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["1.2", "1.3"] },
    { "id": 2, "tasks": ["2.1"] },
    { "id": 3, "tasks": ["2.2", "3.1"] },
    { "id": 4, "tasks": ["2.3", "3.2"] },
    { "id": 5, "tasks": ["3.3", "5.1"] },
    { "id": 6, "tasks": ["5.2", "6.1"] },
    { "id": 7, "tasks": ["6.2", "6.3", "6.4"] },
    { "id": 8, "tasks": ["6.5", "7.1"] },
    { "id": 9, "tasks": ["7.2"] },
    { "id": 10, "tasks": ["9.1", "9.2"] },
    { "id": 11, "tasks": ["9.3"] }
  ]
}
```
