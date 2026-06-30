/**
 * build_executable.h - Concrete task for linking object files into an executable.
 *
 * BuildExecutable links .o files and libraries into an executable binary
 * using the detected linker.
 *
 * Part of the cdo::build pipeline refactor.
 */
#ifndef CDO_BUILD_TASKS_BUILD_EXECUTABLE_H
#define CDO_BUILD_TASKS_BUILD_EXECUTABLE_H

#include "build/task.h"
#include "build/artifact.h"
#include "build/condition.h"

#include <string>
#include <vector>

namespace cdo::build {

/// Links object files (.o) and libraries into an executable binary.
/// Produces a single output: the executable file.
/// Inputs are all object files that contribute to the link step.
class BuildExecutable : public Task {
public:
    struct Config {
        std::vector<std::string> object_paths;
        std::string output_path;
        std::vector<std::string> lib_paths;         // Library search paths (-L)
        std::vector<std::string> link_libs;         // Libraries to link (-l)
        std::vector<std::string> extra_flags;
        std::string linker_path;
        int compiler_family = 0;                    // CompilerFamily enum value
        bool debug_info = false;
    };

    explicit BuildExecutable(Config config, bool forced = false);

    const std::vector<const Artifact*>& inputs() const override;
    const std::vector<const Artifact*>& outputs() const override;
    const Artifact& primaryOutput() const override;
    const TaskCondition& condition() const override;

protected:
    int execute() override;

private:
    Config config_;
    std::vector<FileArtifact> object_artifacts_;
    FileArtifact output_artifact_;
    std::vector<const Artifact*> inputs_cache_;
    std::vector<const Artifact*> outputs_cache_;
    FreshnessCondition condition_;
};

} // namespace cdo::build

#endif // CDO_BUILD_TASKS_BUILD_EXECUTABLE_H
