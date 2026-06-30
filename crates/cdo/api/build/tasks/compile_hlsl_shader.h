/**
 * compile_hlsl_shader.h - Concrete task for compiling HLSL shaders via DXC.
 *
 * CompileHlslShader compiles a .hlsl source file into a .dxil output using the
 * DirectX Shader Compiler (DXC). It produces a single output file.
 *
 * Part of the cdo::build pipeline refactor.
 */
#ifndef CDO_BUILD_TASKS_COMPILE_HLSL_SHADER_H
#define CDO_BUILD_TASKS_COMPILE_HLSL_SHADER_H

#include "build/task.h"
#include "build/artifact.h"
#include "build/condition.h"

#include <string>
#include <vector>

namespace cdo::build {

/// Compiles an HLSL shader source file into a DXIL output via DXC.
/// Produces a single output: the compiled shader file (.dxil).
/// Input is the HLSL source file.
class CompileHlslShader : public Task {
public:
    struct Config {
        std::string source_path;
        std::string output_path;
        std::string dxc_path;
        std::string entry_point;
        std::string target_profile;                 // e.g. "vs_6_0", "ps_6_0"
        std::vector<std::string> extra_flags;
    };

    explicit CompileHlslShader(Config config, bool forced = false);

    const std::vector<const Artifact*>& inputs() const override;
    const std::vector<const Artifact*>& outputs() const override;
    const Artifact& primaryOutput() const override;
    const TaskCondition& condition() const override;

protected:
    int execute() override;

private:
    Config config_;
    FileArtifact source_artifact_;
    FileArtifact output_artifact_;
    std::vector<const Artifact*> inputs_cache_;
    std::vector<const Artifact*> outputs_cache_;
    FreshnessCondition condition_;
};

} // namespace cdo::build

#endif // CDO_BUILD_TASKS_COMPILE_HLSL_SHADER_H
