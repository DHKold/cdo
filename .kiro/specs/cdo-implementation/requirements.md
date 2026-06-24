# Requirements Document

## Introduction

CDo (C Development Ops) is a project build management tool for C/C++ projects, currently implemented as ~2300 lines of PowerShell scripts. This document specifies requirements for a ground-up rewrite as a native C17 binary. The new CDo replaces CMake and Ninja entirely — it owns the full build pipeline from source detection through compiler invocation. It adopts a Cargo-inspired "crate" model for project organization, uses TOML as its primary configuration format, and provides rich terminal output with colors and progress reporting.

This is not a backward-compatible migration. Existing projects will require manual migration to the new format. The new CDo targets only future projects.

## Glossary

- **CDo_Binary**: The native C17 executable implementing the CDo build system and project manager
- **Crate**: A self-contained compilation unit (executable, library, or test) with its own manifest, source directory, and dependencies — analogous to Rust's crate concept
- **Workspace**: A collection of crates managed together, defined by a top-level `cdo.toml`
- **Workspace_Root**: The directory containing the top-level `cdo.toml`
- **Crate_Manifest**: A `crate.toml` file within a crate's directory defining its metadata, dependencies, and build settings
- **Workspace_Manifest**: The top-level `cdo.toml` file declaring workspace members, shared settings, and dependency catalogs
- **Build_Directory**: The output directory for compiled artifacts (default: `build/`)
- **Tool_Store**: The `.cdo/tools/` directory containing vendored portable tools (e.g., DXC)
- **Cache_Directory**: The `.cdo/cache/` directory for downloaded archives and intermediate artifacts
- **Dependency_Registry**: A remote or local catalog of available packages with version metadata and download URLs
- **Local_Cache**: A machine-wide cache of downloaded dependency archives shared across projects
- **Template_Engine**: The component that processes skeleton templates with variable substitution and conditional sections
- **Skeleton_Catalog**: A collection of project templates available locally or from a remote registry
- **Compiler_Driver**: The CDo component that invokes the C/C++ compiler with correct flags, include paths, and link libraries
- **Source_Scanner**: The component that discovers source files within a crate's directory structure
- **JSON_Parser**: The embedded JSON parsing component (for legacy/interop support)
- **TOML_Parser**: The embedded TOML parsing component for reading configuration files
- **Shader_Compiler**: The DXC-based component that compiles HLSL shaders to DXIL and SPIR-V
- **Output_Renderer**: The component responsible for terminal output including colors, formatting, progress bars, and log level filtering
- **Log_Level**: The configurable verbosity setting (error, warn, info, debug, trace)

## Requirements

### Requirement 1: CLI Interface

**User Story:** As a developer, I want a clean, discoverable CLI with subcommands, so that I can perform all project operations through a single tool.

#### Acceptance Criteria

1. THE CDo_Binary SHALL accept the following top-level commands: build, run, test, clean, new, init, add, remove, source, shader, tool, doctor, and self
2. WHEN an unrecognized command is provided, THE CDo_Binary SHALL print an error message naming the unrecognized command, suggest similar commands, and exit with a non-zero status code
3. THE CDo_Binary SHALL accept global options --verbose, --quiet, --log-level, --color (auto|always|never), and --help on all commands
4. WHEN --help is provided, THE CDo_Binary SHALL print usage information for the specified command and exit with status code 0; WHEN invalid arguments trigger usage display in an error context, THE CDo_Binary SHALL exit with a non-zero status code
5. WHEN invoked with no command, THE CDo_Binary SHALL print a summary of available commands

### Requirement 2: Workspace and Crate Model

**User Story:** As a developer, I want to organize my project as a workspace of crates, so that each compilation unit is self-contained with its own configuration and can be built independently or together.

#### Acceptance Criteria

1. THE CDo_Binary SHALL recognize a workspace by the presence of a `cdo.toml` file containing a [workspace] section with a members array
2. THE CDo_Binary SHALL recognize a crate by the presence of a `crate.toml` file in its directory
3. WHEN a crate declares dependencies on other crates in the same workspace, THE CDo_Binary SHALL resolve those dependencies and build them in topological order
4. THE CDo_Binary SHALL support crate types: executable, static-library, shared-library, and test
5. WHEN a circular dependency is detected between crates, THE CDo_Binary SHALL report the cycle and exit immediately with a non-zero status code without attempting further dependency resolution

### Requirement 3: TOML Configuration Format

**User Story:** As a developer, I want to use TOML as my primary configuration format, with optional YAML and JSON support, so that configuration files are human-readable and easy to edit.

#### Acceptance Criteria

1. THE TOML_Parser SHALL parse TOML documents conforming to TOML v1.0 specification
2. THE CDo_Binary SHALL look for configuration files in order: `.toml`, `.yaml`, `.json` — and use the first found
3. WHEN a configuration file contains syntax errors, THE CDo_Binary SHALL report the file path, line number, and character position of the error
4. THE TOML_Parser SHALL always maintain UTF-8 handling capability when processing files
5. FOR ALL valid TOML configuration documents, parsing then serializing then parsing SHALL produce an equivalent object (round-trip property)

### Requirement 4: Build Command — Build All

**User Story:** As a developer, I want `cdo build` with no arguments to build all crates in the workspace, so that a single command compiles everything.

#### Acceptance Criteria

1. WHEN the build command is invoked with no crate argument, THE CDo_Binary SHALL build all crates declared in the Workspace_Manifest in dependency order
2. WHEN the build command is invoked with one or more crate names, THE CDo_Binary SHALL build only the specified crates and their transitive dependencies
3. WHEN a specified crate name does not match any declared crate, THE CDo_Binary SHALL print an error naming the unknown crate and exit with a non-zero status code regardless of whether other specified crates are valid

### Requirement 5: Build System — Direct Compiler Invocation

**User Story:** As a developer, I want CDo to invoke the compiler directly without relying on CMake or Ninja, so that there are no external build system dependencies.

#### Acceptance Criteria

1. THE Compiler_Driver SHALL invoke the C/C++ compiler directly with the correct source files, include paths, library paths, link libraries, and compilation flags
2. THE Compiler_Driver SHALL support GCC, Clang, and MSVC compiler families and detect the available compiler at build time
3. THE Compiler_Driver SHALL perform incremental compilation by tracking source file and header modification timestamps and only recompiling changed translation units; WHEN timestamp tracking fails or becomes corrupted, THE Compiler_Driver SHALL fall back to a full rebuild
4. THE Compiler_Driver SHALL compute the dependency graph between translation units using compiler-generated dependency information (e.g., -MMD flag for GCC/Clang)
5. THE Compiler_Driver SHALL link object files into the final artifact (executable or library) using the appropriate linker invocation
6. THE Compiler_Driver SHALL support configurable C standard (C11, C17, C23) and C++ standard (C++17, C++20, C++23) per crate

### Requirement 6: Parallel Compilation

**User Story:** As a developer, I want CDo to compile translation units in parallel by default, so that builds complete as fast as possible.

#### Acceptance Criteria

1. THE Compiler_Driver SHALL compile independent translation units in parallel using a thread pool
2. THE CDo_Binary SHALL default the parallelism level to the number of logical CPU cores detected on the host
3. WHEN the --jobs option is provided, THE CDo_Binary SHALL use the specified number of parallel compilation jobs
4. THE Output_Renderer SHALL display a progress indicator showing completed and total compilation units during parallel builds

### Requirement 7: Automatic Source Detection

**User Story:** As a developer, I want CDo to automatically detect source files in a crate's directory, so that I do not need to manually register files.

#### Acceptance Criteria

1. THE Source_Scanner SHALL discover all `.c`, `.cpp`, `.h`, and `.hpp` files within a crate's `src/` directory recursively
2. WHEN a crate's manifest specifies explicit source files or glob patterns in an `exclude` field, THE Source_Scanner SHALL omit matching files
3. WHEN a new source file is added to a crate's directory, THE CDo_Binary SHALL detect it on the next build without manual registration; IF automatic detection fails, THEN THE CDo_Binary SHALL fall back to using explicitly registered files from the crate manifest
4. THE Source_Scanner SHALL detect header files in a crate's `include/` directory and add the directory to the include path

### Requirement 8: Test Execution

**User Story:** As a developer, I want `cdo test` to build and run tests with fail-fast on build errors and support for running individual tests, so that I get rapid feedback.

#### Acceptance Criteria

1. WHEN the test command is invoked with no arguments, THE CDo_Binary SHALL build and run all crates of type test in the workspace
2. IF a test crate fails to compile, THEN THE CDo_Binary SHALL report the build error, skip execution of that crate, and indicate in the summary that tests could not run due to build failure
3. WHEN the test command is invoked with a specific crate name, THE CDo_Binary SHALL build and run only the test crate matching that name
4. WHEN a test executable returns a non-zero exit code, THE CDo_Binary SHALL report the test as failed and include the exit code in the output
5. THE CDo_Binary SHALL report a summary of passed and failed test counts after all tests complete

### Requirement 9: Incremental Shader Compilation

**User Story:** As a developer, I want shader compilation to skip unchanged shaders based on file timestamps, so that only modified shaders are recompiled.

#### Acceptance Criteria

1. WHEN shader compile is invoked, THE Shader_Compiler SHALL compare the modification timestamp of each shader source file against its compiled output
2. WHILE a shader source file has a modification timestamp older than or equal to its compiled output, THE Shader_Compiler SHALL skip compilation of that shader
3. WHEN a shader source file has a modification timestamp newer than its compiled output, THE Shader_Compiler SHALL recompile that shader
4. WHEN the compiled output file does not exist, THE Shader_Compiler SHALL compile the shader regardless of source timestamp
5. THE CDo_Binary SHALL report the count of shaders compiled and the count skipped

### Requirement 10: Clean Command

**User Story:** As a developer, I want a `cdo clean` command to remove all build artifacts, so that I can start with a fresh build state.

#### Acceptance Criteria

1. WHEN the clean command is invoked with no arguments, THE CDo_Binary SHALL delete the entire Build_Directory and all its contents
2. WHEN the clean command is invoked with a crate name, THE CDo_Binary SHALL delete only the build artifacts for that crate
3. WHEN the target directory does not exist, THE CDo_Binary SHALL print a message indicating nothing to clean and exit with status code 0; WHEN the clean operation fails for other reasons, THE CDo_Binary SHALL exit with a non-zero status code

### Requirement 11: Dependency Management — Registries and Catalogs

**User Story:** As a developer, I want a rich dependency management system with remote registries, a local catalog, and a machine-wide cache, so that adding libraries is fast and reliable.

#### Acceptance Criteria

1. THE Dependency_Registry SHALL support multiple remote registry URLs configured in the Workspace_Manifest
2. WHEN the add command is invoked with a dependency name, THE CDo_Binary SHALL search configured registries for the package and download it
3. THE CDo_Binary SHALL maintain a machine-wide Local_Cache of downloaded archives so that repeated downloads across projects are avoided
4. WHEN a dependency is already in the Local_Cache with a matching version, THE CDo_Binary SHALL use the cached archive without downloading
5. THE CDo_Binary SHALL record exact dependency versions in a `cdo.lock` file for reproducible builds
6. WHEN the remove command is invoked, THE CDo_Binary SHALL remove the dependency from the crate manifest and regenerate the lock file
7. THE CDo_Binary SHALL support dependencies from: registry packages, Git repositories (with tag/branch/commit), and local filesystem paths

### Requirement 12: Dependency Wiring

**User Story:** As a developer, I want dependencies to be automatically wired into the build (include paths, library paths, link flags) based on their metadata, so that no manual configuration is needed.

#### Acceptance Criteria

1. WHEN a dependency is resolved, THE Compiler_Driver SHALL add the dependency's include directories to the crate's include path
2. WHEN a dependency is resolved, THE Compiler_Driver SHALL add the dependency's library path and link the appropriate library files; IF either operation fails, THEN THE CDo_Binary SHALL fail the build with an error
3. WHEN a dependency has runtime DLLs (shared libraries), THE CDo_Binary SHALL copy them to the output directory alongside the built executable
4. THE CDo_Binary SHALL support dependencies that provide pkg-config metadata, CMake package config, or CDo-native package metadata

### Requirement 13: Template-Based Project Initialization

**User Story:** As a developer, I want `cdo new` to create projects from templates that come from a catalog (local or remote), so that skeletons are extensible without hardcoding.

#### Acceptance Criteria

1. WHEN the new command is invoked with a template name, THE Template_Engine SHALL fetch the template from the Skeleton_Catalog and instantiate it in the target directory
2. THE Template_Engine SHALL perform variable substitution in template files (project name, crate name, author, year, and custom variables)
3. THE Template_Engine SHALL support conditional sections in templates for optional features
4. THE Skeleton_Catalog SHALL support both local templates (in `~/.cdo/templates/`) and remote templates fetched from a registry
5. WHEN the new command is invoked with --list, THE CDo_Binary SHALL display available templates with their descriptions
6. IF the target directory exists and is not empty, THEN THE CDo_Binary SHALL refuse to create the project and skip template fetching unless --force is provided

### Requirement 14: Rich Terminal Output

**User Story:** As a developer, I want CDo to provide colored, well-formatted output with progress indicators and configurable verbosity, so that I can quickly understand build status.

#### Acceptance Criteria

1. THE Output_Renderer SHALL use ANSI color codes to distinguish message types: errors in red, warnings in yellow, success in green, and info in the default color
2. THE Output_Renderer SHALL display a progress bar or spinner during long-running operations (compilation, downloads, extraction)
3. WHEN --quiet is provided, THE Output_Renderer SHALL suppress all output except errors; the process exit code SHALL be the sole indicator of success or failure
4. WHEN --verbose is provided, THE Output_Renderer SHALL include detailed diagnostic output including executed commands and timing
5. THE Output_Renderer SHALL respect the --color option: auto (detect TTY), always, or never
6. WHEN standard output is not a TTY, THE Output_Renderer SHALL default to no colors and no progress animations regardless of whether output is redirected to a file
7. THE CDo_Binary SHALL support configurable Log_Level (error, warn, info, debug, trace) via --log-level option

### Requirement 15: Cross-Platform Support

**User Story:** As a developer, I want CDo to work on Windows, Linux, and macOS from the same codebase, so that teams can collaborate across operating systems.

#### Acceptance Criteria

1. THE CDo_Binary SHALL compile and run on Windows (x86_64), Linux (x86_64, aarch64), and macOS (x86_64, aarch64)
2. THE CDo_Binary SHALL use platform-appropriate APIs for process spawning (CreateProcess on Windows, posix_spawn/fork+exec on POSIX)
3. THE CDo_Binary SHALL use platform-appropriate APIs for file system operations (directory traversal, file timestamps, path separator handling)
4. THE CDo_Binary SHALL use platform-appropriate mechanisms for determining the number of logical CPU cores
5. THE CDo_Binary SHALL handle path separators transparently (accepting both `/` and `\` on Windows)

### Requirement 16: Fast Startup

**User Story:** As a developer, I want CDo to start in under 5 milliseconds, so that there is no perceptible delay when running commands.

#### Acceptance Criteria

1. THE CDo_Binary SHALL complete process startup and begin parsing command-line arguments within 5 milliseconds on a typical development machine
2. THE CDo_Binary SHALL defer loading and parsing of configuration files until the executing command requires them
3. THE CDo_Binary SHALL not perform network operations during startup

### Requirement 17: Self-Contained Binary

**User Story:** As a developer, I want CDo to be a single binary with no runtime dependencies beyond the C standard library and platform APIs, so that it runs on any machine without installation.

#### Acceptance Criteria

1. THE CDo_Binary SHALL compile as a single executable file with no dynamic library dependencies beyond system libraries
2. THE CDo_Binary SHALL be written in C17 conformant code; compiler-specific extensions or newer C standards are permitted when they significantly simplify embedded functionality implementations
3. THE CDo_Binary SHALL embed all required functionality (TOML parsing, JSON parsing, ZIP extraction, HTTP fetching, template processing) without linking external libraries
4. THE CDo_Binary SHALL be distributable as a single file that can be placed anywhere on the filesystem

### Requirement 18: Process Spawning

**User Story:** As a developer, I want CDo to spawn child processes (compilers, test executables, shader tools) reliably on all platforms, so that builds and tests execute correctly.

#### Acceptance Criteria

1. THE CDo_Binary SHALL spawn child processes and capture their exit codes
2. THE CDo_Binary SHALL forward the standard output and standard error of child processes to the terminal in real-time
3. WHEN a child process exits with a non-zero status code, THE CDo_Binary SHALL propagate that non-zero exit code as its own exit code (unless handling multiple crates, in which case it SHALL report individual failures and exit with a non-zero code after all crates are processed)
4. THE CDo_Binary SHALL pass environment variables including PATH modifications to child processes
5. THE CDo_Binary SHALL support spawning multiple child processes concurrently for parallel compilation

### Requirement 19: Archive Extraction

**User Story:** As a developer, I want CDo to extract ZIP and tar archives for tool and dependency installation, so that external utilities are not required.

#### Acceptance Criteria

1. THE CDo_Binary SHALL extract ZIP archives without relying on external tools
2. THE CDo_Binary SHALL extract tar.gz (gzip-compressed tar) archives without relying on external tools
3. THE CDo_Binary SHALL preserve directory structure within extracted archives
4. THE CDo_Binary SHALL preserve file permissions on POSIX platforms during extraction; on non-POSIX platforms, file permissions SHALL be ignored
5. IF an archive is corrupted or unreadable, THEN THE CDo_Binary SHALL report the error including the file path and exit with a non-zero status code

### Requirement 20: Tool Management

**User Story:** As a developer, I want CDo to manage vendored tools (DXC, etc.) in the project-local `.cdo/tools/` directory, so that the build environment is self-contained.

#### Acceptance Criteria

1. WHEN a tool install command is invoked, THE CDo_Binary SHALL download the tool archive to the Local_Cache
2. WHEN a tool archive exists in the Local_Cache with a matching version, THE CDo_Binary SHALL skip the download unless --refresh is specified
3. THE CDo_Binary SHALL extract only archives that were explicitly downloaded by CDo into the Tool_Store
4. WHEN a tool installation completes, THE CDo_Binary SHALL write a `cdo-tool.toml` manifest recording installed version and binary paths
5. THE CDo_Binary SHALL support downloading over HTTPS (via platform APIs or by invoking curl/wget as a fallback)
6. WHEN the tool doctor command is invoked, THE CDo_Binary SHALL verify all declared tools are installed and functional

### Requirement 21: Doctor Command

**User Story:** As a developer, I want `cdo doctor` to diagnose environment problems and optionally fix them, so that I can quickly resolve setup issues.

#### Acceptance Criteria

1. WHEN the doctor command is invoked, THE CDo_Binary SHALL check for the presence of a C/C++ compiler on the system PATH
2. WHEN the doctor command is invoked, THE CDo_Binary SHALL verify that declared dependencies are resolved and present
3. WHEN the doctor command is invoked, THE CDo_Binary SHALL verify workspace and crate manifest files are syntactically valid
4. WHEN the --fix flag is provided and a fixable issue is found, THE CDo_Binary SHALL attempt to resolve it automatically (e.g., install missing tools, regenerate lock file) regardless of whether other checks are passing
5. THE CDo_Binary SHALL print a color-coded status line for each check indicating pass, warning, or fail
6. WHEN every check passes, THE CDo_Binary SHALL exit with status code 0; WHEN any single check fails, THE CDo_Binary SHALL exit with a non-zero status code

### Requirement 22: Build Configuration

**User Story:** As a developer, I want to control build profiles (debug/release/custom) via CLI flags and crate manifests, so that I can switch between debug and optimized builds easily.

#### Acceptance Criteria

1. WHEN the --release flag is provided, THE Compiler_Driver SHALL compile with optimization enabled and debug information disabled by default; WHEN additional flags explicitly request debug information, THE Compiler_Driver SHALL include debug symbols in the optimized build
2. WHEN the --profile option is provided with a value, THE Compiler_Driver SHALL use the named profile's settings from the workspace manifest
3. WHILE no profile flag is specified, THE Compiler_Driver SHALL use the debug profile (no optimization, debug symbols enabled)
4. THE CDo_Binary SHALL support user-defined profiles in the Workspace_Manifest with custom compiler flags, optimization levels, and defines; custom profiles MAY enable optimization without requiring --release

### Requirement 23: Run Command

**User Story:** As a developer, I want `cdo run` to build and then execute an executable crate, forwarding arguments to the launched program, so that the build-run cycle is a single command.

#### Acceptance Criteria

1. WHEN the run command is invoked, THE CDo_Binary SHALL build the specified crate before executing it
2. WHEN the run command is invoked with arguments after `--`, THE CDo_Binary SHALL forward those arguments to the executed program
3. IF the build step fails, THEN THE CDo_Binary SHALL report the build error and not attempt to run the executable or forward arguments
4. WHEN no crate is specified and the workspace contains exactly one executable crate, THE CDo_Binary SHALL run that crate
5. WHEN no crate is specified and the workspace contains multiple executable crates, THE CDo_Binary SHALL print an error listing available executable crates and exit without building or running anything

### Requirement 24: Error Reporting

**User Story:** As a developer, I want CDo to provide clear, actionable error messages with context, so that I can resolve issues without searching external documentation.

#### Acceptance Criteria

1. WHEN a compiler error occurs, THE CDo_Binary SHALL display the compiler's error output with the source file path and line number preserved
2. WHEN a common error pattern is detected (missing header, undefined symbol, linker error), THE CDo_Binary SHALL append a CDo-specific hint suggesting a resolution
3. WHEN a dependency is missing, THE CDo_Binary SHALL suggest the `cdo add` command with the likely package name
4. THE CDo_Binary SHALL not suppress or modify the original error text from the compiler or linker
5. WHEN an internal CDo error occurs (and only when an internal error occurs), THE CDo_Binary SHALL display a clear message with the error context and suggest filing a bug report

### Requirement 25: HTTP(S) Downloads

**User Story:** As a developer, I want CDo to download dependencies and tools over HTTPS, so that the tool is self-sufficient for package resolution.

#### Acceptance Criteria

1. THE CDo_Binary SHALL support HTTPS downloads using platform-native TLS (Schannel on Windows, OpenSSL/LibreSSL on POSIX via system libraries)
2. WHEN a download fails due to network error, THE CDo_Binary SHALL retry up to 3 times with exponential backoff
3. WHEN a download definitively fails after all retries are exhausted, THE CDo_Binary SHALL report the URL, HTTP status code (if available), and suggest checking network connectivity
4. THE Output_Renderer SHALL display download progress including bytes transferred and transfer rate
5. IF platform-native TLS is not available, THEN THE CDo_Binary SHALL fall back to invoking curl or wget and report a warning
