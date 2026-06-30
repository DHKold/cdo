/**
 * build_pipeline.h - Build pipeline entry point and orchestrator.
 *
 * Exposes an extern "C" entry point callable from existing C code (cmd_build.c)
 * to invoke the C++ build pipeline. This is the ONLY interface between the C
 * and C++ layers for the build command.
 *
 * Internally defines the BuildPipeline class which orchestrates the full build:
 * workspace loading, DAG construction, parallel dispatch, and summary reporting.
 */
#ifndef CDO_BUILD_PIPELINE_H
#define CDO_BUILD_PIPELINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cmd/cli_cmd.h"

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include "build/cli_arguments.h"
#include "build/tasks_dag.h"

extern "C" {
#include "model/workspace.h"
}

/// Entry point from C code into the C++ build pipeline.
/// Accepts the CliParseResult directly from the cdo_cli framework.
/// Returns 0 on success, non-zero on failure.
extern "C" int cdo_build_run(const CliParseResult* result);

namespace cdo::build {

/// Orchestrates the full build pipeline: loads the workspace model, constructs
/// the task DAG, dispatches tasks to the runner pool, and prints a summary.
class BuildPipeline {
public:
    /// Construct the pipeline from validated CLI arguments.
    explicit BuildPipeline(const cli::Arguments& args);

    /// Execute the full build pipeline.
    /// Steps: loadWorkspace → handle --clean → buildDag → dispatch → printSummary.
    /// Returns 0 on success, non-zero on failure.
    int run();

private:
    /// Load the workspace model from cdo.toml and resolve crate dependencies.
    int loadWorkspace();

    /// Construct the TasksDag from the loaded workspace model.
    int buildDag();

    /// Run the main dispatch loop: pull ready tasks from the DAG and assign to runners.
    int dispatch();

    /// Emit the final summary log line with built/skipped/failed counts.
    void printSummary();

    const cli::Arguments& args_;
    Workspace ws_;
    TasksDag dag_;
};

} // namespace cdo::build

#else

/* Pure C declaration for the entry point */
int cdo_build_run(const CliParseResult* result);

#endif /* __cplusplus */

#endif /* CDO_BUILD_PIPELINE_H */
