// crates/cdo/lib/commands/cmd_build_e2e.c
// E2E Module builder — stub implementation (to be completed in task 8.2).
// Follows the same pattern as build_test_module in cmd_build_test.c.
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
// E2E Module compilation
// ---------------------------------------------------------------------------

/// Build the E2E_Module for a crate: compile all .c/.cpp files in e2e/
/// (excluding e2e/fixtures/) into object files, then link against
/// cdo_ut + cdo_e2e + declared dependencies.
///
/// Requirements: 10.1, 10.2, 10.3, 10.4, 10.5
int build_e2e_module(const Workspace* ws, Crate* crate,
                     const CompilerInfo* compiler,
                     const char* profile,
                     const BuildProfile* build_prof,
                     int jobs,
                     const CacheConfig* cache_config,
                     CacheStats* cache_stats,
                     bool no_cache,
                     CliProgressBar* progress,
                     int* completed_units) {
    if (!crate->modules[MODULE_E2E].present) return 0; // Nothing to do

    // Req 10.5: Validate implicit dependencies exist in workspace
    int cdo_ut_idx = -1;
    int cdo_e2e_idx = -1;
    for (int i = 0; i < ws->crate_count; i++) {
        if (strcmp(ws->crates[i].name, "cdo_ut") == 0) cdo_ut_idx = i;
        if (strcmp(ws->crates[i].name, "cdo_e2e") == 0) cdo_e2e_idx = i;
    }

    if (cdo_ut_idx < 0) {
        cdo_log_error("crate '%s': e2e/ module requires 'cdo_ut' as a workspace member (searched '%s')", crate->name, ws->root_path);
        return 1;
    }
    if (cdo_e2e_idx < 0) {
        cdo_log_error("crate '%s': e2e/ module requires 'cdo_e2e' as a workspace member (searched '%s')", crate->name, ws->root_path);
        return 1;
    }

    Module* e2e_mod = &crate->modules[MODULE_E2E];

    // --- Scan e2e/ for source files (fixtures/ excluded) ---
    static const char* e2e_excludes[] = { "fixtures/**" };
    FileList sources = {0};
    int rc = scanner_scan_module_sources(e2e_mod->dir_path, MODULE_E2E, e2e_excludes, 1, &sources);
    if (rc != 0) {
        cdo_log_error("failed to scan e2e/ sources for crate '%s'", crate->name);
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
        cdo_log_info("crate '%s' e2e/ module has no compilable source files, skipping", crate->name);
        filelist_free(&sources);
        return 0;
    }

    // --- Create object output directory: build/<profile>/<crate_name>/e2e/ ---
    char build_dir[260];
    build_dir_for_crate(ws, crate, profile, build_dir, sizeof(build_dir));

    char e2e_obj_dir[260];
    char e2e_artifact_path[260];
    int apath_rc = module_compute_artifact_path(ws->root_path, crate->name,
                                                MODULE_E2E, profile,
                                                e2e_artifact_path, sizeof(e2e_artifact_path),
                                                e2e_obj_dir, sizeof(e2e_obj_dir));
    if (apath_rc != 0) {
        cdo_log_error("failed to compute artifact path for e2e/ module in crate '%s'", crate->name);
        filelist_free(&sources);
        return 1;
    }

    // --- Resolve include paths for the e2e module ---
    // Start with module_include_paths (handles own dir, lib/, api/, declared deps)
    char** inc_paths = NULL;
    int inc_count = 0;
    rc = module_include_paths(crate, MODULE_E2E, ws, &inc_paths, &inc_count);
    if (rc != 0) {
        cdo_log_error("failed to compute include paths for e2e/ module in crate '%s'", crate->name);
        filelist_free(&sources);
        return 1;
    }

    // Req 10.3: Add implicit dep api/ directories to include paths
    // Check if they're already included (from declared deps) to deduplicate
    int extra_inc_count = 0;
    char* extra_incs[4]; // max 2 implicit deps * 2 (api + subdirs)
    memset(extra_incs, 0, sizeof(extra_incs));

    // Add cdo_ut api/ if not already in includes
    const char* ut_api = ws->crates[cdo_ut_idx].modules[MODULE_API].dir_path;
    if (ut_api[0] != '\0') {
        bool already_included = false;
        for (int i = 0; i < inc_count; i++) {
            if (strcmp(inc_paths[i], ut_api) == 0) {
                already_included = true;
                break;
            }
        }
        if (!already_included) {
            extra_incs[extra_inc_count++] = strdup(ut_api);
        }
    }

    // Add cdo_e2e api/ if not already in includes
    const char* e2e_api = ws->crates[cdo_e2e_idx].modules[MODULE_API].dir_path;
    if (e2e_api[0] != '\0') {
        bool already_included = false;
        for (int i = 0; i < inc_count; i++) {
            if (strcmp(inc_paths[i], e2e_api) == 0) {
                already_included = true;
                break;
            }
        }
        if (!already_included) {
            extra_incs[extra_inc_count++] = strdup(e2e_api);
        }
    }

    // Merge all include paths
    int total_inc_count = inc_count + extra_inc_count;
    const char** all_includes = (const char**)malloc((total_inc_count + 1) * sizeof(const char*));
    if (!all_includes) {
        for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
        free(inc_paths);
        for (int i = 0; i < extra_inc_count; i++) free(extra_incs[i]);
        filelist_free(&sources);
        return 1;
    }
    for (int i = 0; i < inc_count; i++) {
        all_includes[i] = inc_paths[i];
    }
    for (int i = 0; i < extra_inc_count; i++) {
        all_includes[inc_count + i] = extra_incs[i];
    }

    // --- Compute dirty set (incremental build) ---
    int* compilable_indices = (int*)malloc(sizeof(int) * sources.count);
    if (!compilable_indices) {
        free(all_includes);
        for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
        free(inc_paths);
        for (int i = 0; i < extra_inc_count; i++) free(extra_incs[i]);
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
        for (int i = 0; i < extra_inc_count; i++) free(extra_incs[i]);
        filelist_free(&sources);
        return 1;
    }

    for (int i = 0; i < compilable_count; i++) {
        int src_idx = compilable_indices[i];
        const char* src_path = sources.paths[src_idx];

        char obj_path[260];
        object_path_from_source(src_path, e2e_obj_dir, obj_path, sizeof(obj_path));

        uint64_t src_mtime = 0, obj_mtime = 0;
        bool needs_rebuild = true;

        if (pal_file_mtime(src_path, &src_mtime) == 0 &&
            pal_file_mtime(obj_path, &obj_mtime) == 0) {
            if (obj_mtime >= src_mtime) {
                needs_rebuild = false;
            }
        }

        if (needs_rebuild) {
            dirty_indices[dirty_count++] = src_idx;
        }
    }

    // --- Compile dirty sources ---
    if (dirty_count > 0) {
        cdo_log_info("Compiling e2e/ module for crate '%s' (%d files)", crate->name, dirty_count);

        // Merge defines: profile + crate + CDO_TESTING
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
            // Req 10.1/10.2: Add CDO_TESTING define for e2e module
            merged_defines[di++] = "CDO_TESTING";
        }

        CompileJob* compile_jobs = (CompileJob*)calloc(dirty_count, sizeof(CompileJob));
        char** obj_paths = (char**)calloc(dirty_count, sizeof(char*));
        if (!compile_jobs || !obj_paths) {
            cdo_log_error("out of memory allocating compile jobs for e2e/ module");
            free(compile_jobs);
            free(obj_paths);
            free(merged_defines);
            free(dirty_indices);
            free(compilable_indices);
            free(all_includes);
            for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
            free(inc_paths);
            for (int i = 0; i < extra_inc_count; i++) free(extra_incs[i]);
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
                for (int i = 0; i < extra_inc_count; i++) free(extra_incs[i]);
                filelist_free(&sources);
                return 1;
            }
            object_path_from_source(src_path, e2e_obj_dir, obj_paths[d], 260);

            compile_jobs[d].source_path = src_path;
            compile_jobs[d].object_path = obj_paths[d];
            compile_jobs[d].include_paths = all_includes;
            compile_jobs[d].include_path_count = total_inc_count;
            compile_jobs[d].optimize = build_prof->optimize;
            compile_jobs[d].debug_info = build_prof->debug_info;
            compile_jobs[d].defines = merged_defines;
            compile_jobs[d].define_count = merged_define_count;
            compile_jobs[d].extra_flags = NULL;
            compile_jobs[d].extra_flag_count = 0;

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
            cdo_log_error("compilation failed for e2e/ module in crate '%s'", crate->name);
            free(dirty_indices);
            free(compilable_indices);
            free(all_includes);
            for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
            free(inc_paths);
            for (int i = 0; i < extra_inc_count; i++) free(extra_incs[i]);
            filelist_free(&sources);
            return 1;
        }
    } else {
        cdo_log_info("crate '%s' e2e/ module is up to date", crate->name);
    }

    // Update progress
    if (progress && completed_units) {
        *completed_units += compilable_count;
        cli_out_progress_update(progress, *completed_units);
    }

    // --- Link e2e executable (skip if up to date) ---
    // If no sources were recompiled and the artifact already exists with a newer
    // mtime than all object files, the link step can be skipped. This is critical
    // for self-hosting scenarios where a child `cdo e2e` runs while the parent's
    // e2e executable is still active (Windows locks running executables).
    if (dirty_count == 0 && pal_path_exists(e2e_artifact_path) == 0) {
        uint64_t artifact_mtime = 0;
        bool link_needed = false;
        if (pal_file_mtime(e2e_artifact_path, &artifact_mtime) == 0) {
            for (int i = 0; i < compilable_count; i++) {
                int src_idx = compilable_indices[i];
                char obj_path[260];
                object_path_from_source(sources.paths[src_idx], e2e_obj_dir, obj_path, sizeof(obj_path));
                uint64_t obj_mtime = 0;
                if (pal_file_mtime(obj_path, &obj_mtime) != 0 || obj_mtime > artifact_mtime) {
                    link_needed = true;
                    break;
                }
            }
        } else {
            link_needed = true;
        }

        if (!link_needed) {
            cdo_log_info("Linked: %s", e2e_artifact_path);
            // Store artifact path in the module struct
            strncpy(e2e_mod->artifact_path, e2e_artifact_path, sizeof(e2e_mod->artifact_path) - 1);
            e2e_mod->artifact_path[sizeof(e2e_mod->artifact_path) - 1] = '\0';

            free(dirty_indices);
            free(compilable_indices);
            free(all_includes);
            for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
            free(inc_paths);
            for (int i = 0; i < extra_inc_count; i++) free(extra_incs[i]);
            filelist_free(&sources);
            return 0;
        }
    }
    const char** link_obj_paths = (const char**)malloc(compilable_count * sizeof(const char*));
    char** link_obj_bufs = (char**)calloc(compilable_count, sizeof(char*));
    if (!link_obj_paths || !link_obj_bufs) {
        cdo_log_error("out of memory for link step");
        free(link_obj_paths);
        free(link_obj_bufs);
        free(dirty_indices);
        free(compilable_indices);
        free(all_includes);
        for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
        free(inc_paths);
        for (int i = 0; i < extra_inc_count; i++) free(extra_incs[i]);
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
            for (int i2 = 0; i2 < extra_inc_count; i2++) free(extra_incs[i2]);
            filelist_free(&sources);
            return 1;
        }
        object_path_from_source(sources.paths[src_idx], e2e_obj_dir, link_obj_bufs[i], 260);
        link_obj_paths[i] = link_obj_bufs[i];
    }

    char artifact_path[260];
    strncpy(artifact_path, e2e_artifact_path, sizeof(artifact_path) - 1);
    artifact_path[sizeof(artifact_path) - 1] = '\0';

    // Collect libraries to link against:
    // 1. Own crate's library module artifact (if present)
    // 2. Implicit deps: cdo_ut, cdo_e2e
    // 3. Declared dependency crate library artifacts (deduplicated with implicit)
    // 4. Platform link_libs
    const char* link_libs[192];
    int link_lib_count = 0;
    const char* lib_paths[64];
    int lib_path_count = 0;

    // Own crate's lib (if present — e2e modules don't require lib/)
    if (crate->has_lib) {
        lib_paths[lib_path_count++] = build_dir;
        link_libs[link_lib_count++] = crate->name;
    }

    // Req 10.1, 10.2: Add implicit deps (cdo_ut, cdo_e2e)
    {
        // cdo_ut lib path
        char ut_build_dir[260];
        build_dir_for_crate(ws, &ws->crates[cdo_ut_idx], profile, ut_build_dir, sizeof(ut_build_dir));
        static char ut_lib_path_buf[260];
        strncpy(ut_lib_path_buf, ut_build_dir, 259);
        ut_lib_path_buf[259] = '\0';
        lib_paths[lib_path_count++] = ut_lib_path_buf;

        // cdo_e2e lib path
        char e2e_lib_build_dir[260];
        build_dir_for_crate(ws, &ws->crates[cdo_e2e_idx], profile, e2e_lib_build_dir, sizeof(e2e_lib_build_dir));
        static char e2e_lib_path_buf[260];
        strncpy(e2e_lib_path_buf, e2e_lib_build_dir, 259);
        e2e_lib_path_buf[259] = '\0';
        lib_paths[lib_path_count++] = e2e_lib_path_buf;

        // Req 10.4: Deduplicate — only add if not already a declared dep
        bool ut_in_deps = false;
        bool e2e_in_deps = false;
        for (int d = 0; d < crate->dep_count; d++) {
            int dep_idx = crate->dep_indices[d];
            if (dep_idx == cdo_ut_idx) ut_in_deps = true;
            if (dep_idx == cdo_e2e_idx) e2e_in_deps = true;
        }
        if (!ut_in_deps) {
            link_libs[link_lib_count++] = "cdo_ut";
        }
        if (!e2e_in_deps) {
            link_libs[link_lib_count++] = "cdo_e2e";
        }
    }

    // Declared dependency crate library artifacts
    for (int d = 0; d < crate->dep_count; d++) {
        int dep_idx = crate->dep_indices[d];
        if (dep_idx < 0 || dep_idx >= ws->crate_count) continue;
        Crate* dep_crate = &ws->crates[dep_idx];

        if (dep_crate->has_lib && lib_path_count < 64) {
            char dep_build_dir[260];
            build_dir_for_crate(ws, dep_crate, profile, dep_build_dir, sizeof(dep_build_dir));

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

    // Dev-dependencies
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

    // Platform link libs
    for (int l = 0; l < crate->link_lib_count && link_lib_count < 192; l++) {
        link_libs[link_lib_count++] = crate->link_libs[l];
    }

    // Re-add own crate's lib at end of link line to resolve circular references.
    // GCC's single-pass linker needs this when implicit deps (cdo_e2e) reference
    // symbols from the crate's own lib (e.g., PAL functions).
    if (crate->has_lib && link_lib_count < 192) {
        link_libs[link_lib_count++] = crate->name;
    }

    // Link as executable
    LinkJob link_job = {0};
    link_job.object_paths = link_obj_paths;
    link_job.object_count = compilable_count;
    link_job.output_path = artifact_path;
    link_job.lib_paths = lib_paths;
    link_job.lib_path_count = lib_path_count;
    link_job.link_libs = link_libs;
    link_job.link_lib_count = link_lib_count;
    link_job.shared = false;

    // Use C++ linker if any source is C++
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

    // Artifact freshness check: gather all inputs (objects + dep lib artifacts)
    {
        int fresh_input_count = compilable_count;
        int dep_lib_artifact_count = 0;
        if (crate->has_lib && crate->modules[MODULE_LIB].artifact_path[0] != '\0') dep_lib_artifact_count++;
        for (int d = 0; d < crate->dep_count; d++) {
            int dep_idx = crate->dep_indices[d];
            if (dep_idx < 0 || dep_idx >= ws->crate_count) continue;
            Crate* dep_crate = &ws->crates[dep_idx];
            if (dep_crate->has_lib && dep_crate->modules[MODULE_LIB].artifact_path[0] != '\0') dep_lib_artifact_count++;
        }
        for (int d = 0; d < crate->dev_dep_count; d++) {
            int dep_idx = crate->dev_dep_indices[d];
            if (dep_idx < 0 || dep_idx >= ws->crate_count) continue;
            Crate* dep_crate = &ws->crates[dep_idx];
            if (dep_crate->has_lib && dep_crate->modules[MODULE_LIB].artifact_path[0] != '\0') dep_lib_artifact_count++;
        }
        fresh_input_count += dep_lib_artifact_count;

        const char** fresh_inputs = (const char**)malloc(sizeof(const char*) * (size_t)fresh_input_count);
        if (fresh_inputs) {
            int fi = 0;
            for (int i = 0; i < compilable_count; i++) fresh_inputs[fi++] = link_obj_paths[i];
            if (crate->has_lib && crate->modules[MODULE_LIB].artifact_path[0] != '\0') {
                fresh_inputs[fi++] = crate->modules[MODULE_LIB].artifact_path;
            }
            for (int d = 0; d < crate->dep_count; d++) {
                int dep_idx = crate->dep_indices[d];
                if (dep_idx < 0 || dep_idx >= ws->crate_count) continue;
                Crate* dep_crate = &ws->crates[dep_idx];
                if (dep_crate->has_lib && dep_crate->modules[MODULE_LIB].artifact_path[0] != '\0') {
                    fresh_inputs[fi++] = dep_crate->modules[MODULE_LIB].artifact_path;
                }
            }
            for (int d = 0; d < crate->dev_dep_count; d++) {
                int dep_idx = crate->dev_dep_indices[d];
                if (dep_idx < 0 || dep_idx >= ws->crate_count) continue;
                Crate* dep_crate = &ws->crates[dep_idx];
                if (dep_crate->has_lib && dep_crate->modules[MODULE_LIB].artifact_path[0] != '\0') {
                    fresh_inputs[fi++] = dep_crate->modules[MODULE_LIB].artifact_path;
                }
            }

            if (compiler_link_is_fresh(artifact_path, fresh_inputs, fi)) {
                cdo_log_debug("artifact up-to-date, skipping link: %s", artifact_path);
                free(fresh_inputs);
                strncpy(e2e_mod->artifact_path, artifact_path, sizeof(e2e_mod->artifact_path) - 1);
                e2e_mod->artifact_path[sizeof(e2e_mod->artifact_path) - 1] = '\0';
                for (int i = 0; i < compilable_count; i++) free(link_obj_bufs[i]);
                free(link_obj_bufs);
                free(link_obj_paths);
                free(dirty_indices);
                free(compilable_indices);
                free(all_includes);
                for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
                free(inc_paths);
                for (int i = 0; i < extra_inc_count; i++) free(extra_incs[i]);
                filelist_free(&sources);
                return 0;
            }
            free(fresh_inputs);
        }
    }

    cdo_log_info("Linking e2e/ module: %s", artifact_path);
    rc = compiler_link(&link_job, &link_compiler);

    if (rc != 0) {
        cdo_log_error("linking failed for e2e/ module in crate '%s'", crate->name);
        for (int i = 0; i < compilable_count; i++) free(link_obj_bufs[i]);
        free(link_obj_bufs);
        free(link_obj_paths);
        free(dirty_indices);
        free(compilable_indices);
        free(all_includes);
        for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
        free(inc_paths);
        for (int i = 0; i < extra_inc_count; i++) free(extra_incs[i]);
        filelist_free(&sources);
        return 1;
    }

    // Store artifact path in the module struct
    strncpy(e2e_mod->artifact_path, artifact_path, sizeof(e2e_mod->artifact_path) - 1);
    e2e_mod->artifact_path[sizeof(e2e_mod->artifact_path) - 1] = '\0';

    // Cleanup
    for (int i = 0; i < compilable_count; i++) free(link_obj_bufs[i]);
    free(link_obj_bufs);
    free(link_obj_paths);
    free(dirty_indices);
    free(compilable_indices);
    free(all_includes);
    for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
    free(inc_paths);
    for (int i = 0; i < extra_inc_count; i++) free(extra_incs[i]);
    filelist_free(&sources);

    return 0;
}
