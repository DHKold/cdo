/**
 * build_static_library.h - Concrete task for archiving object files into a static library.
 *
 * BuildStaticLibrary archives .o files into a .lib (Windows) or .a (POSIX)
 * static library using the detected archiver (ar or lib.exe).
 *
 * Part of the cdo::build pipeline refactor.
 */
#ifndef CDO_BUILD_TASKS_BUILD_STATIC_LIBRARY_H
#define CDO_BUILD_TASKS_BUILD_STATIC_LIBRARY_H

#include "build/task.h"
#include "build/artifact.h"
#include "build/condition.h"

#include <string>
#include <vector>

namespace cdo::build {

/// Archives object files (.o) into a static library (.lib/.a).
/// Produces a single output: the static library file.
/// Inputs are all object files that should be included in the archive.
class BuildStaticLibrary : public Task {
public:
    struct Config {
        std::vector<std::string> object_paths;
        std::string output_path;
        std::string archiver_path;                  // ar or lib.exe
        int compiler_family = 0;                    // CompilerFamily enum value
    };

    explicit BuildStaticLibrary(Config config, bool forced = false);

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

#endif // CDO_BUILD_TASKS_BUILD_STATIC_LIBRARY_H
