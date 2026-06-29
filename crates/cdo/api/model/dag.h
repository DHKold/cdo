#ifndef CDO_MODEL_DAG_H
#define CDO_MODEL_DAG_H

#include "model/module.h"
#include "model/hooks.h"

#ifdef __cplusplus
extern "C" {
#endif

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
/// Design decision: the DAG stores *what* to compile (source -> object),
/// the scheduler resolves *how* (include paths, defines, flags) at dispatch
/// time using workspace + compiler info. This keeps model/ pure.
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
    int*            compile_task_ids;   // IDs of compile tasks for this module (for link tasks)
    int             compile_task_count;

    // Hook task data
    HookDef         hook_def;

    // DAG edges
    int*            dep_ids;        // Tasks I depend on
    int             dep_count;
    int             dep_capacity;
    int*            rdep_ids;       // Tasks that depend on me (reverse edges, computed by finalize)
    int             rdep_count;
    int             rdep_capacity;

    // Scheduler state (mutated at runtime by scheduler)
    int             remaining_deps;
    DagTaskStatus   status;
} DagTask;

typedef struct {
    DagTask*    tasks;
    int         task_count;
    int         task_capacity;
} DagGraph;

// Forward declaration to avoid circular include with workspace.h
typedef struct Workspace Workspace;

/// Create an empty DAG graph with the given initial task capacity.
DagGraph* dag_graph_create(int initial_capacity);

/// Free all memory in a DAG graph (tasks, dep arrays, rdep arrays, graph itself).
void dag_graph_free(DagGraph* graph);

/// Finalize the graph: compute reverse edges (rdep_ids) and initial remaining_deps.
/// Must be called after all tasks and dependencies have been added.
void dag_graph_finalize(DagGraph* graph);

/// Generate the build DAG from a resolved workspace.
/// Pure function: reads workspace state, produces a graph. No side effects.
/// Caller must free the graph with dag_graph_free().
/// Returns 0 on success, non-zero on error.
int dag_generate(const Workspace* ws, const char* profile, DagGraph** out);

/// Get the count of tasks by kind (utility for logging).
int dag_graph_task_count_by_kind(const DagGraph* graph, DagTaskKind kind);

#ifdef __cplusplus
}
#endif

#endif // CDO_MODEL_DAG_H
