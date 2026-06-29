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
// Test Module compilation
// ---------------------------------------------------------------------------

/// Build the Test_Module for a crate: compile all .c/.cpp files in tst/
/// into object files placed in build/<profile>/<crate_name>/tst/, then link
/// them against the Library_Module's static library artifact to produce a test
/// executable at build/<profile>/<crate_name>/<crate_name>_test.exe.
///
/// Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 11.1, 11.5
///
/// @param ws           The workspace
/// @param crate        The crate containing the test module
/// @param compiler     Detected compiler info
/// @param profile      Active build profile name (e.g., "debug", "release")
/// @param build_prof   Loaded build profile settings
/// @param jobs         Parallelism level for compilation
/// @param coverage_flags   Coverage flags array (or NULL)
/// @param coverage_flag_count  Number of coverage flags
/// @param progress     Progress bar (may be NULL)
/// @param completed_units  Pointer to completed unit counter (updated in place)
/// @return 0 on success, non-zero on failure
int build_test_module(const Workspace* ws, Crate* crate,
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
    if (!crate->modules[MODULE_TST].present) return 0; // Nothing to do

    // Req 5.5: Test module requires a Library_Module in the same crate
    if (!crate->has_lib) {
        cdo_error("crate '%s': tst/ module requires a lib/ module", crate->name);
        return 1;
    }

    Module* tst_mod = &crate->modules[MODULE_TST];

    // --- Scan tst/ for source files ---
    FileList sources = {0};
    int rc = scanner_scan_module_sources(tst_mod->dir_path, MODULE_TST, NULL, 0, &sources);
    if (rc != 0) {
        cdo_error("failed to scan tst/ sources for crate '%s'", crate->name);
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
        cdo_info("crate '%s' tst/ module has no compilable source files, skipping",
                 crate->name);
        filelist_free(&sources);
        return 0;
    }

    // --- Create object output directory: build/<profile>/<crate_name>/tst/ ---
    char build_dir[260];
    build_dir_for_crate(ws, crate, profile, build_dir, sizeof(build_dir));

    char tst_obj_dir[260];
    char tst_artifact_path[260];
    int apath_rc = module_compute_artifact_path(ws->root_path, crate->name,
                                                MODULE_TST, profile,
                                                tst_artifact_path, sizeof(tst_artifact_path),
                                                tst_obj_dir, sizeof(tst_obj_dir));
    if (apath_rc != 0) {
        cdo_error("failed to compute artifact path for tst/ module in crate '%s'",
                  crate->name);
        filelist_free(&sources);
        return 1;
    }

    // --- Resolve include paths for the test module ---
    char** inc_paths = NULL;
    int inc_count = 0;
    rc = module_include_paths(crate, MODULE_TST, ws, &inc_paths, &inc_count);
    if (rc != 0) {
        cdo_error("failed to compute include paths for tst/ module in crate '%s'",
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
        object_path_from_source(src_path, tst_obj_dir, obj_path, sizeof(obj_path));

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
        cdo_info("Compiling tst/ module for crate '%s' (%d files)", crate->name, dirty_count);

        // Merge crate-level defines with profile defines + CDO_TESTING
        int merged_define_count = build_prof->define_count + crate->define_count + 1;
        const char** merged_defines = (const char**)malloc(sizeof(const char*) * (size_t)merged_define_count);
        if (!merged_defines) {
            merged_define_count = 0;
        } else {
            int di = 0;
            for (int d = 0; d < build_prof->define_count; d++) {
                merged_defines[di++] = build_prof->defines[d];
            }
            for (int d = 0; d < crate->define_count; d++) {
                merged_defines[di++] = crate->defines[d];
            }
            // Req 5.2: Add CDO_TESTING define
            merged_defines[di++] = "CDO_TESTING";
        }

        CompileJob* compile_jobs = (CompileJob*)calloc(dirty_count, sizeof(CompileJob));
        char** obj_paths = (char**)calloc(dirty_count, sizeof(char*));
        if (!compile_jobs || !obj_paths) {
            cdo_error("out of memory allocating compile jobs for tst/ module");
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
            object_path_from_source(src_path, tst_obj_dir, obj_paths[d], 260);

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
            cdo_error("compilation failed for tst/ module in crate '%s'", crate->name);
            free(dirty_indices);
            free(compilable_indices);
            free(all_includes);
            for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
            free(inc_paths);
            filelist_free(&sources);
            return 1;
        }
    } else {
        cdo_info("crate '%s' tst/ module is up to date", crate->name);
    }

    // Update progress
    if (progress && completed_units) {
        *completed_units += compilable_count;
        progress_update(progress, *completed_units);
    }

    // --- Link test executable ---
    // Collect ALL object file paths for the link step
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
        object_path_from_source(sources.paths[src_idx], tst_obj_dir,
                                link_obj_bufs[i], 260);
        link_obj_paths[i] = link_obj_bufs[i];
    }

    // Artifact path already computed by module_compute_artifact_path above
    char artifact_path[260];
    strncpy(artifact_path, tst_artifact_path, sizeof(artifact_path) - 1);
    artifact_path[sizeof(artifact_path) - 1] = '\0';

    // Collect libraries to link against:
    // 1. Own crate's library module artifact
    // 2. Dependency crate library artifacts
    // 3. Platform link_libs
    const char* link_libs[192];
    int link_lib_count = 0;
    const char* lib_paths[64];
    int lib_path_count = 0;

    // Req 5.3: Link against own crate's Library_Module static library
    // We add the build dir as a lib search path and the crate name as a link lib
    lib_paths[lib_path_count++] = build_dir;

    // Extract just the library name from the artifact for linking
    // On Windows: crate.lib -> link with "crate"
    // On Unix: libcrate.a -> link with "crate"
    link_libs[link_lib_count++] = crate->name;

    // Add dependency crate library artifacts
    for (int d = 0; d < crate->dep_count; d++) {
        int dep_idx = crate->dep_indices[d];
        Crate* dep_crate = &ws->crates[dep_idx];

        if (dep_crate->has_lib && lib_path_count < 64) {
            char dep_build_dir[260];
            build_dir_for_crate(ws, dep_crate, profile, dep_build_dir, sizeof(dep_build_dir));

            // Use static buffers for lib paths (persisted for duration of link)
            static char dep_lib_path_bufs[64][260];
            strncpy(dep_lib_path_bufs[lib_path_count], dep_build_dir, 259);
            dep_lib_path_bufs[lib_path_count][259] = '\0';
            lib_paths[lib_path_count] = dep_lib_path_bufs[lib_path_count];
            lib_path_count++;

            if (link_lib_count < 192) {
                link_libs[link_lib_count++] = dep_crate->name;
            }
        }
    }

    // Add dev-dependency crate library artifacts
    for (int d = 0; d < crate->dev_dep_count; d++) {
        int dep_idx = crate->dev_dep_indices[d];
        if (dep_idx < 0 || dep_idx >= ws->crate_count) continue;
        Crate* dep_crate = &ws->crates[dep_idx];

        if (dep_crate->has_lib && lib_path_count < 64) {
            char dep_build_dir[260];
            build_dir_for_crate(ws, dep_crate, profile, dep_build_dir, sizeof(dep_build_dir));

            static char dev_dep_lib_path_bufs[64][260];
            strncpy(dev_dep_lib_path_bufs[lib_path_count], dep_build_dir, 259);
            dev_dep_lib_path_bufs[lib_path_count][259] = '\0';
            lib_paths[lib_path_count] = dev_dep_lib_path_bufs[lib_path_count];
            lib_path_count++;

            if (link_lib_count < 192) {
                link_libs[link_lib_count++] = dep_crate->name;
            }
        }
    }

    // Platform link libs from the crate manifest
    for (int l = 0; l < crate->link_lib_count && link_lib_count < 192; l++) {
        link_libs[link_lib_count++] = crate->link_libs[l];
    }

    // Link as executable (shared = false, no static archive)
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

    // If any source is C++, use C++ linker driver
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

    cdo_info("Linking tst/ module: %s", artifact_path);
    rc = compiler_link(&link_job, &link_compiler);

    if (rc != 0) {
        cdo_error("linking failed for tst/ module in crate '%s'", crate->name);
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
    strncpy(tst_mod->artifact_path, artifact_path, sizeof(tst_mod->artifact_path) - 1);
    tst_mod->artifact_path[sizeof(tst_mod->artifact_path) - 1] = '\0';

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
