// build_executable.cpp — BuildExecutable implementation.
// Constructs artifact vectors from Config and implements execute() stub.
// The execute() spawns the linker via PAL (stub returning 0 until process API is integrated).

#include "build/tasks/build_executable.h"
#include "core/log.h"

namespace cdo::build {

BuildExecutable::BuildExecutable(Config config, bool forced)
    : config_(std::move(config))
    , output_artifact_(config_.output_path, ArtifactType::Executable)
    , condition_(forced) {
    // Build object artifacts from input paths
    object_artifacts_.reserve(config_.object_paths.size());
    for (const auto& obj : config_.object_paths) {
        object_artifacts_.emplace_back(obj, ArtifactType::Object);
    }

    // Build inputs cache: all object files
    inputs_cache_.reserve(object_artifacts_.size());
    for (const auto& obj : object_artifacts_) {
        inputs_cache_.push_back(&obj);
    }

    // Build outputs cache: single executable output
    outputs_cache_.push_back(&output_artifact_);
}

const std::vector<const Artifact*>& BuildExecutable::inputs() const {
    return inputs_cache_;
}

const std::vector<const Artifact*>& BuildExecutable::outputs() const {
    return outputs_cache_;
}

const Artifact& BuildExecutable::primaryOutput() const {
    return output_artifact_;
}

const TaskCondition& BuildExecutable::condition() const {
    return condition_;
}

int BuildExecutable::execute() {
    // TODO: Spawn linker process via PAL when process API is integrated.
    // For now, return 0 (success stub). The actual implementation will build
    // the command line: <linker> <objects...> -o <output> -L<lib_paths> -l<link_libs> <extra_flags>
    cdo_log_debug("BuildExecutable::execute() stub for %s", config_.output_path.c_str());
    return 0;
}

} // namespace cdo::build
