/**
 * dag_builder.h - Constructs a TasksDag from the workspace model.
 *
 * Provides a single generic algorithm that iterates crates in dependency order,
 * processes modules within each crate, and creates the appropriate task types
 * and dependency edges. Module-specific differences (artifact type, link flags,
 * implicit deps) are expressed as parameters, not code paths.
 *
 * Part of the cdo::build pipeline refactor.
 */
#ifndef CDO_BUILD_DAG_BUILDER_H
#define CDO_BUILD_DAG_BUILDER_H

#include "build/tasks_dag.h"

#include <string>

#ifdef __cplusplus
extern "C" {
#endif
#include "model/workspace.h"
#ifdef __cplusplus
}
#endif

namespace cdo::build {

/// Configuration for DAG construction, extracted from CLI arguments.
struct DagBuildConfig {
    std::string profile;            // "debug", "release", "relwithdebinfo"
    bool force = false;             // --force: always rebuild
    std::string compiler_path;      // Detected compiler path
    int compiler_family = 0;        // CompilerFamily enum value
    std::string archiver_path;      // ar or lib.exe
    std::string dxc_path;           // DXC path for shader compilation
};

/// Build a TasksDag from a workspace model.
///
/// Iterates crates in build order, processing each module according to its kind:
///   - lib/ → BuildCSource/BuildCppSource per file + BuildStaticLibrary
///   - exe/ → BuildCSource/BuildCppSource per file + BuildExecutable
///   - tst/ → BuildCSource/BuildCppSource per file + BuildExecutable (links cdo_ut)
///   - e2e/ → BuildCSource/BuildCppSource per file + BuildExecutable (links cdo_e2e)
///   - dyn/ → BuildCSource/BuildCppSource per file + BuildSharedLibrary
///   - shd/ → CompileHlslShader per file (no link task)
///
/// Dependency edges:
///   - Compile tasks within a module are leaf tasks (no internal deps)
///   - Link/archive tasks depend on all compile tasks within the same module
///   - exe/tst/e2e/dyn link tasks depend on the lib archive task of the same crate
///   - Inter-crate: if crate A depends on crate B, A's link tasks depend on B's lib archive
///
/// No filesystem operations (freshness checks, directory creation) are performed
/// during construction. The DAG is purely structural.
///
/// @param workspace  Pointer to a loaded Workspace (with build_order resolved)
/// @param config     Build configuration (profile, force, tool paths)
/// @param dag        Output TasksDag to populate (must be empty)
/// @return 0 on success, non-zero on error (e.g., invalid workspace)
int build_dag(const Workspace* workspace, const DagBuildConfig& config, TasksDag& dag);

} // namespace cdo::build

#endif // CDO_BUILD_DAG_BUILDER_H
