# Implementation Plan: CDo Native Build System

## Overview

This plan implements CDo as a self-contained C17 binary that replaces CMake and Ninja for C/C++ project management. The implementation proceeds bottom-up: platform abstraction layer first, then core subsystems (parsers, thread pool, output), followed by the build pipeline (scanner, compiler driver, workspace resolver, dependency resolver), then high-level command handlers, and finally integration wiring. Each task builds incrementally on prior work.

**Bootstrap Tooling:** The existing PowerShell-based CDo ("old CDo") is the bootstrap build tool for this project. Use `cdo build cdo` to compile the new CDo binary, `cdo run` to execute it, and `cdo test` to run tests. The old CDo is rough and may need fixes during development — treat it as a living tool that evolves alongside the new implementation.

**Project Structure:** The directory structure and workspace already exist. Tasks begin with the existing codebase in place — no scaffolding or project creation is needed.

## Tasks

- [x] 1. Set up testing infrastructure
  - [x] 1.1 Integrate testing framework with existing project structure
    - Download and vendor the `theft` single-header property-based testing library into `tests/vendor/`
    - Create a minimal test runner (`tests/test_main.c`) that discovers and runs test functions
    - Add test build targets to the existing crate manifest (or create a test crate if not present)
    - Verify the test framework compiles with `cdo build` and a trivial test passes with `cdo test`
    - Note: Work within the existing directory structure — do not recreate directories that already exist
    - _Requirements: 17.3_

- [x] 2. Implement Platform Abstraction Layer (PAL)
  - [x] 2.1 Implement filesystem operations (`src/pal/pal_fs.c`)
    - Implement `pal_file_mtime`, `pal_dir_walk`, `pal_mkdir_p`, `pal_rmdir_r`, `pal_path_exists`, `pal_file_read`, `pal_file_write`
    - Use `#ifdef _WIN32` for Windows APIs (FindFirstFile, GetFileTime) and POSIX APIs (opendir, stat) for Linux/macOS
    - _Requirements: 15.3, 15.5_

  - [x] 2.2 Implement path utilities (`src/pal/pal_path.c`)
    - Implement `pal_path_normalize` (convert `\` to `/` on Windows, collapse `//`), `pal_path_join`, `pal_path_ext`
    - Accept both `/` and `\` on Windows transparently
    - _Requirements: 15.5_

  - [x] 2.3 Write property test for path normalization idempotence
    - **Property 15: Path Normalization Idempotence**
    - **Validates: Requirements 15.5**

  - [x] 2.4 Implement process spawning (`src/pal/pal_process.c`)
    - Implement `pal_spawn` (synchronous), `pal_spawn_async`, `pal_wait`
    - Windows: use `CreateProcess` with pipe redirection for stdout/stderr capture
    - POSIX: use `posix_spawn` or `fork+exec` with pipe redirection
    - Forward environment variables including PATH modifications
    - _Requirements: 18.1, 18.2, 18.3, 18.4, 18.5, 15.2_

  - [x] 2.5 Implement system info (`src/pal/pal_sysinfo.c`)
    - Implement `pal_cpu_count` (Windows: `GetSystemInfo`, POSIX: `sysconf(_SC_NPROCESSORS_ONLN)`)
    - Implement `pal_get_home_dir`, `pal_is_tty`
    - _Requirements: 15.4, 6.2_

- [x] 3. Implement Output Renderer
  - [x] 3.1 Implement output system (`src/core/output.c`)
    - Implement `output_init` with TTY detection, color mode, and log level configuration
    - Implement `output_log` with ANSI color codes: red for ERROR, yellow for WARN, green for success, default for INFO
    - Implement `cdo_error`, `cdo_warn`, `cdo_info`, `cdo_debug`, `cdo_trace` macros
    - When stdout is not a TTY, default to no colors and no progress animations
    - _Requirements: 14.1, 14.3, 14.4, 14.5, 14.6, 14.7_

  - [x] 3.2 Implement progress bar (`src/core/output.c`)
    - Implement `progress_create`, `progress_update`, `progress_finish`
    - Display completed/total count and a visual bar during compilation and downloads
    - Suppress progress animations when not a TTY
    - _Requirements: 14.2, 6.4_

  - [x] 3.3 Write property test for quiet mode filtering
    - **Property 16: Quiet Mode Filters Non-Errors**
    - **Validates: Requirements 14.3**

- [x] 4. Implement TOML Parser
  - [x] 4.1 Implement TOML parser core (`src/core/toml.c`)
    - Write a recursive-descent parser for TOML v1.0
    - Support all TOML types: string, integer, float, bool, datetime, array, table, inline table
    - Implement `toml_parse` with error reporting (line, col, message)
    - Implement `toml_get` for dotted key path lookup
    - Implement `toml_free` for memory cleanup
    - Maintain UTF-8 handling capability
    - _Requirements: 3.1, 3.3, 3.4_

  - [x] 4.2 Implement TOML serializer (`src/core/toml.c`)
    - Implement `toml_serialize` to convert in-memory DOM back to TOML text
    - Ensure round-trip fidelity for all value types
    - _Requirements: 3.5_

  - [x] 4.3 Write property test for TOML round-trip
    - **Property 1: TOML Round-Trip**
    - **Validates: Requirements 3.5, 3.4**

  - [x] 4.4 Write property test for TOML error location accuracy
    - **Property 2: TOML Error Location Accuracy**
    - **Validates: Requirements 3.3**

- [x] 5. Implement JSON Parser
  - [x] 5.1 Implement JSON parser (`src/core/json.c`)
    - Write a minimal JSON parser supporting null, bool, number, string, array, object
    - Implement `json_parse`, `json_get`, `json_free`
    - Report parse errors with line/col
    - _Requirements: 17.3_

- [x] 6. Checkpoint - Core parsers and PAL
  - Ensure all tests pass with `cdo test`, ask the user if questions arise.
  - Note: If the old CDo has issues building or testing at this point, fix them before proceeding.

- [x] 7. Implement CLI Parser
  - [x] 7.1 Implement CLI argument parsing (`src/core/cli.c`)
    - Implement `cdo_cli_parse` to parse argv into `CdoOptions` struct
    - Recognize all commands: build, run, test, clean, new, init, add, remove, source, shader, tool, doctor, self
    - Parse global options: --verbose, --quiet, --log-level, --color, --help, --release, --profile, --jobs
    - Handle `--` separator for forwarding arguments
    - No heap allocation on the common path
    - _Requirements: 1.1, 1.3, 1.4, 1.5_

  - [x] 7.2 Implement command suggestion for typos (`src/core/cli.c`)
    - Implement `cdo_cli_suggest` using Levenshtein edit distance
    - When an unrecognized command is provided, suggest similar commands
    - _Requirements: 1.2_

  - [x] 7.3 Implement help text generation (`src/core/cli.c`)
    - Implement `cdo_cli_print_help` for each command
    - Print summary of available commands when invoked with no command
    - Exit with status code 0 for --help, non-zero for error-triggered usage display
    - _Requirements: 1.4, 1.5_

  - [x] 7.4 Write property test for CLI suggestion relevance
    - **Property 8: CLI Suggestion Relevance**
    - **Validates: Requirements 1.2**

  - [x] 7.5 Write property test for global options parsing
    - **Property 9: Global Options Parsing**
    - **Validates: Requirements 1.3**

- [x] 8. Implement Source Scanner
  - [x] 8.1 Implement source file discovery (`src/core/scanner.c`)
    - Implement `scanner_scan_sources` to recursively find `.c`, `.cpp`, `.h`, `.hpp` in `src/`
    - Implement `scanner_scan_headers` to find headers in `include/`
    - Implement exclude pattern matching (glob patterns from crate manifest)
    - Implement `filelist_free`
    - _Requirements: 7.1, 7.2, 7.3, 7.4_

  - [x] 8.2 Write property test for source scanner completeness
    - **Property 10: Source Scanner Completeness**
    - **Validates: Requirements 7.1**

  - [x] 8.3 Write property test for exclude pattern filtering
    - **Property 11: Exclude Pattern Filtering**
    - **Validates: Requirements 7.2**

- [x] 9. Implement Thread Pool
  - [x] 9.1 Implement work-stealing thread pool (`src/core/threadpool.c`)
    - Implement `threadpool_create` (use `pal_cpu_count` for default)
    - Implement `threadpool_submit` to enqueue tasks
    - Implement `threadpool_wait` to block until all tasks complete
    - Implement `threadpool_destroy`
    - Use platform threads (Windows: `CreateThread`, POSIX: `pthread_create`)
    - _Requirements: 6.1, 6.2, 6.3_

  - [x] 9.2 Write property test for thread pool task completion
    - **Property 12: Thread Pool Task Completion**
    - **Validates: Requirements 6.1**

- [x] 10. Implement Workspace Resolver
  - [x] 10.1 Implement workspace loading (`src/core/workspace.c`)
    - Implement `workspace_load` to parse `cdo.toml` and discover crates
    - Parse each crate's `crate.toml` to populate `Crate` structs
    - Support crate types: executable, static-library, shared-library, test
    - Support config file fallback order: .toml, .yaml, .json
    - _Requirements: 2.1, 2.2, 2.4, 3.2_

  - [x] 10.2 Implement dependency graph and topological sort (`src/core/workspace.c`)
    - Implement `workspace_resolve` with topological sort (Kahn's algorithm)
    - Detect circular dependencies and report the cycle path
    - Compute transitive dependency closure for partial builds
    - _Requirements: 2.3, 2.5, 4.1, 4.2_

  - [x] 10.3 Write property test for topological sort ordering
    - **Property 3: Topological Sort Ordering**
    - **Validates: Requirements 2.3**

  - [x] 10.4 Write property test for circular dependency detection
    - **Property 4: Circular Dependency Detection**
    - **Validates: Requirements 2.5**

  - [x] 10.5 Write property test for transitive dependency closure
    - **Property 5: Transitive Dependency Closure**
    - **Validates: Requirements 4.2**

- [x] 11. Checkpoint - Core subsystems complete
  - Ensure all tests pass with `cdo test`, ask the user if questions arise.
  - Verify the new CDo binary still builds correctly with `cdo build cdo`.

- [x] 12. Implement Compiler Driver
  - [x] 12.1 Implement compiler detection (`src/core/compiler.c`)
    - Implement `compiler_detect` to find GCC, Clang, or MSVC on system PATH
    - Determine compiler family, path, version, and linker path
    - _Requirements: 5.2_

  - [x] 12.2 Implement incremental compilation (dirty set) (`src/core/compiler.c`)
    - Implement `compiler_compute_dirty` using source/header modification timestamps
    - Parse compiler-generated dependency files (-MMD for GCC/Clang, /showIncludes for MSVC)
    - Fall back to full rebuild if timestamp tracking fails or is corrupted
    - _Requirements: 5.3, 5.4_

  - [x] 12.3 Write property test for timestamp-based dirty set correctness
    - **Property 6: Timestamp-Based Dirty Set Correctness**
    - **Validates: Requirements 5.3, 9.1, 9.2, 9.3, 9.4**

  - [x] 12.4 Implement compile command generation and batch execution (`src/core/compiler.c`)
    - Implement `compiler_compile_batch` to generate compiler commands with correct include paths, defines, flags, C/C++ standard
    - Dispatch compilation jobs to the thread pool for parallel execution
    - Support configurable C standard (C11, C17, C23) and C++ standard (C++17, C++20, C++23)
    - _Requirements: 5.1, 5.6, 6.1_

  - [x] 12.5 Write property test for compiler command completeness
    - **Property 7: Compiler Command Completeness**
    - **Validates: Requirements 5.1, 22.1, 22.2, 22.3, 22.4**

  - [x] 12.6 Implement linking (`src/core/compiler.c`)
    - Implement `compiler_link` for executables and libraries (static and shared)
    - Generate correct linker invocation per compiler family
    - _Requirements: 5.5_

- [x] 13. Implement HTTP Client
  - [x] 13.1 Implement HTTPS download with retry (`src/core/http.c`)
    - Implement `http_download` with platform-native TLS (Schannel on Windows, system OpenSSL on POSIX)
    - Implement exponential backoff retry (up to 3 retries)
    - Report URL and HTTP status on failure
    - Implement progress callback for download progress display
    - Fall back to invoking curl/wget if native TLS unavailable
    - _Requirements: 25.1, 25.2, 25.3, 25.4, 25.5_

  - [x] 13.2 Implement HTTP GET for metadata (`src/core/http.c`)
    - Implement `http_get` to fetch small payloads (registry metadata) into memory
    - Implement `http_response_free`
    - _Requirements: 25.1_

  - [x] 13.3 Write property test for retry logic correctness
    - **Property 17: Retry Logic Correctness**
    - **Validates: Requirements 25.2**

- [x] 14. Implement Archive Extractor
  - [x] 14.1 Implement ZIP extraction (`src/core/archive.c`)
    - Implement `archive_extract_zip` without external tools
    - Parse ZIP local file headers and central directory
    - Support DEFLATE decompression
    - Preserve directory structure
    - Report corrupted archives with file path
    - _Requirements: 19.1, 19.3, 19.5_

  - [x] 14.2 Implement tar.gz extraction (`src/core/archive.c`)
    - Implement `archive_extract_targz` without external tools
    - Decompress gzip layer, then parse tar headers
    - Preserve directory structure and POSIX file permissions
    - On Windows, ignore permissions
    - _Requirements: 19.2, 19.3, 19.4, 19.5_

  - [x] 14.3 Write property test for archive structure preservation
    - **Property 18: Archive Structure Preservation**
    - **Validates: Requirements 19.1, 19.2, 19.3**

- [x] 15. Implement Dependency Resolver
  - [x] 15.1 Implement dependency resolution and caching (`src/core/deps.c`)
    - Implement `dep_resolve` to check local cache first, download if needed
    - Support registry, Git, and local filesystem dependency sources
    - Maintain machine-wide cache in `~/.cdo/cache/`
    - Wire resolved dependencies into build (include paths, lib paths, link flags)
    - Copy runtime DLLs to output directory
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.7, 12.1, 12.2, 12.3_

  - [x] 15.2 Implement lock file management (`src/core/deps.c`)
    - Implement `dep_lock_write` to record exact dependency versions
    - Implement `dep_lock_read` to restore pinned versions
    - Support pkg-config, CMake package config, and CDo-native metadata
    - _Requirements: 11.5, 12.4_

  - [x] 15.3 Write property test for lock file round-trip
    - **Property 14: Lock File Round-Trip**
    - **Validates: Requirements 11.5**

- [x] 16. Implement Template Engine
  - [x] 16.1 Implement template rendering (`src/core/template.c`)
    - Implement `template_render` with variable substitution (project name, crate name, author, year, custom vars)
    - Implement conditional section processing (include/exclude sections based on variable truth)
    - Remove all placeholder markers from output
    - _Requirements: 13.2, 13.3_

  - [x] 16.2 Write property test for template rendering correctness
    - **Property 13: Template Rendering Correctness**
    - **Validates: Requirements 13.2, 13.3**

- [x] 17. Implement Shader Compiler
  - [x] 17.1 Implement incremental shader compilation (`src/core/shader.c`)
    - Compare source file mtime against compiled output mtime
    - Skip compilation when source is older than or equal to output
    - Recompile when source is newer or output doesn't exist
    - Invoke DXC from tool store for HLSL → DXIL/SPIR-V
    - Report compiled/skipped counts
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5_

- [x] 18. Checkpoint - All core subsystems complete
  - Ensure all tests pass with `cdo test`, ask the user if questions arise.
  - Rebuild with `cdo build cdo` and verify the binary is functional.

- [x] 19. Implement Command Handlers
  - [x] 19.1 Implement build command (`src/commands/cmd_build.c`)
    - Build all crates when no argument provided (dependency order)
    - Build specific crates + transitive deps when names provided
    - Report error and exit non-zero for unknown crate names
    - Support --release, --profile, --jobs flags
    - Display progress (completed/total compilation units)
    - _Requirements: 4.1, 4.2, 4.3, 22.1, 22.2, 22.3, 22.4_

  - [x] 19.2 Implement run command (`src/commands/cmd_run.c`)
    - Build specified crate before executing
    - Forward arguments after `--` to the executable
    - Abort if build fails
    - Auto-select single executable crate; error if multiple
    - _Requirements: 23.1, 23.2, 23.3, 23.4, 23.5_

  - [x] 19.3 Implement test command (`src/commands/cmd_test.c`)
    - Build and run all test crates when no argument
    - Build and run specific test crate when name provided
    - Report build errors without executing; report non-zero exit as failure
    - Print summary of passed/failed test counts
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5_

  - [x] 19.4 Implement clean command (`src/commands/cmd_clean.c`)
    - Delete entire build directory when no argument
    - Delete specific crate's artifacts when name provided
    - Print "nothing to clean" if directory doesn't exist; exit 0
    - Exit non-zero if clean operation fails for other reasons
    - _Requirements: 10.1, 10.2, 10.3_

  - [x] 19.5 Implement new/init commands (`src/commands/cmd_new.c`)
    - Fetch template from skeleton catalog (local `~/.cdo/templates/` or remote)
    - Instantiate template with variable substitution
    - Support --list to display available templates
    - Refuse to create in non-empty directory unless --force
    - _Requirements: 13.1, 13.2, 13.3, 13.4, 13.5, 13.6_

  - [x] 19.6 Implement add/remove commands (`src/commands/cmd_deps.c`)
    - Search registries for package on add
    - Download to local cache; add to crate manifest
    - Remove from manifest and regenerate lock file on remove
    - _Requirements: 11.2, 11.3, 11.4, 11.5, 11.6_

  - [x] 19.7 Implement tool command (`src/commands/cmd_tool.c`)
    - Download tool archive to local cache
    - Skip download if cached (unless --refresh)
    - Extract into `.cdo/tools/`
    - Write `cdo-tool.toml` manifest
    - Support HTTPS download with fallback
    - _Requirements: 20.1, 20.2, 20.3, 20.4, 20.5_

  - [x] 19.8 Implement doctor command (`src/commands/cmd_doctor.c`)
    - Check for C/C++ compiler on PATH
    - Verify dependencies resolved and present
    - Verify manifest files syntactically valid
    - Auto-fix with --fix flag (install missing tools, regenerate lock file)
    - Print color-coded pass/warn/fail per check
    - Exit 0 if all pass, non-zero if any fail
    - _Requirements: 21.1, 21.2, 21.3, 21.4, 21.5, 21.6_

  - [x] 19.9 Implement shader command (`src/commands/cmd_shader.c`)
    - Wire shader compiler into the command handler
    - Verify DXC tool is installed (suggest `cdo tool install dxc` if missing)
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5, 20.6_

- [x] 20. Implement Error Reporting Enhancements
  - [x] 20.1 Implement compiler error hints (`src/core/errors.c`)
    - Scan compiler error output for common patterns (missing header, undefined symbol, linker error)
    - Append CDo-specific hint suggesting resolution (e.g., `cdo add <package>`)
    - Preserve original compiler/linker error text unmodified
    - Display clear message for internal CDo errors with bug report suggestion
    - _Requirements: 24.1, 24.2, 24.3, 24.4, 24.5_

- [x] 21. Checkpoint - All commands implemented
  - Ensure all tests pass with `cdo test`, ask the user if questions arise.
  - Do a full rebuild with `cdo build cdo` and smoke-test key commands.

- [x] 22. Wire main entry point and integration
  - [x] 22.1 Wire main.c command dispatch (`src/main.c`)
    - Parse CLI args via `cdo_cli_parse`
    - Initialize output renderer with parsed color/log-level/TTY settings
    - Dispatch to appropriate command handler based on parsed command
    - Defer configuration file loading until command handler requests it
    - Ensure startup completes and begins arg parsing within 5ms
    - _Requirements: 16.1, 16.2, 16.3, 1.1_

  - [x] 22.2 Implement build profile wiring
    - Load workspace profiles from manifest (debug/release/custom)
    - Pass profile settings (optimize, debug, defines, flags) through to compiler driver
    - Default to debug profile when no flags specified
    - _Requirements: 22.1, 22.2, 22.3, 22.4_

  - [x] 22.3 Write unit tests for command integration
    - Test CLI → command handler dispatch for each command
    - Test build profile selection logic
    - Test error propagation from subsystems to exit codes
    - _Requirements: 1.1, 18.3_

- [x] 23. Final checkpoint - Full integration verified
  - Ensure all tests pass with `cdo test`, ask the user if questions arise.
  - Perform a clean build with `cdo build cdo` and verify the new CDo binary can build itself (self-hosting milestone).

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from the design document
- Unit tests validate specific examples and edge cases
- The implementation language is C17 as specified in the design
- All embedded functionality (TOML, JSON, ZIP, tar.gz, HTTP/TLS, templates) is implemented without external library dependencies
- Platform-specific code is isolated behind the PAL interface using compile-time `#ifdef` selection
- **Bootstrap:** The old PowerShell-based CDo is the build tool. Use `cdo build cdo` to compile, `cdo test` to test, `cdo run` to execute. The old CDo may be rough and may require fixes as development progresses — this is expected and acceptable.
- **Existing structure:** The project directory already exists. Do not create directories or files that are already present — integrate with what's there.

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["2.1", "2.2", "2.5"] },
    { "id": 2, "tasks": ["2.3", "2.4", "3.1"] },
    { "id": 3, "tasks": ["3.2", "3.3", "4.1", "5.1"] },
    { "id": 4, "tasks": ["4.2", "4.3", "4.4", "7.1"] },
    { "id": 5, "tasks": ["7.2", "7.3", "7.4", "7.5", "8.1", "9.1"] },
    { "id": 6, "tasks": ["8.2", "8.3", "9.2", "10.1"] },
    { "id": 7, "tasks": ["10.2", "13.1"] },
    { "id": 8, "tasks": ["10.3", "10.4", "10.5", "13.2", "13.3", "14.1", "14.2"] },
    { "id": 9, "tasks": ["12.1", "14.3", "15.1", "16.1"] },
    { "id": 10, "tasks": ["12.2", "15.2", "16.2", "17.1"] },
    { "id": 11, "tasks": ["12.3", "12.4", "15.3"] },
    { "id": 12, "tasks": ["12.5", "12.6"] },
    { "id": 13, "tasks": ["19.1", "19.4", "19.5", "19.6", "19.7"] },
    { "id": 14, "tasks": ["19.2", "19.3", "19.8", "19.9", "20.1"] },
    { "id": 15, "tasks": ["22.1", "22.2"] },
    { "id": 16, "tasks": ["22.3"] }
  ]
}
```
