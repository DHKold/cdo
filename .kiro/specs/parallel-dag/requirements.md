# Feature 1: Parallel Compilation with Dependency Graph Awareness

## Overview

Allow files from downstream crates to begin compiling as soon as the upstream crate's
`api/` headers are available — before the upstream crate finishes linking. Only the
link step of a downstream crate truly depends on the upstream `.a`/`.dll` being ready.

Currently `build_crate_modules` processes crates sequentially in topological order.
On a workspace with 5+ crates and 8+ CPU cores, the CPU sits idle during link phases.
This feature introduces a fine-grained task DAG that exploits compile/link phase separation
to maximize parallelism across crates.

As a prerequisite, the project model (workspace, crates, modules, dependency graph,
build planning) is extracted from the overloaded `core/` folder into a dedicated
`model/` folder with a clean interface. This separates "what to build" (pure data +
algorithms) from "how to build" (process spawning, caching, I/O).

## Motivation

Ninja and Bazel both exploit fine-grained DAG parallelism. A workspace with 5 crates
and 50+ source files can see 20-40% build time reduction when compilation of downstream
crates overlaps with linking of upstream crates.

The extraction of `model/` is motivated by:
- `core/` currently mixes data model, algorithms, and I/O-heavy execution in one flat namespace (14 headers, 23 source files)
- The DAG feature adds ~800-1000 lines of graph generation and scheduling logic — without separation, `core/` becomes unmaintainable
- A pure `model/` layer is testable in isolation without mocking process spawning or file I/O
- Clear interface boundaries make the dependency graph, build order, and DAG generation auditable

## Requirements

### Requirement 0: Extract Project Model into `model/` Folder

**ID:** REQ-DAG-0
**Description:** Extract workspace, crate, module, dependency graph, and build planning data structures and algorithms from `core/` into a new `model/` folder, establishing a clean layering.
**Acceptance Criteria:**
- A new `api/model/` and `lib/model/` folder exists alongside `api/core/` and `lib/core/`
- The following move to `model/`:
  - `workspace.h` / `workspace_load.c` / `workspace_resolve.c` — workspace parsing, crate discovery, topological sort
  - `module.h` / `module.c` — module kinds, artifact path computation, include path computation
  - `scanner.h` / `scanner.c` — source file discovery
  - `deps.h` / `deps_lock.c` — dependency spec types and lock file format (NOT fetching/network)
  - `hooks.h` (definition types only) — HookDef, HookSet, HookLifecycle, hooks_parse/hooks_parse_table
  - `fmt_settings.h` — FmtSettings struct
- The following stay in `core/` (execution, I/O, side effects):
  - `compiler.h` / `compiler_*.c` — spawns processes
  - `cache.h` / `cache.c` / `cache_key.c` — file I/O for object cache
  - `catalog.h` / `catalog_*.c` — file I/O for catalog loading
  - `deps_resolve.c` — network I/O (HTTP download, git clone)
  - `hooks.c` (`hook_execute` function) — spawns processes
  - `output.h` / `output.c` — stdout/stderr I/O
  - `shader.c` — spawns DXC
  - `template.c` — file generation
  - `errors.h` / `errors.c` — error codes
  - `cli.h` / `cli_*.c` — CLI parsing
- All existing `#include` paths are updated to reflect the new locations
- The build still compiles and all tests pass after the move
- No behavioral changes — purely structural refactor

**Layering principle:** `model/` depends on `pal/` and `commons/` only. `core/` depends on `model/`, `pal/`, and `commons/`. `commands/` depends on all of the above.

### Requirement 1: Task DAG Generation

**ID:** REQ-DAG-1
**Description:** Generate a directed acyclic graph of build tasks (compile and link) from the workspace build order, where each task has explicit dependency edges. The DAG data structures and generation logic live in `model/`.
**Acceptance Criteria:**
- Each source file produces one compile task
- Each module that produces an artifact (lib, exe, dyn, tst) produces one link task
- Compile tasks for a crate depend only on the api/ headers of upstream crates being present (not their link artifacts)
- Link tasks depend on all compile tasks of their own module AND the link tasks of upstream crate libraries
- The DAG correctly handles transitive dependencies
- Hook tasks (pre-build/post-build) are represented as tasks with appropriate ordering constraints
- `dag.h` lives in `api/model/`, `dag.c` (generation) lives in `lib/model/`

### Requirement 2: DAG Scheduler

**ID:** REQ-DAG-2
**Description:** Implement a scheduler that dispatches ready tasks (all dependencies satisfied) to the existing threadpool for execution. The scheduler lives in `core/` (it has side effects: spawning processes, cache I/O).
**Acceptance Criteria:**
- Tasks whose dependencies are all complete are dispatched immediately
- The scheduler respects the configured parallelism level (`--jobs`)
- Tasks are dispatched in a deterministic order when multiple tasks become ready simultaneously (lexicographic by crate name, then module kind)
- The scheduler handles task failure: when a task fails, all tasks that depend on it (transitively) are cancelled
- The scheduler terminates when all tasks are complete or a fatal failure occurs
- The scheduler lives in `lib/core/dag_scheduler.c` (it spawns compilers, interacts with cache)

### Requirement 3: Compile Task Independence from Upstream Link

**ID:** REQ-DAG-3
**Description:** Compile tasks for a downstream crate must depend only on the availability of upstream api/ headers, NOT on the completion of upstream link tasks.
**Acceptance Criteria:**
- A downstream crate's compile jobs start as soon as the upstream crate's api/ headers exist (which is immediate — they're source files)
- The downstream crate's link job waits for the upstream crate's link job to produce `.a`/`.dll`
- This means compile jobs across crates can run in parallel even while upstream crates are still linking
- Correctness: if a downstream compile `#include`s a header from upstream's api/ that is always present in the source tree, this is safe

### Requirement 4: Module Build Order Within a Crate

**ID:** REQ-DAG-4
**Description:** Within a single crate, maintain the correct intra-crate module ordering: lib before exe/dyn/tst; shd before res; api is always available.
**Acceptance Criteria:**
- Compile tasks for exe/, dyn/, tst/ modules depend on the lib/ module's link task completing (they link against it)
- Compile tasks for exe/, dyn/, tst/ have access to the crate's own api/ and lib/ headers
- The shd/ and res/ modules follow their existing ordering constraints
- Pre-build hooks for a crate run before any of that crate's compile tasks
- Post-build hooks for a crate run after all of that crate's tasks complete

### Requirement 5: Progress Reporting

**ID:** REQ-DAG-5
**Description:** The progress bar must handle interleaved compilation across multiple crates correctly.
**Acceptance Criteria:**
- The progress bar shows total compile units across all crates (same as current behavior)
- Progress increments as individual compile tasks complete, regardless of which crate they belong to
- When a compile task fails, the progress bar finalizes at its current state
- Debug-level output indicates which crate each compile task belongs to

### Requirement 6: Error Reporting

**ID:** REQ-DAG-6
**Description:** When a task fails, report which crate and module the failure occurred in, and list any downstream crates that were cancelled as a result.
**Acceptance Criteria:**
- Compilation errors clearly identify the crate and module (e.g., "Compilation failed in crate 'foo', module lib/")
- Link errors clearly identify what was being linked
- After failure, log at info level which pending tasks were cancelled (e.g., "Cancelled 5 tasks in crates: bar, baz")
- The first error is the most prominent; cascading cancellations are secondary

### Requirement 7: Fallback to Sequential Mode

**ID:** REQ-DAG-7
**Description:** When `--jobs 1` is specified, the DAG scheduler degrades gracefully to sequential execution matching the current behavior.
**Acceptance Criteria:**
- With `--jobs 1`, tasks execute one at a time in topological order
- Output and behavior are indistinguishable from the current sequential pipeline
- No performance regression for `--jobs 1` vs current implementation

### Requirement 8: Cache Integration

**ID:** REQ-DAG-8
**Description:** The DAG scheduler integrates with the existing build cache, performing cache lookups and stores identically to the current `compiler_compile_batch`.
**Acceptance Criteria:**
- Cache lookups happen per compile task before spawning the compiler
- Cache hits produce a completed task (dependency satisfied for downstream)
- Cache stores happen after successful compilation
- Cache statistics (hits/misses) are accumulated correctly across all concurrent tasks
- Thread safety: cache operations are safe under concurrent access (already handled by file-system atomicity)

## Non-Requirements (Out of Scope)

- Distributed/remote build execution
- Automatic parallelism tuning based on system load
- DAG visualization or export (possible future `cdo build --dag` flag)
- Changing the module system or adding new module kinds
- Parallelizing within a single compile command (that's the compiler's job)
- Moving `commons/`, `pal/`, or `commands/` — only `core/` is split

## Dependencies

- Existing `threadpool.c` (task dispatch)
- Existing `workspace_resolve.c` (topological ordering — moves to model/)
- Existing `compiler_compile_batch` (compilation logic, to be decomposed)
- Existing `cache.h` (cache lookup/store per job)
- Existing `cmd_build.c` (orchestration, to be refactored)

## Technical Notes

- The key insight: api/ headers are always present in the source tree. They don't need to be "built". So downstream compile tasks can start immediately — their only real dependency is that the compiler can find the include path, which points to the source directory.
- Link tasks genuinely depend on upstream `.a` files existing. The link DAG mirrors the current sequential crate ordering.
- The threadpool already exists and handles arbitrary task dispatch. The new component is a "DAG scheduler" that tracks dependency edges and submits tasks to the pool as they become ready.
- The `model/` extraction is a mechanical refactor (move files, update includes) that can be validated by "build still passes, tests still pass." No logic changes.
- After extraction, the dependency layering is: `pal/` → `commons/` → `model/` → `core/` → `commands/` → `exe/`
- Estimated new code: ~400-600 lines for the DAG scheduler (core/), ~200-300 lines for DAG generation (model/), ~100 lines for refactoring cmd_build.c.
