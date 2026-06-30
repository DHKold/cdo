// build_static_library.cpp — BuildStaticLibrary implementation.
// Constructs artifact vectors from Config and implements execute() stub.
// The execute() spawns the archiver (ar/lib.exe) via PAL (stub returning 0 until process API is integrated).

#include "build/tasks/build_static_library.h"
#include "core/log.h"

namespace cdo::build {

BuildStaticLibrary::BuildStaticLibrary(Config config, bool forced)
    : config_(std::move(config))
    , output_artifact_(config_.output_path, ArtifactType::StaticLibrary)
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

    // Build outputs cache: single library output
    outputs_cache_.push_back(&output_artifact_);
}

const std::vector<const Artifact*>& BuildStaticLibrary::inputs() const {
    return inputs_cache_;
}

const std::vector<const Artifact*>& BuildStaticLibrary::outputs() const {
    return outputs_cache_;
}

const Artifact& BuildStaticLibrary::primaryOutput() const {
    return output_artifact_;
}

const TaskCondition& BuildStaticLibrary::condition() const {
    return condition_;
}

int BuildStaticLibrary::execute() {
    // TODO: Spawn archiver process via PAL when process API is integrated.
    // For now, return 0 (success stub). The actual implementation will build
    // the command line: ar rcs <output> <object_files...>  (POSIX)
    //              or:  lib.exe /OUT:<output> <object_files...>  (MSVC)
    cdo_log_debug("BuildStaticLibrary::execute() stub for %s", config_.output_path.c_str());
    return 0;
}

} // namespace cdo::build
