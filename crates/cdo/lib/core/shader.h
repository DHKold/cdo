#ifndef CDO_CORE_SHADER_H
#define CDO_CORE_SHADER_H

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

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_SHADER_H
