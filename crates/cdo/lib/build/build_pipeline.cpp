/**
 * build_pipeline.cpp - BuildPipeline orchestrator and extern "C" entry point.
 *
 * Orchestrates the full build pipeline:
 *   1. Validate CLI arguments
 *   2. Load workspace model
 *   3. Handle --clean (delete build directory)
 *   4. Detect compiler toolchain
 *   5. Construct the TasksDag from workspace model
 *   6. Create RunnerPool and SHA256CacheLayer
 *   7. Dispatch loop: dag.waitNextTask → cache.tryRestore → pool.waitFreeRunner → runner.run
 *   8. After each runner completes: check result, markCompleted/markFailed
 *   9. Print summary with built/skipped/failed counts
 *
 * The extern "C" entry point cdo_build_run() is the ONLY interface between the
 * C and C++ layers for the build command.
 */

#include "build/build_pipeline.h"
#include "build/cli_arguments.h"
#include "build/dag_builder.h"
#include "build/runner.h"
#include "build/sha256_cache.h"
#include "build/task.h"
#include "build/tasks_dag.h"

extern "C" {
#include "core/log.h"
#include "core/compiler.h"
#include "model/workspace.h"
#include "pal/pal.h"
}

#include <string>
#include <vector>

namespace cdo::build {

// ---------------------------------------------------------------------------
// BuildPipeline implementation
// ---------------------------------------------------------------------------

BuildPipeline::BuildPipeline(const cli::Arguments& args) : args_(args) {}

int BuildPipeline::run() {
    // Step 1: Load workspace (basic load without crate resolution)
    ws_ = {};
    int rc = workspace_load(args_.workspaceRoot().c_str(), &ws_);
    if (rc != 0) {
        cdo_log_error("Failed to load workspace from: %s", args_.workspaceRoot().c_str());
        return 1;
    }

    // Step 2: Handle --clean (delete build/<profile>/ directory, then recreate)
    // This happens BEFORE crate resolution / DAG construction.
    if (args_.clean()) {
        char build_dir[520];
        pal_path_join(build_dir, sizeof(build_dir), args_.workspaceRoot().c_str(), "build");
        char profile_dir[560];
        pal_path_join(profile_dir, sizeof(profile_dir), build_dir, args_.profile().c_str());

        if (pal_path_exists(profile_dir) == 0) {
            cdo_log_info("Cleaning build directory: %s", profile_dir);
            pal_rmdir_r(profile_dir);
        }
        // Recreate the profile directory so subsequent steps have a valid output root
        pal_mkdir_p(profile_dir);
        cdo_log_debug("Recreated build directory: %s", profile_dir);
    }

    // Step 3: Resolve workspace build order with crate filter
    rc = loadWorkspace();
    if (rc != 0) {
        workspace_free(&ws_);
        return rc;
    }

    // Step 4: Build the DAG
    rc = buildDag();
    if (rc != 0) {
        workspace_free(&ws_);
        return rc;
    }

    // Step 5: Dispatch tasks to runners
    rc = dispatch();

    // Step 6: Print summary
    printSummary();

    workspace_free(&ws_);
    return rc;
}

int BuildPipeline::loadWorkspace() {
    // Resolve build order with crate filter
    const char* filter_ptrs[64] = {};
    int filter_count = 0;
    for (const auto& name : args_.crateFilter()) {
        if (filter_count < 64) {
            filter_ptrs[filter_count++] = name.c_str();
        }
    }

    int rc = workspace_resolve(&ws_, filter_count > 0 ? filter_ptrs : nullptr, filter_count);
    if (rc != 0) {
        cdo_log_error("Failed to resolve workspace build order");
        return 1;
    }

    // Check that at least one crate matched the filter
    if (filter_count > 0 && ws_.build_order_count == 0) {
        cdo_log_error("No crates matched the filter");
        return 1;
    }

    return 0;
}

int BuildPipeline::buildDag() {
    // Detect compiler toolchain
    CompilerInfo compiler = {};
    int rc = compiler_detect(&compiler);
    if (rc != 0) {
        cdo_log_error("No compiler detected");
        return 1;
    }

    // Configure DAG builder
    DagBuildConfig config;
    config.profile = args_.profile();
    config.force = args_.force();
    config.compiler_path = compiler.path;
    config.compiler_family = static_cast<int>(compiler.family);
    config.archiver_path = compiler.linker_path; // archiver resolved from linker path
    config.dxc_path = "";  // DXC path resolved separately if shaders are present

    // Build the DAG
    rc = build_dag(&ws_, config, dag_);
    if (rc != 0) {
        cdo_log_error("Failed to construct task DAG");
        return 1;
    }

    // Finalize: validate acyclicity and seed ready set
    rc = dag_.finalize();
    if (rc != 0) {
        cdo_log_error("Cycle detected in task DAG");
        return 1;
    }

    return 0;
}

int BuildPipeline::dispatch() {
    // If no tasks, return success immediately
    if (!dag_.hasActiveTask()) {
        return 0;
    }

    // Resolve jobs count
    int jobs = args_.jobs();
    if (jobs <= 0) {
        jobs = pal_cpu_count();
        if (jobs <= 0) jobs = 1;
    }
    cdo_log_debug("Dispatching with %d parallel job(s)", jobs);

    // Create runner pool and cache layer
    RunnerPool pool(jobs);
    SHA256CacheLayer cache({ args_.workspaceRoot() + "/.cdo/cache/objects/", args_.cacheEnabled() });

    // Track in-flight tasks for cache.store() after completion.
    // Key: task ID → Task pointer
    std::vector<Task*> dispatched_tasks;

    int result = 0;
    int in_flight = 0;

    while (dag_.hasActiveTask()) {
        // Drain: if all runners are busy, wait for one to finish and process its result
        if (in_flight >= pool.size()) {
            Runner& runner = pool.waitFreeRunner();
            in_flight--;
            int completed_id = runner.lastTaskId();
            if (runner.lastResult() != 0) {
                dag_.markFailed(completed_id);
                result = 1;
                break;
            }
            // Store successful result in cache
            for (auto& slot : dispatched_tasks) {
                if (slot && slot->id() == completed_id) {
                    if (!slot->wasSkipped()) {
                        cache.store(*slot);
                    }
                    slot = nullptr;
                    break;
                }
            }
            dag_.markCompleted(completed_id);
            continue; // Re-check hasActiveTask (completing may unblock new tasks)
        }

        // Get next ready task from DAG
        Task* task = dag_.waitNextTask();
        if (!task) break;

        // Try cache restore before execution
        if (cache.tryRestore(*task)) {
            cdo_log_debug("Cache hit: %s", task->primaryOutput().path().c_str());
            dag_.markCompleted(task->id());
            continue;
        }

        // Get a free runner and dispatch
        Runner& runner = pool.waitFreeRunner();
        dispatched_tasks.push_back(task);
        runner.run(*task);
        in_flight++;
    }

    // Drain all remaining in-flight runners
    while (in_flight > 0) {
        Runner& runner = pool.waitFreeRunner();
        in_flight--;
        int completed_id = runner.lastTaskId();
        if (runner.lastResult() != 0) {
            dag_.markFailed(completed_id);
            if (result == 0) result = 1;
        } else {
            // Store successful result in cache
            for (auto& slot : dispatched_tasks) {
                if (slot && slot->id() == completed_id) {
                    if (!slot->wasSkipped()) {
                        cache.store(*slot);
                    }
                    slot = nullptr;
                    break;
                }
            }
            dag_.markCompleted(completed_id);
        }
    }

    return result;
}

void BuildPipeline::printSummary() {
    int built = dag_.completedCount() - dag_.skippedCount();
    int skipped = dag_.skippedCount();
    int failed = dag_.failedCount();
    cdo_log_info("Build complete: %d built, %d skipped, %d failed", built, skipped, failed);
}

} // namespace cdo::build

// ---------------------------------------------------------------------------
// extern "C" entry point
// ---------------------------------------------------------------------------

extern "C" int cdo_build_run(const CliParseResult* result) {
    cdo::build::cli::Arguments args(result);
    if (!args.isValid()) {
        cdo_log_error("build: %s", args.lastError().c_str());
        return 1;
    }

    cdo::build::BuildPipeline pipeline(args);
    return pipeline.run();
}
