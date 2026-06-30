#include "cmd_build_internal.h"
#include "core/compiler.h"
#include "model/scanner.h"
#include "core/log.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Legacy crate build (crates without module directories)
// ---------------------------------------------------------------------------

int build_legacy_crate(const LegacyBuildCtx* ctx) {
    const Workspace* ws = ctx->ws;
    Crate* crate = ctx->crate;
    const CompilerInfo* compiler = ctx->compiler;
    const char* profile = ctx->profile;
    const BuildProfile* build_prof = ctx->build_prof;
    int jobs = ctx->jobs;
    const char** coverage_flags = ctx->coverage_flags;
    int coverage_flag_count = ctx->coverage_flag_count;
    const CacheConfig* cache_config = ctx->cache_config;
    CacheStats* cache_stats = ctx->cache_stats;
    bool no_cache = ctx->no_cache;
    CliProgressBar* progress = ctx->progress;
    int* completed_units = ctx->completed_units;
    const char* crate_full_path = ctx->crate_full_path;
    const char* build_dir = ctx->build_dir;
    const char** dep_include_paths = ctx->dep_include_paths;
    int dep_include_count = ctx->dep_include_count;
    const char** dep_lib_paths = ctx->dep_lib_paths;
    int dep_lib_count = ctx->dep_lib_count;
    const char** dep_link_libs = ctx->dep_link_libs;
    int dep_link_lib_count = ctx->dep_link_lib_count;

    int rc = 0;
    int failed = 0;

    // Scan sources
    FileList sources = {0};
    rc = scanner_scan_sources(crate_full_path, NULL, 0, &sources);
    if (rc != 0) {
        cdo_log_error("failed to scan sources for crate '%s'", crate->name);
        return 1;
    }

    if (sources.count == 0) {
        cdo_log_warn("crate '%s' has no source files", crate->name);
        filelist_free(&sources);
        return -1; // skip
    }

    // Build list of compilable sources (skip headers)
    int compilable_src_count = 0;
    int* compilable_indices = (int*)malloc(sizeof(int) * sources.count);
    if (!compilable_indices) {
        filelist_free(&sources);
        return 1;
    }
    for (int s = 0; s < sources.count; s++) {
        const char* ext = pal_path_ext(sources.paths[s]);
        if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0 ||
                   strcmp(ext, ".cxx") == 0 || strcmp(ext, ".cc") == 0)) {
            compilable_indices[compilable_src_count++] = s;
        }
    }

    if (compilable_src_count == 0) {
        cdo_log_warn("crate '%s' has no compilable source files", crate->name);
        free(compilable_indices);
        filelist_free(&sources);
        return -1; // skip
    }

    // Compute dirty set
    int* dirty_indices = (int*)malloc(sizeof(int) * compilable_src_count);
    int dirty_count = 0;
    if (!dirty_indices) {
        free(compilable_indices);
        filelist_free(&sources);
        return 1;
    }

    for (int ci = 0; ci < compilable_src_count; ci++) {
        int src_idx = compilable_indices[ci];
        const char* src_path = sources.paths[src_idx];
        char obj_path[260];
        object_path_from_source(src_path, build_dir, obj_path, sizeof(obj_path));
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
    free(compilable_indices);

    if (dirty_count == 0) {
        cdo_log_info("crate '%s' is up to date", crate->name);
        *completed_units += compilable_src_count;
        cli_out_progress_update(progress, *completed_units);
        filelist_free(&sources);
        free(dirty_indices);
        return 0;
    }

    cdo_log_info("Compiling crate '%s' (%d files)", crate->name, dirty_count);
    cdo_log_debug("  %s: %d/%d files need rebuild", crate->name, dirty_count, sources.count);

    // Include paths: crate's own src/ and include/ directories, plus deps
    char crate_src[260];
    pal_path_join(crate_src, sizeof(crate_src), crate_full_path, "src");
    char crate_inc[260];
    pal_path_join(crate_inc, sizeof(crate_inc), crate_full_path, "include");

    const char* all_includes[128] = {0};
    int all_include_count = 0;
    all_includes[all_include_count++] = crate_src;
    if (pal_path_exists(crate_inc) == 0) {
        all_includes[all_include_count++] = crate_inc;
    }
    for (int d = 0; d < dep_include_count && all_include_count < 128; d++) {
        all_includes[all_include_count++] = dep_include_paths[d];
    }

    CompileJob* compile_jobs = (CompileJob*)calloc(dirty_count, sizeof(CompileJob));
    char** obj_paths = (char**)calloc(dirty_count, sizeof(char*));
    if (!compile_jobs || !obj_paths) {
        cdo_log_error("out of memory allocating compile jobs");
        filelist_free(&sources);
        free(dirty_indices);
        free(compile_jobs);
        free(obj_paths);
        return 1;
    }

    // Merge crate-level defines with profile defines
    int merged_define_count = build_prof->define_count + crate->define_count;
    const char** merged_defines = NULL;
    if (merged_define_count > 0) {
        merged_defines = (const char**)malloc(sizeof(const char*) * (size_t)merged_define_count);
        if (merged_defines) {
            int idx = 0;
            for (int d = 0; d < build_prof->define_count; d++)
                merged_defines[idx++] = build_prof->defines[d];
            for (int d = 0; d < crate->define_count; d++)
                merged_defines[idx++] = crate->defines[d];
        } else {
            merged_define_count = 0;
        }
    }

    for (int d = 0; d < dirty_count; d++) {
        int src_idx = dirty_indices[d];
        const char* src_path = sources.paths[src_idx];
        obj_paths[d] = (char*)malloc(260);
        if (!obj_paths[d]) { cdo_log_error("out of memory"); failed = 1; break; }
        object_path_from_source(src_path, build_dir, obj_paths[d], 260);
        compile_jobs[d].source_path = src_path;
        compile_jobs[d].object_path = obj_paths[d];
        compile_jobs[d].include_paths = all_includes;
        compile_jobs[d].include_path_count = all_include_count;
        compile_jobs[d].optimize = build_prof->optimize;
        compile_jobs[d].debug_info = build_prof->debug_info;
        compile_jobs[d].defines = merged_define_count > 0 ? merged_defines : NULL;
        compile_jobs[d].define_count = merged_define_count;
        {
            int total_extra = build_prof->extra_flag_count + coverage_flag_count;
            if (total_extra > 0) {
                static const char* merged_extra_flags[BUILD_PROFILE_MAX_FLAGS + 2];
                int mf = 0;
                for (int f = 0; f < build_prof->extra_flag_count; f++)
                    merged_extra_flags[mf++] = build_prof->extra_flags[f];
                for (int f = 0; f < coverage_flag_count; f++)
                    merged_extra_flags[mf++] = coverage_flags[f];
                compile_jobs[d].extra_flags = merged_extra_flags;
                compile_jobs[d].extra_flag_count = total_extra;
            }
        }
        if (is_cpp_source(src_path)) {
            compile_jobs[d].c_standard = NULL;
            compile_jobs[d].cpp_standard = cpp_standard_str(crate->cpp_standard);
        } else {
            compile_jobs[d].c_standard = c_standard_str(crate->c_standard);
            compile_jobs[d].cpp_standard = NULL;
        }
    }

    if (failed) {
        for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
        free(obj_paths); free(compile_jobs); free(merged_defines);
        filelist_free(&sources); free(dirty_indices);
        return 1;
    }

    rc = compiler_compile_batch(compile_jobs, dirty_count, compiler, jobs, cache_config, cache_stats, no_cache);
    if (rc != 0) {
        cdo_log_error("compilation failed for crate '%s'", crate->name);
        for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
        free(obj_paths); free(compile_jobs); free(merged_defines);
        filelist_free(&sources); free(dirty_indices);
        return 1;
    }

    *completed_units += compilable_src_count;
    cli_out_progress_update(progress, *completed_units);

    // Link - collect ALL object files (not just dirty)
    int compilable_count = 0;
    for (int s = 0; s < sources.count; s++) {
        if (is_cpp_source(sources.paths[s]) ||
            (pal_path_ext(sources.paths[s]) && strcmp(pal_path_ext(sources.paths[s]), ".c") == 0))
            compilable_count++;
    }

    char** all_obj_paths = (char**)calloc(compilable_count, sizeof(char*));
    if (!all_obj_paths) {
        cdo_log_error("out of memory");
        for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
        free(obj_paths); free(compile_jobs); free(merged_defines);
        filelist_free(&sources); free(dirty_indices);
        return 1;
    }

    int obj_idx = 0;
    for (int s = 0; s < sources.count; s++) {
        const char* ext = pal_path_ext(sources.paths[s]);
        bool compilable = is_cpp_source(sources.paths[s]) || (ext && strcmp(ext, ".c") == 0);
        if (!compilable) continue;
        all_obj_paths[obj_idx] = (char*)malloc(260);
        if (!all_obj_paths[obj_idx]) { cdo_log_error("out of memory"); failed = 1; break; }
        object_path_from_source(sources.paths[s], build_dir, all_obj_paths[obj_idx], 260);
        obj_idx++;
    }

    if (!failed) {
        // For test crates depending on executables, recompile dep sources (excl main)
        int dep_obj_count = 0;
        char** dep_obj_paths_arr = NULL;

        if (crate->type == CRATE_TEST) {
            for (int d = 0; d < crate->dep_count; d++) {
                int dep_idx_loop = crate->dep_indices[d];
                Crate* dep_crate = &ws->crates[dep_idx_loop];
                if (dep_crate->type != CRATE_EXECUTABLE) continue;
                char dep_full_path[260];
                pal_path_join(dep_full_path, sizeof(dep_full_path), ws->root_path, dep_crate->path);
                FileList dep_sources = {0};
                if (scanner_scan_sources(dep_full_path, NULL, 0, &dep_sources) != 0) continue;
                int dep_compilable = 0;
                for (int s = 0; s < dep_sources.count; s++) {
                    const char* ext2 = pal_path_ext(dep_sources.paths[s]);
                    bool comp = is_cpp_source(dep_sources.paths[s]) || (ext2 && strcmp(ext2, ".c") == 0);
                    if (!comp) continue;
                    const char* fn = dep_sources.paths[s];
                    const char* pp = fn;
                    while (*pp) { if (*pp == '/' || *pp == '\\') fn = pp + 1; pp++; }
                    if (strcmp(fn, "main.c") == 0 || strcmp(fn, "main.cpp") == 0 ||
                        strcmp(fn, "main.cxx") == 0 || strcmp(fn, "main.cc") == 0) continue;
                    dep_compilable++;
                }
                if (dep_compilable == 0) { filelist_free(&dep_sources); continue; }
                char** new_dep_objs = (char**)realloc(dep_obj_paths_arr, (dep_obj_count + dep_compilable) * sizeof(char*));
                if (!new_dep_objs) { filelist_free(&dep_sources); continue; }
                dep_obj_paths_arr = new_dep_objs;
                CompileJob* dep_jobs = (CompileJob*)calloc(dep_compilable, sizeof(CompileJob));
                char** dep_obj_bufs = (char**)calloc(dep_compilable, sizeof(char*));
                if (!dep_jobs || !dep_obj_bufs) {
                    free(dep_jobs); free(dep_obj_bufs);
                    filelist_free(&dep_sources); continue;
                }
                int dj = 0;
                for (int s = 0; s < dep_sources.count; s++) {
                    const char* ext2 = pal_path_ext(dep_sources.paths[s]);
                    bool comp = is_cpp_source(dep_sources.paths[s]) || (ext2 && strcmp(ext2, ".c") == 0);
                    if (!comp) continue;
                    const char* fn = dep_sources.paths[s];
                    const char* pp = fn;
                    while (*pp) { if (*pp == '/' || *pp == '\\') fn = pp + 1; pp++; }
                    if (strcmp(fn, "main.c") == 0 || strcmp(fn, "main.cpp") == 0 ||
                        strcmp(fn, "main.cxx") == 0 || strcmp(fn, "main.cc") == 0) continue;
                    dep_obj_bufs[dj] = (char*)malloc(260);
                    if (!dep_obj_bufs[dj]) { dj++; continue; }
                    object_path_from_source(dep_sources.paths[s], build_dir, dep_obj_bufs[dj], 260);
                    dep_jobs[dj].source_path = dep_sources.paths[s];
                    dep_jobs[dj].object_path = dep_obj_bufs[dj];
                    dep_jobs[dj].include_paths = all_includes;
                    dep_jobs[dj].include_path_count = all_include_count;
                    dep_jobs[dj].optimize = build_prof->optimize;
                    dep_jobs[dj].debug_info = build_prof->debug_info;
                    dep_jobs[dj].defines = merged_defines;
                    dep_jobs[dj].define_count = merged_define_count;
                    if (is_cpp_source(dep_sources.paths[s]))
                        dep_jobs[dj].cpp_standard = cpp_standard_str(dep_crate->cpp_standard);
                    else
                        dep_jobs[dj].c_standard = c_standard_str(dep_crate->c_standard);
                    dj++;
                }
                cdo_log_info("Compiling dep '%s' for test (%d files)", dep_crate->name, dj);
                int dep_rc = compiler_compile_batch(dep_jobs, dj, compiler, jobs, cache_config, cache_stats, no_cache);
                if (dep_rc != 0) {
                    cdo_log_error("failed to compile dep '%s' for test crate", dep_crate->name);
                    for (int x = 0; x < dj; x++) free(dep_obj_bufs[x]);
                    free(dep_obj_bufs); free(dep_jobs);
                    filelist_free(&dep_sources);
                    failed = 1; break;
                }
                for (int x = 0; x < dj; x++) dep_obj_paths_arr[dep_obj_count++] = dep_obj_bufs[x];
                free(dep_obj_bufs); free(dep_jobs);
                filelist_free(&dep_sources);
            }
        }

        char output_path[260];
        output_path_for_crate(ws, crate, profile, output_path, sizeof(output_path));

        // Merge dependency link libs with crate's own link libs
        const char* all_link_libs[192];
        int all_link_lib_count = 0;
        for (int l = 0; l < dep_link_lib_count && all_link_lib_count < 192; l++)
            all_link_libs[all_link_lib_count++] = dep_link_libs[l];
        for (int d = 0; d < crate->dep_count; d++) {
            int dep_idx = crate->dep_indices[d];
            Crate* dep_crate = &ws->crates[dep_idx];
            if (dep_crate->module_count > 0 && dep_crate->has_lib) {
                for (int l = 0; l < dep_crate->link_lib_count && all_link_lib_count < 192; l++)
                    all_link_libs[all_link_lib_count++] = dep_crate->link_libs[l];
            }
        }
        for (int l = 0; l < crate->link_lib_count && all_link_lib_count < 192; l++)
            all_link_libs[all_link_lib_count++] = crate->link_libs[l];

        // Combine own objects + dep objects for linking
        int total_link_objs = compilable_count + dep_obj_count;
        const char** merged_obj_paths = (const char**)malloc(total_link_objs * sizeof(const char*));
        if (!merged_obj_paths) {
            cdo_log_error("out of memory");
            for (int oi = 0; oi < dep_obj_count; oi++) free(dep_obj_paths_arr[oi]);
            free(dep_obj_paths_arr);
            failed = 1;
        } else {
            for (int oi = 0; oi < compilable_count; oi++) merged_obj_paths[oi] = all_obj_paths[oi];
            for (int oi = 0; oi < dep_obj_count; oi++) merged_obj_paths[compilable_count + oi] = dep_obj_paths_arr[oi];
        }

        if (!failed) {
            LinkJob link_job = {0};
            link_job.object_paths = merged_obj_paths;
            link_job.object_count = total_link_objs;
            link_job.output_path = output_path;
            link_job.lib_paths = dep_lib_paths;
            link_job.lib_path_count = dep_lib_count;
            link_job.link_libs = all_link_libs;
            link_job.link_lib_count = all_link_lib_count;
            link_job.shared = (crate->type == CRATE_SHARED_LIB);
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

            cdo_log_info("Linking %s", output_path);

            // Artifact freshness check: use merged objects as inputs
            if (compiler_link_is_fresh(output_path, merged_obj_paths, total_link_objs)) {
                cdo_log_debug("artifact up-to-date, skipping link: %s", output_path);
            } else {
                rc = compiler_link(&link_job, &link_compiler);
                if (rc != 0) { cdo_log_error("linking failed for crate '%s'", crate->name); failed = 1; }
            }

            if (!failed && crate->type == CRATE_EXECUTABLE) {
                int deployed = deploy_catalog_files(ws->root_path, build_dir);
                if (deployed > 0)
                    cdo_log_debug("deployed %d catalog file(s) to %s/catalogs/", deployed, build_dir);
            }
        }

        free(merged_obj_paths);
        for (int oi = 0; oi < dep_obj_count; oi++) free(dep_obj_paths_arr[oi]);
        free(dep_obj_paths_arr);
    }

    // Cleanup
    for (int s = 0; s < compilable_count; s++) free(all_obj_paths[s]);
    free(all_obj_paths);
    for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
    free(obj_paths); free(compile_jobs); free(merged_defines);
    filelist_free(&sources); free(dirty_indices);

    return failed ? 1 : 0;
}
