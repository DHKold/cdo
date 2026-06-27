#include "cmd_build_internal.h"
#include "core/compiler.h"
#include "core/scanner.h"
#include "core/module.h"
#include "core/output.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Executable Module compilation
// ---------------------------------------------------------------------------

/// Build the Executable_Module for a crate: compile all .c/.cpp files in exe/
/// into object files placed in build/<profile>/<crate_name>/exe/, then link
/// them (along with the library artifact and inter-crate deps) into an
/// executable at build/<profile>/<crate_name>/<crate_name>.exe.
///
/// If no Library_Module exists, compiles as a standalone executable with only
/// exe/ as an include path.
///
/// Requirements: 3.1, 3.2, 3.3, 3.4, 11.1, 11.3
///
/// @param ws           The workspace
/// @param crate        The crate containing the executable module
/// @param compiler     Detected compiler info
/// @param profile      Active build profile name (e.g., "debug", "release")
/// @param build_prof   Loaded build profile settings
/// @param jobs         Parallelism level for compilation
/// @param coverage_flags   Coverage flags array (or NULL)
/// @param coverage_flag_count  Number of coverage flags
/// @param progress     Progress bar (may be NULL)
/// @param completed_units  Pointer to completed unit counter (updated in place)
/// @return 0 on success, non-zero on failure
int build_executable_module(const Workspace* ws, Crate* crate,
                            const CompilerInfo* compiler,
                            const char* profile,
                            const BuildProfile* build_prof,
                            int jobs,
                            const char** coverage_flags,
                            int coverage_flag_count,
                            ProgressBar* progress,
                            int* completed_units) {
    Module* exe_mod = &crate->modules[MODULE_EXE];
    if (!exe_mod->present) return 0; // Nothing to do

    // --- Scan exe/ for source files ---
    FileList sources = {0};
    int rc = scanner_scan_module_sources(exe_mod->dir_path, MODULE_EXE, NULL, 0, &sources);
    if (rc != 0) {
        cdo_error("failed to scan exe/ sources for crate '%s'", crate->name);
        return 1;
    }

    // Filter to compilable sources only
    int compilable_count = 0;
    for (int s = 0; s < sources.count; s++) {
        const char* ext = pal_path_ext(sources.paths[s]);
        if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0 ||
                   strcmp(ext, ".cxx") == 0 || strcmp(ext, ".cc") == 0)) {
            compilable_count++;
        }
    }

    if (compilable_count == 0) {
        cdo_warn("crate '%s': exe/ module has no compilable source files", crate->name);
        filelist_free(&sources);
        return 0;
    }

    // --- Create object output directory: build/<profile>/<crate_name>/exe/ ---
    char build_dir[260];
    build_dir_for_crate(ws, crate, profile, build_dir, sizeof(build_dir));

    char exe_obj_dir[260];
    char exe_artifact_path[260];
    int apath_rc = module_compute_artifact_path(ws->root_path, crate->name,
                                                MODULE_EXE, profile,
                                                exe_artifact_path, sizeof(exe_artifact_path),
                                                exe_obj_dir, sizeof(exe_obj_dir));
    if (apath_rc != 0) {
        cdo_error("failed to compute artifact path for exe/ module in crate '%s'",
                  crate->name);
        filelist_free(&sources);
        return 1;
    }

    // --- Resolve include paths for the executable module ---
    char** inc_paths = NULL;
    int inc_count = 0;
    rc = module_include_paths(crate, MODULE_EXE, ws, &inc_paths, &inc_count);
    if (rc != 0) {
        cdo_error("failed to compute include paths for exe/ module in crate '%s'",
                  crate->name);
        filelist_free(&sources);
        return 1;
    }

    // Build const char** from the returned paths for CompileJob
    const char** all_includes = (const char**)malloc((inc_count + 1) * sizeof(const char*));
    if (!all_includes) {
        for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
        free(inc_paths);
        filelist_free(&sources);
        return 1;
    }
    for (int i = 0; i < inc_count; i++) {
        all_includes[i] = inc_paths[i];
    }

    // --- Compute dirty set (incremental build) ---
    int* compilable_indices = (int*)malloc(sizeof(int) * sources.count);
    if (!compilable_indices) {
        free(all_includes);
        for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
        free(inc_paths);
        filelist_free(&sources);
        return 1;
    }

    int ci = 0;
    for (int s = 0; s < sources.count; s++) {
        const char* ext = pal_path_ext(sources.paths[s]);
        if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0 ||
                   strcmp(ext, ".cxx") == 0 || strcmp(ext, ".cc") == 0)) {
            compilable_indices[ci++] = s;
        }
    }

    int* dirty_indices = (int*)malloc(sizeof(int) * compilable_count);
    int dirty_count = 0;
    if (!dirty_indices) {
        free(compilable_indices);
        free(all_includes);
        for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
        free(inc_paths);
        filelist_free(&sources);
        return 1;
    }

    for (int i = 0; i < compilable_count; i++) {
        int src_idx = compilable_indices[i];
        const char* src_path = sources.paths[src_idx];

        char obj_path[260];
        object_path_from_source(src_path, exe_obj_dir, obj_path, sizeof(obj_path));

        uint64_t src_mtime = 0, obj_mtime = 0;
        bool needs_rebuild = true;

        if (pal_file_mtime(src_path, &src_mtime) == 0 &&
            pal_file_mtime(obj_path, &obj_mtime) == 0) {
            if (obj_mtime >= src_mtime) {
                needs_rebuild = false;
            }
        }

        // Detect coverage instrumentation mismatch:
        // - If coverage is requested but .gcno is missing → need to rebuild with instrumentation.
        // - If coverage is NOT requested but .gcno exists → need to rebuild without instrumentation.
        if (!needs_rebuild) {
            char gcno_path[260];
            size_t obj_len = strlen(obj_path);
            if (obj_len > 2 && obj_len < sizeof(gcno_path) - 3) {
                memcpy(gcno_path, obj_path, obj_len - 2);
                memcpy(gcno_path + obj_len - 2, ".gcno", 6);
                bool gcno_exists = (pal_path_exists(gcno_path) == 0);
                if (coverage_flag_count > 0 && !gcno_exists) {
                    needs_rebuild = true;
                } else if (coverage_flag_count == 0 && gcno_exists) {
                    needs_rebuild = true;
                    remove(gcno_path); // Clean up stale coverage artifacts
                }
            }
        }

        if (needs_rebuild) {
            dirty_indices[dirty_count++] = src_idx;
        }
    }

    // --- Compile dirty sources ---
    if (dirty_count > 0) {
        cdo_info("Compiling exe/ module for crate '%s' (%d files)", crate->name, dirty_count);

        // Merge crate-level defines with profile defines
        int merged_define_count = build_prof->define_count + crate->define_count;
        const char** merged_defines = NULL;
        if (merged_define_count > 0) {
            merged_defines = (const char**)malloc(sizeof(const char*) * (size_t)merged_define_count);
            if (merged_defines) {
                int di = 0;
                for (int d = 0; d < build_prof->define_count; d++) {
                    merged_defines[di++] = build_prof->defines[d];
                }
                for (int d = 0; d < crate->define_count; d++) {
                    merged_defines[di++] = crate->defines[d];
                }
            } else {
                merged_define_count = 0;
            }
        }

        CompileJob* compile_jobs = (CompileJob*)calloc(dirty_count, sizeof(CompileJob));
        char** obj_paths = (char**)calloc(dirty_count, sizeof(char*));
        if (!compile_jobs || !obj_paths) {
            cdo_error("out of memory allocating compile jobs for exe/ module");
            free(compile_jobs);
            free(obj_paths);
            free(merged_defines);
            free(dirty_indices);
            free(compilable_indices);
            free(all_includes);
            for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
            free(inc_paths);
            filelist_free(&sources);
            return 1;
        }

        for (int d = 0; d < dirty_count; d++) {
            int src_idx = dirty_indices[d];
            const char* src_path = sources.paths[src_idx];

            obj_paths[d] = (char*)malloc(260);
            if (!obj_paths[d]) {
                cdo_error("out of memory");
                for (int x = 0; x < d; x++) free(obj_paths[x]);
                free(obj_paths);
                free(compile_jobs);
                free(merged_defines);
                free(dirty_indices);
                free(compilable_indices);
                free(all_includes);
                for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
                free(inc_paths);
                filelist_free(&sources);
                return 1;
            }
            object_path_from_source(src_path, exe_obj_dir, obj_paths[d], 260);

            compile_jobs[d].source_path = src_path;
            compile_jobs[d].object_path = obj_paths[d];
            compile_jobs[d].include_paths = all_includes;
            compile_jobs[d].include_path_count = inc_count;
            compile_jobs[d].optimize = build_prof->optimize;
            compile_jobs[d].debug_info = build_prof->debug_info;
            compile_jobs[d].defines = merged_defines;
            compile_jobs[d].define_count = merged_define_count;

            // Apply profile extra flags + coverage flags
            {
                int total_extra = build_prof->extra_flag_count + coverage_flag_count;
                if (total_extra > 0) {
                    static const char* merged_extra_flags[BUILD_PROFILE_MAX_FLAGS + 2];
                    int mf = 0;
                    for (int f = 0; f < build_prof->extra_flag_count; f++) {
                        merged_extra_flags[mf++] = build_prof->extra_flags[f];
                    }
                    for (int f = 0; f < coverage_flag_count; f++) {
                        merged_extra_flags[mf++] = coverage_flags[f];
                    }
                    compile_jobs[d].extra_flags = merged_extra_flags;
                    compile_jobs[d].extra_flag_count = total_extra;
                } else {
                    compile_jobs[d].extra_flags = NULL;
                    compile_jobs[d].extra_flag_count = 0;
                }
            }

            // Set language standard based on file type
            if (is_cpp_source(src_path)) {
                compile_jobs[d].c_standard = NULL;
                compile_jobs[d].cpp_standard = cpp_standard_str(crate->cpp_standard);
            } else {
                compile_jobs[d].c_standard = c_standard_str(crate->c_standard);
                compile_jobs[d].cpp_standard = NULL;
            }
        }

        // Execute compilation batch
        rc = compiler_compile_batch(compile_jobs, dirty_count, compiler, jobs);

        for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
        free(obj_paths);
        free(compile_jobs);
        free(merged_defines);

        if (rc != 0) {
            cdo_error("compilation failed for exe/ module in crate '%s'", crate->name);
            free(dirty_indices);
            free(compilable_indices);
            free(all_includes);
            for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
            free(inc_paths);
            filelist_free(&sources);
            return 1;
        }
    } else {
        cdo_info("crate '%s' exe/ module is up to date", crate->name);
    }

    // Update progress
    if (progress && completed_units) {
        *completed_units += compilable_count;
        progress_update(progress, *completed_units);
    }

    // --- Link exe/ objects into executable ---
    // Collect ALL object file paths (not just dirty ones) for linking
    const char** link_obj_paths = (const char**)malloc(compilable_count * sizeof(const char*));
    char** link_obj_bufs = (char**)calloc(compilable_count, sizeof(char*));
    if (!link_obj_paths || !link_obj_bufs) {
        cdo_error("out of memory for link step");
        free(link_obj_paths);
        free(link_obj_bufs);
        free(dirty_indices);
        free(compilable_indices);
        free(all_includes);
        for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
        free(inc_paths);
        filelist_free(&sources);
        return 1;
    }

    for (int i = 0; i < compilable_count; i++) {
        int src_idx = compilable_indices[i];
        link_obj_bufs[i] = (char*)malloc(260);
        if (!link_obj_bufs[i]) {
            for (int x = 0; x < i; x++) free(link_obj_bufs[x]);
            free(link_obj_bufs);
            free(link_obj_paths);
            free(dirty_indices);
            free(compilable_indices);
            free(all_includes);
            for (int j = 0; j < inc_count; j++) free(inc_paths[j]);
            free(inc_paths);
            filelist_free(&sources);
            return 1;
        }
        object_path_from_source(sources.paths[src_idx], exe_obj_dir,
                                link_obj_bufs[i], 260);
        link_obj_paths[i] = link_obj_bufs[i];
    }

    // Collect libraries to link against:
    // 1. Own crate's library artifact (if has_lib)
    // 2. All dependency crates' library artifacts
    // 3. Platform link_libs from crate manifest
    const char* link_libs[192];
    int link_lib_count = 0;
    const char* lib_paths[64];
    int lib_path_count = 0;

    // If the crate has a Library_Module, link against it
    if (crate->has_lib) {
        Module* lib_mod = &crate->modules[MODULE_LIB];
        if (lib_mod->artifact_path[0] != '\0') {
            // Extract directory from the artifact path for -L
            char lib_dir[260];
            strncpy(lib_dir, lib_mod->artifact_path, sizeof(lib_dir) - 1);
            lib_dir[sizeof(lib_dir) - 1] = '\0';
            // Find last separator to get directory
            char* last_sep = strrchr(lib_dir, '/');
            char* last_bsep = strrchr(lib_dir, '\\');
            if (last_bsep > last_sep) last_sep = last_bsep;
            if (last_sep) *last_sep = '\0';

            static char own_lib_dir[260];
            strncpy(own_lib_dir, lib_dir, sizeof(own_lib_dir) - 1);
            own_lib_dir[sizeof(own_lib_dir) - 1] = '\0';
            lib_paths[lib_path_count++] = own_lib_dir;

            // Add library name for -l (strip prefix/suffix)
            static char own_lib_name[64];
            strncpy(own_lib_name, crate->name, sizeof(own_lib_name) - 1);
            own_lib_name[sizeof(own_lib_name) - 1] = '\0';
            link_libs[link_lib_count++] = own_lib_name;
        }
    }

    // Inter-crate dependency libraries
    for (int d = 0; d < crate->dep_count; d++) {
        int dep_idx = crate->dep_indices[d];
        if (dep_idx < 0 || dep_idx >= ws->crate_count) continue;
        Crate* dep_crate = &ws->crates[dep_idx];

        if (dep_crate->has_lib && dep_crate->modules[MODULE_LIB].artifact_path[0] != '\0') {
            // Extract directory from dep's artifact path
            static char dep_lib_dirs[64][260];
            strncpy(dep_lib_dirs[lib_path_count], dep_crate->modules[MODULE_LIB].artifact_path,
                    sizeof(dep_lib_dirs[lib_path_count]) - 1);
            dep_lib_dirs[lib_path_count][sizeof(dep_lib_dirs[lib_path_count]) - 1] = '\0';
            char* last_sep = strrchr(dep_lib_dirs[lib_path_count], '/');
            char* last_bsep = strrchr(dep_lib_dirs[lib_path_count], '\\');
            if (last_bsep > last_sep) last_sep = last_bsep;
            if (last_sep) *last_sep = '\0';
            lib_paths[lib_path_count] = dep_lib_dirs[lib_path_count];
            lib_path_count++;

            // Add dep library name for -l
            static char dep_lib_names[128][64];
            strncpy(dep_lib_names[link_lib_count], dep_crate->name,
                    sizeof(dep_lib_names[link_lib_count]) - 1);
            dep_lib_names[link_lib_count][sizeof(dep_lib_names[link_lib_count]) - 1] = '\0';
            link_libs[link_lib_count] = dep_lib_names[link_lib_count];
            link_lib_count++;
        }
    }

    // Platform link_libs from crate manifest
    for (int l = 0; l < crate->link_lib_count && link_lib_count < 192; l++) {
        link_libs[link_lib_count++] = crate->link_libs[l];
    }

    // Artifact path already computed by module_compute_artifact_path above
    char artifact_path[260];
    strncpy(artifact_path, exe_artifact_path, sizeof(artifact_path) - 1);
    artifact_path[sizeof(artifact_path) - 1] = '\0';

    // Determine if we need C++ linker based on sources
    CompilerInfo link_compiler = *compiler;
    for (int s = 0; s < sources.count; s++) {
        if (is_cpp_source(sources.paths[s])) {
            if (compiler->family == COMPILER_GCC) {
                size_t plen = strlen(compiler->path);
                if (plen >= 7 && strcmp(compiler->path + plen - 7, "gcc.exe") == 0) {
                    strncpy(link_compiler.path, compiler->path, sizeof(link_compiler.path) - 1);
                    link_compiler.path[sizeof(link_compiler.path) - 1] = '\0';
                    strcpy(link_compiler.path + plen - 7, "g++.exe");
                } else if (plen >= 3 && strcmp(compiler->path + plen - 3, "gcc") == 0) {
                    strncpy(link_compiler.path, compiler->path, sizeof(link_compiler.path) - 1);
                    link_compiler.path[sizeof(link_compiler.path) - 1] = '\0';
                    strcpy(link_compiler.path + plen - 3, "g++");
                } else if (strcmp(compiler->path, "cc") == 0) {
                    strncpy(link_compiler.path, "c++", sizeof(link_compiler.path) - 1);
                }
            } else if (compiler->family == COMPILER_CLANG) {
                size_t plen = strlen(compiler->path);
                if (plen >= 9 && strcmp(compiler->path + plen - 9, "clang.exe") == 0) {
                    strncpy(link_compiler.path, compiler->path, sizeof(link_compiler.path) - 1);
                    link_compiler.path[sizeof(link_compiler.path) - 1] = '\0';
                    strcpy(link_compiler.path + plen - 9, "clang++.exe");
                } else if (plen >= 5 && strcmp(compiler->path + plen - 5, "clang") == 0) {
                    strncpy(link_compiler.path, compiler->path, sizeof(link_compiler.path) - 1);
                    link_compiler.path[sizeof(link_compiler.path) - 1] = '\0';
                    strcpy(link_compiler.path + plen - 5, "clang++");
                }
            }
            strncpy(link_compiler.linker_path, link_compiler.path, sizeof(link_compiler.linker_path) - 1);
            break;
        }
    }

    // Link the executable
    LinkJob link_job = {0};
    link_job.object_paths = link_obj_paths;
    link_job.object_count = compilable_count;
    link_job.output_path = artifact_path;
    link_job.lib_paths = lib_paths;
    link_job.lib_path_count = lib_path_count;
    link_job.link_libs = link_libs;
    link_job.link_lib_count = link_lib_count;
    link_job.shared = false;

    // Propagate coverage flags to linker
    if (coverage_flag_count > 0) {
        link_job.extra_flags = coverage_flags;
        link_job.extra_flag_count = coverage_flag_count;
    }

    cdo_info("Linking exe/ module: %s", artifact_path);
    rc = compiler_link(&link_job, &link_compiler);

    if (rc != 0) {
        cdo_error("linking failed for exe/ module in crate '%s'", crate->name);
        for (int i = 0; i < compilable_count; i++) free(link_obj_bufs[i]);
        free(link_obj_bufs);
        free(link_obj_paths);
        free(dirty_indices);
        free(compilable_indices);
        free(all_includes);
        for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
        free(inc_paths);
        filelist_free(&sources);
        return 1;
    }

    // Store artifact path in the module struct
    strncpy(exe_mod->artifact_path, artifact_path, sizeof(exe_mod->artifact_path) - 1);
    exe_mod->artifact_path[sizeof(exe_mod->artifact_path) - 1] = '\0';

    // Post-link: deploy catalog files alongside executable binaries
    int deployed = deploy_catalog_files(ws->root_path, build_dir);
    if (deployed > 0) {
        cdo_debug("deployed %d catalog file(s) to %s/catalogs/", deployed, build_dir);
    }

    // Cleanup
    for (int i = 0; i < compilable_count; i++) free(link_obj_bufs[i]);
    free(link_obj_bufs);
    free(link_obj_paths);
    free(dirty_indices);
    free(compilable_indices);
    free(all_includes);
    for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
    free(inc_paths);
    filelist_free(&sources);

    return 0;
}
