# Implementation Plan: Build Pipeline Refactor

## Overview

Restructure the CDo build pipeline from C-based procedural code into a C++17 layered architecture under `cdo::build`. The implementation follows TDD: define interfaces first, write unit tests, then implement. Each layer is built bottom-up so higher layers can compose tested lower-layer components.

## Tasks

- [x] 0. Git Setup — Create feature branch
  - Run `git checkout -b feature/build-pipeline-refactor`
  - Ensure working tree is clean before starting

- [x] 1. Artifact Layer — Interfaces and Implementation
  - [x] 1.1 Create Artifact abstract base class and FileArtifact header
    - Create `crates/cdo/api/build/artifact.h`
    - Define `Artifact` abstract class with `exists()`, `mtime()`, `path()` pure virtual methods
    - Define `ArtifactType` enum (Source, Object, StaticLibrary, Executable, SharedLibrary, ShaderOutput, DepFile, Header)
    - Define `FileArtifact` concrete class with constructor accepting path and optional type
    - _Requirements: 6.1, 6.2, 6.3, 6.6_

  - [x] 1.2 Write unit tests for FileArtifact
    - Create `crates/cdo/tst/unit/build/test_artifact.cpp`
    - Test FileArtifact construction with valid path and type metadata
    - Test `exists()` returns true for real temp file, false for non-existent path
    - Test `mtime()` returns non-zero nanoseconds for existing file, 0 for missing file
    - Test `path()` returns the constructed path string
    - Test `ArtifactType` storage and retrieval
    - _Requirements: 6.2, 6.3_

  - [x] 1.3 Implement FileArtifact
    - Create `crates/cdo/lib/build/artifact.cpp`
    - Implement `exists()` using PAL (`pal_path_exists(path) == 0` means exists)
    - Implement `mtime()` using PAL `pal_file_mtime()` returning nanoseconds since epoch
    - Implement `path()` getter and `type()` getter
    - _Requirements: 6.2, 6.3_

- [x] 2. Condition Layer — TaskCondition and FreshnessCondition
  - [x] 2.1 Create TaskCondition abstract class and FreshnessCondition header
    - Create `crates/cdo/api/build/condition.h`
    - Define `ConditionResult` struct with `Decision` enum (Build, Skip) and reason string
    - Define `TaskCondition` abstract base class with `evaluate(inputs, primary_output)` pure virtual
    - Define `FreshnessCondition` concrete class with `forced` flag constructor parameter
    - _Requirements: 5.1, 5.2, 5.3, 5.5_

  - [x] 2.2 Write unit tests for FreshnessCondition
    - Create `crates/cdo/tst/unit/build/test_condition.cpp`
    - Test: output does not exist → Build("does not exist")
    - Test: forced=true and output up-to-date → Build("forced")
    - Test: any input mtime > output mtime → Build("outdated")
    - Test: all inputs older or equal → Skip("up-to-date")
    - Test: empty inputs list with existing output → Skip("up-to-date")
    - Test: identical mtimes (input == output) → Skip("up-to-date")
    - Use real temp files to create known mtime scenarios
    - _Requirements: 5.2, 5.3, 5.6_

  - [x] 2.3 Implement FreshnessCondition
    - Create `crates/cdo/lib/build/condition.cpp`
    - Implement evaluate() logic: check output exists, then forced override, then mtime comparison
    - Use `Artifact::mtime()` method (not raw PAL calls) per requirement 5.6
    - _Requirements: 5.2, 5.3, 5.6_

- [x] 3. Checkpoint — Ensure artifact and condition layers pass all tests
  - Ensure all tests pass, ask the user if questions arise.
  - Commit: `git add crates/cdo/api/build/artifact.h crates/cdo/api/build/condition.h crates/cdo/lib/build/artifact.cpp crates/cdo/lib/build/condition.cpp crates/cdo/tst/unit/build/test_artifact.cpp crates/cdo/tst/unit/build/test_condition.cpp`
  - `git commit -m "feat(build): add Artifact and TaskCondition layers with unit tests"`

- [x] 4. Task Layer — Abstract Task and Concrete Tasks
  - [x] 4.1 Create Task abstract base class header
    - Create `crates/cdo/api/build/task.h`
    - Define `Task` abstract class with: `inputs()`, `outputs()`, `primaryOutput()`, `condition()`, `run()`, `execute()`, `wasSkipped()`, `id()`/`setId()`
    - `run()` is non-virtual (template method): evaluates condition, logs, calls execute() if needed
    - `execute()` is protected pure virtual
    - _Requirements: 3.1, 3.2, 3.9_

  - [x] 4.2 Write unit tests for Task::run() base logic
    - Create `crates/cdo/tst/unit/build/test_task.cpp`
    - Create a minimal concrete `MockTask` that exposes whether execute() was called
    - Test: condition returns Skip → execute() not called, wasSkipped()=true, run() returns 0
    - Test: condition returns Build → execute() called exactly once
    - Test: execute() returns non-zero → run() returns that non-zero code
    - Test: INFO log emitted on Build, DEBUG log emitted on Skip
    - _Requirements: 3.2, 9.1, 9.2_

  - [x] 4.3 Implement Task::run() base logic
    - Create `crates/cdo/lib/build/task.cpp`
    - Implement `run()`: call `condition().evaluate(inputs(), primaryOutput())`
    - On Build: log INFO `"Building: <primaryOutput().path()> (<reason>)"`, call execute()
    - On Skip: log DEBUG `"Up-to-date: <primaryOutput().path()>"`, set skipped_=true, return 0
    - _Requirements: 3.2, 9.1, 9.2_

  - [x] 4.4 Create concrete task class headers (BuildCSource, BuildCppSource, BuildStaticLibrary, BuildExecutable, BuildSharedLibrary, CompileHlslShader)
    - Add all concrete task class definitions to `crates/cdo/api/build/task.h` (or split into `crates/cdo/api/build/tasks/` subfolder if header exceeds 500 lines)
    - Each task has a `Config` struct with all required build parameters
    - Each task owns its FileArtifact instances and FreshnessCondition
    - _Requirements: 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9_

  - [x] 4.5 Write unit tests for concrete tasks (construction and config)
    - Extend `crates/cdo/tst/unit/build/test_task.cpp` or create per-task test files under `crates/cdo/tst/unit/build/`
    - Test BuildCSource construction: inputs include source + headers, outputs include .o and .d, primaryOutput is .o
    - Test BuildCppSource construction: same pattern as C source
    - Test BuildStaticLibrary: inputs are object files, single output
    - Test BuildExecutable: inputs are objects + libs, single output
    - Test BuildSharedLibrary: same pattern as executable
    - Test CompileHlslShader: source input, .dxil output
    - _Requirements: 3.3, 3.4, 3.5, 3.6, 3.7, 3.8_

  - [x] 4.6 Implement concrete task execute() methods
    - Create `crates/cdo/lib/build/tasks/build_c_source.cpp`
    - Create `crates/cdo/lib/build/tasks/build_cpp_source.cpp`
    - Create `crates/cdo/lib/build/tasks/build_static_library.cpp`
    - Create `crates/cdo/lib/build/tasks/build_executable.cpp`
    - Create `crates/cdo/lib/build/tasks/build_shared_library.cpp`
    - Create `crates/cdo/lib/build/tasks/compile_hlsl_shader.cpp`
    - Each execute() spawns the appropriate tool (compiler/linker/archiver/DXC) via PAL
    - On failure: log stderr at ERROR level exactly once, return non-zero
    - _Requirements: 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 9.3_

- [x] 5. Checkpoint — Ensure task layer tests pass
  - Ensure all tests pass, ask the user if questions arise.
  - Commit: `git add crates/cdo/api/build/task.h crates/cdo/lib/build/task.cpp crates/cdo/lib/build/tasks/ crates/cdo/tst/unit/build/test_task.cpp`
  - `git commit -m "feat(build): add Task base class and concrete task implementations"`

- [x] 6. DepFile Parser
  - [x] 6.1 Create DepFileParser header
    - Create `crates/cdo/api/build/depfile_parser.h`
    - Define `DepFileParser` class with Format enum (GccClang, Msvc, Auto)
    - Define `parse(path)`, `dependencies()`, `target()`, `lastError()` methods
    - _Requirements: 10.1, 10.3_

  - [x] 6.2 Write unit tests for DepFileParser
    - Create `crates/cdo/tst/unit/build/test_depfile_parser.cpp`
    - Test GCC single-line format: `target.o: source.c header.h`
    - Test GCC multi-line with backslash continuation
    - Test paths with escaped spaces (`\ ` → ` `)
    - Test MSVC /showIncludes format parsing
    - Test empty dep file → success with empty dependency list
    - Test malformed/unreadable file → parse returns false, lastError() populated
    - Test paths with mixed line endings (CRLF, LF)
    - Test very long paths (>260 chars)
    - Test round-trip: known deps formatted as .d content → parsed back to original list
    - _Requirements: 10.1, 10.3_

  - [x] 6.3 Implement DepFileParser
    - Create `crates/cdo/lib/build/depfile_parser.cpp`
    - Implement `parseGccClang()`: strip target, handle backslash-newline, unescape spaces/special chars
    - Implement `parseMsvc()`: detect "including file:" prefix, extract paths, deduplicate
    - Implement `normalizePathSeparators()`: backslash to forward slash
    - Implement Auto mode: try GCC first, fall back to MSVC
    - _Requirements: 10.1, 10.3_

- [x] 7. TasksDag — Dependency Graph
  - [x] 7.1 Create TasksDag header
    - Create `crates/cdo/api/build/tasks_dag.h`
    - Define `TasksDag` class with: `addTask()`, `addDependency()`, `finalize()`, `waitNextTask()`, `hasActiveTask()`, `markCompleted()`, `markFailed()`, count accessors
    - Use pimpl pattern (`struct Impl`) for thread-safe internals
    - _Requirements: 4.1, 4.2, 4.3, 4.5, 4.6, 4.7_

  - [x] 7.2 Write unit tests for TasksDag
    - Create `crates/cdo/tst/unit/build/test_tasks_dag.cpp`
    - Test: linear chain A→B→C dispatches in order
    - Test: diamond dependency (A→B, A→C, B→D, C→D) allows B and C in parallel
    - Test: `finalize()` detects cycle and returns non-zero
    - Test: `waitNextTask()` only returns tasks with all deps completed
    - Test: `markCompleted()` unblocks dependent tasks
    - Test: `markFailed()` causes `hasActiveTask()` to return false and `waitNextTask()` to return nullptr
    - Test: `hasActiveTask()` returns true while tasks pending/running, false when all done
    - Test: count accessors match actual state (total, completed, skipped, failed)
    - _Requirements: 4.1, 4.2, 4.3, 4.6, 4.7_

  - [x] 7.3 Implement TasksDag
    - Create `crates/cdo/lib/build/tasks_dag.cpp`
    - Implement Impl with: task vector, adjacency list, reverse edges, per-task `remaining_deps` counter
    - Use `std::mutex` + `std::condition_variable` for thread-safe ready-set
    - `finalize()`: validate acyclicity (topological sort), seed ready set with zero-dep tasks
    - `waitNextTask()`: block on condvar until ready set non-empty or terminated
    - `markCompleted()`/`markFailed()`: update state, decrement dependent counters, signal condvar
    - _Requirements: 4.1, 4.2, 4.3, 4.6, 4.7_

- [x] 8. Runner and RunnerPool
  - [x] 8.1 Create Runner, ThreadRunner, RunnerPool headers
    - Create `crates/cdo/api/build/runner.h`
    - Define `Runner` abstract class with: `run(Task&)`, `isIdle()`, `wait()`, `lastResult()`, `lastTaskId()`
    - Define `ThreadRunner` concrete class with pimpl
    - Define `RunnerPool` class with `waitFreeRunner()` and `size()`
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6_

  - [x] 8.2 Write unit tests for ThreadRunner and RunnerPool
    - Create `crates/cdo/tst/unit/build/test_runner.cpp`
    - Test: ThreadRunner `run()` returns before task completes (async dispatch)
    - Test: ThreadRunner executes task on a different thread than caller
    - Test: `isIdle()` is false during execution, true after `wait()`
    - Test: `lastResult()` reflects task return code after completion
    - Test: RunnerPool(N) creates exactly N runners
    - Test: `waitFreeRunner()` blocks when all runners busy, returns when one finishes
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5_

  - [x] 8.3 Implement ThreadRunner and RunnerPool
    - Create `crates/cdo/lib/build/runner.cpp`
    - ThreadRunner: dedicated `std::thread` per runner, mutex+condvar for dispatch/completion signaling
    - RunnerPool: vector of ThreadRunners, `waitFreeRunner()` polls idle state or uses condvar notification
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5_

- [x] 9. Checkpoint — Ensure DAG and runner layers pass all tests
  - Ensure all tests pass, ask the user if questions arise.
  - Commit: `git add crates/cdo/api/build/depfile_parser.h crates/cdo/api/build/tasks_dag.h crates/cdo/api/build/runner.h crates/cdo/lib/build/depfile_parser.cpp crates/cdo/lib/build/tasks_dag.cpp crates/cdo/lib/build/runner.cpp crates/cdo/tst/unit/build/test_depfile_parser.cpp crates/cdo/tst/unit/build/test_tasks_dag.cpp crates/cdo/tst/unit/build/test_runner.cpp`
  - `git commit -m "feat(build): add DepFileParser, TasksDag, and Runner/RunnerPool"`

- [x] 10. CLI Arguments Conversion
  - [x] 10.1 Create cli::Arguments header
    - Create `crates/cdo/api/build/cli_arguments.h`
    - Define `cdo::build::cli::Arguments` class converting from `CliParseResult*`
    - Expose typed accessors: workspaceRoot, crateFilter, profile, jobs, force, clean, cacheEnabled, verbosity
    - _Requirements: 13.1, 13.2_

  - [x] 10.2 Write unit tests for cli::Arguments
    - Create `crates/cdo/tst/unit/build/test_cli_arguments.cpp`
    - Test: valid CliParseResult converts correctly (profile, jobs, force, etc.)
    - Test: `--release` sets profile to "release"
    - Test: `--no-cache` sets cacheEnabled to false
    - Test: `jobs=0` is accepted (resolved later to cpu count)
    - Test: invalid/missing workspace → isValid()=false, lastError() populated
    - Test: positional values map to crateFilter
    - _Requirements: 13.1, 13.2_

  - [x] 10.3 Implement cli::Arguments
    - Create `crates/cdo/lib/build/cli_arguments.cpp`
    - Extract named args from `result->arg_values[]` by matching long_name
    - Extract positional crate names from `result->positional_values[]`
    - Resolve workspace root via `cdo.toml` walk-up discovery
    - Handle `--release` shorthand for `--profile release`
    - _Requirements: 13.1, 13.2_

- [x] 11. SHA256 Cache Layer
  - [x] 11.1 Create SHA256CacheLayer header
    - Create `crates/cdo/api/build/sha256_cache.h`
    - Define `SHA256CacheLayer` class with Config struct (cache_root, enabled)
    - Define `tryRestore(task)`, `store(task)`, `hits()`, `misses()` methods
    - _Requirements: 8.1, 8.2, 8.3, 8.4_

  - [x] 11.2 Write unit tests for SHA256CacheLayer
    - Create `crates/cdo/tst/unit/build/test_sha256_cache.cpp`
    - Test: tryRestore with cache hit → returns true, file copied to output path
    - Test: tryRestore with cache miss → returns false, misses incremented
    - Test: store after successful build → artifact stored at SHA-256 keyed path
    - Test: disabled config → tryRestore returns false without filesystem access
    - Test: hits/misses counters accurate
    - _Requirements: 8.1, 8.2, 8.3, 8.4_

  - [x] 11.3 Implement SHA256CacheLayer
    - Create `crates/cdo/lib/build/sha256_cache.cpp`
    - Implement `tryRestore()`: compute hash of inputs, check `cache_root/<prefix>/<hash>.o`, copy if exists
    - Implement `store()`: compute hash, copy primary output to cache path
    - Short-circuit all operations when `enabled=false`
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5_

- [x] 12. DAG Builder — Workspace to TasksDag Construction
  - [x] 12.1 Write unit tests for DAG builder
    - Create `crates/cdo/tst/unit/build/test_dag_builder.cpp`
    - Test: single crate with lib module → compile tasks + one archive task with correct edges
    - Test: crate with lib + exe → compile tasks for both, exe link depends on lib archive
    - Test: multi-crate with inter-crate dependency → dependent crate's link depends on dependency's lib
    - Test: all module kinds (exe, lib, tst, e2e, dyn, shd) produce correct task types
    - Test: shader module produces CompileHlslShader tasks with no link task
    - Test: no filesystem operations during construction (mock workspace model)
    - _Requirements: 4.5, 11.1, 11.2_

  - [x] 12.2 Implement DAG builder
    - Create `crates/cdo/lib/build/dag_builder.cpp`
    - Single generic algorithm iterates crates in dependency order, modules within each crate
    - Module kind determines task type (BuildCSource/BuildCppSource for compilation, appropriate link task)
    - Module-specific differences (artifact type, link flags, implicit deps) are parameters, not code paths
    - Integrate DepFileParser: for each source, if .d exists, parse and add header deps to task inputs
    - _Requirements: 4.5, 10.1, 10.2, 11.1, 11.2, 11.3_

- [x] 13. BuildPipeline Orchestrator and Entry Point
  - [x] 13.1 Create BuildPipeline header and extern "C" entry point
    - Create `crates/cdo/api/build/build_pipeline.h`
    - Declare `extern "C" int cdo_build_run(const CliParseResult* result)`
    - Define internal `BuildPipeline` class with `run()` method
    - _Requirements: 1.2, 13.1, 13.4_

  - [x] 13.2 Write unit tests for BuildPipeline orchestration logic
    - Create `crates/cdo/tst/unit/build/test_build_pipeline.cpp`
    - Test: `--clean` flag triggers build dir deletion before DAG construction
    - Test: pipeline dispatch loop pattern (dag.hasActiveTask → waitNextTask → waitFreeRunner → run)
    - Test: summary log line emitted with correct built/skipped/failed counts
    - Test: pipeline returns non-zero on task failure
    - Test: cache layer integrated when enabled, skipped when disabled
    - _Requirements: 7.4, 8.4, 9.5, 13.1_

  - [x] 13.3 Implement BuildPipeline orchestrator
    - Create `crates/cdo/lib/build/build_pipeline.cpp`
    - Implement `cdo_build_run()`: construct cli::Arguments, validate, create BuildPipeline, call run()
    - Implement `BuildPipeline::run()`: loadWorkspace → handle --clean → buildDag → create RunnerPool → dispatch loop → printSummary
    - Dispatch loop: `while (dag.hasActiveTask()) { task = dag.waitNextTask(); runner = pool.waitFreeRunner(); runner.run(*task); }` with result checking
    - After each runner completes: check lastResult(), call markCompleted or markFailed on DAG
    - SHA256CacheLayer: tryRestore before dispatch, store after successful execute
    - Summary: emit INFO log with `"Build complete: N built, M skipped, K failed"`
    - _Requirements: 1.2, 4.4, 7.1, 7.2, 7.3, 7.4, 8.1, 8.4, 9.4, 9.5, 13.1, 13.4_

- [x] 14. Checkpoint — Ensure orchestrator and all unit tests pass
  - Ensure all tests pass, ask the user if questions arise.
  - Commit: `git add crates/cdo/api/build/cli_arguments.h crates/cdo/api/build/sha256_cache.h crates/cdo/api/build/build_pipeline.h crates/cdo/lib/build/cli_arguments.cpp crates/cdo/lib/build/sha256_cache.cpp crates/cdo/lib/build/dag_builder.cpp crates/cdo/lib/build/build_pipeline.cpp crates/cdo/tst/unit/build/test_cli_arguments.cpp crates/cdo/tst/unit/build/test_sha256_cache.cpp crates/cdo/tst/unit/build/test_dag_builder.cpp crates/cdo/tst/unit/build/test_build_pipeline.cpp`
  - `git commit -m "feat(build): add CLI args, SHA256 cache, DAG builder, and BuildPipeline orchestrator"`

- [x] 15. Integration with Existing C Layer
  - [x] 15.1 Modify cmd_build.c to call cdo_build_run()
    - Update existing `cmd_build.c` to call `cdo_build_run(result)` instead of current inline build logic
    - Include `build/build_pipeline.h` header
    - Ensure the extern "C" linkage is correct for mixed C/C++ compilation
    - _Requirements: 13.3, 13.4_

  - [x] 15.2 Remove superseded cache-performance-improvements code
    - Remove `cache_fastpath.c`, `cache_hash_parallel.c`, `cache_threshold.c`, `mtime_index.c`
    - Remove `compiler_link_is_fresh` function
    - Remove module-specific build files (`cmd_build_exe.c`, `cmd_build_lib.c`, `cmd_build_test.c`, `cmd_build_e2e.c`, `cmd_build_dyn.c`) or reduce to thin wrappers
    - Retain `cache.c` and `cache_key.c` for SHA256CacheLayer support
    - _Requirements: 12.1, 12.2, 12.3_

  - [x] 15.3 Update build system to compile C++ sources alongside C
    - Update `crate.toml` or build configuration to include `crates/cdo/lib/build/*.cpp` files
    - Ensure C++17 standard flag is passed for the build/ subdirectory sources
    - Verify mixed C/C++ linkage works correctly
    - _Requirements: 1.1, 1.3, 1.5_

- [x] 16. E2E Tests for Build Command
  - [x] 16.1 Create E2E test fixtures for build pipeline
    - Create `crates/cdo/e2e/fixtures/build_pipeline/` with minimal workspace layouts
    - Fixture: single crate with one .c file (lib module)
    - Fixture: multi-crate workspace with inter-crate dependency
    - Fixture: crate with all module kinds (lib, exe, tst, dyn, shd)
    - _Requirements: 1.1, 11.1_

  - [x] 16.2 Write E2E tests for build command
    - Create `crates/cdo/e2e/test_build_pipeline.c`
    - Test: `cdo build` succeeds on single-crate workspace, produces expected output files
    - Test: `cdo build --release` produces artifacts in `build/release/`
    - Test: `cdo build crate_name` builds only the specified crate
    - Test: incremental build skips up-to-date targets (second build produces no "Building:" lines)
    - Test: `cdo build --force` rebuilds all targets
    - Test: `cdo build --clean` deletes and recreates build directory
    - Test: build failure (bad source file) produces error output and non-zero exit
    - Test: `cdo build --jobs 4` completes without errors
    - _Requirements: 7.4, 9.1, 9.2, 9.5, 13.1_

- [-] 17. Final Checkpoint — Full validation
  - Ensure all tests pass, ask the user if questions arise.
  - Run `./cdo.exe build --release` for the entire workspace
  - Run `./cdo.exe test --coverage` and verify >90% coverage on `lib/build/` files
  - Run `./cdo.exe e2e` for the entire workspace
  - Run `./cdo.exe fmt` to verify formatting
  - Commit integration and E2E: `git add -A`
  - `git commit -m "feat(build): integrate C++ pipeline with C layer, remove legacy code, add E2E tests"`
  - Push branch: `git push -u origin feature/build-pipeline-refactor`
  - Final code quality review before merge (user responsibility)

## Notes

- No PBT — extensive unit tests target >90% line coverage on all `lib/build/` sources
- TDD approach: interfaces → unit tests → implementation for each layer
- Files kept under 500 lines; concrete tasks split into `lib/build/tasks/` subfolder
- Test files go in `crates/cdo/tst/unit/build/` mirroring source structure
- Uses existing `cdo_ut` test framework
- No external C++ dependencies — only the C++ standard library and existing PAL layer
- All error handling via return codes (no exceptions), matching PAL convention
- The `cache-performance-improvements` spec is fully superseded by this implementation

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["0"] },
    { "id": 1, "tasks": ["1.1", "2.1"] },
    { "id": 2, "tasks": ["1.2", "2.2"] },
    { "id": 3, "tasks": ["1.3", "2.3", "4.1"] },
    { "id": 4, "tasks": ["4.2", "4.4", "6.1", "7.1", "8.1", "10.1"] },
    { "id": 5, "tasks": ["4.3", "4.5", "6.2", "7.2", "8.2", "10.2", "11.1"] },
    { "id": 6, "tasks": ["4.6", "6.3", "7.3", "8.3", "10.3", "11.2"] },
    { "id": 7, "tasks": ["11.3", "12.1"] },
    { "id": 8, "tasks": ["12.2", "13.1"] },
    { "id": 9, "tasks": ["13.2"] },
    { "id": 10, "tasks": ["13.3"] },
    { "id": 11, "tasks": ["15.1", "15.3"] },
    { "id": 12, "tasks": ["15.2", "16.1"] },
    { "id": 13, "tasks": ["16.2"] }
  ]
}
```
