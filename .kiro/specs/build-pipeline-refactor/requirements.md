# Requirements Document

## Introduction

This specification defines the restructuring of the CDo build pipeline as a clean C++ layered architecture with abstract Runner/Task patterns and a condition-based build-or-skip decision. The current C implementation suffers from duplicated freshness-check logic across module types, interleaved caching/threshold/hashing in the compilation path, and inconsistent patterns across builders. This refactor introduces a proper OOP design with abstract `Runner` and `Task` classes, a `TasksDag` for dependency ordering, type-specific task classes (e.g., `BuildCSource`, `BuildSharedLibrary`), an abstract `TaskCondition` system for build decisions, and an `Artifact` abstraction for inputs/outputs. The build pipeline is implemented in C++ under the `cdo::build` namespace, with an `extern "C"` entry point so existing C code can invoke it during migration. This spec supersedes the `cache-performance-improvements` spec entirely.

## Glossary

- **Build_Pipeline**: The complete C++ system under `cdo::build` responsible for transforming source files into output artifacts
- **Runner**: Abstract base class representing an executor capable of running Tasks asynchronously. The main thread dispatches tasks to Runners; Runners do not poll or fetch tasks themselves
- **ThreadRunner**: A Runner implementation that executes a Task on a dedicated worker thread. `run(task)` is asynchronous — it returns immediately and the task executes on the runner's thread
- **RunnerPool**: A pool of Runner instances managed by the main thread. The main thread dispatches tasks to free runners via `waitFreeRunner()`
- **Task**: Abstract base class representing a unit of build work with input Artifacts, an output Artifact, a TaskCondition, and a `run()` method
- **TaskCondition**: Abstract base class that evaluates whether a Task needs to execute. Returns a decision (must-build with reason, or skip)
- **FreshnessCondition**: Concrete TaskCondition that compares input mtime values against the destination mtime
- **BuildCSource**: Concrete Task that compiles a `.c` file into a `.o` object file
- **BuildCppSource**: Concrete Task that compiles a `.cpp` file into a `.o` object file
- **BuildStaticLibrary**: Concrete Task that archives `.o` files into a `.lib`/`.a` static library
- **BuildExecutable**: Concrete Task that links `.o` files and libraries into an `.exe`
- **BuildSharedLibrary**: Concrete Task that links `.o` files into a `.dll`/`.so`
- **CompileHlslShader**: Concrete Task that compiles a `.hlsl` file into a `.dxil` via DXC
- **TasksDag**: A directed acyclic graph of Tasks with dependency edges, providing `waitNextTask()` and `hasActiveTask()` for the main dispatch loop
- **Artifact**: Abstract base class representing a build input or output. Subclassed by access method (not file type): `FileArtifact` for local files (with an optional type/metadata property), `HttpArtifact` for remote resources, etc.
- **FileArtifact**: Concrete Artifact representing a local file. Provides path access, existence check, mtime retrieval. Carries an optional `ArtifactType` property (source, object, library, executable, shader, etc.)
- **RunOptions**: Configuration struct carrying profile, verbosity, force flag, jobs count, and optional SHA-256 cache settings
- **SHA256_Cache_Layer**: Optional wrapper around task execution that checks/stores artifacts in `.cdo/cache/objects/`
- **Build_Profile_Dir**: The directory `build/<profile>/` serving as both build output and incremental cache

## Requirements

### Requirement 1: C++ Implementation with Namespace

**User Story:** As a developer, I want the build pipeline implemented in C++ with proper OOP patterns, so that the architecture is extensible, readable, and maintainable.

#### Acceptance Criteria

1. THE Build_Pipeline SHALL be implemented in C++ (C++17 or later) under the `cdo::build` namespace
2. THE Build_Pipeline SHALL expose an `extern "C"` entry point function that existing C code can call to invoke the build command
3. THE Build_Pipeline C++ source files SHALL coexist with existing C source files in the same crate, compiled by the same build system
4. THE Build_Pipeline SHALL use OOP where it improves clarity: abstract base classes for Runner, Task, TaskCondition, and Artifact; concrete implementations for specific types; namespaces for organization
5. THE Build_Pipeline SHALL NOT introduce external C++ dependencies (no Boost, no external libraries beyond the C++ standard library)

### Requirement 2: Abstract Runner and RunnerPool

**User Story:** As a developer, I want an abstract Runner interface with a pool-based dispatch model, so that task execution is decoupled from threading specifics and can be extended to other execution models later.

#### Acceptance Criteria

1. THE Runner SHALL be an abstract base class with an asynchronous `run(Task&)` method that returns immediately while the task executes on the Runner's execution context
2. THE ThreadRunner SHALL be a concrete Runner implementation that executes a dispatched Task on its dedicated worker thread
3. THE RunnerPool SHALL manage a configurable number of Runner instances (set by `--jobs` flag)
4. THE RunnerPool SHALL provide a `waitFreeRunner()` method that blocks until a Runner has finished its current task and is available for a new one
5. THE main thread SHALL be responsible for dispatching tasks to Runners; Runners SHALL NOT poll or fetch tasks from the DAG themselves
6. THE Runner interface SHALL be designed so that alternative implementations (e.g., a future `RemoteRunner`) can be added without modifying the pool or task logic

### Requirement 3: Abstract Task and Concrete Task Types

**User Story:** As a developer, I want abstract Tasks with concrete implementations per artifact type, so that each build step is self-contained and the DAG operates on a uniform interface.

#### Acceptance Criteria

1. THE Task SHALL be an abstract base class with: `inputs()` returning the list of input Artifacts, `output()` returning the output Artifact, `condition()` returning the TaskCondition, and `execute()` performing the actual build operation
2. THE Task `run()` method SHALL first evaluate the TaskCondition; if the condition says skip, the task completes without calling `execute()`; otherwise it logs the reason and calls `execute()`
3. THE BuildCSource Task SHALL compile a C source file into a `.o` object file using the detected compiler
4. THE BuildCppSource Task SHALL compile a C++ source file into a `.o` object file using the detected C++ compiler
5. THE BuildStaticLibrary Task SHALL archive `.o` files into a `.lib` (Windows) or `.a` (POSIX) static library
6. THE BuildExecutable Task SHALL link `.o` files and libraries into an executable binary
7. THE BuildSharedLibrary Task SHALL link `.o` files into a `.dll` (Windows) or `.so` (POSIX) shared library
8. THE CompileHlslShader Task SHALL compile a `.hlsl` source file into a `.dxil` output via DXC
9. EACH concrete Task SHALL carry all data needed for execution (input Artifacts, configuration flags, output Artifact) so that it can be dispatched to any Runner without external context

### Requirement 4: TasksDag

**User Story:** As a developer, I want a DAG of tasks with dependency edges, so that tasks execute in the correct order and parallelism is maximized.

#### Acceptance Criteria

1. THE TasksDag SHALL represent build tasks as nodes and dependency relationships as directed edges
2. THE TasksDag SHALL provide a `waitNextTask()` method that blocks until a ready task (all dependencies satisfied) is available, then returns it
3. THE TasksDag SHALL provide a `hasActiveTask()` method that returns true while there are tasks still pending or in-flight, and false when all tasks are completed (or one has failed)
4. THE main build loop SHALL follow the pattern: `while (dag.hasActiveTask()) { auto task = dag.waitNextTask(); auto runner = pool.waitFreeRunner(); runner.run(task); }`
5. THE TasksDag SHALL be constructed from the workspace/crate model without performing any freshness checks or build decisions during construction
6. WHEN a Task is marked as completed, THE TasksDag SHALL unblock dependent tasks that have no remaining unsatisfied dependencies
7. WHEN a task fails, THE TasksDag SHALL signal termination so that `hasActiveTask()` returns false and `waitNextTask()` unblocks

### Requirement 5: TaskCondition Abstraction

**User Story:** As a developer, I want an abstract condition system for build decisions, so that the skip-or-build logic is extensible and can support different strategies beyond mtime comparison.

#### Acceptance Criteria

1. THE TaskCondition SHALL be an abstract base class with an `evaluate(inputs, output)` method that returns a decision: either `Skip` (with reason) or `Build` (with reason string)
2. THE FreshnessCondition SHALL be a concrete TaskCondition that implements mtime-based comparison: if output does not exist → Build("does not exist"); if any input mtime > output mtime → Build("outdated"); otherwise → Skip("up-to-date")
3. THE FreshnessCondition SHALL also accept a `forced` flag that overrides the skip decision: when forced is true AND output is up-to-date → Build("forced")
4. EACH Task SHALL carry a TaskCondition instance that is evaluated before execution
5. THE TaskCondition interface SHALL be designed so that alternative implementations (e.g., `AlwaysRunCondition`, `ContentHashCondition`) can be added without modifying Task or Runner code
6. THE FreshnessCondition SHALL use the Artifact's `mtime()` method for timestamp retrieval, not raw PAL calls

### Requirement 6: Artifact Abstraction

**User Story:** As a developer, I want build inputs and outputs represented as typed Artifact objects rather than raw path strings, so that the pipeline is type-safe and extensible to different artifact sources.

#### Acceptance Criteria

1. THE Artifact SHALL be an abstract base class providing methods for: `exists()`, `mtime()`, and `path()` (or equivalent access method)
2. THE FileArtifact SHALL be a concrete Artifact representing a local file on disk, providing existence check and mtime retrieval via the PAL layer
3. THE FileArtifact SHALL carry an optional `ArtifactType` property (e.g., source, object, static_library, executable, shared_library, shader_output) for metadata purposes without affecting class hierarchy
4. ALL Task classes SHALL accept and produce Artifact references (not raw path strings) for their inputs and output
5. THE TaskCondition `evaluate()` method SHALL accept Artifact references
6. THE Artifact class hierarchy SHALL be based on access method (FileArtifact, HttpArtifact, etc.), NOT on file type, so that future artifact sources can be added as new subclasses

### Requirement 7: Build Directory as Cache

**User Story:** As a developer, I want the `build/<profile>/` directory to serve as both output and incremental cache, so that no separate store is needed for day-to-day incremental builds.

#### Acceptance Criteria

1. ALL Tasks SHALL write their output directly to `build/<profile>/<crate_name>/` subdirectories
2. THE FreshnessCondition SHALL compare input mtimes against the output FileArtifact in `build/<profile>/`
3. THE Build_Pipeline SHALL NOT copy files to or from `.cdo/cache/objects/` during normal incremental builds when the SHA256_Cache_Layer is disabled
4. WHEN a clean build is requested (`--clean` flag), THE Build_Pipeline SHALL delete the `build/<profile>/` directory and rebuild all artifacts

### Requirement 8: Optional SHA-256 Cache Integration

**User Story:** As a developer, I want an optional SHA-256 cache layer for cross-branch and cross-machine sharing, without complicating the core build path.

#### Acceptance Criteria

1. WHERE the SHA256_Cache_Layer is enabled, THE layer SHALL check the content-addressable store before invoking the Task's `execute()` method
2. WHERE a cache hit occurs, THE layer SHALL copy the cached artifact to the output path and mark the Task as completed without calling `execute()`
3. WHERE a cache miss occurs AND `execute()` succeeds, THE layer SHALL store the built artifact in `.cdo/cache/objects/` keyed by its SHA-256 hash
4. WHERE the SHA256_Cache_Layer is disabled, THE Build_Pipeline SHALL function correctly using only the TaskCondition-based incremental builds
5. THE SHA256_Cache_Layer SHALL be configurable via `cdo.toml` under `[workspace.settings.cache]`

### Requirement 9: Consistent Logging

**User Story:** As a developer, I want consistent, predictable log output from the build pipeline, so that I can quickly see what was rebuilt and why.

#### Acceptance Criteria

1. WHEN the TaskCondition returns Build, THE Task SHALL emit one INFO-level log line: `"Building: <output> (<reason>)"`
2. WHEN the TaskCondition returns Skip, THE Task SHALL emit one DEBUG-level log line: `"Up-to-date: <output>"`
3. WHEN a build error occurs, THE Task SHALL emit the tool output (compiler/linker/DXC stderr) at ERROR level exactly once
4. THE Build_Pipeline SHALL NOT emit redundant messages from multiple locations for the same operation
5. WHEN all tasks complete, THE Build_Pipeline SHALL emit one INFO-level summary line with total built/skipped/failed counts

### Requirement 10: Dependency-Aware Compilation Inputs

**User Story:** As a developer, I want the build decision for object files to consider header dependencies, so that changes to included headers trigger recompilation.

#### Acceptance Criteria

1. WHEN a `.d` dependency file exists for a source file, THE BuildCSource/BuildCppSource Task SHALL include all header paths listed in the `.d` file as input Artifacts to the FreshnessCondition
2. WHEN no `.d` file exists (first build), THE FreshnessCondition SHALL return Build("does not exist") because the `.o` output does not yet exist
3. THE Build_Pipeline SHALL parse GCC/Clang/MSVC `.d` file format to extract the list of header file paths that the source depends on

### Requirement 11: Elimination of Duplicated Module Build Functions

**User Story:** As a developer, I want the DAG construction to produce the correct task graph for any module kind, so that exe, lib, tst, e2e, and dyn do not each need separate orchestration code.

#### Acceptance Criteria

1. THE TasksDag construction SHALL produce the correct task types and dependency edges for all module kinds (exe, lib, tst, e2e, dyn, shd) from a single generic algorithm
2. Module-specific differences (artifact type, link flags, shared-library mode, implicit dependencies like cdo_ut for e2e) SHALL be expressed as parameters to the Task constructors, not as separate code paths
3. WHEN the refactor is complete, the separate `cmd_build_exe.c`, `cmd_build_lib.c`, `cmd_build_test.c`, `cmd_build_e2e.c`, `cmd_build_dyn.c` files SHALL be removed or reduced to thin wrappers

### Requirement 12: Supersede Cache Performance Improvements Spec

**User Story:** As a developer, I want this refactored architecture to replace the `cache-performance-improvements` spec, so that the codebase does not carry two competing approaches.

#### Acceptance Criteria

1. WHEN this refactor is complete, THE Build_Pipeline SHALL NOT contain the mtime_index, parallel hashing, threshold logic, or link freshness functions from the previous spec
2. THE Build_Pipeline SHALL remove: `cache_fastpath.c`, `cache_hash_parallel.c`, `cache_threshold.c`, `mtime_index.c`, `compiler_link_is_fresh` function
3. THE Build_Pipeline SHALL retain `cache.c` and `cache_key.c` only to support the optional SHA256_Cache_Layer
4. THE `cache-performance-improvements` spec folder SHALL be archived or deleted

### Requirement 13: Build Command Entry Point

**User Story:** As a developer, I want a clean entry point from the existing C CLI layer into the new C++ build pipeline, so that migration is seamless.

#### Acceptance Criteria

1. THE Build_Pipeline SHALL expose an `extern "C" int cdo_build_run(const BuildArgs* args)` function callable from the existing C command handler
2. THE `BuildArgs` struct SHALL be a C-compatible struct carrying: workspace root path, crate filter, profile name, jobs count, force flag, verbosity level, and cache enabled flag
3. THE existing `cmd_build.c` SHALL be modified to call `cdo_build_run()` instead of the current inline build logic
4. THE `extern "C"` entry point SHALL be the ONLY interface between the C and C++ layers for the build command
