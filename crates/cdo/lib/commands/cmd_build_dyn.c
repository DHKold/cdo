#include "cmd_build_internal.h"
#include "core/compiler.h"
#include "model/scanner.h"
#include "model/module.h"
#include "core/output.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Shared Library Module compilation
// ---------------------------------------------------------------------------

/// Build the Shared_Library_Module for a crate: compile all .c/.cpp files in dyn/
/// into object files placed in build/<profile>/<crate_name>/dyn/, then link them
/// (together with the Library_Module's static library) into a shared library
/// artifact at build/<profile>/<crate_name>/<artifact>.
///
/// Requirements: 4.1, 4.2, 4.3, 4.4, 11.1, 11.4
///
/// @param ws           The workspace
/// @param crate        The crate containing the shared library module
/// @param compiler     Detected compiler info
/// @param profile      Active build profile name (e.g., "debug", "release")
/// @param build_prof   Loaded build profile settings
/// @param jobs         Parallelism level for compilation
/// @param coverage_flags   Coverage flags array (or NULL)
/// @param coverage_flag_count  Number of coverage flags
/// @param progress     Progress bar (may be NULL)
/// @param completed_units  Pointer to completed unit counter (updated in place)
/// @return 0 on success, non-zero on failure
int build_shared_library_module(const Workspace* ws, Crate* crate,
                                const CompilerInfo* compiler,
                                const char* profile,
                                const BuildProfile* build_prof,
                                int jobs,
                                const char** coverage_flags,
                                int coverage_flag_count,
                                const CacheConfig* cache_config,
                                CacheStats* cache_stats,
                                bool no_cache,
                                ProgressBar* progress,
                                int* completed_units) {
    Module* dyn_mod = &crate->modules[MODULE_DYN];
    if (!dyn_mod->present) return 0; // Nothing to do

    // Req 4.4: dyn/ requires a lib/ module in the same crate
    if (!crate->has_lib) {
        cdo_error("crate '%s': dyn/ module requires a lib/ module", crate->name);
        return 1;
    }

    // --- Scan dyn/ for source files ---
    FileList sources = {0};
    int rc = scanner_scan_module_sources(dyn_mod->dir_path, MODULE_DYN, NULL, 0, &sources);
    if (rc != 0) {
        cdo_error("failed to scan dyn/ sources for crate '%s'", crate->name);
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
        cdo_error("crate '%s': dyn/ module has no compilable source files (.c or .cpp)",
                  crate->name);
        filelist_free(&sources);
        return 1;
    }

    // --- Create object output directory: build/<profile>/<crate_name>/dyn/ ---
    char build_dir[260];
    build_dir_for_crate(ws, crate, profile, build_dir, sizeof(build_dir));

    char dyn_obj_dir[260];
    char dyn_artifact_path[260];
    int apath_rc = module_compute_artifact_path(ws->root_path, crate->name,
                                                MODULE_DYN, profile,
                                                dyn_artifact_path, sizeof(dyn_artifact_path),
                                                dyn_obj_dir, sizeof(dyn_obj_dir));
    if (apath_rc != 0) {
        cdo_error("failed to compute artifact path for dyn/ module in crate '%s'",
                  crate->name);
        filelist_free(&sources);
        return 1;
    }

    // --- Resolve include paths for the dyn module ---
    char** inc_paths = NULL;
    int inc_count = 0;
    rc = module_include_paths(crate, MODULE_DYN, ws, &inc_paths, &inc_count);
    if (rc != 0) {
        cdo_error("failed to compute include paths for dyn/ module in crate '%s'",
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
        object_path_from_source(src_path, dyn_obj_dir, obj_path, sizeof(obj_path));

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
        cdo_info("Compiling dyn/ module for crate '%s' (%d files)", crate->name, dirty_count);

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
            cdo_error("out of memory allocating compile jobs for dyn/ module");
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
            object_path_from_source(src_path, dyn_obj_dir, obj_paths[d], 260);

            compile_jobs[d].source_path = src_path;
            compile_jobs[d].object_path = obj_paths[d];
            compile_jobs[d].include_paths = all_includes;
            compile_jobs[d].include_path_count = inc_count;
            compile_jobs[d].optimize = build_prof->optimize;
            compile_jobs[d].debug_info = build_prof->debug_info;
            compile_jobs[d].defines = merged_defines;
            compile_jobs[d].define_count = merged_define_count;

            // Apply profile extra flags + coverage flags + -fPIC for shared library
            {
                int total_extra = build_prof->extra_flag_count + coverage_flag_count + 1; // +1 for -fPIC
                static const char* merged_extra_flags[BUILD_PROFILE_MAX_FLAGS + 4];
                int mf = 0;
                // Add -fPIC for position-independent code (Req 4.2)
                // On Windows/MinGW this is harmless as all code is PIC by default
                merged_extra_flags[mf++] = "-fPIC";
                for (int f = 0; f < build_prof->extra_flag_count; f++) {
                    merged_extra_flags[mf++] = build_prof->extra_flags[f];
                }
                for (int f = 0; f < coverage_flag_count; f++) {
                    merged_extra_flags[mf++] = coverage_flags[f];
                }
                compile_jobs[d].extra_flags = merged_extra_flags;
                compile_jobs[d].extra_flag_count = total_extra;
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
        rc = compiler_compile_batch(compile_jobs, dirty_count, compiler, jobs, cache_config, cache_stats, no_cache);

        for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
        free(obj_paths);
        free(compile_jobs);
        free(merged_defines);

        if (rc != 0) {
            cdo_error("compilation failed for dyn/ module in crate '%s'", crate->name);
            free(dirty_indices);
            free(compilable_indices);
            free(all_includes);
            for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
            free(inc_paths);
            filelist_free(&sources);
            return 1;
        }
    } else {
        cdo_info("crate '%s' dyn/ module is up to date", crate->name);
    }

    // Update progress
    if (progress && completed_units) {
        *completed_units += compilable_count;
        progress_update(progress, *completed_units);
    }

    // --- Link object files into shared library ---
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
        object_path_from_source(sources.paths[src_idx], dyn_obj_dir,
                                link_obj_bufs[i], 260);
        link_obj_paths[i] = link_obj_bufs[i];
    }

    // Artifact path already computed by module_compute_artifact_path above
    char artifact_path[260];
    strncpy(artifact_path, dyn_artifact_path, sizeof(artifact_path) - 1);
    artifact_path[sizeof(artifact_path) - 1] = '\0';

    // Prepare link job: link dyn/ objects against lib/ static library + dependency libs
    const char* lib_paths[64];
    int lib_path_count = 0;
    lib_paths[lib_path_count++] = build_dir;

    // Also add dependency crate build directories as library search paths
    for (int d = 0; d < crate->dep_count; d++) {
        int dep_idx = crate->dep_indices[d];
        Crate* dep_crate = &ws->crates[dep_idx];
        if (dep_crate->has_lib && lib_path_count < 64) {
            static char dep_lib_dirs[64][260];
            build_dir_for_crate(ws, dep_crate, profile,
                                dep_lib_dirs[lib_path_count], sizeof(dep_lib_dirs[lib_path_count]));
            lib_paths[lib_path_count] = dep_lib_dirs[lib_path_count];
            lib_path_count++;
        }
    }

    // Link libraries: own crate's library + dependency crate libraries + platform link_libs
    const char* link_libs[128];
    int link_lib_count = 0;
    link_libs[link_lib_count++] = crate->name;

    for (int d = 0; d < crate->dep_count; d++) {
        int dep_idx = crate->dep_indices[d];
        Crate* dep_crate = &ws->crates[dep_idx];
        if (dep_crate->has_lib && link_lib_count < 128) {
            link_libs[link_lib_count++] = dep_crate->name;
        }
    }

    for (int l = 0; l < crate->link_lib_count && link_lib_count < 128; l++) {
        link_libs[link_lib_count++] = crate->link_libs[l];
    }

    LinkJob link_job = {0};
    link_job.object_paths = link_obj_paths;
    link_job.object_count = compilable_count;
    link_job.output_path = artifact_path;
    link_job.lib_paths = lib_paths;
    link_job.lib_path_count = lib_path_count;
    link_job.link_libs = link_libs;
    link_job.link_lib_count = link_lib_count;
    link_job.shared = true; // Produce a shared library (.dll / .so)

    // Propagate coverage flags to linker
    if (coverage_flag_count > 0) {
        link_job.extra_flags = coverage_flags;
        link_job.extra_flag_count = coverage_flag_count;
    }

    cdo_info("Linking dyn/ module (shared library): %s", artifact_path);
    rc = compiler_link(&link_job, compiler);

    if (rc != 0) {
        cdo_error("linking failed for dyn/ module in crate '%s'", crate->name);
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
    strncpy(dyn_mod->artifact_path, artifact_path, sizeof(dyn_mod->artifact_path) - 1);
    dyn_mod->artifact_path[sizeof(dyn_mod->artifact_path) - 1] = '\0';

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
