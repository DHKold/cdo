# Requirements Document

## Introduction

This feature introduces module support to CDo crates, adding resource (`res`) and shader (`shd`) module types that integrate into the build and run commands. The `cdo run` command is enhanced to create a staging folder with the executable, dependencies, and resources for real-condition execution. The standalone `cdo shader` command is removed in favor of a `shd` module processed by the build system. Additionally, three cross-cutting bug fixes are addressed: PAL `pal_path_exists` return code inconsistency, build progress bar accuracy, and coverage source filtering.

## Glossary

- **CDo**: The C/C++ build system and project manager (the `cdo.exe` binary).
- **Crate**: A unit of compilation in a CDo workspace, configured via `crate.toml`.
- **Module**: A named subdirectory within a crate (`lib/`, `exe/`, `dyn/`, `tst/`, `api/`) that defines a compilation target or role. This feature adds `res/` and `shd/` module types.
- **Resource_Module**: A module type (`res/`) containing non-compiled resource files (config files, assets, compiled shaders, etc.) that the executable requires at runtime.
- **Shader_Module**: A module type (`shd/`) containing HLSL shader source files that are compiled to DXIL bytecode during the build.
- **Run_Command**: The `cdo run <crate>` CLI command that builds and executes a crate in a staging environment.
- **Staging_Folder**: A temporary directory (`.cdo/<crate>/run/`) containing the executable, shared library dependencies, and resources, used as the working directory during `cdo run`.
- **Build_Command**: The `cdo build` CLI command that compiles all modules within a crate.
- **Progress_Bar**: The visual progress indicator displayed during builds showing compilation advancement.
- **Coverage_Analyzer**: The component that computes code coverage metrics from gcov data after test execution.
- **PAL**: The Platform Abstraction Layer providing OS-independent file, path, and process operations.
- **DXC**: The DirectX Shader Compiler that compiles HLSL source files into DXIL bytecode.
- **HLSL**: High Level Shading Language source files (`.hlsl` extension).
- **DXIL**: DirectX Intermediate Language bytecode files (`.dxil` extension) produced by DXC.
- **Shared_Library**: A dynamically-linked library (`.dll` on Windows, `.so` on Linux).

## Requirements

### Requirement 1: Resource Module Type

**User Story:** As a developer, I want to declare a `res/` module in my crate, so that I can bundle runtime resource files (config, assets, compiled shaders) alongside my executable.

#### Acceptance Criteria

1. WHEN a crate directory contains a `res/` subdirectory, THE Build_Command SHALL detect the Resource_Module as present during workspace loading.
2. THE Build_Command SHALL register `res` as a valid module kind alongside the existing kinds (`lib`, `exe`, `dyn`, `tst`, `api`).
3. THE Build_Command SHALL treat the Resource_Module as a non-compiled module that produces no object files and no linked artifact.
4. WHEN the Build_Command encounters a Resource_Module, THE Build_Command SHALL copy all files and subdirectories from the `res/` directory into `build/<profile>/<crate>/res/`, preserving the relative directory structure such that for any file at `res/<relative_path>`, the destination is `build/<profile>/<crate>/res/<relative_path>`.
5. WHEN the Build_Command copies Resource_Module files, THE Build_Command SHALL perform incremental copying: a file is copied only when the source modification time is strictly newer than the destination modification time or the destination file does not exist.
6. WHEN the Build_Command completes a Resource_Module build step, THE Build_Command SHALL report the count of copied files and the count of skipped (up-to-date) files at debug verbosity.
7. WHEN the `res/` directory contains no files, THE Build_Command SHALL report zero copied and zero skipped files at debug verbosity and return exit code zero.
8. IF a file within the Resource_Module cannot be copied due to a filesystem error, THEN THE Build_Command SHALL report an error message indicating the source path and the failure reason, and return a non-zero exit code.
9. WHEN a file exists in the destination `build/<profile>/<crate>/res/` but no longer exists in the source `res/` directory, THE Build_Command SHALL remove the stale destination file during the Resource_Module build step.

### Requirement 2: Inter-Crate Module Dependencies

**User Story:** As a developer, I want a crate to depend on another crate's `lib`, `shd`, `res`, `dyn`, and `api` modules, so that I can share shader crates, resource crates, or mixed crates across multiple executables.

#### Acceptance Criteria

1. WHEN a crate declares a dependency on another crate, THE Build_Command SHALL add the dependency crate's `api/` directory to the dependent crate's include search path and link the dependency crate's `lib` module output into the dependent crate's linked artifacts.
2. THE Build_Command SHALL build dependency crates before dependent crates, respecting the existing topological build order.
3. WHEN a dependency crate contains a Resource_Module, THE Build_Command SHALL copy the dependency's built resources from `build/<profile>/<dep_crate>/res/` into `build/<profile>/<crate>/res/` preserving the relative directory structure, using incremental copying where a file is copied only when the source modification time is newer than the destination or the destination does not exist.
4. WHEN a dependency crate contains a Shader_Module, THE Build_Command SHALL copy the dependency's compiled shaders from `build/<profile>/<dep_crate>/shd/` into `build/<profile>/<crate>/shd/` preserving the relative directory structure, using incremental copying where a file is copied only when the source modification time is newer than the destination or the destination does not exist.
5. WHEN a dependency crate contains a `dyn` module, THE Build_Command SHALL add the dependency's Shared_Library to the dependent crate's linker search path and copy the Shared_Library file into `build/<profile>/<crate>/` so it is adjacent to the dependent crate's executable artifact.
6. THE Build_Command SHALL resolve transitive dependencies: if crate A depends on crate B which depends on crate C, crate A SHALL receive crate C's `res`, `shd`, and `dyn` outputs transitively.
7. IF two dependency crates provide resource or shader files with identical relative paths, THEN THE Build_Command SHALL report a conflict error listing both source crates and the conflicting path, and return a non-zero exit code.
8. THE dependency system SHALL exclude `exe` and `tst` modules from inter-crate dependency resolution; only `lib`, `api`, `dyn`, `shd`, and `res` modules are shared.
9. IF the dependency graph contains a cycle, THEN THE Build_Command SHALL report an error message identifying the crates involved in the cycle and return a non-zero exit code without building any crate in the cycle.

### Requirement 3: Enhanced Run Command with Staging Folder

**User Story:** As a developer, I want `cdo run <crate>` to execute my binary in a staging folder with all dependencies and resources, so that I can test under real-condition execution.

#### Acceptance Criteria

1. WHEN `cdo run <crate>` is invoked, THE Run_Command SHALL build the specified crate (equivalent to `cdo build <crate>` with the same profile flags) before proceeding to staging.
2. WHEN the build succeeds, THE Run_Command SHALL create the Staging_Folder at `.cdo/<crate>/run/` relative to the workspace root.
3. WHEN the Staging_Folder is created, THE Run_Command SHALL copy the built executable from `build/<profile>/<crate>/<executable>` into the Staging_Folder, where `<profile>` is `debug` by default or `release` when `--release`/`-r` is passed to the run command.
4. WHEN the crate has Shared_Library dependencies (from `dyn/` modules of dependency crates, including transitive dependencies), THE Run_Command SHALL copy all Shared_Library files (`.dll` on Windows, `.so` on Linux) into the Staging_Folder.
5. WHEN the crate has Resource_Module output (from the crate itself or from dependency crates), THE Run_Command SHALL copy all files from `build/<profile>/<crate>/res/` into the Staging_Folder, preserving the relative directory structure under a `res/` subdirectory.
6. WHEN the crate has Shader_Module output (from the crate itself or from dependency crates), THE Run_Command SHALL copy all files from `build/<profile>/<crate>/shd/` into the Staging_Folder, preserving the relative directory structure under a `shd/` subdirectory.
7. WHEN the Staging_Folder is populated, THE Run_Command SHALL spawn the executable with the current working directory set to the Staging_Folder.
8. WHEN arguments are provided after `--` (argv_rest), THE Run_Command SHALL forward those arguments to the spawned executable in their original order.
9. WHEN no `--` separator is present in the command invocation, THE Run_Command SHALL spawn the executable with no additional arguments.
10. WHEN the spawned executable exits, THE Run_Command SHALL return the executable's exit code as its own exit code.
11. IF the Staging_Folder already exists from a prior run, THEN THE Run_Command SHALL remove its contents before populating it with fresh files.
12. IF the build step fails, THEN THE Run_Command SHALL report the build error and return a non-zero exit code without creating or populating the Staging_Folder.
13. IF the specified crate does not contain an `exe/` module, THEN THE Run_Command SHALL report an error indicating the crate has no executable target and return a non-zero exit code.

### Requirement 4: Shader Module Type

**User Story:** As a developer, I want to declare a `shd/` module in my crate, so that the build command compiles my HLSL shaders as part of the standard build pipeline.

#### Acceptance Criteria

1. WHEN a crate directory contains a `shd/` subdirectory, THE Build_Command SHALL detect the Shader_Module as present during workspace loading.
2. THE Build_Command SHALL register `shd` as a valid module kind alongside the existing kinds (`lib`, `exe`, `dyn`, `tst`, `api`).
3. WHEN the Build_Command encounters a Shader_Module, THE Build_Command SHALL compile all `.hlsl` files in the `shd/` directory (recursively) to DXIL bytecode using DXC, producing one `.dxil` output file per `.hlsl` source file with the same basename.
4. THE Build_Command SHALL place compiled `.dxil` output files in `build/<profile>/<crate>/shd/`, preserving the relative directory structure of the source files within `shd/`.
5. WHEN compiling shaders, THE Build_Command SHALL perform incremental compilation: a shader is recompiled only when the source file modification time is newer than the output file modification time or the output file does not exist.
6. WHEN `--force` is passed to the Build_Command, THE Build_Command SHALL recompile all shaders regardless of modification times.
7. IF DXC is not installed at `.cdo/tools/dxc/bin/dxc.exe`, THEN THE Build_Command SHALL print an error message suggesting `cdo tool install dxc` and return a non-zero exit code without attempting to compile any shaders.
8. IF any shader fails to compile, THEN THE Build_Command SHALL report the DXC error output for each failing shader, continue attempting to compile remaining shaders, and return a non-zero exit code after all shaders have been processed.
9. WHEN all shaders compile successfully, THE Build_Command SHALL print a summary at info verbosity showing the count of compiled shaders and the count of skipped (up-to-date) shaders.
10. WHEN the Shader_Module directory contains no `.hlsl` files, THE Build_Command SHALL print a summary showing zero compiled and zero skipped shaders and return exit code zero.

### Requirement 5: Remove Standalone Shader Command

**User Story:** As a developer, I want shader compilation handled entirely by the build system through the `shd` module, so that I have a single consistent build workflow.

#### Acceptance Criteria

1. WHEN a user invokes `cdo shader` or `cdo shader <subcommand>`, THE CDo binary SHALL print an error message indicating the command has been removed and suggest using a `shd/` module in the crate instead, and return exit code 1.
2. THE CDo binary SHALL remove the `shader` entry from the `--help` output command listing.
3. THE CDo binary SHALL remove all source files related to the standalone shader command (`cmd_shader.c`, `cmd_shader.h`, `shader.c`, `shader.h`) from the build.

### Requirement 6: PAL path_exists Return Code Fix

**User Story:** As a developer, I want `pal_path_exists` to return consistent values across platforms, so that callers do not need platform-specific checks.

#### Acceptance Criteria

1. THE PAL SHALL define `pal_path_exists` to return `0` (PAL_OK) when the path exists and `PAL_ERR_NOT_FOUND` (9) when the path does not exist, consistent with the CDo error-code convention used by all other PAL functions.
2. WHEN `pal_path_exists` is called with a NULL or empty-string path, THE PAL SHALL return `PAL_ERR_NOT_FOUND`.
3. WHEN `pal_path_exists` is called with a path pointing to an existing regular file, THE PAL SHALL return `0` on both Windows and Linux.
4. WHEN `pal_path_exists` is called with a path pointing to an existing directory, THE PAL SHALL return `0` on both Windows and Linux.
5. WHEN `pal_path_exists` is called with a path that does not exist, THE PAL SHALL return `PAL_ERR_NOT_FOUND` on both Windows and Linux.
6. WHEN the return code convention is changed, all existing callers of `pal_path_exists` throughout the codebase SHALL be updated to use the corrected convention (checking `== 0` for existence, `!= 0` for absence).
7. THE Windows implementation SHALL use `GetFileAttributesW` and return `0` when attributes are valid, `PAL_ERR_NOT_FOUND` when `INVALID_FILE_ATTRIBUTES` is returned.
8. THE Linux implementation SHALL use `stat()` and return `0` when `stat` succeeds, `PAL_ERR_NOT_FOUND` when `stat` fails.

### Requirement 7: Build Progress Bar Global Accuracy

**User Story:** As a developer, I want the build progress bar to show the total number of files being compiled across all crates, so that I can monitor overall build progress.

#### Acceptance Criteria

1. WHEN a build is initiated, THE Build_Command SHALL count the total number of compilable source files (files with extensions `.c`, `.cpp`, `.cxx`, or `.cc`) across all crates in the build order before compilation begins.
2. THE Progress_Bar SHALL display the current completed count out of the total file count (e.g., "Building [====    ] 5/12") rather than per-crate counts.
3. WHEN compilation of each source file completes, THE Build_Command SHALL increment the completed count and update the Progress_Bar.
4. WHEN a build targets a single crate, THE Progress_Bar SHALL show the total for that crate and its dependencies only.
5. WHEN a build targets multiple crates, THE Progress_Bar SHALL show the combined total across all targeted crates and their dependencies.
6. WHEN a crate's compilable source files are all up-to-date and no recompilation is needed, THE Build_Command SHALL immediately add that crate's compilable file count to the completed count and update the Progress_Bar.
7. IF the total compilable source file count is zero, THEN THE Progress_Bar SHALL not be displayed and the Build_Command SHALL proceed without error.
8. IF a compilation error occurs mid-build, THEN THE Progress_Bar SHALL finalize at its current completed count before the Build_Command reports the error and returns a non-zero exit code.

### Requirement 8: Coverage Source Filtering

**User Story:** As a developer, I want code coverage to exclude third-party and standard library sources, so that coverage metrics reflect only my project code.

#### Acceptance Criteria

1. WHEN the Coverage_Analyzer generates coverage data, THE Coverage_Analyzer SHALL include only source files whose resolved absolute path starts with the workspace root's `crates/` directory prefix.
2. WHEN gcov produces coverage data referencing a source file whose resolved absolute path does not start with the workspace root's `crates/` directory prefix, THE Coverage_Analyzer SHALL omit that file from the coverage report and from the computed coverage percentage.
3. THE Coverage_Analyzer SHALL resolve each source file path reported by gcov to an absolute path using the workspace root as the base directory before applying the inclusion prefix check, normalizing both forward slashes and backslashes to the platform's native separator.
4. IF all source files in the gcov output are excluded by the filtering rules, THEN THE Coverage_Analyzer SHALL report 0% coverage and display no per-file entries in the coverage report.
5. THE Coverage_Analyzer SHALL treat the inclusion prefix as a case-sensitive path comparison on Linux and a case-insensitive path comparison on Windows.
