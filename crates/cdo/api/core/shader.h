#ifndef CDO_CORE_SHADER_H
#define CDO_CORE_SHADER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Incrementally compile HLSL shaders in shader_dir to output_dir using DXC.
///
/// For each .hlsl file found in shader_dir:
///   1. Determines the output path (same basename with .dxil extension in output_dir)
///   2. Compares source mtime against output mtime
///   3. Skips compilation when source is older than or equal to output
///   4. Invokes DXC when source is newer or output doesn't exist
///
/// Parameters:
///   shader_dir     - Directory to walk for .hlsl source files
///   output_dir     - Directory for compiled shader output (.dxil/.spv)
///   dxc_path       - Path to the DXC binary (e.g., ".cdo/tools/dxc/bin/dxc.exe")
///   compiled_count - [out] Number of shaders that were recompiled (may be NULL)
///   skipped_count  - [out] Number of shaders that were skipped (may be NULL)
///
/// Returns 0 on success, non-zero if any shader fails to compile.
int shader_compile(const char* shader_dir, const char* output_dir,
                   const char* dxc_path, int* compiled_count, int* skipped_count);

// ---------------------------------------------------------------------------
// Extended compilation API
// ---------------------------------------------------------------------------

/// Options for extended shader compilation.
typedef struct {
    const char* shader_dir;      ///< Directory containing .hlsl source files
    const char* output_dir;      ///< Directory for compiled .dxil output
    const char* dxc_path;        ///< Path to the DXC binary
    const char* target_profile;  ///< DXC target profile (e.g., "lib_6_3")
    bool        force;           ///< If true, skip incremental mtime checks
} ShaderCompileOpts;

/// Result of extended shader compilation.
typedef struct {
    int compiled_count;  ///< Number of shaders that were compiled
    int skipped_count;   ///< Number of shaders skipped (up-to-date)
    int error_count;     ///< Number of shaders that failed to compile
} ShaderCompileResult;

/// Extended incremental shader compilation with configurable target profile
/// and force-recompile option.
///
/// Walks shader_dir for .hlsl files and compiles each to output_dir as .dxil.
/// On per-shader failures, reports the error and continues with remaining shaders.
///
/// Parameters:
///   opts   - Compilation options (all fields required except force which defaults to false)
///   result - [out] Compilation statistics (may be NULL)
///
/// Returns 0 on success (all shaders compiled or skipped), non-zero if any errors occurred.
int shader_compile_ex(const ShaderCompileOpts* opts, ShaderCompileResult* result);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_SHADER_H
