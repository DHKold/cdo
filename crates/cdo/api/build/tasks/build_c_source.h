/**
 * build_c_source.h - Concrete task for compiling C source files.
 *
 * BuildCSource compiles a .c file into a .o object file using the detected
 * compiler. It produces both the object file (primary output) and a .d
 * dependency file (secondary output) listing header dependencies.
 *
 * Part of the cdo::build pipeline refactor.
 */
#ifndef CDO_BUILD_TASKS_BUILD_C_SOURCE_H
#define CDO_BUILD_TASKS_BUILD_C_SOURCE_H

#include "build/task.h"
#include "build/artifact.h"
#include "build/condition.h"

#include <string>
#include <vector>

namespace cdo::build {

/// Compiles a C source file into an object file (.o).
/// Produces both the .o (primary output used for freshness) and the .d
/// dependency file (secondary output, build metadata side-effect).
/// Header dependencies from a previous .d file are included as inputs
/// so that header changes trigger recompilation.
class BuildCSource : public Task {
public:
    struct Config {
        std::string source_path;
        std::string object_path;                    // Primary output (.o)
        std::string depfile_path;                   // Secondary output (.d)
        std::vector<std::string> include_paths;
        std::vector<std::string> defines;
        std::string c_standard;                     // e.g. "c17"
        std::vector<std::string> extra_flags;
        bool optimize = false;
        bool debug_info = false;
        std::string compiler_path;
        int compiler_family = 0;                    // CompilerFamily enum value
        std::vector<std::string> header_deps;       // Parsed from .d file
    };

    explicit BuildCSource(Config config, bool forced = false);

    const std::vector<const Artifact*>& inputs() const override;
    const std::vector<const Artifact*>& outputs() const override;
    const Artifact& primaryOutput() const override;
    const TaskCondition& condition() const override;

protected:
    int execute() override;

private:
    Config config_;
    FileArtifact source_artifact_;
    FileArtifact output_artifact_;                  // .o (primary)
    FileArtifact depfile_artifact_;                 // .d (secondary)
    std::vector<FileArtifact> header_artifacts_;
    std::vector<const Artifact*> inputs_cache_;
    std::vector<const Artifact*> outputs_cache_;
    FreshnessCondition condition_;
};

} // namespace cdo::build

#endif // CDO_BUILD_TASKS_BUILD_C_SOURCE_H
