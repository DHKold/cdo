#include "core/dag_scheduler.h"
#include "model/module.h"
#include "model/scanner.h"
#include "core/hooks_exec.h"
#include "commons/output.h"
#include "pal/pal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static int execute_compile_task(DagTask* task, const DagSchedulerConfig* config);
static int execute_link_task(DagTask* task, DagGraph* graph, const DagSchedulerConfig* config);
static int execute_hook_task(DagTask* task, const DagSchedulerConfig* config);
static int execute_resource_task(DagTask* task, const DagSchedulerConfig* config);
static int execute_shader_task(DagTask* task, const DagSchedulerConfig* config);
static void cancel_dependents(DagGraph* graph, int failed_task_id, int* cancelled_count);

// ---------------------------------------------------------------------------
// Ready queue: simple dynamic array with deterministic sort
// ---------------------------------------------------------------------------

typedef struct {
    int*    task_ids;
    int     count;
    int     capacity;
} ReadyQueue;

static void ready_queue_init(ReadyQueue* q, int initial_cap) {
    q->task_ids = (int*)malloc(sizeof(int) * (size_t)initial_cap);
    q->count = 0;
    q->capacity = initial_cap;
}

static void ready_queue_push(ReadyQueue* q, int task_id) {
    if (q->count >= q->capacity) {
        q->capacity *= 2;
        q->task_ids = (int*)realloc(q->task_ids, sizeof(int) * (size_t)q->capacity);
    }
    q->task_ids[q->count++] = task_id;
}

static void ready_queue_free(ReadyQueue* q) {
    free(q->task_ids);
    q->task_ids = NULL;
    q->count = 0;
    q->capacity = 0;
}

// ---------------------------------------------------------------------------
// Deterministic sort comparator for the ready queue
// Sort by: (crate_idx ASC, module_kind ASC, source_path lexicographic ASC)
// ---------------------------------------------------------------------------

static DagGraph* g_sort_graph = NULL;  // used by qsort comparator

static int ready_queue_compare(const void* a, const void* b) {
    int id_a = *(const int*)a;
    int id_b = *(const int*)b;
    DagTask* ta = &g_sort_graph->tasks[id_a];
    DagTask* tb = &g_sort_graph->tasks[id_b];

    if (ta->crate_idx != tb->crate_idx) return ta->crate_idx - tb->crate_idx;
    if (ta->module_kind != tb->module_kind) return (int)ta->module_kind - (int)tb->module_kind;
    return strcmp(ta->source_path, tb->source_path);
}

static void ready_queue_sort(ReadyQueue* q, DagGraph* graph) {
    if (q->count <= 1) return;
    g_sort_graph = graph;
    qsort(q->task_ids, (size_t)q->count, sizeof(int), ready_queue_compare);
    g_sort_graph = NULL;
}

// ---------------------------------------------------------------------------
// Task 14: Scheduler Core Loop (Serial implementation)
// ---------------------------------------------------------------------------

int dag_scheduler_run(DagGraph* graph, const DagSchedulerConfig* config) {
    if (!graph || !config) return -1;
    if (graph->task_count == 0) return 0;

    int compile_done = 0;
    int failed_count = 0;
    int completed = 0;

    // Seed ready queue with tasks whose remaining_deps == 0
    ReadyQueue ready;
    ready_queue_init(&ready, graph->task_count > 16 ? graph->task_count : 16);

    for (int i = 0; i < graph->task_count; i++) {
        if (graph->tasks[i].remaining_deps == 0 && graph->tasks[i].status == DAG_STATUS_READY) {
            ready_queue_push(&ready, i);
        }
    }

    // Sort the initial ready queue for deterministic dispatch
    ready_queue_sort(&ready, graph);

    cdo_debug("DAG scheduler: %d tasks, %d initially ready, jobs=%d",
              graph->task_count, ready.count, config->jobs);

    // Main loop: process tasks sequentially from the ready queue
    while (completed < graph->task_count) {
        if (ready.count == 0) {
            // No tasks ready and not all completed — possible deadlock or all remaining are cancelled
            // Check if all remaining tasks are cancelled
            bool all_done = true;
            for (int i = 0; i < graph->task_count; i++) {
                if (graph->tasks[i].status != DAG_STATUS_DONE &&
                    graph->tasks[i].status != DAG_STATUS_FAILED &&
                    graph->tasks[i].status != DAG_STATUS_CANCELLED) {
                    all_done = false;
                    break;
                }
            }
            if (all_done) break;

            cdo_error("DAG scheduler: deadlock detected - no ready tasks but %d/%d completed",
                      completed, graph->task_count);
            ready_queue_free(&ready);
            return -1;
        }

        // Pop the first ready task (deterministic: already sorted)
        int task_id = ready.task_ids[0];
        // Shift remaining items left
        for (int i = 1; i < ready.count; i++) {
            ready.task_ids[i - 1] = ready.task_ids[i];
        }
        ready.count--;

        DagTask* task = &graph->tasks[task_id];
        task->status = DAG_STATUS_RUNNING;

        // Execute the task based on its kind
        int rc = 0;
        switch (task->kind) {
            case DAG_TASK_COMPILE:
                rc = execute_compile_task(task, config);
                break;
            case DAG_TASK_LINK:
                rc = execute_link_task(task, graph, config);
                break;
            case DAG_TASK_HOOK_PRE:
            case DAG_TASK_HOOK_POST:
                rc = execute_hook_task(task, config);
                break;
            case DAG_TASK_RESOURCE:
                rc = execute_resource_task(task, config);
                break;
            case DAG_TASK_SHADER:
                rc = execute_shader_task(task, config);
                break;
        }

        if (rc != 0) {
            // Task failed
            task->status = DAG_STATUS_FAILED;
            failed_count++;
            completed++;

            // Task 17: On failure, finish progress before logging errors
            if (config->progress) {
                progress_finish(config->progress);
            }

            const Workspace* ws = config->workspace;
            const char* crate_name = (task->crate_idx >= 0 && task->crate_idx < ws->crate_count)
                ? ws->crates[task->crate_idx].name : "unknown";
            const char* mod_str = module_kind_to_string(task->module_kind);

            if (task->kind == DAG_TASK_COMPILE) {
                cdo_error("Compilation failed in crate '%s', module %s/: %s",
                          crate_name, mod_str, task->source_path);
            } else if (task->kind == DAG_TASK_LINK) {
                cdo_error("Link failed in crate '%s', module %s/: %s",
                          crate_name, mod_str, task->artifact_path);
            } else {
                cdo_error("Task failed in crate '%s' (kind=%d)", crate_name, task->kind);
            }

            // Task 16: Cancel all transitive dependents
            int cancelled = 0;
            cancel_dependents(graph, task_id, &cancelled);
            completed += cancelled;

        } else {
            // Task succeeded
            task->status = DAG_STATUS_DONE;
            completed++;

            // Task 17: Progress reporting for compile tasks
            if (task->kind == DAG_TASK_COMPILE) {
                compile_done++;
                if (config->progress) {
                    progress_update(config->progress, compile_done);
                }

                // Debug log per compile
                const Workspace* ws = config->workspace;
                const char* crate_name = (task->crate_idx >= 0 && task->crate_idx < ws->crate_count)
                    ? ws->crates[task->crate_idx].name : "unknown";
                const char* mod_str = module_kind_to_string(task->module_kind);
                cdo_debug("Compiled [%s/%s]: %s", crate_name, mod_str, task->source_path);
            }

            // Propagate readiness to dependents (rdeps)
            for (int r = 0; r < task->rdep_count; r++) {
                int rdep_id = task->rdep_ids[r];
                DagTask* rdep = &graph->tasks[rdep_id];
                rdep->remaining_deps--;
                if (rdep->remaining_deps == 0 && rdep->status == DAG_STATUS_PENDING) {
                    rdep->status = DAG_STATUS_READY;
                    ready_queue_push(&ready, rdep_id);
                }
            }

            // Re-sort if new tasks were added
            if (ready.count > 1) {
                ready_queue_sort(&ready, graph);
            }
        }
    }

    ready_queue_free(&ready);

    if (failed_count > 0) {
        cdo_error("DAG scheduler: %d task(s) failed, %d completed", failed_count, completed);
        return 1;
    }

    cdo_debug("DAG scheduler: all %d tasks completed successfully", graph->task_count);
    return 0;
}

// ---------------------------------------------------------------------------
// Task 16: Failure Propagation — cancel_dependents
// BFS through rdep_ids from the failed task, marking all reachable
// PENDING/READY tasks as CANCELLED.
// ---------------------------------------------------------------------------

static void cancel_dependents(DagGraph* graph, int failed_task_id, int* cancelled_count) {
    *cancelled_count = 0;

    // BFS queue (reuse simple array)
    int* bfs_queue = (int*)malloc(sizeof(int) * (size_t)graph->task_count);
    if (!bfs_queue) return;

    int bfs_head = 0, bfs_tail = 0;

    // Seed BFS with direct dependents of the failed task
    DagTask* failed = &graph->tasks[failed_task_id];
    for (int r = 0; r < failed->rdep_count; r++) {
        int rdep_id = failed->rdep_ids[r];
        DagTask* rdep = &graph->tasks[rdep_id];
        if (rdep->status == DAG_STATUS_PENDING || rdep->status == DAG_STATUS_READY) {
            rdep->status = DAG_STATUS_CANCELLED;
            bfs_queue[bfs_tail++] = rdep_id;
            (*cancelled_count)++;
        }
    }

    // BFS: propagate cancellation through transitive dependents
    while (bfs_head < bfs_tail) {
        int current_id = bfs_queue[bfs_head++];
        DagTask* current = &graph->tasks[current_id];

        for (int r = 0; r < current->rdep_count; r++) {
            int rdep_id = current->rdep_ids[r];
            DagTask* rdep = &graph->tasks[rdep_id];
            if (rdep->status == DAG_STATUS_PENDING || rdep->status == DAG_STATUS_READY) {
                rdep->status = DAG_STATUS_CANCELLED;
                bfs_queue[bfs_tail++] = rdep_id;
                (*cancelled_count)++;
            }
        }
    }

    // Log cancelled tasks per crate
    if (*cancelled_count > 0) {
        // Collect unique crate names that had cancellations
        char crate_names_buf[1024] = {0};
        int buf_pos = 0;
        bool* crate_seen = (bool*)calloc((size_t)graph->task_count, sizeof(bool));

        for (int i = 0; i < bfs_tail; i++) {
            int tid = bfs_queue[i];
            int cidx = graph->tasks[tid].crate_idx;
            if (cidx >= 0 && !crate_seen[cidx]) {
                crate_seen[cidx] = true;
                // Append crate name (we don't have workspace here, use index)
                if (buf_pos > 0 && buf_pos < (int)sizeof(crate_names_buf) - 2) {
                    crate_names_buf[buf_pos++] = ',';
                    crate_names_buf[buf_pos++] = ' ';
                }
                int written = snprintf(crate_names_buf + buf_pos, sizeof(crate_names_buf) - (size_t)buf_pos, "crate[%d]", cidx);
                if (written > 0) buf_pos += written;
            }
        }

        free(crate_seen);
        cdo_info("Cancelled %d pending tasks in crates: %s", *cancelled_count, crate_names_buf);
    }

    free(bfs_queue);
}

// ---------------------------------------------------------------------------
// Task 15: Task Execution Dispatch
// ---------------------------------------------------------------------------

/// Determine if a source file is C++ based on its extension.
static bool dag_is_cpp_source(const char* path) {
    const char* ext = pal_path_ext(path);
    if (!ext) return false;
    return (strcmp(ext, ".cpp") == 0 || strcmp(ext, ".cxx") == 0 ||
            strcmp(ext, ".cc") == 0  || strcmp(ext, ".CPP") == 0);
}

/// Convert integer C standard to string flag (e.g., 17 -> "c17").
static const char* dag_c_standard_str(int std_val) {
    switch (std_val) {
        case 11: return "c11";
        case 17: return "c17";
        case 23: return "c23";
        default: return "c17";
    }
}

/// Convert integer C++ standard to string flag (e.g., 20 -> "c++20").
static const char* dag_cpp_standard_str(int std_val) {
    switch (std_val) {
        case 17: return "c++17";
        case 20: return "c++20";
        case 23: return "c++23";
        default: return "c++20";
    }
}

// ---------------------------------------------------------------------------
// execute_compile_task: compile a single source file
// - Compute include paths using module_include_paths()
// - Cache lookup if enabled; on hit return success
// - Call compiler_compile_batch with 1 job (reuses existing compile logic)
// - Cache store on success (handled internally by compiler_compile_batch)
// ---------------------------------------------------------------------------

static int execute_compile_task(DagTask* task, const DagSchedulerConfig* config) {
    const Workspace* ws = config->workspace;
    if (task->crate_idx < 0 || task->crate_idx >= ws->crate_count) {
        cdo_error("DAG compile task has invalid crate_idx: %d", task->crate_idx);
        return -1;
    }

    const Crate* crate = &ws->crates[task->crate_idx];

    // Resolve include paths for this module
    char** inc_paths = NULL;
    int inc_count = 0;
    int rc = module_include_paths(crate, task->module_kind, ws, &inc_paths, &inc_count);
    if (rc != 0) {
        cdo_error("Failed to compute include paths for compile task: %s", task->source_path);
        return -1;
    }

    const char** all_includes = (const char**)malloc((size_t)(inc_count + 1) * sizeof(const char*));
    if (!all_includes) {
        for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
        free(inc_paths);
        return -1;
    }
    for (int i = 0; i < inc_count; i++) {
        all_includes[i] = inc_paths[i];
    }

    // Ensure object output directory exists
    {
        char dir_buf[260];
        strncpy(dir_buf, task->object_path, sizeof(dir_buf) - 1);
        dir_buf[sizeof(dir_buf) - 1] = '\0';
        char* last_sep = NULL;
        for (char* p = dir_buf; *p; p++) {
            if (*p == '/' || *p == '\\') last_sep = p;
        }
        if (last_sep) {
            *last_sep = '\0';
            pal_mkdir_p(dir_buf);
        }
    }

    // Build CompileJob
    CompileJob job = {0};
    job.source_path = task->source_path;
    job.object_path = task->object_path;
    job.include_paths = all_includes;
    job.include_path_count = inc_count;
    job.optimize = (config->profile && strcmp(config->profile, "release") == 0);
    job.debug_info = (config->profile && strcmp(config->profile, "debug") == 0);

    // Crate defines
    job.defines = (const char**)crate->defines;
    job.define_count = crate->define_count;

    // Language standard
    if (dag_is_cpp_source(task->source_path)) {
        job.c_standard = NULL;
        job.cpp_standard = dag_cpp_standard_str(crate->cpp_standard);
    } else {
        job.c_standard = dag_c_standard_str(crate->c_standard);
        job.cpp_standard = NULL;
    }

    job.extra_flags = NULL;
    job.extra_flag_count = 0;

    // Compile using compiler_compile_batch (with caching integrated)
    rc = compiler_compile_batch(&job, 1, config->compiler, 1,
                                config->cache_config, config->cache_stats,
                                config->no_cache);

    // Cleanup
    free(all_includes);
    for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
    free(inc_paths);

    return rc;
}

// ---------------------------------------------------------------------------
// execute_link_task: link object files into an artifact
// - Collect object paths from compile_task_ids stored in the DagTask
// - Build LinkJob and call compiler_link
// ---------------------------------------------------------------------------

static int execute_link_task(DagTask* task, DagGraph* graph, const DagSchedulerConfig* config) {
    const Workspace* ws = config->workspace;
    if (task->crate_idx < 0 || task->crate_idx >= ws->crate_count) {
        cdo_error("DAG link task has invalid crate_idx: %d", task->crate_idx);
        return -1;
    }

    const Crate* crate = &ws->crates[task->crate_idx];

    // Collect object paths from the compile tasks this link depends on
    int obj_count = task->compile_task_count;
    if (obj_count <= 0) {
        cdo_warn("Link task for crate '%s' has no compile tasks", crate->name);
        // An empty link is technically valid (creates empty archive)
    }

    const char** obj_paths = (const char**)malloc(sizeof(const char*) * (size_t)(obj_count > 0 ? obj_count : 1));
    if (!obj_paths) return -1;

    for (int i = 0; i < obj_count; i++) {
        int compile_id = task->compile_task_ids[i];
        if (compile_id < 0 || compile_id >= graph->task_count) {
            cdo_error("Link task references invalid compile task id: %d", compile_id);
            free(obj_paths);
            return -1;
        }
        obj_paths[i] = graph->tasks[compile_id].object_path;
    }

    // Ensure output directory exists
    {
        char dir_buf[260];
        strncpy(dir_buf, task->artifact_path, sizeof(dir_buf) - 1);
        dir_buf[sizeof(dir_buf) - 1] = '\0';
        char* last_sep = NULL;
        for (char* p = dir_buf; *p; p++) {
            if (*p == '/' || *p == '\\') last_sep = p;
        }
        if (last_sep) {
            *last_sep = '\0';
            pal_mkdir_p(dir_buf);
        }
    }

    // Build link libraries from crate dependencies
    const char** lib_paths = NULL;
    int lib_path_count = 0;
    const char** link_libs = NULL;
    int link_lib_count = 0;

    // For exe/dyn/tst modules, we need to link against dependency libraries
    if (task->module_kind != MODULE_LIB) {
        // Collect lib paths and link libs from crate's dep_indices
        int max_deps = crate->dep_count + 1; // +1 for own lib
        lib_paths = (const char**)calloc((size_t)max_deps, sizeof(const char*));
        link_libs = (const char**)calloc((size_t)(max_deps + crate->link_lib_count), sizeof(const char*));

        if (lib_paths && link_libs) {
            // Own crate's lib build dir
            static char own_lib_dir[260];
            snprintf(own_lib_dir, sizeof(own_lib_dir), "%s/build/%s/%s",
                     ws->root_path, config->profile, crate->name);
            if (crate->has_lib) {
                lib_paths[lib_path_count++] = own_lib_dir;
                static char own_lib_name[64];
                strncpy(own_lib_name, crate->name, sizeof(own_lib_name) - 1);
                link_libs[link_lib_count++] = own_lib_name;
            }

            // Dependency crate libraries
            for (int d = 0; d < crate->dep_count; d++) {
                int dep_idx = crate->dep_indices[d];
                if (dep_idx < 0 || dep_idx >= ws->crate_count) continue;
                const Crate* dep_crate = &ws->crates[dep_idx];
                if (!dep_crate->has_lib) continue;

                static char dep_lib_dirs[64][260];
                static char dep_lib_names[64][64];
                snprintf(dep_lib_dirs[d], sizeof(dep_lib_dirs[d]), "%s/build/%s/%s",
                         ws->root_path, config->profile, dep_crate->name);
                strncpy(dep_lib_names[d], dep_crate->name, sizeof(dep_lib_names[d]) - 1);
                lib_paths[lib_path_count++] = dep_lib_dirs[d];
                link_libs[link_lib_count++] = dep_lib_names[d];
            }

            // Platform link libraries
            for (int l = 0; l < crate->link_lib_count; l++) {
                link_libs[link_lib_count++] = crate->link_libs[l];
            }
        }
    }

    // Build LinkJob
    LinkJob link_job = {0};
    link_job.object_paths = obj_paths;
    link_job.object_count = obj_count;
    link_job.output_path = task->artifact_path;
    link_job.lib_paths = lib_paths;
    link_job.lib_path_count = lib_path_count;
    link_job.link_libs = link_libs;
    link_job.link_lib_count = link_lib_count;
    link_job.shared = (task->module_kind == MODULE_DYN);

    cdo_info("Linking %s: %s", module_kind_to_string(task->module_kind), task->artifact_path);
    int rc = compiler_link(&link_job, config->compiler);

    free(obj_paths);
    free(lib_paths);
    free(link_libs);

    if (rc != 0) {
        cdo_error("Linking failed for crate '%s', module %s/",
                  crate->name, module_kind_to_string(task->module_kind));
    }

    return rc;
}

// ---------------------------------------------------------------------------
// execute_hook_task: execute a pre/post build hook
// ---------------------------------------------------------------------------

static int execute_hook_task(DagTask* task, const DagSchedulerConfig* config) {
    if (!task->hook_def.present) {
        return 0;  // No hook defined, considered success
    }

    const Workspace* ws = config->workspace;

    // Build HookEnv
    HookEnv env = {0};
    env.ws_root = ws->root_path;
    env.profile = config->profile;

    char build_dir_buf[260];
    snprintf(build_dir_buf, sizeof(build_dir_buf), "%s/build/%s", ws->root_path, config->profile);
    env.build_dir = build_dir_buf;

    // Crate-level hooks get crate context
    if (task->crate_idx >= 0 && task->crate_idx < ws->crate_count) {
        const Crate* crate = &ws->crates[task->crate_idx];
        env.crate_name = crate->name;

        static char crate_abs[260];
        pal_path_join(crate_abs, sizeof(crate_abs), ws->root_path, crate->path);
        env.crate_path = crate_abs;

        static char crate_build[260];
        snprintf(crate_build, sizeof(crate_build), "%s/build/%s/%s",
                 ws->root_path, config->profile, crate->name);
        env.crate_build_dir = crate_build;
    }

    int rc = hook_execute(&task->hook_def, &env);
    if (rc != 0) {
        const char* lifecycle = (task->kind == DAG_TASK_HOOK_PRE) ? "pre-build" : "post-build";
        if (task->crate_idx >= 0 && task->crate_idx < ws->crate_count) {
            cdo_error("Crate '%s' %s hook failed",
                      ws->crates[task->crate_idx].name, lifecycle);
        } else {
            cdo_error("Workspace %s hook failed", lifecycle);
        }
    }
    return rc;
}

// ---------------------------------------------------------------------------
// execute_resource_task: copy resource files
// TODO: Connect to build_resource_module logic when DAG is fully integrated
// ---------------------------------------------------------------------------

static int execute_resource_task(DagTask* task, const DagSchedulerConfig* config) {
    (void)config;
    const Workspace* ws = config->workspace;
    if (task->crate_idx >= 0 && task->crate_idx < ws->crate_count) {
        cdo_debug("Resource task for crate '%s' (no-op in DAG scheduler, handled by integration)",
                  ws->crates[task->crate_idx].name);
    }
    // TODO: Implement resource copy logic when DAG path is wired into cmd_build
    return 0;
}

// ---------------------------------------------------------------------------
// execute_shader_task: compile shaders
// TODO: Connect to build_shader_module logic when DAG is fully integrated
// ---------------------------------------------------------------------------

static int execute_shader_task(DagTask* task, const DagSchedulerConfig* config) {
    (void)config;
    const Workspace* ws = config->workspace;
    if (task->crate_idx >= 0 && task->crate_idx < ws->crate_count) {
        cdo_debug("Shader task for crate '%s' (no-op in DAG scheduler, handled by integration)",
                  ws->crates[task->crate_idx].name);
    }
    // TODO: Implement shader compile logic when DAG path is wired into cmd_build
    return 0;
}
