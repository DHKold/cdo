#include "commands/cmd_build.h"
#include "core/workspace.h"
#include "core/compiler.h"
#include "core/scanner.h"
#include "core/module.h"
#include "core/toml.h"
#include "core/output.h"
#include "core/deps.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Build Profile
// ---------------------------------------------------------------------------

/// Maximum number of defines or extra flags a profile can hold.
#define BUILD_PROFILE_MAX_DEFINES 32
#define BUILD_PROFILE_MAX_FLAGS   32

/// Build profile loaded from workspace manifest [workspace.profiles.<name>].
typedef struct {
    bool        optimize;
    bool        debug_info;
    char*       defines[BUILD_PROFILE_MAX_DEFINES];
    int         define_count;
    char*       extra_flags[BUILD_PROFILE_MAX_FLAGS];
    int         extra_flag_count;
    bool        loaded;   // true if profile was found and loaded from manifest
} BuildProfile;

/// Free heap-allocated strings within a BuildProfile.
static void build_profile_free(BuildProfile* p) {
    if (!p) return;
    for (int i = 0; i < p->define_count; i++) {
        free(p->defines[i]);
        p->defines[i] = NULL;
    }
    for (int i = 0; i < p->extra_flag_count; i++) {
        free(p->extra_flags[i]);
        p->extra_flags[i] = NULL;
    }
    p->define_count = 0;
    p->extra_flag_count = 0;
}

/// Load a build profile by name from the workspace manifest.
/// Reads [workspace.profiles.<profile_name>] from the cdo.toml at ws_root.
/// If the profile section is not found, falls back to built-in defaults:
///   "debug"   -> optimize=false, debug=true,  defines=["DEBUG"]
///   "release" -> optimize=true,  debug=false, defines=["NDEBUG"]
/// Returns 0 on success, non-zero on read/parse failure (profile defaults still set).
static int build_profile_load(const char* ws_root, const char* profile_name,
                              BuildProfile* out) {
    memset(out, 0, sizeof(BuildProfile));

    // Set built-in defaults based on well-known profile names
    if (strcmp(profile_name, "release") == 0) {
        out->optimize = true;
        out->debug_info = false;
        out->defines[0] = strdup("NDEBUG");
        out->define_count = 1;
    } else if (strcmp(profile_name, "relwithdebinfo") == 0) {
        out->optimize = true;
        out->debug_info = true;
        out->defines[0] = strdup("NDEBUG");
        out->define_count = 1;
    } else {
        // Default: debug profile
        out->optimize = false;
        out->debug_info = true;
        out->defines[0] = strdup("DEBUG");
        out->define_count = 1;
    }

    // Attempt to read the workspace manifest for custom profile overrides
    char manifest_path[520];
    if (pal_path_join(manifest_path, sizeof(manifest_path), ws_root, "cdo.toml") != 0) {
        return -1;
    }

    char* buf = NULL;
    size_t buf_len = 0;
    if (pal_file_read(manifest_path, &buf, &buf_len) != 0) {
        // No cdo.toml found — use built-in defaults (not an error for profile loading)
        out->loaded = false;
        return 0;
    }

    TomlTable* root = NULL;
    TomlError err;
    if (toml_parse(buf, buf_len, &root, &err) != 0) {
        free(buf);
        out->loaded = false;
        return 0; // Parse error — use defaults silently
    }
    free(buf);

    // Look up [workspace.profiles.<profile_name>]
    char key_path[128];
    snprintf(key_path, sizeof(key_path), "workspace.profiles.%s", profile_name);

    const TomlValue* profile_val = toml_get(root, key_path);
    if (!profile_val || (profile_val->type != TOML_TABLE &&
                         profile_val->type != TOML_INLINE_TABLE)) {
        // Profile not defined in manifest — keep built-in defaults
        toml_free(root);
        out->loaded = false;
        return 0;
    }

    out->loaded = true;

    // Read "optimize" (bool)
    char opt_key[160];
    snprintf(opt_key, sizeof(opt_key), "%s.optimize", key_path);
    const TomlValue* opt_val = toml_get(root, opt_key);
    if (opt_val && opt_val->type == TOML_BOOL) {
        out->optimize = opt_val->as.boolean;
    }

    // Read "debug" (bool)
    char dbg_key[160];
    snprintf(dbg_key, sizeof(dbg_key), "%s.debug", key_path);
    const TomlValue* dbg_val = toml_get(root, dbg_key);
    if (dbg_val && dbg_val->type == TOML_BOOL) {
        out->debug_info = dbg_val->as.boolean;
    }

    // Read "defines" (array of strings) — overrides defaults
    char def_key[160];
    snprintf(def_key, sizeof(def_key), "%s.defines", key_path);
    const TomlValue* def_val = toml_get(root, def_key);
    if (def_val && def_val->type == TOML_ARRAY && def_val->as.array) {
        // Clear default defines since the manifest provides explicit ones
        for (int i = 0; i < out->define_count; i++) {
            free(out->defines[i]);
            out->defines[i] = NULL;
        }
        out->define_count = 0;

        TomlArray* arr = def_val->as.array;
        for (int i = 0; i < arr->count && out->define_count < BUILD_PROFILE_MAX_DEFINES; i++) {
            TomlValue* item = arr->items[i];
            if (item && item->type == TOML_STRING && item->as.string) {
                out->defines[out->define_count] = strdup(item->as.string);
                out->define_count++;
            }
        }
    }

    // Read "flags" (array of strings) — extra compiler flags
    char flags_key[160];
    snprintf(flags_key, sizeof(flags_key), "%s.flags", key_path);
    const TomlValue* flags_val = toml_get(root, flags_key);
    if (flags_val && flags_val->type == TOML_ARRAY && flags_val->as.array) {
        TomlArray* arr = flags_val->as.array;
        for (int i = 0; i < arr->count && out->extra_flag_count < BUILD_PROFILE_MAX_FLAGS; i++) {
            TomlValue* item = arr->items[i];
            if (item && item->type == TOML_STRING && item->as.string) {
                out->extra_flags[out->extra_flag_count] = strdup(item->as.string);
                out->extra_flag_count++;
            }
        }
    }

    toml_free(root);
    return 0;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Determine the build directory path for a crate within the workspace.
/// Format: <ws_root>/build/<profile>/<crate_name>/
static void build_dir_for_crate(const Workspace* ws, const Crate* crate,
                                const char* profile, char* out, size_t out_size) {
    char tmp[260];
    pal_path_join(tmp, sizeof(tmp), ws->root_path, "build");

    char tmp2[260];
    pal_path_join(tmp2, sizeof(tmp2), tmp, profile);

    pal_path_join(out, out_size, tmp2, crate->name);
}

/// Determine the output artifact path for a crate.
static void output_path_for_crate(const Workspace* ws, const Crate* crate,
                                  const char* profile, char* out, size_t out_size) {
    char build_dir[260];
    build_dir_for_crate(ws, crate, profile, build_dir, sizeof(build_dir));

    char artifact[128];
#ifdef _WIN32
    switch (crate->type) {
        case CRATE_EXECUTABLE:
        case CRATE_TEST:
            snprintf(artifact, sizeof(artifact), "%s.exe", crate->name);
            break;
        case CRATE_STATIC_LIB:
            snprintf(artifact, sizeof(artifact), "%s.lib", crate->name);
            break;
        case CRATE_SHARED_LIB:
            snprintf(artifact, sizeof(artifact), "%s.dll", crate->name);
            break;
    }
#else
    switch (crate->type) {
        case CRATE_EXECUTABLE:
        case CRATE_TEST:
            snprintf(artifact, sizeof(artifact), "%s", crate->name);
            break;
        case CRATE_STATIC_LIB:
            snprintf(artifact, sizeof(artifact), "lib%s.a", crate->name);
            break;
        case CRATE_SHARED_LIB:
            snprintf(artifact, sizeof(artifact), "lib%s.so", crate->name);
            break;
    }
#endif
    pal_path_join(out, out_size, build_dir, artifact);
}

/// Convert integer C standard to string flag (e.g., 17 -> "c17").
static const char* c_standard_str(int std_val) {
    switch (std_val) {
        case 11: return "c11";
        case 17: return "c17";
        case 23: return "c23";
        default: return "c17";
    }
}

/// Convert integer C++ standard to string flag (e.g., 20 -> "c++20").
static const char* cpp_standard_str(int std_val) {
    switch (std_val) {
        case 17: return "c++17";
        case 20: return "c++20";
        case 23: return "c++23";
        default: return "c++20";
    }
}

/// Determine the active profile name from options.
static const char* resolve_profile(const CdoOptions* opts) {
    if (opts->profile && opts->profile[0] != '\0') {
        return opts->profile;
    }
    if (opts->release) {
        return "release";
    }
    return "debug";
}

/// Determine parallelism level from options.
static int resolve_jobs(const CdoOptions* opts) {
    if (opts->jobs > 0) {
        return opts->jobs;
    }
    int cpus = pal_cpu_count();
    return (cpus > 0) ? cpus : 1;
}

/// Check if a source file is a C++ file based on extension.
static bool is_cpp_source(const char* path) {
    const char* ext = pal_path_ext(path);
    if (!ext) return false;
    return (strcmp(ext, ".cpp") == 0 || strcmp(ext, ".cxx") == 0 ||
            strcmp(ext, ".cc") == 0  || strcmp(ext, ".CPP") == 0);
}

/// Replace the file extension of a path with .o (or .obj on MSVC).
static void object_path_from_source(const char* source, const char* build_dir,
                                    char* out, size_t out_size) {
    // Extract just the filename from the source path
    const char* filename = source;
    const char* p = source;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            filename = p + 1;
        }
        p++;
    }

    // Build object filename
    char obj_name[260];
    size_t name_len = strlen(filename);
    const char* ext = pal_path_ext(filename);
    size_t base_len = (ext && ext[0]) ? (size_t)(ext - filename) : name_len;

    snprintf(obj_name, sizeof(obj_name), "%.*s.o", (int)base_len, filename);

    pal_path_join(out, out_size, build_dir, obj_name);
}

// ---------------------------------------------------------------------------
// Post-link: Deploy catalog files alongside the binary
// ---------------------------------------------------------------------------

/// Copy catalog TOML files from {workspace_root}/catalogs/ to
/// {build_dir}/catalogs/ so that the built binary can find built-in catalogs
/// relative to its output directory.
/// Returns the number of files copied, or -1 on error.
static int deploy_catalog_files(const char* ws_root, const char* build_dir) {
    char src_dir[520];
    if (pal_path_join(src_dir, sizeof(src_dir), ws_root, "catalogs") != 0) {
        return -1;
    }

    /* Check if workspace has a catalogs/ directory */
    if (pal_path_exists(src_dir) != 1) {
        return 0; /* No catalogs to deploy — not an error */
    }

    /* Create destination catalogs/ directory */
    char dest_dir[520];
    if (pal_path_join(dest_dir, sizeof(dest_dir), build_dir, "catalogs") != 0) {
        return -1;
    }
    pal_mkdir_p(dest_dir);

    /* Copy known catalog .toml files */
    const char* catalog_files[] = { "tools.toml", "packages.toml" };
    int num_catalog_files = 2;
    int copied = 0;

    for (int i = 0; i < num_catalog_files; i++) {
        char src_path[520];
        if (pal_path_join(src_path, sizeof(src_path), src_dir, catalog_files[i]) != 0) {
            continue;
        }

        if (pal_path_exists(src_path) != 1) {
            continue; /* File doesn't exist — skip */
        }

        char* buf = NULL;
        size_t buf_len = 0;
        if (pal_file_read(src_path, &buf, &buf_len) != 0) {
            cdo_warn("failed to read catalog file '%s' for deployment", src_path);
            continue;
        }

        char dest_path[520];
        if (pal_path_join(dest_path, sizeof(dest_path), dest_dir, catalog_files[i]) != 0) {
            free(buf);
            continue;
        }

        if (pal_file_write(dest_path, buf, buf_len) != 0) {
            cdo_warn("failed to write catalog file '%s'", dest_path);
            free(buf);
            continue;
        }

        free(buf);
        copied++;
    }

    return copied;
}

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
static int build_library_module(const Workspace* ws, Crate* crate,
                                const CompilerInfo* compiler,
                                const char* profile,
                                const BuildProfile* build_prof,
                                int jobs,
                                const char** coverage_flags,
                                int coverage_flag_count,
                                ProgressBar* progress,
                                int* completed_units) {
    if (!crate->has_lib) return 0; // Nothing to do

    Module* lib_mod = &crate->modules[MODULE_LIB];

    // --- Scan lib/ for source files ---
    FileList sources = {0};
    int rc = scanner_scan_module_sources(lib_mod->dir_path, MODULE_LIB, NULL, 0, &sources);
    if (rc != 0) {
        cdo_error("failed to scan lib/ sources for crate '%s'", crate->name);
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
        cdo_error("crate '%s': lib/ module has no compilable source files (.c or .cpp)",
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
        cdo_error("failed to compute artifact path for lib/ module in crate '%s'",
                  crate->name);
        filelist_free(&sources);
        return 1;
    }

    // --- Resolve include paths for the library module ---
    char** inc_paths = NULL;
    int inc_count = 0;
    rc = module_include_paths(crate, MODULE_LIB, ws, &inc_paths, &inc_count);
    if (rc != 0) {
        cdo_error("failed to compute include paths for lib/ module in crate '%s'",
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

        if (needs_rebuild) {
            dirty_indices[dirty_count++] = src_idx;
        }
    }

    // --- Compile dirty sources ---
    if (dirty_count > 0) {
        cdo_info("Compiling lib/ module for crate '%s' (%d files)", crate->name, dirty_count);

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
            cdo_error("out of memory allocating compile jobs for lib/ module");
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
        rc = compiler_compile_batch(compile_jobs, dirty_count, compiler, jobs);

        for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
        free(obj_paths);
        free(compile_jobs);
        free(merged_defines);

        if (rc != 0) {
            cdo_error("compilation failed for lib/ module in crate '%s'", crate->name);
            free(dirty_indices);
            free(compilable_indices);
            free(all_includes);
            for (int i = 0; i < inc_count; i++) free(inc_paths[i]);
            free(inc_paths);
            filelist_free(&sources);
            return 1;
        }
    } else {
        cdo_info("crate '%s' lib/ module is up to date", crate->name);
    }

    // Update progress
    if (progress && completed_units) {
        *completed_units += compilable_count;
        progress_update(progress, *completed_units);
    }

    // --- Archive object files into static library ---
    // Collect ALL object file paths (not just dirty ones) for the archive
    const char** archive_obj_paths = (const char**)malloc(compilable_count * sizeof(const char*));
    char** archive_obj_bufs = (char**)calloc(compilable_count, sizeof(char*));
    if (!archive_obj_paths || !archive_obj_bufs) {
        cdo_error("out of memory for archive step");
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

    cdo_info("Archiving lib/ module: %s", artifact_path);
    rc = compiler_link(&link_job, compiler);

    if (rc != 0) {
        cdo_error("archiving failed for lib/ module in crate '%s'", crate->name);
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
static int build_executable_module(const Workspace* ws, Crate* crate,
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
static int build_test_module(const Workspace* ws, Crate* crate,
                             const CompilerInfo* compiler,
                             const char* profile,
                             const BuildProfile* build_prof,
                             int jobs,
                             const char** coverage_flags,
                             int coverage_flag_count,
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
        rc = compiler_compile_batch(compile_jobs, dirty_count, compiler, jobs);

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
static int build_shared_library_module(const Workspace* ws, Crate* crate,
                                       const CompilerInfo* compiler,
                                       const char* profile,
                                       const BuildProfile* build_prof,
                                       int jobs,
                                       const char** coverage_flags,
                                       int coverage_flag_count,
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
        rc = compiler_compile_batch(compile_jobs, dirty_count, compiler, jobs);

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
    // Library search path: the crate's own build dir (where the .lib/.a lives)
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

    // Link against own crate's library module
    link_libs[link_lib_count++] = crate->name;

    // Link against dependency crate libraries
    for (int d = 0; d < crate->dep_count; d++) {
        int dep_idx = crate->dep_indices[d];
        Crate* dep_crate = &ws->crates[dep_idx];
        if (dep_crate->has_lib && link_lib_count < 128) {
            link_libs[link_lib_count++] = dep_crate->name;
        }
    }

    // Platform link libraries from crate manifest
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

// ---------------------------------------------------------------------------
// Crate Module Build Orchestrator
// ---------------------------------------------------------------------------

/// Build all modules within a crate in the correct dependency order.
/// Compiles lib/ first; if it fails, skips all other modules and reports error.
/// After lib/ succeeds, builds exe/, dyn/, tst/ sequentially.
///
/// Inter-crate library linking and transitive dependencies are already handled
/// by the individual module build functions via dep_indices (resolved by BFS
/// in workspace_resolve).
///
/// Requirements: 8.1, 8.2, 8.3, 7.2, 7.5
///
/// @param ws           The workspace
/// @param crate        The crate to build modules for
/// @param compiler     Detected compiler info
/// @param profile      Active build profile name (e.g., "debug", "release")
/// @param build_prof   Loaded build profile settings
/// @param jobs         Parallelism level for compilation
/// @param coverage_flags   Coverage flags array (or NULL)
/// @param coverage_flag_count  Number of coverage flags
/// @param progress     Progress bar (may be NULL)
/// @param completed_units  Pointer to completed unit counter (updated in place)
/// @return 0 on success, non-zero on failure
static int build_crate_modules(const Workspace* ws, Crate* crate,
                               const CompilerInfo* compiler,
                               const char* profile,
                               const BuildProfile* build_prof,
                               int jobs,
                               const char** coverage_flags,
                               int coverage_flag_count,
                               ProgressBar* progress,
                               int* completed_units) {
    int rc = 0;

    // Req 8.1: Build Library_Module FIRST if present
    if (crate->has_lib) {
        rc = build_library_module(ws, crate, compiler, profile,
                                  build_prof, jobs,
                                  coverage_flags, coverage_flag_count,
                                  progress, completed_units);
        if (rc != 0) {
            // Req 8.2: If lib/ fails, skip ALL dependent modules in this crate
            cdo_error("Library module build failed for crate '%s'; "
                      "skipping remaining modules", crate->name);
            return rc;
        }
    }

    // Req 8.1: After lib/ succeeds, build exe/, dyn/, tst/ (sequential for now)

    // Build Executable_Module if present (Req 3.1, 3.2, 3.3, 3.4)
    if (crate->modules[MODULE_EXE].present) {
        rc = build_executable_module(ws, crate, compiler, profile,
                                     build_prof, jobs,
                                     coverage_flags, coverage_flag_count,
                                     progress, completed_units);
        if (rc != 0) {
            cdo_error("Executable module build failed for crate '%s'", crate->name);
            return rc;
        }
    }

    // Build Shared_Library_Module if present (Req 4.1, 4.2, 4.3, 4.4)
    if (crate->modules[MODULE_DYN].present) {
        rc = build_shared_library_module(ws, crate, compiler, profile,
                                         build_prof, jobs,
                                         coverage_flags, coverage_flag_count,
                                         progress, completed_units);
        if (rc != 0) {
            cdo_error("Shared library module build failed for crate '%s'", crate->name);
            return rc;
        }
    }

    // Build Test_Module if present (Req 5.1, 5.2, 5.3, 5.4, 5.5)
    if (crate->modules[MODULE_TST].present) {
        rc = build_test_module(ws, crate, compiler, profile,
                               build_prof, jobs,
                               coverage_flags, coverage_flag_count,
                               progress, completed_units);
        if (rc != 0) {
            cdo_error("Test module build failed for crate '%s'", crate->name);
            return rc;
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// cmd_build implementation
// ---------------------------------------------------------------------------

int cmd_build(const CdoOptions* opts) {
    if (!opts) {
        cdo_error("internal error: NULL options passed to build command");
        return 1;
    }

    uint64_t build_start_ms = pal_time_ms();

    // --- Step 1: Load workspace ---
    Workspace ws = {0};
    char cwd[260];
#ifdef _WIN32
    if (_getcwd(cwd, sizeof(cwd)) == NULL) {
#else
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
#endif
        cdo_error("failed to determine current working directory");
        return 1;
    }

    int rc = workspace_load(cwd, &ws);
    if (rc != 0) {
        cdo_error("failed to load workspace");
        return 1;
    }

    // --- Step 2: Resolve build order ---
    const char** crate_names = NULL;
    int crate_name_count = 0;

    if (opts->positional_count > 0) {
        crate_names = opts->positional_args;
        crate_name_count = opts->positional_count;
    }

    rc = workspace_resolve(&ws, crate_names, crate_name_count);
    if (rc != 0) {
        // workspace_resolve reports errors internally for cycles, etc.
        // Check for unknown crate names specifically.
        if (crate_names) {
            for (int i = 0; i < crate_name_count; i++) {
                bool found = false;
                for (int j = 0; j < ws.crate_count; j++) {
                    if (strcmp(ws.crates[j].name, crate_names[i]) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    cdo_error("unknown crate: '%s'", crate_names[i]);
                }
            }
        }
        workspace_free(&ws);
        return 1;
    }

    // --- Step 3: Verify all specified crate names are valid ---
    if (crate_names) {
        for (int i = 0; i < crate_name_count; i++) {
            bool found = false;
            for (int j = 0; j < ws.crate_count; j++) {
                if (strcmp(ws.crates[j].name, crate_names[i]) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                cdo_error("unknown crate: '%s'", crate_names[i]);
                workspace_free(&ws);
                return 1;
            }
        }
    }

    // --- Step 4: Detect compiler ---
    CompilerInfo compiler = {0};
    rc = compiler_detect(&compiler);
    if (rc != 0) {
        cdo_error("no C/C++ compiler found on PATH or vendored tools");
        workspace_free(&ws);
        return 1;
    }

    // If compiler was found via a relative/absolute path (vendored), prepend its
    // directory to PATH so sub-tools (as, ld, etc.) can be found by the compiler.
    {
        const char* slash = strrchr(compiler.path, '/');
        const char* bslash = strrchr(compiler.path, '\\');
        const char* sep = slash > bslash ? slash : bslash;
        if (sep != NULL) {
            // Extract directory portion
            char compiler_dir[260];
            size_t dir_len = (size_t)(sep - compiler.path);
            if (dir_len >= sizeof(compiler_dir)) dir_len = sizeof(compiler_dir) - 1;
            memcpy(compiler_dir, compiler.path, dir_len);
            compiler_dir[dir_len] = '\0';

            // Prepend to PATH
            const char* old_path = getenv("PATH");
            if (old_path) {
                size_t new_len = dir_len + 1 + strlen(old_path) + 1;
                char* new_path = (char*)malloc(new_len);
                if (new_path) {
                    snprintf(new_path, new_len, "%s;%s", compiler_dir, old_path);
                    _putenv_s("PATH", new_path);
                    free(new_path);
                    cdo_debug("Prepended vendored tool dir to PATH: %s", compiler_dir);
                }
            } else {
                _putenv_s("PATH", compiler_dir);
            }
        }
    }

    cdo_info("using %s (%s)",
             compiler.family == COMPILER_GCC   ? "gcc" :
             compiler.family == COMPILER_CLANG ? "clang" :
             compiler.family == COMPILER_MSVC  ? "msvc" : "unknown",
             compiler.version);

    // --- Resolve settings ---
    const char* profile = resolve_profile(opts);
    int jobs = resolve_jobs(opts);

    // Load build profile from workspace manifest (or use built-in defaults)
    BuildProfile build_profile;
    build_profile_load(ws.root_path, profile, &build_profile);

    if (build_profile.loaded) {
        cdo_debug("loaded profile '%s' from workspace manifest", profile);
    } else {
        cdo_debug("using built-in defaults for profile '%s'", profile);
    }

    cdo_info("profile: %s, jobs: %d", profile, jobs);

    // --- Coverage flags (--coverage) ---
    static const char* coverage_flags[] = {"-fprofile-arcs", "-ftest-coverage"};
    int coverage_flag_count = opts->coverage ? 2 : 0;

    // --- Step 5: Count total compilation units for progress ---
    int total_units = 0;
    for (int i = 0; i < ws.build_order_count; i++) {
        int idx = ws.build_order[i];
        Crate* crate = &ws.crates[idx];

        char crate_full_path[260];
        pal_path_join(crate_full_path, sizeof(crate_full_path),
                      ws.root_path, crate->path);

        FileList sources = {0};
        if (scanner_scan_sources(crate_full_path, NULL, 0, &sources) == 0) {
            // Count only compilable sources (.c, .cpp, etc.) not headers
            for (int s = 0; s < sources.count; s++) {
                const char* ext = pal_path_ext(sources.paths[s]);
                if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0 ||
                           strcmp(ext, ".cxx") == 0 || strcmp(ext, ".cc") == 0)) {
                    total_units++;
                }
            }
            filelist_free(&sources);
        }
    }

    ProgressBar* progress = progress_create("Building", total_units);
    int completed_units = 0;
    int failed = 0;

    // --- Step 6: Build each crate in dependency order ---
    for (int i = 0; i < ws.build_order_count; i++) {
        int idx = ws.build_order[i];
        Crate* crate = &ws.crates[idx];

        // Resolve full crate path
        char crate_full_path[260];
        pal_path_join(crate_full_path, sizeof(crate_full_path),
                      ws.root_path, crate->path);

        // Build directory for this crate
        char build_dir[260];
        build_dir_for_crate(&ws, crate, profile, build_dir, sizeof(build_dir));
        pal_mkdir_p(build_dir);

        // 5a: Resolve dependencies
        // Collect include paths and link info from dependencies
        const char* dep_include_paths[64] = {0};
        int dep_include_count = 0;
        const char* dep_lib_paths[64] = {0};
        int dep_lib_count = 0;
        const char* dep_link_libs[128] = {0};
        int dep_link_lib_count = 0;

        // Internal crate dependencies: add their include paths
        for (int d = 0; d < crate->dep_count; d++) {
            int dep_idx = crate->dep_indices[d];
            Crate* dep_crate = &ws.crates[dep_idx];

            // Include the dependency crate's include/ and src/ directories
            static char dep_inc_buf[64][260];
            char dep_path[260];
            pal_path_join(dep_path, sizeof(dep_path), ws.root_path, dep_crate->path);

            char inc_path[260];
            pal_path_join(inc_path, sizeof(inc_path), dep_path, "include");
            if (pal_path_exists(inc_path) == 0) {
                strncpy(dep_inc_buf[dep_include_count], inc_path, 259);
                dep_include_paths[dep_include_count] = dep_inc_buf[dep_include_count];
                dep_include_count++;
            }

            // Also add the dependency's src/ directory for header access
            char dep_src_path[260];
            pal_path_join(dep_src_path, sizeof(dep_src_path), dep_path, "src");
            if (pal_path_exists(dep_src_path) == 0 && dep_include_count < 64) {
                strncpy(dep_inc_buf[dep_include_count], dep_src_path, 259);
                dep_include_paths[dep_include_count] = dep_inc_buf[dep_include_count];
                dep_include_count++;
            }

            // Also add the dependency's build dir for linked artifacts
            char dep_build_dir[260];
            build_dir_for_crate(&ws, dep_crate, profile, dep_build_dir, sizeof(dep_build_dir));
            if (dep_crate->type == CRATE_STATIC_LIB || dep_crate->type == CRATE_SHARED_LIB) {
                static char dep_lib_buf[64][260];
                strncpy(dep_lib_buf[dep_lib_count], dep_build_dir, 259);
                dep_lib_paths[dep_lib_count] = dep_lib_buf[dep_lib_count];
                dep_lib_count++;

                static char dep_link_buf[128][64];
                strncpy(dep_link_buf[dep_link_lib_count], dep_crate->name, 63);
                dep_link_libs[dep_link_lib_count] = dep_link_buf[dep_link_lib_count];
                dep_link_lib_count++;
            }
        }

        // 5a.1: Resolve dev-dependencies (conditionally)
        // Include dev-deps when:
        //   - Profile is "debug" (not optimized)
        //   - Crate type is CRATE_TEST (always, regardless of profile)
        // Exclude dev-deps when:
        //   - Profile is "release" (optimized) and crate is not a test crate
        bool include_dev_deps = false;
        if (crate->type == CRATE_TEST) {
            include_dev_deps = true;  // Tests always get dev-deps
        } else if (!build_profile.optimize) {
            include_dev_deps = true;  // Debug profile includes dev-deps
        }

        if (include_dev_deps && crate->dev_dep_count > 0) {
            for (int d = 0; d < crate->dev_dep_count; d++) {
                int dep_idx = crate->dev_dep_indices[d];
                if (dep_idx < 0 || dep_idx >= ws.crate_count) continue;
                Crate* dep_crate = &ws.crates[dep_idx];

                // Include the dev-dependency crate's include/ directory
                static char dev_dep_inc_buf[64][260];
                char dep_path[260];
                pal_path_join(dep_path, sizeof(dep_path), ws.root_path, dep_crate->path);

                char inc_path[260];
                pal_path_join(inc_path, sizeof(inc_path), dep_path, "include");
                if (pal_path_exists(inc_path) == 0 && dep_include_count < 64) {
                    strncpy(dev_dep_inc_buf[dep_include_count], inc_path, 259);
                    dep_include_paths[dep_include_count] = dev_dep_inc_buf[dep_include_count];
                    dep_include_count++;
                }

                // Also add the dev-dependency's build dir for linked artifacts
                char dep_build_dir[260];
                build_dir_for_crate(&ws, dep_crate, profile, dep_build_dir, sizeof(dep_build_dir));
                if (dep_crate->type == CRATE_STATIC_LIB || dep_crate->type == CRATE_SHARED_LIB) {
                    static char dev_dep_lib_buf[64][260];
                    if (dep_lib_count < 64) {
                        strncpy(dev_dep_lib_buf[dep_lib_count], dep_build_dir, 259);
                        dep_lib_paths[dep_lib_count] = dev_dep_lib_buf[dep_lib_count];
                        dep_lib_count++;
                    }

                    static char dev_dep_link_buf[128][64];
                    if (dep_link_lib_count < 128) {
                        strncpy(dev_dep_link_buf[dep_link_lib_count], dep_crate->name, 63);
                        dep_link_libs[dep_link_lib_count] = dev_dep_link_buf[dep_link_lib_count];
                        dep_link_lib_count++;
                    }
                }
            }
        }

        // 5a.2: Build all modules via the orchestrator (Req 8.1, 8.2, 8.3)
        // Builds lib/ first, then exe/, dyn/, tst/ sequentially.
        // If lib/ fails, all other modules are skipped.
        if (crate->module_count > 0) {
            rc = build_crate_modules(&ws, crate, &compiler, profile,
                                     &build_profile, jobs,
                                     coverage_flags, coverage_flag_count,
                                     progress, &completed_units);
            if (rc != 0) {
                failed = 1;
                break;
            }
            // Module-based crate fully built; skip legacy src/ flow
            continue;
        }

        // --- Legacy src/-based build path (crates without module directories) ---

        // 5b: Scan sources
        FileList sources = {0};
        rc = scanner_scan_sources(crate_full_path, NULL, 0, &sources);
        if (rc != 0) {
            cdo_error("failed to scan sources for crate '%s'", crate->name);
            failed = 1;
            break;
        }

        if (sources.count == 0) {
            cdo_warn("crate '%s' has no source files", crate->name);
            filelist_free(&sources);
            continue;
        }

        // 5c: Build list of compilable sources (skip headers)
        int compilable_src_count = 0;
        int* compilable_indices = (int*)malloc(sizeof(int) * sources.count);
        if (!compilable_indices) {
            filelist_free(&sources);
            failed = 1;
            break;
        }
        for (int s = 0; s < sources.count; s++) {
            const char* ext = pal_path_ext(sources.paths[s]);
            if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0 ||
                       strcmp(ext, ".cxx") == 0 || strcmp(ext, ".cc") == 0)) {
                compilable_indices[compilable_src_count++] = s;
            }
        }

        if (compilable_src_count == 0) {
            cdo_warn("crate '%s' has no compilable source files", crate->name);
            free(compilable_indices);
            filelist_free(&sources);
            continue;
        }

        // Compute dirty set: only rebuild files where the object is missing or older than source
        int* dirty_indices = (int*)malloc(sizeof(int) * compilable_src_count);
        int dirty_count = 0;
        if (!dirty_indices) {
            free(compilable_indices);
            filelist_free(&sources);
            failed = 1;
            break;
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
                // Object exists and we have both mtimes
                if (obj_mtime >= src_mtime) {
                    needs_rebuild = false;
                }
            }

            if (needs_rebuild) {
                dirty_indices[dirty_count++] = src_idx;
            }
        }
        free(compilable_indices);

        if (dirty_count == 0) {
            cdo_info("crate '%s' is up to date", crate->name);
            completed_units += compilable_src_count;
            progress_update(progress, completed_units);
            filelist_free(&sources);
            free(dirty_indices);
            continue;
        }

        cdo_info("Compiling crate '%s' (%d files)", crate->name, dirty_count);

        cdo_debug("  %s: %d/%d files need rebuild", crate->name, dirty_count, sources.count);

        // 5d: Build CompileJob array from dirty sources
        // Include paths: crate's own src/ and include/ directories, plus deps
        char crate_src[260];
        pal_path_join(crate_src, sizeof(crate_src), crate_full_path, "src");
        char crate_inc[260];
        pal_path_join(crate_inc, sizeof(crate_inc), crate_full_path, "include");

        // Collect all include paths
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
            cdo_error("out of memory allocating compile jobs");
            filelist_free(&sources);
            free(dirty_indices);
            free(compile_jobs);
            free(obj_paths);
            failed = 1;
            break;
        }

        // Merge crate-level defines with profile defines
        int merged_define_count = build_profile.define_count + crate->define_count;
        const char** merged_defines = NULL;
        if (merged_define_count > 0) {
            merged_defines = (const char**)malloc(sizeof(const char*) * (size_t)merged_define_count);
            if (merged_defines) {
                int idx = 0;
                for (int d = 0; d < build_profile.define_count; d++) {
                    merged_defines[idx++] = build_profile.defines[d];
                }
                for (int d = 0; d < crate->define_count; d++) {
                    merged_defines[idx++] = crate->defines[d];
                }
            } else {
                merged_define_count = 0;
            }
        }

        for (int d = 0; d < dirty_count; d++) {
            int src_idx = dirty_indices[d];
            const char* src_path = sources.paths[src_idx];

            // Compute object path
            obj_paths[d] = (char*)malloc(260);
            if (!obj_paths[d]) {
                cdo_error("out of memory");
                failed = 1;
                break;
            }
            object_path_from_source(src_path, build_dir, obj_paths[d], 260);

            compile_jobs[d].source_path = src_path;
            compile_jobs[d].object_path = obj_paths[d];
            compile_jobs[d].include_paths = all_includes;
            compile_jobs[d].include_path_count = all_include_count;
            compile_jobs[d].optimize = build_profile.optimize;
            compile_jobs[d].debug_info = build_profile.debug_info;

            // Apply merged defines (profile + crate-level)
            if (merged_define_count > 0) {
                compile_jobs[d].defines = merged_defines;
                compile_jobs[d].define_count = merged_define_count;
            } else {
                compile_jobs[d].defines = NULL;
                compile_jobs[d].define_count = 0;
            }

            // Apply profile extra flags + coverage flags
            {
                int total_extra = build_profile.extra_flag_count + coverage_flag_count;
                if (total_extra > 0) {
                    static const char* merged_extra_flags[BUILD_PROFILE_MAX_FLAGS + 2];
                    int mf = 0;
                    for (int f = 0; f < build_profile.extra_flag_count; f++) {
                        merged_extra_flags[mf++] = build_profile.extra_flags[f];
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

        if (failed) {
            for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
            free(obj_paths);
            free(compile_jobs);
            free(merged_defines);
            filelist_free(&sources);
            free(dirty_indices);
            break;
        }

        // 5e: Compile
        rc = compiler_compile_batch(compile_jobs, dirty_count, &compiler, jobs);
        if (rc != 0) {
            cdo_error("compilation failed for crate '%s'", crate->name);
            for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
            free(obj_paths);
            free(compile_jobs);
            free(merged_defines);
            filelist_free(&sources);
            free(dirty_indices);
            failed = 1;
            break;
        }

        completed_units += compilable_src_count;
        progress_update(progress, completed_units);

        // 5f: Link — collect object files from compilable sources only
        // Count compilable sources first
        int compilable_count = 0;
        for (int s = 0; s < sources.count; s++) {
            if (is_cpp_source(sources.paths[s]) ||
                (pal_path_ext(sources.paths[s]) &&
                 strcmp(pal_path_ext(sources.paths[s]), ".c") == 0)) {
                compilable_count++;
            }
        }

        char** all_obj_paths = (char**)calloc(compilable_count, sizeof(char*));
        if (!all_obj_paths) {
            cdo_error("out of memory");
            for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
            free(obj_paths);
            free(compile_jobs);
            free(merged_defines);
            filelist_free(&sources);
            free(dirty_indices);
            failed = 1;
            break;
        }

        int obj_idx = 0;
        for (int s = 0; s < sources.count; s++) {
            const char* ext = pal_path_ext(sources.paths[s]);
            bool compilable = is_cpp_source(sources.paths[s]) ||
                              (ext && strcmp(ext, ".c") == 0);
            if (!compilable) continue;

            all_obj_paths[obj_idx] = (char*)malloc(260);
            if (!all_obj_paths[obj_idx]) {
                cdo_error("out of memory");
                failed = 1;
                break;
            }
            object_path_from_source(sources.paths[s], build_dir, all_obj_paths[obj_idx], 260);
            obj_idx++;
        }

        if (!failed) {
            // 5f.1: For test crates depending on executables, recompile dep sources
            //       (excluding main) with test defines into the test's build directory
            int dep_obj_count = 0;
            char** dep_obj_paths = NULL;

            if (crate->type == CRATE_TEST) {
                for (int d = 0; d < crate->dep_count; d++) {
                    int dep_idx_loop = crate->dep_indices[d];
                    Crate* dep_crate = &ws.crates[dep_idx_loop];
                    if (dep_crate->type != CRATE_EXECUTABLE) continue;

                    char dep_full_path[260];
                    pal_path_join(dep_full_path, sizeof(dep_full_path), ws.root_path, dep_crate->path);

                    FileList dep_sources = {0};
                    if (scanner_scan_sources(dep_full_path, NULL, 0, &dep_sources) != 0) continue;

                    // Count compilable non-main sources
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

                    // Allocate/grow dep_obj_paths
                    char** new_dep_objs = (char**)realloc(dep_obj_paths, (dep_obj_count + dep_compilable) * sizeof(char*));
                    if (!new_dep_objs) { filelist_free(&dep_sources); continue; }
                    dep_obj_paths = new_dep_objs;

                    // Build compile jobs for dep sources
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
                        // Put dep objects in the TEST's build dir to avoid collisions
                        object_path_from_source(dep_sources.paths[s], build_dir, dep_obj_bufs[dj], 260);

                        dep_jobs[dj].source_path = dep_sources.paths[s];
                        dep_jobs[dj].object_path = dep_obj_bufs[dj];
                        dep_jobs[dj].include_paths = all_includes;
                        dep_jobs[dj].include_path_count = all_include_count;
                        dep_jobs[dj].optimize = build_profile.optimize;
                        dep_jobs[dj].debug_info = build_profile.debug_info;
                        dep_jobs[dj].defines = merged_defines;  // includes CDO_TESTING from the test crate
                        dep_jobs[dj].define_count = merged_define_count;
                        if (is_cpp_source(dep_sources.paths[s])) {
                            dep_jobs[dj].cpp_standard = cpp_standard_str(dep_crate->cpp_standard);
                        } else {
                            dep_jobs[dj].c_standard = c_standard_str(dep_crate->c_standard);
                        }
                        dj++;
                    }

                    // Compile dep sources with test defines
                    cdo_info("Compiling dep '%s' for test (%d files)", dep_crate->name, dj);
                    int dep_rc = compiler_compile_batch(dep_jobs, dj, &compiler, jobs);
                    if (dep_rc != 0) {
                        cdo_error("failed to compile dep '%s' for test crate", dep_crate->name);
                        for (int x = 0; x < dj; x++) free(dep_obj_bufs[x]);
                        free(dep_obj_bufs); free(dep_jobs);
                        filelist_free(&dep_sources);
                        failed = 1;
                        break;
                    }

                    // Collect the object paths
                    for (int x = 0; x < dj; x++) {
                        dep_obj_paths[dep_obj_count++] = dep_obj_bufs[x];
                    }
                    free(dep_obj_bufs); // free the array, not the strings (owned by dep_obj_paths now)
                    free(dep_jobs);
                    filelist_free(&dep_sources);
                }
            }

            char output_path[260];
            output_path_for_crate(&ws, crate, profile, output_path, sizeof(output_path));

            // Merge dependency link libs with crate's own link libs
            const char* all_link_libs[192];
            int all_link_lib_count = 0;
            for (int l = 0; l < dep_link_lib_count && all_link_lib_count < 192; l++) {
                all_link_libs[all_link_lib_count++] = dep_link_libs[l];
            }
            // For test crates, also inherit link_libs from executable dependencies
            if (crate->type == CRATE_TEST) {
                for (int d = 0; d < crate->dep_count; d++) {
                    int dep_idx = crate->dep_indices[d];
                    Crate* dep_crate = &ws.crates[dep_idx];
                    if (dep_crate->type == CRATE_EXECUTABLE) {
                        for (int l = 0; l < dep_crate->link_lib_count && all_link_lib_count < 192; l++) {
                            all_link_libs[all_link_lib_count++] = dep_crate->link_libs[l];
                        }
                    }
                }
            }
            for (int l = 0; l < crate->link_lib_count && all_link_lib_count < 192; l++) {
                all_link_libs[all_link_lib_count++] = crate->link_libs[l];
            }

            // Combine own objects + dep objects for linking
            int total_link_objs = compilable_count + dep_obj_count;
            const char** merged_obj_paths = (const char**)malloc(total_link_objs * sizeof(const char*));
            if (!merged_obj_paths) {
                cdo_error("out of memory");
                for (int oi = 0; oi < dep_obj_count; oi++) free(dep_obj_paths[oi]);
                free(dep_obj_paths);
                failed = 1;
            } else {
                for (int oi = 0; oi < compilable_count; oi++) {
                    merged_obj_paths[oi] = all_obj_paths[oi];
                }
                for (int oi = 0; oi < dep_obj_count; oi++) {
                    merged_obj_paths[compilable_count + oi] = dep_obj_paths[oi];
                }
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

                // Propagate coverage flags to linker
                if (coverage_flag_count > 0) {
                    link_job.extra_flags = coverage_flags;
                    link_job.extra_flag_count = coverage_flag_count;
                }

                // If any source is C++, use C++ linker driver
                CompilerInfo link_compiler = compiler;
                for (int s = 0; s < sources.count; s++) {
                    if (is_cpp_source(sources.paths[s])) {
                        if (compiler.family == COMPILER_GCC) {
                            // Handle both bare "gcc" and full paths like ".cdo/tools/w64devkit/bin/gcc.exe"
                            size_t plen = strlen(compiler.path);
                            if (plen >= 7 && strcmp(compiler.path + plen - 7, "gcc.exe") == 0) {
                                // Replace "gcc.exe" with "g++.exe"
                                strncpy(link_compiler.path, compiler.path, sizeof(link_compiler.path) - 1);
                                link_compiler.path[sizeof(link_compiler.path) - 1] = '\0';
                                strcpy(link_compiler.path + plen - 7, "g++.exe");
                            } else if (plen >= 3 && strcmp(compiler.path + plen - 3, "gcc") == 0) {
                                // Replace "gcc" with "g++"
                                strncpy(link_compiler.path, compiler.path, sizeof(link_compiler.path) - 1);
                                link_compiler.path[sizeof(link_compiler.path) - 1] = '\0';
                                strcpy(link_compiler.path + plen - 3, "g++");
                            } else if (strcmp(compiler.path, "cc") == 0) {
                                strncpy(link_compiler.path, "c++", sizeof(link_compiler.path) - 1);
                            }
                        } else if (compiler.family == COMPILER_CLANG) {
                            size_t plen = strlen(compiler.path);
                            if (plen >= 9 && strcmp(compiler.path + plen - 9, "clang.exe") == 0) {
                                strncpy(link_compiler.path, compiler.path, sizeof(link_compiler.path) - 1);
                                link_compiler.path[sizeof(link_compiler.path) - 1] = '\0';
                                strcpy(link_compiler.path + plen - 9, "clang++.exe");
                            } else if (plen >= 5 && strcmp(compiler.path + plen - 5, "clang") == 0) {
                                strncpy(link_compiler.path, compiler.path, sizeof(link_compiler.path) - 1);
                                link_compiler.path[sizeof(link_compiler.path) - 1] = '\0';
                                strcpy(link_compiler.path + plen - 5, "clang++");
                            }
                        }
                        strncpy(link_compiler.linker_path, link_compiler.path, sizeof(link_compiler.linker_path) - 1);
                        break;
                    }
                }

                cdo_info("Linking %s", output_path);
                rc = compiler_link(&link_job, &link_compiler);
                if (rc != 0) {
                    cdo_error("linking failed for crate '%s'", crate->name);
                    failed = 1;
                }

                // Post-link: deploy catalog files alongside executable binaries
                if (!failed && crate->type == CRATE_EXECUTABLE) {
                    int deployed = deploy_catalog_files(ws.root_path, build_dir);
                    if (deployed > 0) {
                        cdo_debug("deployed %d catalog file(s) to %s/catalogs/",
                                  deployed, build_dir);
                    }
                }
            }

            free(merged_obj_paths);
            for (int oi = 0; oi < dep_obj_count; oi++) free(dep_obj_paths[oi]);
            free(dep_obj_paths);
        }

        // Cleanup
        for (int s = 0; s < compilable_count; s++) free(all_obj_paths[s]);
        free(all_obj_paths);
        for (int d = 0; d < dirty_count; d++) free(obj_paths[d]);
        free(obj_paths);
        free(compile_jobs);
        free(merged_defines);
        filelist_free(&sources);
        free(dirty_indices);

        if (failed) break;
    }

    // --- Finish ---
    progress_finish(progress);

    if (failed) {
        cdo_error("build failed");
    } else {
        double elapsed_s = (double)(pal_time_ms() - build_start_ms) / 1000.0;
        cdo_info("Build completed in %.2fs", elapsed_s);
    }

    build_profile_free(&build_profile);
    workspace_free(&ws);
    return failed ? 1 : 0;
}
