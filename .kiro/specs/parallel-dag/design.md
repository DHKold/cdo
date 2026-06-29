# Feature 1: Parallel DAG Compilation — Design

## Architecture

The feature is implemented in two phases:
1. **Phase A:** Extract project model into `model/` (structural refactor, no behavior change)
2. **Phase B:** Implement DAG generation (in `model/`) and DAG scheduling (in `core/`)

### Layering After Extraction

```
┌─────────────────────────────────────────────────────┐
│ exe/           Entry point, CLI dispatch             │
├─────────────────────────────────────────────────────┤
│ commands/      Command handlers (build, run, test..) │
├─────────────────────────────────────────────────────┤
│ core/          Execution: compiler, cache, scheduler │
├─────────────────────────────────────────────────────┤
│ model/         Pure data: workspace, DAG, modules    │
├─────────────────────────────────────────────────────┤
│ commons/       Utilities: toml, http, archive, etc.  │
├─────────────────────────────────────────────────────┤
│ pal/           Platform abstraction: fs, process     │
└─────────────────────────────────────────────────────┘
```

Each layer only depends on layers below it. `model/` never calls `core/` functions.

### Build Flow with DAG

```
┌────────────────────┐
│ workspace_load     │  (model/ — parse cdo.toml, crate.toml, discover modules)
└────────┬───────────┘
         ▼
┌────────────────────┐
│ workspace_resolve  │  (model/ — topological sort, transitive closure)
└────────┬───────────┘
         ▼
┌────────────────────┐
│ dag_generate       │  (model/ — emit compile + link tasks with edges)
└────────┬───────────┘
         ▼
┌────────────────────┐
│ dag_scheduler_run  │  (core/ — dispatch tasks, cache, spawn compilers)
└────────┬───────────┘
         ▼
┌────────────────────┐
│ threadpool         │  (commons/ — execute task functions in parallel)
└────────────────────┘
```

## Phase A: `model/` Extraction

### What Moves

| From (api/core/) | To (api/model/) | Rationale |
|---|---|---|
| `workspace.h` | `workspace.h` | Pure data: Workspace, Crate structs |
| `module.h` | `module.h` | Pure data: Module, ModuleKind, path computation |
| `scanner.h` | `scanner.h` | Pure algorithm: file discovery (uses pal/ only) |
| `deps.h` | `deps.h` | Pure data: DepSpec, ResolvedDep, lock format |
| `hooks.h` | `hooks.h` | Pure data: HookDef, HookSet, hooks_parse (no execution) |
| `fmt_settings.h` | `fmt_settings.h` | Pure data: FmtSettings struct |

| From (lib/core/) | To (lib/model/) | Rationale |
|---|---|---|
| `workspace_load.c` | `workspace_load.c` | TOML parsing → struct (uses pal/ for file read) |
| `workspace_resolve.c` | `workspace_resolve.c` | Topological sort (pure algorithm) |
| `workspace_internal.h` | `workspace_internal.h` | Shared helpers between workspace files |
| `module.c` | `module.c` | Module path computation (uses pal/ for mkdir) |
| `scanner.c` | `scanner.c` | File discovery (uses pal/ for dir_walk) |
| `deps_lock.c` | `deps_lock.c` | Lock file read/write (uses pal/ for file I/O, commons/toml) |
| `hooks.c` (parse-only) | `hooks_parse.c` | Parsing hook definitions from TOML |

### What Stays in `core/`

| File | Rationale |
|---|---|
| `compiler.h` / `compiler_*.c` | Spawns compiler processes |
| `cache.h` / `cache.c` / `cache_key.c` | File I/O for object cache |
| `catalog.h` / `catalog_*.c` | File I/O, network for catalog |
| `deps_resolve.c` | HTTP download, git clone (network) |
| `hooks.c` (`hook_execute`) | Spawns hook processes → rename to `hooks_exec.c` |
| `output.h` / `output.c` | stdout/stderr logging |
| `shader.c` | Spawns DXC |
| `template.c` | File generation |
| `errors.h` / `errors.c` | Error codes (could go either way, leave in core/) |
| `cli.h` / `cli_*.c` | CLI parsing |

### Hooks Split

The current `hooks.c` does both parsing and execution. Split into:
- `lib/model/hooks_parse.c` — `hooks_parse()`, `hooks_parse_table()`, `hook_lifecycle_name()`
- `lib/core/hooks_exec.c` — `hook_execute()` (spawns processes)

The header `api/model/hooks.h` declares the types and parse functions.
A new `api/core/hooks_exec.h` declares `hook_execute()`.

### Include Path Updates

After the move, includes change from:
```c
#include "core/workspace.h"
#include "core/module.h"
```
to:
```c
#include "model/workspace.h"
#include "model/module.h"
```

This is a mechanical find-and-replace across all source files.

## Phase B: DAG Implementation

### New Files

| File | Layer | Purpose |
|------|-------|---------|
| `api/model/dag.h` | model | DagTask, DagGraph types, dag_generate, dag_graph_free |
| `lib/model/dag.c` | model | DAG generation from Workspace (pure algorithm) |
| `api/core/dag_scheduler.h` | core | Scheduler config, dag_scheduler_run |
| `lib/core/dag_scheduler.c` | core | Execution: dispatch tasks, cache, spawn compilers |

### Data Structures (in `api/model/dag.h`)

```c
typedef enum {
    DAG_TASK_HOOK_PRE,
    DAG_TASK_COMPILE,
    DAG_TASK_LINK,
    DAG_TASK_RESOURCE,
    DAG_TASK_SHADER,
    DAG_TASK_HOOK_POST,
} DagTaskKind;

typedef enum {
    DAG_STATUS_PENDING,
    DAG_STATUS_READY,
    DAG_STATUS_RUNNING,
    DAG_STATUS_DONE,
    DAG_STATUS_FAILED,
    DAG_STATUS_CANCELLED,
} DagTaskStatus;

/// A single task in the build DAG.
/// Contains the "what" (kind, crate, module, source/object paths)
/// but NOT the "how" (no CompilerInfo, no CacheConfig — those are scheduler concerns).
typedef struct {
    int             id;
    DagTaskKind     kind;
    int             crate_idx;
    ModuleKind      module_kind;

    // Compile task data
    char            source_path[260];
    char            object_path[260];

    // Link task data
    char            artifact_path[260];

    // Hook task data
    HookDef         hook_def;
    
    // DAG edges (populated by dag_graph_finalize)
    int*            dep_ids;        // Tasks I depend on
    int             dep_count;
    int*            rdep_ids;       // Tasks that depend on me
    int             rdep_count;

    // Scheduler state (mutated at runtime by scheduler)
    int             remaining_deps;
    DagTaskStatus   status;
} DagTask;

typedef struct {
    DagTask*    tasks;
    int         task_count;
    int         task_capacity;
} DagGraph;

/// Generate the build DAG from a resolved workspace.
/// Pure function: reads workspace state, produces a graph. No side effects.
/// Caller must free the graph with dag_graph_free().
int dag_generate(const Workspace* ws, const char* profile, DagGraph** out);

/// Free all memory in a DAG graph.
void dag_graph_free(DagGraph* graph);
```

### Scheduler Interface (in `api/core/dag_scheduler.h`)

```c
#include "model/dag.h"
#include "core/compiler.h"
#include "core/cache.h"
#include "core/output.h"
#include "commons/threadpool.h"

typedef struct {
    const CompilerInfo* compiler;
    const CacheConfig*  cache_config;
    CacheStats*         cache_stats;
    bool                no_cache;
    int                 jobs;
    ProgressBar*        progress;
    int                 total_compile_units;
} DagSchedulerConfig;

/// Execute all tasks in the DAG graph using the provided configuration.
/// Dispatches ready tasks to a thread pool, handles failures and cancellation.
/// Returns 0 if all tasks succeeded, non-zero if any failed.
int dag_scheduler_run(DagGraph* graph, const DagSchedulerConfig* config);
```

### DAG Generation Algorithm (in `lib/model/dag.c`)

```
dag_generate(ws, profile):
    graph = new empty graph
    
    // Phase 1: Create workspace pre-build hook task
    ws_pre_hook_id = add_hook_task(graph, ws.ws_hooks.pre_build, -1)
    
    // Phase 2: For each crate in build order
    crate_post_hooks = []
    crate_lib_link_ids = {}  // map: crate_idx -> lib link task id
    
    for crate_idx in ws.build_order:
        crate = ws.crates[crate_idx]
        
        // 2a: Crate pre-hook (depends on ws pre-hook)
        crate_pre_id = add_hook_task(graph, crate.hooks.pre_build, crate_idx)
        add_dep(crate_pre_id, ws_pre_hook_id)
        
        // 2b: lib/ compile tasks (depend on crate pre-hook only)
        lib_compile_ids = []
        for source in crate.modules[LIB].sources:
            task_id = add_compile_task(graph, crate_idx, MODULE_LIB, source)
            add_dep(task_id, crate_pre_id)
            lib_compile_ids.append(task_id)
        
        // 2c: lib/ link task (depends on all lib/ compiles + upstream lib/ links)
        lib_link_id = add_link_task(graph, crate_idx, MODULE_LIB)
        for id in lib_compile_ids: add_dep(lib_link_id, id)
        for dep_idx in crate.dep_indices:
            if dep_idx in crate_lib_link_ids:
                add_dep(lib_link_id, crate_lib_link_ids[dep_idx])
        crate_lib_link_ids[crate_idx] = lib_link_id
        
        // 2d: exe/, dyn/, tst/ compile tasks (depend on own lib/ link)
        for module_kind in [EXE, DYN, TST]:
            if not crate.modules[module_kind].present: continue
            mod_compile_ids = []
            for source in crate.modules[module_kind].sources:
                task_id = add_compile_task(graph, crate_idx, module_kind, source)
                add_dep(task_id, lib_link_id)  // need own lib
                add_dep(task_id, crate_pre_id)
                mod_compile_ids.append(task_id)
            
            // Module link task
            mod_link_id = add_link_task(graph, crate_idx, module_kind)
            for id in mod_compile_ids: add_dep(mod_link_id, id)
            add_dep(mod_link_id, lib_link_id)
            for dep_idx in crate.dep_indices:
                if dep_idx in crate_lib_link_ids:
                    add_dep(mod_link_id, crate_lib_link_ids[dep_idx])
        
        // 2e: res/ and shd/ tasks
        if crate.has_shd:
            shd_id = add_shader_task(graph, crate_idx)
            add_dep(shd_id, crate_pre_id)
        if crate.has_res:
            res_id = add_resource_task(graph, crate_idx)
            add_dep(res_id, crate_pre_id)
        
        // 2f: Crate post-hook (depends on ALL tasks in this crate)
        crate_post_id = add_hook_task(graph, crate.hooks.post_build, crate_idx)
        // add_dep for all tasks created for this crate
        crate_post_hooks.append(crate_post_id)
    
    // Phase 3: Workspace post-build hook
    ws_post_id = add_hook_task(graph, ws.ws_hooks.post_build, -1)
    for id in crate_post_hooks: add_dep(ws_post_id, id)
    
    // Phase 4: Finalize (compute reverse edges, initial remaining_deps)
    dag_graph_finalize(graph)
    
    return graph
```

### Scheduler Algorithm (in `lib/core/dag_scheduler.c`)

```
dag_scheduler_run(graph, config):
    pool = threadpool_create(config->jobs)
    mutex, cond_done = init synchronization
    
    // Find initially ready tasks (remaining_deps == 0)
    ready_queue = [t for t in graph.tasks if t.remaining_deps == 0]
    running = 0, completed = 0, failed = 0, compile_done = 0
    
    while completed < graph.task_count:
        // Dispatch ready tasks
        while ready_queue not empty AND running < config->jobs:
            task = ready_queue.pop()
            task.status = RUNNING
            running++
            threadpool_submit(pool, execute_task_wrapper, task_ctx)
        
        // Wait for a task to complete
        wait(cond_done)
        
        // Process completed task
        task = dequeue_completion()
        running--
        completed++
        
        if task.status == FAILED:
            failed++
            cancel_dependents(graph, task)
            // cancelled tasks count toward completed
            completed += count_cancelled
        else:  // DONE
            if task.kind == COMPILE:
                compile_done++
                progress_update(config->progress, compile_done)
            // Propagate readiness
            for rdep in task.rdep_ids:
                rdep.remaining_deps--
                if rdep.remaining_deps == 0 and rdep.status == PENDING:
                    rdep.status = READY
                    ready_queue.push(rdep)
    
    threadpool_destroy(pool)
    return failed > 0 ? 1 : 0
```

### Task Execution (in scheduler)

The scheduler wraps each task kind with the appropriate execution:

```c
static void execute_task(DagTask* task, DagSchedulerConfig* config) {
    switch (task->kind) {
        case DAG_TASK_COMPILE:
            // Cache lookup → compile if miss → cache store
            execute_compile(task, config);
            break;
        case DAG_TASK_LINK:
            // Build LinkJob from task data, call compiler_link
            execute_link(task, config);
            break;
        case DAG_TASK_HOOK_PRE:
        case DAG_TASK_HOOK_POST:
            // Call hook_execute
            execute_hook(task, config);
            break;
        case DAG_TASK_RESOURCE:
            execute_resource(task, config);
            break;
        case DAG_TASK_SHADER:
            execute_shader(task, config);
            break;
    }
}
```

## Thread Safety

| Resource | Protection |
|----------|-----------|
| `remaining_deps`, `status` fields | Scheduler mutex (single writer) |
| Ready queue | Scheduler mutex |
| Completion signaling | Condition variable |
| Cache file operations | Filesystem atomicity (existing design) |
| Progress bar | Updated only from scheduler thread (after dequeuing completion) |
| CacheStats | Atomic increments or scheduler-mutex-protected |

## Integration with cmd_build.c

```c
// In cmd_build():
if (jobs > 1 || jobs == 0) {
    // DAG path
    DagGraph* graph = NULL;
    rc = dag_generate(&ws, profile, &graph);
    if (rc != 0) { /* error */ }
    
    DagSchedulerConfig sched_config = {
        .compiler = &compiler,
        .cache_config = cache_active ? &ws.cache_config : NULL,
        .cache_stats = &cache_stats,
        .no_cache = opts->no_cache,
        .jobs = jobs,
        .progress = progress,
        .total_compile_units = total_units,
    };
    
    rc = dag_scheduler_run(graph, &sched_config);
    dag_graph_free(graph);
} else {
    // Sequential path (existing code, unchanged)
    // ...
}
```

## Memory Management

- `DagGraph` owns all `DagTask` memory (one contiguous allocation)
- `dep_ids` and `rdep_ids` are separately allocated per task
- `dag_graph_free()` frees everything
- Task `source_path`/`object_path` are copied into the task (fixed-size buffers, no heap)
- Graph lifetime: one build invocation (allocated after workspace_resolve, freed after scheduler completes)
