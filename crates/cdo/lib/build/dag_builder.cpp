/**
 * dag_builder.cpp - Constructs a TasksDag from the workspace model.
 *
 * Single generic algorithm that iterates crates in dependency order, processes
 * modules within each crate, and creates the appropriate task types and
 * dependency edges based on module kind. No filesystem operations are performed
 * during construction — the DAG is purely structural.
 *
 * Part of the cdo::build pipeline refactor.
 */

#include "build/dag_builder.h"
#include "build/tasks/build_c_source.h"
#include "build/tasks/build_cpp_source.h"
#include "build/tasks/build_static_library.h"
#include "build/tasks/build_executable.h"
#include "build/tasks/build_shared_library.h"
#include "build/tasks/compile_hlsl_shader.h"

#include <cstring>
#include <string>
#include <vector>
#include <memory>

namespace cdo::build {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Check if a source path has a C++ extension (.cpp, .cc, .cxx)
static bool is_cpp_source(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return false;
    return (strcmp(ext, ".cpp") == 0 ||
            strcmp(ext, ".cc") == 0 ||
            strcmp(ext, ".cxx") == 0);
}

/// Check if a source path has a C extension (.c)
static bool is_c_source(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return false;
    return (strcmp(ext, ".c") == 0);
}

/// Check if a source path is a shader file (.hlsl)
static bool is_shader_source(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return false;
    return (strcmp(ext, ".hlsl") == 0);
}

/// Compute the object file output path for a source file.
/// Pattern: build/<profile>/<crate>/<module_kind>/<basename>.o
static std::string compute_object_path(const char* ws_root, const char* crate_name,
                                       const char* module_kind_str, const char* source_path) {
    // Extract filename from source path
    const char* basename = strrchr(source_path, '/');
    if (!basename) basename = strrchr(source_path, '\\');
    if (basename) basename++;
    else basename = source_path;

    // Replace extension with .o
    std::string name(basename);
    auto dot = name.rfind('.');
    if (dot != std::string::npos) {
        name = name.substr(0, dot);
    }

    std::string result(ws_root);
    result += "/build/debug/";
    result += crate_name;
    result += "/";
    result += module_kind_str;
    result += "/";
    result += name;
    result += ".o";
    return result;
}

/// Compute the depfile output path for a source file.
/// Pattern: build/<profile>/<crate>/<module_kind>/<basename>.d
static std::string compute_depfile_path(const char* ws_root, const char* crate_name,
                                        const char* module_kind_str, const char* source_path) {
    std::string obj = compute_object_path(ws_root, crate_name, module_kind_str, source_path);
    // Replace .o with .d
    if (obj.size() >= 2 && obj.back() == 'o' && obj[obj.size() - 2] == '.') {
        obj.back() = 'd';
    }
    return obj;
}

/// Compute the artifact output path for a link/archive task.
/// Pattern: build/<profile>/<crate>/<artifact_name>
static std::string compute_artifact_path(const char* ws_root, const char* crate_name,
                                         const char* profile, const char* artifact_name) {
    std::string result(ws_root);
    result += "/build/";
    result += profile;
    result += "/";
    result += crate_name;
    result += "/";
    result += artifact_name;
    return result;
}

/// Compute shader output path (.dxil)
static std::string compute_shader_output_path(const char* ws_root, const char* crate_name,
                                              const char* profile, const char* source_path) {
    const char* basename = strrchr(source_path, '/');
    if (!basename) basename = strrchr(source_path, '\\');
    if (basename) basename++;
    else basename = source_path;

    std::string name(basename);
    auto dot = name.rfind('.');
    if (dot != std::string::npos) {
        name = name.substr(0, dot);
    }

    std::string result(ws_root);
    result += "/build/";
    result += profile;
    result += "/";
    result += crate_name;
    result += "/shd/";
    result += name;
    result += ".dxil";
    return result;
}

/// Get the module kind as a short string ("lib", "exe", etc.)
static const char* module_kind_str(ModuleKind kind) {
    switch (kind) {
        case MODULE_LIB: return "lib";
        case MODULE_EXE: return "exe";
        case MODULE_DYN: return "dyn";
        case MODULE_TST: return "tst";
        case MODULE_E2E: return "e2e";
        case MODULE_SHD: return "shd";
        case MODULE_API: return "api";
        case MODULE_RES: return "res";
        default: return "unknown";
    }
}

// ---------------------------------------------------------------------------
// build_dag implementation
// ---------------------------------------------------------------------------

int build_dag(const Workspace* workspace, const DagBuildConfig& config, TasksDag& dag) {
    if (!workspace || workspace->build_order_count == 0) {
        return 0; // Empty workspace → empty DAG (not an error)
    }

    const char* ws_root = workspace->root_path;
    const char* profile = config.profile.c_str();
    bool forced = config.force;

    // Track per-crate lib archive task IDs for inter-crate dependency resolution
    // Index matches workspace->crates[] index. -1 means no lib archive for that crate.
    std::vector<int> crate_lib_archive_ids(workspace->crate_count, -1);

    // Process crates in build order
    for (int order_idx = 0; order_idx < workspace->build_order_count; order_idx++) {
        int crate_idx = workspace->build_order[order_idx];
        const Crate& crate = workspace->crates[crate_idx];

        // Track the lib archive task ID for this crate (if it has a lib module)
        int lib_archive_id = -1;

        // Collect inter-crate dependency lib archive task IDs
        std::vector<int> dep_lib_ids;
        for (int d = 0; d < crate.dep_count; d++) {
            int dep_crate_idx = crate.dep_indices[d];
            if (crate_lib_archive_ids[dep_crate_idx] >= 0) {
                dep_lib_ids.push_back(crate_lib_archive_ids[dep_crate_idx]);
            }
        }

        // Process each module in the crate
        for (int mk = 0; mk < MODULE_KIND_COUNT; mk++) {
            const Module& mod = crate.modules[mk];
            if (!mod.present) continue;

            ModuleKind kind = mod.kind;
            const char* kind_str = module_kind_str(kind);

            // Skip API and RES modules (no compilation)
            if (kind == MODULE_API || kind == MODULE_RES) continue;

            // --- Shader module: CompileHlslShader per file, no link ---
            if (kind == MODULE_SHD) {
                for (int s = 0; s < mod.sources.count; s++) {
                    const char* src = mod.sources.paths[s];
                    CompileHlslShader::Config cfg;
                    cfg.source_path = src;
                    cfg.output_path = compute_shader_output_path(ws_root, crate.name, profile, src);
                    cfg.dxc_path = config.dxc_path;
                    cfg.entry_point = "main";
                    cfg.target_profile = "vs_6_0";
                    dag.addTask(std::make_unique<CompileHlslShader>(std::move(cfg), forced));
                }
                continue;
            }

            // --- Compilable module (lib, exe, tst, e2e, dyn) ---
            std::vector<int> compile_task_ids;
            std::vector<std::string> object_paths;

            for (int s = 0; s < mod.sources.count; s++) {
                const char* src = mod.sources.paths[s];
                std::string obj_path = compute_object_path(ws_root, crate.name, kind_str, src);
                std::string dep_path = compute_depfile_path(ws_root, crate.name, kind_str, src);
                object_paths.push_back(obj_path);

                int task_id = -1;
                if (is_cpp_source(src)) {
                    BuildCppSource::Config cfg;
                    cfg.source_path = src;
                    cfg.object_path = obj_path;
                    cfg.depfile_path = dep_path;
                    cfg.cpp_standard = "c++" + std::to_string(crate.cpp_standard);
                    cfg.compiler_path = config.compiler_path;
                    cfg.compiler_family = config.compiler_family;
                    cfg.optimize = (config.profile == "release");
                    cfg.debug_info = (config.profile != "release");
                    task_id = dag.addTask(std::make_unique<BuildCppSource>(std::move(cfg), forced));
                } else {
                    BuildCSource::Config cfg;
                    cfg.source_path = src;
                    cfg.object_path = obj_path;
                    cfg.depfile_path = dep_path;
                    cfg.c_standard = "c" + std::to_string(crate.c_standard);
                    cfg.compiler_path = config.compiler_path;
                    cfg.compiler_family = config.compiler_family;
                    cfg.optimize = (config.profile == "release");
                    cfg.debug_info = (config.profile != "release");
                    task_id = dag.addTask(std::make_unique<BuildCSource>(std::move(cfg), forced));
                }
                compile_task_ids.push_back(task_id);
            }

            // --- Create the link/archive task ---
            int link_task_id = -1;

            if (kind == MODULE_LIB) {
                // Static library archive
                BuildStaticLibrary::Config cfg;
                cfg.object_paths = object_paths;
                std::string artifact = compute_artifact_path(ws_root, crate.name, profile,
                    (std::string("lib") + crate.name + ".a").c_str());
                cfg.output_path = artifact;
                cfg.archiver_path = config.archiver_path;
                cfg.compiler_family = config.compiler_family;
                link_task_id = dag.addTask(std::make_unique<BuildStaticLibrary>(std::move(cfg), forced));
                lib_archive_id = link_task_id;
            } else if (kind == MODULE_DYN) {
                // Shared library
                BuildSharedLibrary::Config cfg;
                cfg.object_paths = object_paths;
                std::string artifact = compute_artifact_path(ws_root, crate.name, profile,
                    (std::string("lib") + crate.name + ".so").c_str());
                cfg.output_path = artifact;
                cfg.linker_path = config.compiler_path;
                cfg.compiler_family = config.compiler_family;
                link_task_id = dag.addTask(std::make_unique<BuildSharedLibrary>(std::move(cfg), forced));
            } else {
                // Executable (exe, tst, e2e)
                BuildExecutable::Config cfg;
                cfg.object_paths = object_paths;
                std::string exe_name = std::string(crate.name);
                if (kind == MODULE_TST) exe_name += "_test";
                else if (kind == MODULE_E2E) exe_name += "_e2e";
                std::string artifact = compute_artifact_path(ws_root, crate.name, profile,
                    (exe_name + ".exe").c_str());
                cfg.output_path = artifact;
                cfg.linker_path = config.compiler_path;
                cfg.compiler_family = config.compiler_family;
                cfg.debug_info = (config.profile != "release");
                // Add implicit link libs for test executables
                if (kind == MODULE_TST) cfg.link_libs.push_back("cdo_ut");
                if (kind == MODULE_E2E) cfg.link_libs.push_back("cdo_e2e");
                link_task_id = dag.addTask(std::make_unique<BuildExecutable>(std::move(cfg), forced));
            }

            // --- Add dependency edges ---

            // Link/archive task depends on all compile tasks within the same module
            for (int compile_id : compile_task_ids) {
                dag.addDependency(link_task_id, compile_id);
            }

            // exe/tst/e2e/dyn link tasks depend on same-crate lib archive
            if (kind != MODULE_LIB && lib_archive_id >= 0) {
                dag.addDependency(link_task_id, lib_archive_id);
            }

            // Inter-crate: link tasks depend on dependency crates' lib archives
            if (kind != MODULE_LIB) {
                for (int dep_lib_id : dep_lib_ids) {
                    dag.addDependency(link_task_id, dep_lib_id);
                }
            }
        }

        // Record this crate's lib archive ID for dependents
        if (lib_archive_id >= 0) {
            crate_lib_archive_ids[crate_idx] = lib_archive_id;
        }
    }

    return 0;
}

} // namespace cdo::build
