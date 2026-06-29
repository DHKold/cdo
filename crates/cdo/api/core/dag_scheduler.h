#ifndef CDO_CORE_DAG_SCHEDULER_H
#define CDO_CORE_DAG_SCHEDULER_H

#include "model/dag.h"
#include "model/workspace.h"
#include "core/compiler.h"
#include "core/cache.h"
#include "commons/output.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Configuration for the DAG scheduler.
/// Contains everything the scheduler needs to execute tasks: compiler info,
/// cache settings, progress reporting, and workspace context.
/// The DAG stores *what* to build; this config provides the *how*.
typedef struct {
    const Workspace*    workspace;
    const CompilerInfo* compiler;
    const CacheConfig*  cache_config;
    CacheStats*         cache_stats;
    bool                no_cache;
    int                 jobs;
    const char*         profile;
    ProgressBar*        progress;
    int                 total_compile_units;
} DagSchedulerConfig;

/// Execute all tasks in the DAG graph using the provided configuration.
/// Dispatches ready tasks (remaining_deps == 0) in deterministic order,
/// handles failures and cancellation of dependent tasks.
///
/// Serial implementation: processes one task at a time from the ready queue,
/// sorted by (crate_idx, module_kind, source_path) for deterministic ordering.
///
/// Returns 0 if all tasks succeeded, non-zero if any failed.
int dag_scheduler_run(DagGraph* graph, const DagSchedulerConfig* config);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_DAG_SCHEDULER_H
