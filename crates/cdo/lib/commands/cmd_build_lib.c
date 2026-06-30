#include "cmd_build_internal.h"
#include "core/compiler.h"
#include "model/scanner.h"
#include "model/module.h"
#include "core/log.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Library Module compilation
// ---------------------------------------------------------------------------

/// Build the Library_Module for a crate: compile all .c/.cpp files in lib/
/// into object files placed in build/<profile>/<crate_name>/lib/, then archive
/// them into a static library artifact at build/<profile>/<crate_name>/<artifact>.
///
/// Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 8.1, 11.1, 11.2
///
/// @param ws           The workspace
/// @param crate        The crate containing the library module (must have has_lib == true)
/// @param compiler     Detected compiler info
/// @param profile      Active build profile name (e.g., "debug", "release")
/// @param build_prof   Loaded build profile settings
/// @param jobs         Parallelism level for compilation
/// @param coverage_flags   Coverage flags array (or NULL)
/// @param coverage_flag_count  Number of coverage flags
/// @param progress     Progress bar (may be NULL)
/// @param completed_units  Pointer to completed unit counter (updated in place)
/// @return 0 on success, non-zero on failure
int build_library_module(const Workspace* ws, Crate* crate,
                         const CompilerInfo* compiler,
                         const char* profile,
                         const BuildProfile* build_prof,
                         int jobs,
                         const char** coverage_flags,
                         int coverage_flag_count,
                         const CacheConfig* cache_config,
                         CacheStats* cache_stats,
                         bool no_cache,
                         CliProgressBar* progress,
                         int* completed_units) {
    if (!crate->has_lib) return 0; // Nothing to do

    Module* lib_mod = &crate->modules[MODULE_LIB];

    // --- Scan lib/ for source files ---
    FileList sources = {0};
    int rc = scanner_scan_module_sources(lib_mod->dir_path, MODULE_LIB, NULL, 0, &sources);
    if (rc != 0) {
        cdo_log_error("failed to scan lib/ sources for crate '%s'", crate->name);
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

    // Req 2.5: If lib/ has no compilable source files, report error and halt
    if (compilable_count == 0) {
        cdo_log_error("crate '%s': lib/ module has no compilable source files (.c or .cpp)",
                  crate->name);
        filelist_free(&sources);
        return 1;
    }

    // --- Create object output directory: build/<profile>/<crate_name>/lib/ ---
    char build_dir[260];
    build_dir_for_crate(ws, crate, profile, build_dir, sizeof(build_dir));

    char lib_obj_dir[260];
    char lib_artifact_path[260];
    int apath_rc = module_compute_artifact_path(ws->root_path, crate->name,
                                                MODULE_LIB, profile,
                                                lib_artifact_path, sizeof(lib_artifact_path),
                                                lib_obj_dir, sizeof(lib_obj_dir));
    if (apath_rc != 0) {
        cdo_log_error("failed to compute artifact path for lib/ module in crate '%s'",
                  crate->name);
        filelist_free(&sources);
        return 1;
    }

    // --- Resolve include paths for the library module ---
    char** inc_paths = NULL;
    int inc_count = 0;
    rc = module_include_paths(crate, MODULE_LIB, ws, &inc_paths, &inc_count);
    if (rc != 0) {
        cdo_log_error("failed to compute include paths for lib/ module in crate '%s'",
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
        object_path_from_source(src_path, lib_obj_dir, obj_path, sizeof(obj_path));

        uint64_t src_mtime = 0, obj_mtime = 0;
        bool needs_rebuild = true;

        if (pal_file_mtime(src_path, &src_mtime) == 0 &&
            pal_file_mtime(obj_path, &obj_mtime) == 0) {
            if (obj_mtime >= src_mtime) {
                needs_rebuild = false;
            }
        }

        // Detect coverage instrumentation mismatch:
        // - If coverage is requested but .gcno is missing â†’ need to rebuild with instrumentation.
        // - If coverage is NOT requested but .gcno exists â†’ need to rebuild without instrumentation.
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
        cdo_log_info("Compiling lib/ module for crate '%s' (%d files)", crate->name, dirty_count);

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
            cdo_log_error("out of memory allocating compile jobs for lib/ module");
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
                cdo_log_error("out of memory");
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
            object_path_from_source(src_path, lib_obj_dir, obj_paths[d], 260);

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
        rc = compiler_compile_batch(compile_jobs, dirty_count, compiler, jobs, cache_config, cache_stats, no_cache);

        for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
        free(obj_paths);
        free(compile_jobs);
        free(merged_defines);

        if (rc != 0) {
            cdo_log_error("compilation failed for lib/ module in crate '%s'", crate->name);
            free(dirty_indices);
            free(compilable_indices);
            free(all_includes);
            for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
            free(inc_paths);
            filelist_free(&sources);
            return 1;
        }
    } else {
        cdo_log_info("crate '%s' lib/ module is up to date", crate->name);
    }

    // Update progress
    if (progress && completed_units) {
        *completed_units += compilable_count;
        cli_out_progress_update(progress, *completed_units);
    }

    // --- Archive object files into static library ---
    // Collect ALL object file paths (not just dirty ones) for the archive
    const char** archive_obj_paths = (const char**)malloc(compilable_count * sizeof(const char*));
    char** archive_obj_bufs = (char**)calloc(compilable_count, sizeof(char*));
    if (!archive_obj_paths || !archive_obj_bufs) {
        cdo_log_error("out of memory for archive step");
        free(archive_obj_paths);
        free(archive_obj_bufs);
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
        archive_obj_bufs[i] = (char*)malloc(260);
        if (!archive_obj_bufs[i]) {
            for (int x = 0; x < i; x++) free(archive_obj_bufs[x]);
            free(archive_obj_bufs);
            free(archive_obj_paths);
            free(dirty_indices);
            free(compilable_indices);
            free(all_includes);
            for (int j = 0; j < inc_count; j++) free(inc_paths[j]);
            free(inc_paths);
            filelist_free(&sources);
            return 1;
        }
        object_path_from_source(sources.paths[src_idx], lib_obj_dir,
                                archive_obj_bufs[i], 260);
        archive_obj_paths[i] = archive_obj_bufs[i];
    }

    // Artifact path already computed by module_compute_artifact_path above
    char artifact_path[260];
    strncpy(artifact_path, lib_artifact_path, sizeof(artifact_path) - 1);
    artifact_path[sizeof(artifact_path) - 1] = '\0';

    // Use compiler_link which handles static library archiving via ar/lib.exe
    LinkJob link_job = {0};
    link_job.object_paths = archive_obj_paths;
    link_job.object_count = compilable_count;
    link_job.output_path = artifact_path;
    link_job.lib_paths = NULL;
    link_job.lib_path_count = 0;
    link_job.link_libs = NULL;
    link_job.link_lib_count = 0;
    link_job.shared = false;

    cdo_log_info("Archiving lib/ module: %s", artifact_path);
    rc = compiler_link(&link_job, compiler);

    if (rc != 0) {
        cdo_log_error("archiving failed for lib/ module in crate '%s'", crate->name);
        for (int i = 0; i < compilable_count; i++) free(archive_obj_bufs[i]);
        free(archive_obj_bufs);
        free(archive_obj_paths);
        free(dirty_indices);
        free(compilable_indices);
        free(all_includes);
        for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
        free(inc_paths);
        filelist_free(&sources);
        return 1;
    }

    // Store artifact path in the module struct for use by dependent modules
    strncpy(lib_mod->artifact_path, artifact_path, sizeof(lib_mod->artifact_path) - 1);
    lib_mod->artifact_path[sizeof(lib_mod->artifact_path) - 1] = '\0';

    // Cleanup
    for (int i = 0; i < compilable_count; i++) free(archive_obj_bufs[i]);
    free(archive_obj_bufs);
    free(archive_obj_paths);
    free(dirty_indices);
    free(compilable_indices);
    free(all_includes);
    for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
    free(inc_paths);
    filelist_free(&sources);

    return 0;
}
