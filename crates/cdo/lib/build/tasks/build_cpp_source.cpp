// build_cpp_source.cpp — BuildCppSource implementation.
// Constructs artifact vectors from Config and implements execute() stub.
// The execute() spawns the C++ compiler via PAL (stub returning 0 until process API is integrated).

#include "build/tasks/build_cpp_source.h"
#include "core/log.h"

namespace cdo::build {

BuildCppSource::BuildCppSource(Config config, bool forced)
    : config_(std::move(config))
    , source_artifact_(config_.source_path, ArtifactType::Source)
    , output_artifact_(config_.object_path, ArtifactType::Object)
    , depfile_artifact_(config_.depfile_path, ArtifactType::DepFile)
    , condition_(forced) {
    // Build header artifacts from parsed .d file dependencies
    header_artifacts_.reserve(config_.header_deps.size());
    for (const auto& hdr : config_.header_deps) {
        header_artifacts_.emplace_back(hdr, ArtifactType::Header);
    }

    // Build inputs cache: source + all headers
    inputs_cache_.reserve(1 + header_artifacts_.size());
    inputs_cache_.push_back(&source_artifact_);
    for (const auto& hdr : header_artifacts_) {
        inputs_cache_.push_back(&hdr);
    }

    // Build outputs cache: .o (primary) + .d (secondary)
    outputs_cache_.push_back(&output_artifact_);
    outputs_cache_.push_back(&depfile_artifact_);
}

const std::vector<const Artifact*>& BuildCppSource::inputs() const {
    return inputs_cache_;
}

const std::vector<const Artifact*>& BuildCppSource::outputs() const {
    return outputs_cache_;
}

const Artifact& BuildCppSource::primaryOutput() const {
    return output_artifact_;
}

const TaskCondition& BuildCppSource::condition() const {
    return condition_;
}

int BuildCppSource::execute() {
    // TODO: Spawn C++ compiler process via PAL when process API is integrated.
    // For now, return 0 (success stub). The actual implementation will build
    // the command line: <compiler> -c <source> -o <object> -MMD -MF <depfile>
    //                   -std=<cpp_standard> -I<include_paths> -D<defines> <extra_flags>
    cdo_log_debug("BuildCppSource::execute() stub for %s", config_.source_path.c_str());
    return 0;
}

} // namespace cdo::build
