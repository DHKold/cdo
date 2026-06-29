#include "model/dag.h"
#include "model/workspace.h"
#include "model/module.h"
#include "pal/pal.h"
#include "commons/output.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// --- Internal helpers (static, used by dag_generate in Task 11) ---

/// Append a new task to the graph with the given kind, crate index, and module kind.
/// Returns the task ID (index in the tasks array).
static int dag_task_add(DagGraph* graph, DagTaskKind kind, int crate_idx, ModuleKind module_kind) {
    if (graph->task_count >= graph->task_capacity) {
        int new_cap = graph->task_capacity * 2;
        if (new_cap < 16) new_cap = 16;
        DagTask* new_tasks = (DagTask*)realloc(graph->tasks, (size_t)new_cap * sizeof(DagTask));
        if (!new_tasks) {
            cdo_error("dag_task_add: failed to grow task array to %d", new_cap);
            return -1;
        }
        graph->tasks = new_tasks;
        graph->task_capacity = new_cap;
    }

    int id = graph->task_count;
    DagTask* task = &graph->tasks[id];
    memset(task, 0, sizeof(DagTask));

    task->id = id;
    task->kind = kind;
    task->crate_idx = crate_idx;
    task->module_kind = module_kind;
    task->status = DAG_STATUS_PENDING;

    // Start dep arrays with small initial capacity
    task->dep_ids = NULL;
    task->dep_count = 0;
    task->dep_capacity = 0;
    task->rdep_ids = NULL;
    task->rdep_count = 0;
    task->rdep_capacity = 0;

    task->compile_task_ids = NULL;
    task->compile_task_count = 0;

    graph->task_count++;
    cdo_trace("dag_task_add: id=%d kind=%d crate_idx=%d module_kind=%d", id, kind, crate_idx, module_kind);
    return id;
}

/// Append dep_task_id to the dependency list of task_id.
/// Grows the dep_ids array if needed.
static void dag_task_add_dep(DagGraph* graph, int task_id, int dep_task_id) {
    if (task_id < 0 || task_id >= graph->task_count) {
        cdo_error("dag_task_add_dep: invalid task_id %d (count=%d)", task_id, graph->task_count);
        return;
    }
    if (dep_task_id < 0 || dep_task_id >= graph->task_count) {
        cdo_error("dag_task_add_dep: invalid dep_task_id %d (count=%d)", dep_task_id, graph->task_count);
        return;
    }

    DagTask* task = &graph->tasks[task_id];

    if (task->dep_count >= task->dep_capacity) {
        int new_cap = task->dep_capacity * 2;
        if (new_cap < 4) new_cap = 4;
        int* new_deps = (int*)realloc(task->dep_ids, (size_t)new_cap * sizeof(int));
        if (!new_deps) {
            cdo_error("dag_task_add_dep: failed to grow dep array for task %d", task_id);
            return;
        }
        task->dep_ids = new_deps;
        task->dep_capacity = new_cap;
    }

    task->dep_ids[task->dep_count] = dep_task_id;
    task->dep_count++;
    cdo_trace("dag_task_add_dep: task %d depends on task %d", task_id, dep_task_id);
}

// --- Internal helper: add reverse edge ---

static void dag_task_add_rdep(DagTask* task, int rdep_task_id) {
    if (task->rdep_count >= task->rdep_capacity) {
        int new_cap = task->rdep_capacity * 2;
        if (new_cap < 4) new_cap = 4;
        int* new_rdeps = (int*)realloc(task->rdep_ids, (size_t)new_cap * sizeof(int));
        if (!new_rdeps) {
            cdo_error("dag_task_add_rdep: failed to grow rdep array for task %d", task->id);
            return;
        }
        task->rdep_ids = new_rdeps;
        task->rdep_capacity = new_cap;
    }
    task->rdep_ids[task->rdep_count] = rdep_task_id;
    task->rdep_count++;
}

// --- Public API ---

DagGraph* dag_graph_create(int initial_capacity) {
    if (initial_capacity < 1) initial_capacity = 16;

    DagGraph* graph = (DagGraph*)malloc(sizeof(DagGraph));
    if (!graph) {
        cdo_error("dag_graph_create: failed to allocate DagGraph");
        return NULL;
    }

    graph->tasks = (DagTask*)malloc((size_t)initial_capacity * sizeof(DagTask));
    if (!graph->tasks) {
        cdo_error("dag_graph_create: failed to allocate task array (capacity=%d)", initial_capacity);
        free(graph);
        return NULL;
    }

    graph->task_count = 0;
    graph->task_capacity = initial_capacity;

    cdo_debug("dag_graph_create: allocated graph with capacity %d", initial_capacity);
    return graph;
}

void dag_graph_free(DagGraph* graph) {
    if (!graph) return;

    for (int i = 0; i < graph->task_count; i++) {
        DagTask* task = &graph->tasks[i];
        free(task->dep_ids);
        free(task->rdep_ids);
        free(task->compile_task_ids);
    }

    free(graph->tasks);
    free(graph);
    cdo_trace("dag_graph_free: freed graph");
}

void dag_graph_finalize(DagGraph* graph) {
    if (!graph) return;

    cdo_debug("dag_graph_finalize: computing reverse edges for %d tasks", graph->task_count);

    // For each task, iterate its dep_ids and add reverse edges to the dependent tasks
    for (int i = 0; i < graph->task_count; i++) {
        DagTask* task = &graph->tasks[i];
        for (int d = 0; d < task->dep_count; d++) {
            int dep_id = task->dep_ids[d];
            dag_task_add_rdep(&graph->tasks[dep_id], i);
        }
    }

    // Set remaining_deps and mark tasks with 0 deps as READY
    int ready_count = 0;
    for (int i = 0; i < graph->task_count; i++) {
        DagTask* task = &graph->tasks[i];
        task->remaining_deps = task->dep_count;
        if (task->remaining_deps == 0) {
            task->status = DAG_STATUS_READY;
            ready_count++;
        }
    }

    cdo_debug("dag_graph_finalize: done. %d tasks ready (0 deps), %d total tasks", ready_count, graph->task_count);
}

int dag_graph_task_count_by_kind(const DagGraph* graph, DagTaskKind kind) {
    if (!graph) return 0;

    int count = 0;
    for (int i = 0; i < graph->task_count; i++) {
        if (graph->tasks[i].kind == kind) {
            count++;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// dag_generate — Build the DAG from a resolved workspace
// ---------------------------------------------------------------------------

/// Compute object path from a source file: takes the basename, replaces extension with .o,
/// and places it in obj_dir. This is a local helper mirroring the logic in cmd_build_util.c
/// (model/ layer cannot include commands/).
static void dag_object_path_from_source(const char* source, const char* obj_dir, char* out, size_t out_size) {
    // Extract just the filename from the source path
    const char* filename = source;
    const char* p = source;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            filename = p + 1;
        }
        p++;
    }

    // Build object filename: replace extension with .o
    char obj_name[260];
    size_t name_len = strlen(filename);
    const char* ext = pal_path_ext(filename);
    size_t base_len = (ext && ext[0]) ? (size_t)(ext - filename) : name_len;

    snprintf(obj_name, sizeof(obj_name), "%.*s.o", (int)base_len, filename);
    pal_path_join(out, out_size, obj_dir, obj_name);
}

/// Check if a source file has a compilable extension (.c, .cpp, .cxx, .cc).
static bool dag_is_compilable_source(const char* path) {
    const char* ext = pal_path_ext(path);
    if (!ext) return false;
    return (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0 ||
            strcmp(ext, ".cxx") == 0 || strcmp(ext, ".cc") == 0);
}

int dag_generate(const Workspace* ws, const char* profile, DagGraph** out) {
    if (!ws || !profile || !out) {
        cdo_error("dag_generate: NULL argument (ws=%p, profile=%p, out=%p)", (const void*)ws, (const void*)profile, (void*)out);
        return -1;
    }

    // Estimate capacity: rough heuristic based on crate count
    int estimated_capacity = ws->crate_count * 16;
    if (estimated_capacity < 32) estimated_capacity = 32;

    DagGraph* graph = dag_graph_create(estimated_capacity);
    if (!graph) {
        cdo_error("dag_generate: failed to create graph");
        return -1;
    }

    // Mapping: crate_idx -> lib link task ID (-1 if none)
    int* lib_link_ids = (int*)malloc((size_t)ws->crate_count * sizeof(int));
    if (!lib_link_ids) {
        cdo_error("dag_generate: failed to allocate lib_link_ids array");
        dag_graph_free(graph);
        return -1;
    }
    for (int i = 0; i < ws->crate_count; i++) {
        lib_link_ids[i] = -1;
    }

    // Track crate post-hook task IDs for the workspace post-hook
    int* crate_post_hook_ids = (int*)malloc((size_t)ws->crate_count * sizeof(int));
    if (!crate_post_hook_ids) {
        cdo_error("dag_generate: failed to allocate crate_post_hook_ids array");
        free(lib_link_ids);
        dag_graph_free(graph);
        return -1;
    }
    int crate_post_hook_count = 0;

    // -----------------------------------------------------------------------
    // Phase 1: Workspace pre-build hook
    // -----------------------------------------------------------------------
    int ws_pre_hook_id = -1;
    if (ws->ws_hooks.hooks[HOOK_PRE_BUILD].present) {
        ws_pre_hook_id = dag_task_add(graph, DAG_TASK_HOOK_PRE, -1, MODULE_API);
        if (ws_pre_hook_id >= 0) {
            graph->tasks[ws_pre_hook_id].hook_def = ws->ws_hooks.hooks[HOOK_PRE_BUILD];
            cdo_debug("dag_generate: added workspace pre-build hook (task %d)", ws_pre_hook_id);
        }
    }

    // -----------------------------------------------------------------------
    // Phase 2: For each crate in build order
    // -----------------------------------------------------------------------
    for (int bo = 0; bo < ws->build_order_count; bo++) {
        int crate_idx = ws->build_order[bo];
        const Crate* crate = &ws->crates[crate_idx];

        cdo_debug("dag_generate: processing crate '%s' (idx=%d)", crate->name, crate_idx);

        // Track all tasks created for this crate (for post-hook dependencies)
        int* crate_task_ids = (int*)malloc(256 * sizeof(int));
        int crate_task_count = 0;
        int crate_task_capacity = 256;

        #define CRATE_TASK_PUSH(tid) do { \
            if ((tid) >= 0) { \
                if (crate_task_count >= crate_task_capacity) { \
                    crate_task_capacity *= 2; \
                    int* _tmp = (int*)realloc(crate_task_ids, (size_t)crate_task_capacity * sizeof(int)); \
                    if (_tmp) crate_task_ids = _tmp; \
                } \
                if (crate_task_count < crate_task_capacity) { \
                    crate_task_ids[crate_task_count++] = (tid); \
                } \
            } \
        } while(0)

        // --- 2a: Crate pre-build hook ---
        int crate_pre_id = -1;
        if (crate->hooks.hooks[HOOK_PRE_BUILD].present) {
            crate_pre_id = dag_task_add(graph, DAG_TASK_HOOK_PRE, crate_idx, MODULE_API);
            if (crate_pre_id >= 0) {
                graph->tasks[crate_pre_id].hook_def = crate->hooks.hooks[HOOK_PRE_BUILD];
                if (ws_pre_hook_id >= 0) {
                    dag_task_add_dep(graph, crate_pre_id, ws_pre_hook_id);
                }
                CRATE_TASK_PUSH(crate_pre_id);
                cdo_trace("dag_generate: crate '%s' pre-hook task %d", crate->name, crate_pre_id);
            }
        }

        // Compute the object directory for each module: build/<profile>/<crate_name>/<kind>/
        // We'll compute these on-demand per module kind below.

        // --- 2b: lib/ compile tasks ---
        int* lib_compile_ids_arr = NULL;
        int lib_compile_count = 0;
        int lib_link_id = -1;

        if (crate->has_lib && crate->modules[MODULE_LIB].present && crate->modules[MODULE_LIB].sources.count > 0) {
            const Module* lib_mod = &crate->modules[MODULE_LIB];

            // Compute obj directory: build/<profile>/<crate_name>/lib/
            char obj_dir[260];
            {
                char tmp1[260], tmp2[260], tmp3[260];
                pal_path_join(tmp1, sizeof(tmp1), ws->root_path, "build");
                pal_path_join(tmp2, sizeof(tmp2), tmp1, profile);
                pal_path_join(tmp3, sizeof(tmp3), tmp2, crate->name);
                pal_path_join(obj_dir, sizeof(obj_dir), tmp3, "lib");
            }

            lib_compile_ids_arr = (int*)malloc((size_t)lib_mod->sources.count * sizeof(int));
            if (!lib_compile_ids_arr) {
                cdo_error("dag_generate: failed to allocate lib compile ids for crate '%s'", crate->name);
                free(crate_task_ids);
                free(lib_link_ids);
                free(crate_post_hook_ids);
                dag_graph_free(graph);
                return -1;
            }

            for (int s = 0; s < lib_mod->sources.count; s++) {
                const char* src_path = lib_mod->sources.paths[s];
                if (!dag_is_compilable_source(src_path)) continue;

                int task_id = dag_task_add(graph, DAG_TASK_COMPILE, crate_idx, MODULE_LIB);
                if (task_id < 0) continue;

                DagTask* task = &graph->tasks[task_id];
                strncpy(task->source_path, src_path, sizeof(task->source_path) - 1);
                task->source_path[sizeof(task->source_path) - 1] = '\0';

                dag_object_path_from_source(src_path, obj_dir, task->object_path, sizeof(task->object_path));

                if (crate_pre_id >= 0) {
                    dag_task_add_dep(graph, task_id, crate_pre_id);
                }

                lib_compile_ids_arr[lib_compile_count++] = task_id;
                CRATE_TASK_PUSH(task_id);
            }

            cdo_debug("dag_generate: crate '%s' lib/ -> %d compile tasks", crate->name, lib_compile_count);
        }

        // --- 2c: lib/ link task ---
        if (crate->has_lib && crate->modules[MODULE_LIB].present) {
            // Compute artifact path for lib/
            char artifact_path[260];
            char obj_dir_unused[260];
            int rc = module_compute_artifact_path(ws->root_path, crate->name, MODULE_LIB, profile, artifact_path, sizeof(artifact_path), obj_dir_unused, sizeof(obj_dir_unused));

            lib_link_id = dag_task_add(graph, DAG_TASK_LINK, crate_idx, MODULE_LIB);
            if (lib_link_id >= 0) {
                DagTask* link_task = &graph->tasks[lib_link_id];
                if (rc == 0) {
                    strncpy(link_task->artifact_path, artifact_path, sizeof(link_task->artifact_path) - 1);
                    link_task->artifact_path[sizeof(link_task->artifact_path) - 1] = '\0';
                }

                // Depends on all lib/ compile tasks
                for (int c = 0; c < lib_compile_count; c++) {
                    dag_task_add_dep(graph, lib_link_id, lib_compile_ids_arr[c]);
                }

                // Store compile_task_ids on the link task
                if (lib_compile_count > 0) {
                    link_task->compile_task_ids = (int*)malloc((size_t)lib_compile_count * sizeof(int));
                    if (link_task->compile_task_ids) {
                        memcpy(link_task->compile_task_ids, lib_compile_ids_arr, (size_t)lib_compile_count * sizeof(int));
                        link_task->compile_task_count = lib_compile_count;
                    }
                }

                // Depends on upstream lib/ link tasks
                for (int d = 0; d < crate->dep_count; d++) {
                    int dep_idx = crate->dep_indices[d];
                    if (dep_idx >= 0 && dep_idx < ws->crate_count && lib_link_ids[dep_idx] >= 0) {
                        dag_task_add_dep(graph, lib_link_id, lib_link_ids[dep_idx]);
                    }
                }

                lib_link_ids[crate_idx] = lib_link_id;
                CRATE_TASK_PUSH(lib_link_id);
                cdo_debug("dag_generate: crate '%s' lib/ link task %d (artifact: %s)", crate->name, lib_link_id, link_task->artifact_path);
            }
        }

        free(lib_compile_ids_arr);
        lib_compile_ids_arr = NULL;

        // --- 2d: exe/, dyn/, tst/ compile + link tasks ---
        ModuleKind secondary_kinds[] = { MODULE_EXE, MODULE_DYN, MODULE_TST };
        for (int mk = 0; mk < 3; mk++) {
            ModuleKind mod_kind = secondary_kinds[mk];
            const Module* mod = &crate->modules[mod_kind];

            if (!mod->present || mod->sources.count == 0) continue;

            // Compute obj directory: build/<profile>/<crate_name>/<kind>/
            const char* kind_str = module_kind_to_string(mod_kind);
            char obj_dir[260];
            {
                char tmp1[260], tmp2[260], tmp3[260];
                pal_path_join(tmp1, sizeof(tmp1), ws->root_path, "build");
                pal_path_join(tmp2, sizeof(tmp2), tmp1, profile);
                pal_path_join(tmp3, sizeof(tmp3), tmp2, crate->name);
                pal_path_join(obj_dir, sizeof(obj_dir), tmp3, kind_str);
            }

            int* mod_compile_ids = (int*)malloc((size_t)mod->sources.count * sizeof(int));
            int mod_compile_count = 0;
            if (!mod_compile_ids) {
                cdo_error("dag_generate: failed to allocate compile ids for crate '%s' module %s", crate->name, kind_str);
                continue;
            }

            for (int s = 0; s < mod->sources.count; s++) {
                const char* src_path = mod->sources.paths[s];
                if (!dag_is_compilable_source(src_path)) continue;

                int task_id = dag_task_add(graph, DAG_TASK_COMPILE, crate_idx, mod_kind);
                if (task_id < 0) continue;

                DagTask* task = &graph->tasks[task_id];
                strncpy(task->source_path, src_path, sizeof(task->source_path) - 1);
                task->source_path[sizeof(task->source_path) - 1] = '\0';

                dag_object_path_from_source(src_path, obj_dir, task->object_path, sizeof(task->object_path));

                // Depends on own lib/ link (if present)
                if (lib_link_id >= 0) {
                    dag_task_add_dep(graph, task_id, lib_link_id);
                }
                // Depends on crate pre-hook
                if (crate_pre_id >= 0) {
                    dag_task_add_dep(graph, task_id, crate_pre_id);
                }

                mod_compile_ids[mod_compile_count++] = task_id;
                CRATE_TASK_PUSH(task_id);
            }

            cdo_debug("dag_generate: crate '%s' %s/ -> %d compile tasks", crate->name, kind_str, mod_compile_count);

            // Module link task
            char artifact_path[260];
            char obj_dir_unused[260];
            int rc = module_compute_artifact_path(ws->root_path, crate->name, mod_kind, profile, artifact_path, sizeof(artifact_path), obj_dir_unused, sizeof(obj_dir_unused));

            int mod_link_id = dag_task_add(graph, DAG_TASK_LINK, crate_idx, mod_kind);
            if (mod_link_id >= 0) {
                DagTask* link_task = &graph->tasks[mod_link_id];
                if (rc == 0) {
                    strncpy(link_task->artifact_path, artifact_path, sizeof(link_task->artifact_path) - 1);
                    link_task->artifact_path[sizeof(link_task->artifact_path) - 1] = '\0';
                }

                // Depends on all module compile tasks
                for (int c = 0; c < mod_compile_count; c++) {
                    dag_task_add_dep(graph, mod_link_id, mod_compile_ids[c]);
                }

                // Store compile_task_ids on the link task
                if (mod_compile_count > 0) {
                    link_task->compile_task_ids = (int*)malloc((size_t)mod_compile_count * sizeof(int));
                    if (link_task->compile_task_ids) {
                        memcpy(link_task->compile_task_ids, mod_compile_ids, (size_t)mod_compile_count * sizeof(int));
                        link_task->compile_task_count = mod_compile_count;
                    }
                }

                // Depends on own lib/ link (if present)
                if (lib_link_id >= 0) {
                    dag_task_add_dep(graph, mod_link_id, lib_link_id);
                }

                // Depends on upstream lib/ link tasks
                for (int d = 0; d < crate->dep_count; d++) {
                    int dep_idx = crate->dep_indices[d];
                    if (dep_idx >= 0 && dep_idx < ws->crate_count && lib_link_ids[dep_idx] >= 0) {
                        dag_task_add_dep(graph, mod_link_id, lib_link_ids[dep_idx]);
                    }
                }

                CRATE_TASK_PUSH(mod_link_id);
                cdo_debug("dag_generate: crate '%s' %s/ link task %d (artifact: %s)", crate->name, kind_str, mod_link_id, link_task->artifact_path);
            }

            free(mod_compile_ids);
        }

        // --- 2e: shd/ task ---
        if (crate->has_shd && crate->modules[MODULE_SHD].present) {
            int shd_id = dag_task_add(graph, DAG_TASK_SHADER, crate_idx, MODULE_SHD);
            if (shd_id >= 0) {
                if (crate_pre_id >= 0) {
                    dag_task_add_dep(graph, shd_id, crate_pre_id);
                }
                CRATE_TASK_PUSH(shd_id);
                cdo_trace("dag_generate: crate '%s' shader task %d", crate->name, shd_id);
            }
        }

        // --- 2f: res/ task ---
        if (crate->has_res && crate->modules[MODULE_RES].present) {
            int res_id = dag_task_add(graph, DAG_TASK_RESOURCE, crate_idx, MODULE_RES);
            if (res_id >= 0) {
                if (crate_pre_id >= 0) {
                    dag_task_add_dep(graph, res_id, crate_pre_id);
                }
                CRATE_TASK_PUSH(res_id);
                cdo_trace("dag_generate: crate '%s' resource task %d", crate->name, res_id);
            }
        }

        // --- 2g: Crate post-build hook ---
        if (crate->hooks.hooks[HOOK_POST_BUILD].present) {
            int crate_post_id = dag_task_add(graph, DAG_TASK_HOOK_POST, crate_idx, MODULE_API);
            if (crate_post_id >= 0) {
                graph->tasks[crate_post_id].hook_def = crate->hooks.hooks[HOOK_POST_BUILD];

                // Depends on ALL tasks created for this crate
                for (int t = 0; t < crate_task_count; t++) {
                    dag_task_add_dep(graph, crate_post_id, crate_task_ids[t]);
                }

                crate_post_hook_ids[crate_post_hook_count++] = crate_post_id;
                cdo_trace("dag_generate: crate '%s' post-hook task %d (depends on %d crate tasks)", crate->name, crate_post_id, crate_task_count);
            }
        }

        #undef CRATE_TASK_PUSH
        free(crate_task_ids);
    }

    // -----------------------------------------------------------------------
    // Phase 3: Workspace post-build hook
    // -----------------------------------------------------------------------
    if (ws->ws_hooks.hooks[HOOK_POST_BUILD].present) {
        int ws_post_id = dag_task_add(graph, DAG_TASK_HOOK_POST, -1, MODULE_API);
        if (ws_post_id >= 0) {
            graph->tasks[ws_post_id].hook_def = ws->ws_hooks.hooks[HOOK_POST_BUILD];

            // Depends on all crate post-hooks
            for (int i = 0; i < crate_post_hook_count; i++) {
                dag_task_add_dep(graph, ws_post_id, crate_post_hook_ids[i]);
            }
            cdo_debug("dag_generate: workspace post-hook task %d (depends on %d crate post-hooks)", ws_post_id, crate_post_hook_count);
        }
    }

    // -----------------------------------------------------------------------
    // Phase 4: Finalize
    // -----------------------------------------------------------------------
    dag_graph_finalize(graph);

    free(lib_link_ids);
    free(crate_post_hook_ids);

    cdo_info("DAG generated: %d tasks, %d compile, %d link", graph->task_count, dag_graph_task_count_by_kind(graph, DAG_TASK_COMPILE), dag_graph_task_count_by_kind(graph, DAG_TASK_LINK));

    *out = graph;
    return 0;
}
