// compile_hlsl_shader.cpp — CompileHlslShader implementation.
// Constructs artifact vectors from Config and implements execute() stub.
// The execute() spawns DXC via PAL (stub returning 0 until process API is integrated).

#include "build/tasks/compile_hlsl_shader.h"
#include "core/log.h"

namespace cdo::build {

CompileHlslShader::CompileHlslShader(Config config, bool forced)
    : config_(std::move(config))
    , source_artifact_(config_.source_path, ArtifactType::Source)
    , output_artifact_(config_.output_path, ArtifactType::ShaderOutput)
    , condition_(forced) {
    // Build inputs cache: single source file
    inputs_cache_.push_back(&source_artifact_);

    // Build outputs cache: single .dxil output
    outputs_cache_.push_back(&output_artifact_);
}

const std::vector<const Artifact*>& CompileHlslShader::inputs() const {
    return inputs_cache_;
}

const std::vector<const Artifact*>& CompileHlslShader::outputs() const {
    return outputs_cache_;
}

const Artifact& CompileHlslShader::primaryOutput() const {
    return output_artifact_;
}

const TaskCondition& CompileHlslShader::condition() const {
    return condition_;
}

int CompileHlslShader::execute() {
    // TODO: Spawn DXC process via PAL when process API is integrated.
    // For now, return 0 (success stub). The actual implementation will build
    // the command line: <dxc> -T <target_profile> -E <entry_point> <source> -Fo <output> <extra_flags>
    cdo_log_debug("CompileHlslShader::execute() stub for %s", config_.source_path.c_str());
    return 0;
}

} // namespace cdo::build
