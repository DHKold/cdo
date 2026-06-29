# Feature 1: Parallel DAG Compilation — Implementation Tasks

## Phase A: Extract `model/` from `core/`

### Task 1: Create model/ Directory Structure

- [x] Create `crates/cdo/api/model/` directory
- [x] Create `crates/cdo/lib/model/` directory
- [x] Verify the build system picks up files in the new directories (scanner should detect them automatically)

### Task 2: Move Workspace Files

- [x] Move `api/core/workspace.h` → `api/model/workspace.h`
- [x] Move `lib/core/workspace_load.c` → `lib/model/workspace_load.c`
- [x] Move `lib/core/workspace_resolve.c` → `lib/model/workspace_resolve.c`
- [x] Move `lib/core/workspace_internal.h` → `lib/model/workspace_internal.h`
- [x] Update all `#include "core/workspace.h"` → `#include "model/workspace.h"` across the codebase
- [x] Verify build compiles

### Task 3: Move Module Files

- [x] Move `api/core/module.h` → `api/model/module.h`
- [x] Move `lib/core/module.c` → `lib/model/module.c`
- [x] Update all `#include "core/module.h"` → `#include "model/module.h"`
- [x] Verify build compiles

### Task 4: Move Scanner Files

- [x] Move `api/core/scanner.h` → `api/model/scanner.h`
- [x] Move `lib/core/scanner.c` → `lib/model/scanner.c`
- [x] Update all `#include "core/scanner.h"` → `#include "model/scanner.h"`
- [x] Verify build compiles

### Task 5: Move Deps Spec & Lock Files

- [x] Move `api/core/deps.h` → `api/model/deps.h`
- [x] Move `lib/core/deps_lock.c` → `lib/model/deps_lock.c`
- [x] Keep `lib/core/deps_resolve.c` in core/ (it does HTTP/git I/O)
- [x] Update `deps_resolve.c` to include from `model/deps.h`
- [x] Update all other `#include "core/deps.h"` → `#include "model/deps.h"`
- [x] Verify build compiles

### Task 6: Move Hooks Parsing (Split hooks.c)

- [x] Move `api/core/hooks.h` → `api/model/hooks.h` (types + parse declarations)
- [x] Create `api/core/hooks_exec.h` with `hook_execute()` declaration only
- [x] Split `lib/core/hooks.c` into:
  - `lib/model/hooks_parse.c` — `hooks_parse()`, `hooks_parse_table()`, `hook_lifecycle_name()`
  - `lib/core/hooks_exec.c` — `hook_execute()` (spawns processes via pal_spawn)
- [x] `hooks_exec.c` includes `model/hooks.h` for types
- [x] Update all includes:
  - Files using types/parsing: `#include "model/hooks.h"`
  - Files calling `hook_execute`: `#include "core/hooks_exec.h"` (and `model/hooks.h` for types)
- [x] Verify build compiles

### Task 7: Move FmtSettings

- [x] Move `api/core/fmt_settings.h` → `api/model/fmt_settings.h`
- [x] Update all `#include "core/fmt_settings.h"` → `#include "model/fmt_settings.h"`
- [x] Verify build compiles

### Task 8: Verify Layering & Run Tests

- [x] Verify no file in `lib/model/` includes from `core/` (only from `model/`, `commons/`, `pal/`)
- [x] Verify no file in `api/model/` includes from `core/`
- [x] Run full build: `.\cdo.exe build`
- [x] Run full test suite: `.\cdo.exe test cdo_ut`
- [x] All tests pass — no behavioral change

---

## Phase B: DAG Data Structures & Generation

### Task 9: DAG Types Header

- [x] Create `api/model/dag.h` with:
  - `DagTaskKind` enum (HOOK_PRE, COMPILE, LINK, RESOURCE, SHADER, HOOK_POST)
  - `DagTaskStatus` enum (PENDING, READY, RUNNING, DONE, FAILED, CANCELLED)
  - `DagTask` struct (id, kind, crate_idx, module_kind, source_path, object_path, artifact_path, hook_def, dep_ids, rdep_ids, remaining_deps, status)
  - `DagGraph` struct (tasks array, count, capacity)
  - Function declarations: `dag_generate`, `dag_graph_free`, `dag_graph_task_count_by_kind`

### Task 10: DAG Graph Utilities

- [x] Create `lib/model/dag.c`
- [x] Implement `dag_graph_create(int initial_capacity)` — allocate empty graph
- [x] Implement `dag_graph_free(DagGraph*)` — free all tasks, dep arrays, graph
- [x] Implement internal `dag_task_add(DagGraph*, DagTaskKind, int crate_idx, ModuleKind)` — return task ID
- [x] Implement internal `dag_task_add_dep(DagGraph*, int task_id, int dep_task_id)` — append dep edge
- [x] Implement `dag_graph_finalize(DagGraph*)` — compute rdep_ids (reverse edges) and initial remaining_deps

### Task 11: DAG Generation from Workspace

- [x] Implement `dag_generate(const Workspace* ws, const char* profile, DagGraph** out)`:
  1. Create workspace pre-build hook task (if hook present)
  2. For each crate in `ws->build_order`:
     a. Create crate pre-build hook task (deps: ws pre-hook)
     b. If lib/ present: create compile tasks for lib/ sources (deps: crate pre-hook)
     c. Create lib/ link task (deps: all lib/ compile tasks + upstream lib/ link tasks)
     d. For exe/, dyn/, tst/: create compile tasks (deps: own lib/ link + crate pre-hook)
     e. For exe/, dyn/, tst/: create link tasks (deps: module compile tasks + own lib/ link + upstream links)
     f. Create shd/ task if present (deps: crate pre-hook)
     g. Create res/ task if present (deps: crate pre-hook)
     h. Create crate post-hook (deps: all tasks in this crate)
  3. Create workspace post-hook (deps: all crate post-hooks)
  4. Call `dag_graph_finalize`
- [x] Compute source paths and object paths for compile tasks (using existing path helpers from model/)
- [x] Compute artifact paths for link tasks
- [x] Handle crates without lib/ (exe-only crates, test crates)
- [x] Handle crates with no sources (skip graph nodes for empty modules)

### Task 12: DAG Generation — Include Path Tracking

- [x] Each compile task needs to know its include paths (for the scheduler to build CompileJobs)
- [x] Option A: Store include paths in the DagTask struct (makes DagTask large)
- [x] Option B: Store crate_idx + module_kind, and let the scheduler compute include paths at execution time (keeps model/ pure, scheduler uses existing `module_include_paths`)
- [x] Choose Option B: the DAG stores *what* to compile (source → object), the scheduler resolves *how* (include paths, defines, flags) at dispatch time using workspace + compiler info
- [x] Document this decision in dag.h comments

---

## Phase C: DAG Scheduler

### Task 13: Scheduler Header

- [x] Create `api/core/dag_scheduler.h` with:
  - `DagSchedulerConfig` struct (compiler, cache_config, cache_stats, no_cache, jobs, progress, etc.)
  - `int dag_scheduler_run(DagGraph* graph, const DagSchedulerConfig* config)` declaration

### Task 14: Scheduler Core Loop

- [x] Create `lib/core/dag_scheduler.c`
- [x] Implement scheduler state: mutex, condition variable, ready queue (array-based), completion queue
- [x] Implement `dag_scheduler_run()`:
  1. Scan graph for tasks with `remaining_deps == 0` → seed ready queue
  2. Main loop: dispatch ready tasks up to `jobs` concurrency, wait for completions
  3. On completion: decrement rdep `remaining_deps`, enqueue newly-ready tasks
  4. On failure: call `cancel_dependents`
  5. Terminate when `completed + cancelled == task_count`
  6. Return 0 if no failures, 1 otherwise
- [x] Deterministic dispatch order: when multiple tasks are ready, sort by (crate_idx, module_kind, source_path)

### Task 15: Task Execution Dispatch

- [x] Implement `execute_compile_task(DagTask*, DagSchedulerConfig*)`:
  - Build CompileJob from task (source_path, object_path) + config (compiler, profile)
  - Compute include paths via `module_include_paths`
  - Cache lookup (if enabled): on hit, return success
  - Spawn compiler process (reuse logic from compiler_compile.c's `compile_task`)
  - Cache store on success
  - Update cache_stats
- [x] Implement `execute_link_task(DagTask*, DagSchedulerConfig*)`:
  - Build LinkJob from task (collect object paths for this module from graph)
  - Call `compiler_link`
- [x] Implement `execute_hook_task(DagTask*, DagSchedulerConfig*)`:
  - Build HookEnv, call `hook_execute`
- [x] Implement `execute_resource_task(DagTask*, DagSchedulerConfig*)`:
  - Reuse resource copy logic from `build_resource_module`
- [x] Implement `execute_shader_task(DagTask*, DagSchedulerConfig*)`:
  - Reuse shader compile logic from `build_shader_module`

### Task 16: Failure Propagation

- [x] Implement `cancel_dependents(DagGraph*, int failed_task_id)`:
  - BFS through rdep_ids from failed task
  - Mark all reachable PENDING/READY tasks as CANCELLED
  - Count cancelled tasks per crate for reporting
- [x] Log: "Cancelled N pending tasks in crates: X, Y, Z"
- [x] Cancelled tasks do not execute but contribute to termination count

### Task 17: Progress Reporting

- [x] Track `compile_done` counter in scheduler
- [x] On each DAG_TASK_COMPILE completion (success or cache hit): increment and call `progress_update`
- [x] On failure: `progress_finish` before logging errors
- [x] Debug log per compile: "Compiled [crate/module]: source_file"

---

## Phase D: Integration

### Task 18: Wire DAG into cmd_build.c

- [x] In `cmd_build()`, after workspace resolve and compiler detect:
  - If `jobs != 1`: generate DAG, run scheduler
  - If `jobs == 1`: keep existing sequential path unchanged
- [x] Adapt the existing sequential path to remain as-is (no regression for --jobs 1)
- [x] Handle the case where DAG generation fails (fall back to sequential with a warning?)
- [x] Report cache stats and timing from scheduler results (same output format as current)
- [x] Ensure workspace/build hooks still execute in correct order

### Task 19: Dirty Set Integration

- [x] Before `dag_generate`, compute dirty set per module (existing logic)
- [x] Only add compile tasks for dirty files to the DAG
- [x] If no files are dirty in a module, still add the link task (no-op: linker skips if artifact is fresh)
- [x] Skip link task if artifact exists and all inputs are older (optimization)

### Task 20: Link Task Object Collection

- [x] The link task needs all object paths for its module
- [x] Strategy: after all compile tasks for a module are identified during generation, store their object_paths in a list on the link task (or store module compile task IDs so scheduler can collect post-completion)
- [x] Implement: link task stores array of compile task IDs; scheduler collects object_paths from completed compile tasks when dispatching the link

---

## Phase E: Testing

### Task 21: Unit Tests — model/ extraction

- [x] Verify all existing unit tests still pass after file moves
- [x] Add a compile-time check: model/ source files include nothing from core/ (can be a grep-based CI check or a comment convention)

### Task 22: Unit Tests — DAG Generation

- [x] Test: single crate with lib/ only → 1 link task, N compile tasks
- [x] Test: single crate with lib/ + exe/ → lib compile + lib link + exe compile + exe link, correct deps
- [x] Test: 2 crates (B depends on A) → B compile has NO dep on A link; B link depends on A link
- [x] Test: 3-crate diamond (A deps B,C; B,C dep D) → correct transitive edges
- [x] Test: crate with no lib/ (exe-only) → compile tasks have no lib link dep
- [x] Test: empty crate (no sources) → no tasks generated
- [x] Test: hooks present → hook tasks in correct position with correct deps
- [x] Test: dag_graph_finalize computes correct rdep_ids and remaining_deps
- [x] Target >90% line coverage on dag.c

### Task 23: Unit Tests — DAG Scheduler

- [x] Test: all tasks succeed → returns 0, all marked DONE
- [x] Test: one compile fails → dependents cancelled, returns 1
- [x] Test: jobs=1 → tasks execute in strict topological order
- [x] Test: cache hit → task completes immediately without spawning compiler
- [x] Test: progress increments match compile task completions
- [x] Test: deterministic dispatch order for simultaneous ready tasks
- [x] Target >90% line coverage on dag_scheduler.c

### Task 24: Integration / E2E Tests

- [x] Create `e2e/parallel_dag/` workspace with 3 crates: `base` (lib), `mid` (lib, deps base), `app` (exe, deps mid)
- [x] Test: `.\cdo_temp.exe build` succeeds, all artifacts produced
- [x] Test: `.\cdo_temp.exe build --jobs 1` produces identical artifacts
- [x] Test: introduce compile error in `mid` → `app` tasks cancelled, clear error message
- [x] Benchmark: compare `--jobs 1` vs `--jobs 8` wall-clock time, verify speedup on multi-crate workspace

### Task 25: Documentation

- [x] Update README.md to mention parallel multi-crate compilation
- [x] Document `--jobs` / `-j` flag behavior
- [x] Log at info level: "Building N crates (M compile tasks, K link tasks) with J threads"
- [x] Log at debug level: "DAG: N tasks, M dependency edges"
