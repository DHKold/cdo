#ifndef CDO_COMMANDS_CMD_SHADER_H
#define CDO_COMMANDS_CMD_SHADER_H

#include "core/cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute the shader compilation command.
/// Compiles HLSL shaders using DXC. Verifies DXC is installed in the local
/// tool store before proceeding.
///
/// Positional args:
///   [0] shader_dir  - Directory containing .hlsl files (default: "shaders/")
///   [1] output_dir  - Directory for compiled output (default: "build/shaders/")
///
/// Returns 0 on success, non-zero on failure.
int cmd_shader(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_SHADER_H
