#include "cmd_build_internal.h"
#include "commands/build_lock.h"
#include "core/compiler.h"
#include "model/scanner.h"
#include "model/module.h"
#include "model/dag.h"
#include "core/cache.h"
#include "core/dag_scheduler.h"
#include "model/hooks.h"
#include "core/hooks_exec.h"
#include "commons/toml.h"
#include "core/output.h"
#include "model/deps.h"
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
                               const CacheConfig* cache_config,
                               CacheStats* cache_stats,
                               bool no_cache,
                               ProgressBar* progress,
                               int* completed_units) {
    int rc = 0;

    // --- Crate pre-build hook ---
    {
        char crate_abs[260];
        pal_path_join(crate_abs, sizeof(crate_abs), ws->root_path, crate->path);
        char crate_build[260];
        snprintf(crate_build, sizeof(crate_build), "%s/build/%s/%s", ws->root_path, profile, crate->name);

        HookEnv crate_env = {0};
        crate_env.ws_root = ws->root_path;
        crate_env.profile = profile;
        char build_dir_buf[260];
        snprintf(build_dir_buf, sizeof(build_dir_buf), "%s/build/%s", ws->root_path, profile);
        crate_env.build_dir = build_dir_buf;
        crate_env.crate_name = crate->name;
        crate_env.crate_path = crate_abs;
        crate_env.crate_build_dir = crate_build;

        if (hook_execute(&crate->hooks.hooks[HOOK_PRE_BUILD], &crate_env) != 0) {
            cdo_error("Crate '%s' pre-build hook failed, aborting crate build", crate->name);
            return -1;
        }
    }

    // Req 8.1: Build Library_Module FIRST if present
    if (crate->has_lib) {
        rc = build_library_module(ws, crate, compiler, profile,
                                  build_prof, jobs,
                                  coverage_flags, coverage_flag_count,
                                  cache_config, cache_stats, no_cache,
                                  progress, completed_units);
        if (rc != 0) {
            // Req 8.2: If lib/ fails, skip ALL dependent modules in this crate
            cdo_error("Library module build failed for crate '%s'; "
                      "skipping remaining modules", crate->name);
            return rc;
        }
    }

    // Req 8.1: After lib/ succeeds, build shd/, res/, exe/, dyn/, tst/ (sequential)

    // Build Shader_Module if present (Req 4.3) — after lib/, before res/
    if (crate->has_shd) {
        rc = build_shader_module(ws, crate, profile, build_prof, false, progress, completed_units);
        if (rc != 0) {
            cdo_error("Shader module build failed for crate '%s'", crate->name);
            return rc;
        }
    }

    // Build Resource_Module if present (Req 1.4) — after shd/, before exe/
    if (crate->has_res) {
        rc = build_resource_module(ws, crate, profile, progress, completed_units);
        if (rc != 0) {
            cdo_error("Resource module build failed for crate '%s'", crate->name);
            return rc;
        }
    }

    // Build Executable_Module if present (Req 3.1, 3.2, 3.3, 3.4)
    if (crate->modules[MODULE_EXE].present) {
        rc = build_executable_module(ws, crate, compiler, profile,
                                     build_prof, jobs,
                                     coverage_flags, coverage_flag_count,
                                     cache_config, cache_stats, no_cache,
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
                                         cache_config, cache_stats, no_cache,
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
                               cache_config, cache_stats, no_cache,
                               progress, completed_units);
        if (rc != 0) {
            cdo_error("Test module build failed for crate '%s'", crate->name);
            return rc;
        }
    }

    // Propagate dependency module outputs (res, shd, dyn) into this crate's build dir (Req 2.1, 2.2)
    if (crate->dep_count > 0) {
        rc = propagate_dep_modules(ws, crate, profile);
        if (rc != 0) {
            cdo_error("Dependency module propagation failed for crate '%s'", crate->name);
            return rc;
        }
    }

    // --- Crate post-build hook ---
    {
        char crate_abs[260];
        pal_path_join(crate_abs, sizeof(crate_abs), ws->root_path, crate->path);
        char crate_build[260];
        snprintf(crate_build, sizeof(crate_build), "%s/build/%s/%s", ws->root_path, profile, crate->name);

        HookEnv crate_env = {0};
        crate_env.ws_root = ws->root_path;
        crate_env.profile = profile;
        char build_dir_buf[260];
        snprintf(build_dir_buf, sizeof(build_dir_buf), "%s/build/%s", ws->root_path, profile);
        crate_env.build_dir = build_dir_buf;
        crate_env.crate_name = crate->name;
        crate_env.crate_path = crate_abs;
        crate_env.crate_build_dir = crate_build;

        if (hook_execute(&crate->hooks.hooks[HOOK_POST_BUILD], &crate_env) != 0) {
            cdo_warn("Crate '%s' post-build hook failed (artifacts preserved)", crate->name);
            rc = 1;  // Mark as failed but don't abort
        }
    }

    return rc;
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

    // --- Step 1b: Acquire build lock ---
    int lock_timeout = (opts->lock_timeout >= 0) ? opts->lock_timeout : 30;
    BuildLock* lock = NULL;
    int lock_rc = build_lock_acquire(ws.root_path, lock_timeout, &lock);
    if (lock_rc != 0) {
        if (lock_rc == PAL_ERR_TIMEOUT) {
            cdo_error("could not acquire build lock within %d seconds "
                      "(another cdo process may be building)", lock_timeout);
        } else {
            cdo_error("failed to acquire build lock");
        }
        workspace_free(&ws);
        return 1;
    }

    // Set re-entrancy env var so nested builds (e.g., cmd_test → cmd_build) skip locking
#ifdef _WIN32
    _putenv_s("CDO_BUILD_LOCK_HELD", "1");
#else
    setenv("CDO_BUILD_LOCK_HELD", "1", 1);
#endif

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
        build_lock_release(lock);
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
                build_lock_release(lock);
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
        build_lock_release(lock);
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

    // --- Initialize cache if enabled (Req 5.1, 7.1, 8.4) ---
    bool cache_active = false;
    if (ws.cache_config.enabled && !opts->no_cache) {
        if (strcmp(ws.cache_config.backend, "builtin") == 0) {
            // Builtin cache: initialize the store directory
            if (cache_init(&ws.cache_config, ws.root_path) != 0) {
                cdo_warn("Cache initialization failed, continuing without cache");
                ws.cache_config.enabled = false;
            } else {
                cache_active = true;
            }
        } else {
            // External backend (ccache/sccache): verify it exists on PATH (Req 8.4)
            PalSpawnOpts probe_opts = {0};
            probe_opts.program = ws.cache_config.backend;
            const char* probe_args[] = {"--version"};
            probe_opts.args = probe_args;
            probe_opts.arg_count = 1;
            probe_opts.capture_output = true;
            probe_opts.timeout_ms = 5000; // 5s timeout for version check

            PalSpawnResult probe_result = {0};
            int probe_rc = pal_spawn(&probe_opts, &probe_result);
            if (probe_rc == 0 && probe_result.exit_code == 0) {
                cache_active = true;
                cdo_debug("External cache backend '%s' found", ws.cache_config.backend);
            } else {
                cdo_warn("External cache backend '%s' not found on PATH, continuing without cache", ws.cache_config.backend);
                ws.cache_config.enabled = false;
            }
            pal_spawn_result_free(&probe_result);
        }
    }

    // --- Workspace pre-build hook ---
    // In DAG path (jobs != 1), hooks are embedded in the DAG and execute there.
    // In sequential path (jobs == 1), we execute them explicitly here.
    if (jobs == 1) {
        HookEnv ws_hook_env = {0};
        ws_hook_env.ws_root = ws.root_path;
        ws_hook_env.profile = profile;
        char ws_build_dir[260];
        snprintf(ws_build_dir, sizeof(ws_build_dir), "%s/build/%s", ws.root_path, profile);
        ws_hook_env.build_dir = ws_build_dir;
        // crate fields NULL for workspace hooks

        if (hook_execute(&ws.ws_hooks.hooks[HOOK_PRE_BUILD], &ws_hook_env) != 0) {
            cdo_error("Workspace pre-build hook failed, aborting build");
            build_lock_release(lock);
            workspace_free(&ws);
            return 1;
        }
    }

    // --- Step 5: Count total compilation units for progress (Req 7.1–7.7) ---
    // Count compilable files (.c, .cpp, .cxx, .cc) across compiled modules
    // (lib, exe, dyn, tst) only. Exclude res/ and shd/ files.
    // build_order already contains only targeted crates + transitive deps
    // when specific crate names are provided (handled by workspace_resolve).
    int total_units = 0;
    static const ModuleKind compiled_kinds[] = {MODULE_LIB, MODULE_EXE, MODULE_DYN, MODULE_TST};
    static const int compiled_kind_count = 4;

    for (int i = 0; i < ws.build_order_count; i++) {
        int idx = ws.build_order[i];
        Crate* crate = &ws.crates[idx];

        if (crate->module_count > 0) {
            // Module-based crate: count per compiled module
            for (int k = 0; k < compiled_kind_count; k++) {
                ModuleKind kind = compiled_kinds[k];
                Module* mod = &crate->modules[kind];
                if (!mod->present) continue;

                FileList sources = {0};
                if (scanner_scan_module_sources(mod->dir_path, kind, NULL, 0, &sources) == 0) {
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
        } else {
            // Legacy crate: count from src/ directory
            char crate_full_path[260];
            pal_path_join(crate_full_path, sizeof(crate_full_path), ws.root_path, crate->path);

            FileList sources = {0};
            if (scanner_scan_sources(crate_full_path, NULL, 0, &sources) == 0) {
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
    }

    // Req 7.7: If total compilable count is zero, skip progress bar entirely
    ProgressBar* progress = (total_units > 0) ? progress_create("Building", total_units) : NULL;
    int completed_units = 0;
    CacheStats cache_stats = {0};
    int failed = 0;
    bool dag_path_used = false;  // Set to true when DAG path executes successfully (hooks ran inside DAG)

    // --- Step 6: Build crates ---
    // If jobs != 1, use DAG-based parallel build path.
    // If jobs == 1, use existing sequential per-crate build.
    if (jobs != 1) {
        // --- DAG-based build path ---
        // TODO: Add dirty set filtering to only add compile tasks for modified files (incremental builds)
        DagGraph* graph = NULL;
        rc = dag_generate(&ws, profile, &graph);
        if (rc != 0) {
            cdo_warn("DAG generation failed (rc=%d), falling back to sequential build", rc);
            goto sequential_path;
        }

        int compile_tasks = dag_graph_task_count_by_kind(graph, DAG_TASK_COMPILE);
        int link_tasks = dag_graph_task_count_by_kind(graph, DAG_TASK_LINK);

        // Count total dependency edges for debug logging
        int total_edges = 0;
        for (int i = 0; i < graph->task_count; i++) {
            total_edges += graph->tasks[i].dep_count;
        }

        cdo_info("Building %d crates (%d compile tasks, %d link tasks) with %d threads", ws.build_order_count, compile_tasks, link_tasks, jobs);
        cdo_debug("DAG: %d tasks, %d dependency edges", graph->task_count, total_edges);

        // Update progress bar to match DAG compile task count (may differ from initial scan
        // if DAG skips empty modules or hooks are present).
        if (progress && compile_tasks != total_units) {
            progress_finish(progress);
            progress = (compile_tasks > 0) ? progress_create("Building", compile_tasks) : NULL;
        }

        DagSchedulerConfig sched_config = {0};
        sched_config.workspace = &ws;
        sched_config.compiler = &compiler;
        sched_config.cache_config = cache_active ? &ws.cache_config : NULL;
        sched_config.cache_stats = &cache_stats;
        sched_config.no_cache = opts->no_cache;
        sched_config.jobs = jobs;
        sched_config.profile = profile;
        sched_config.progress = progress;
        sched_config.total_compile_units = compile_tasks;

        dag_path_used = true;
        rc = dag_scheduler_run(graph, &sched_config);
        dag_graph_free(graph);

        if (rc != 0) {
            failed = 1;
        }

        // Progress is finished by the scheduler on failure, finish it on success
        if (!failed && progress) {
            progress_finish(progress);
            progress = NULL;
        }

        goto build_done;
    }

sequential_path:
    // If we fell through from DAG failure (jobs != 1 but DAG failed), run pre-build hook now
    if (jobs != 1) {
        HookEnv ws_hook_env = {0};
        ws_hook_env.ws_root = ws.root_path;
        ws_hook_env.profile = profile;
        char ws_build_dir[260];
        snprintf(ws_build_dir, sizeof(ws_build_dir), "%s/build/%s", ws.root_path, profile);
        ws_hook_env.build_dir = ws_build_dir;

        if (hook_execute(&ws.ws_hooks.hooks[HOOK_PRE_BUILD], &ws_hook_env) != 0) {
            cdo_error("Workspace pre-build hook failed, aborting build");
            build_lock_release(lock);
            workspace_free(&ws);
            return 1;
        }
    }

    // --- Step 6 (sequential): Build each crate in dependency order ---
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

        // Internal crate dependencies: add their include paths and link info
        for (int d = 0; d < crate->dep_count; d++) {
            int dep_idx = crate->dep_indices[d];
            Crate* dep_crate = &ws.crates[dep_idx];

            static char dep_inc_buf[64][260];

            // Include resolution: use api/ as public headers
            if (dep_crate->has_api && dep_crate->modules[MODULE_API].dir_path[0] != '\0'
                && dep_include_count < 64) {
                strncpy(dep_inc_buf[dep_include_count],
                        dep_crate->modules[MODULE_API].dir_path, 259);
                dep_include_paths[dep_include_count] = dep_inc_buf[dep_include_count];
                dep_include_count++;
            }

            // Link against dependency's library artifact
            if (dep_crate->has_lib) {
                char dep_build_dir[260];
                build_dir_for_crate(&ws, dep_crate, profile, dep_build_dir, sizeof(dep_build_dir));

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

                static char dev_dep_inc_buf[64][260];

                // Include resolution: use api/ as public headers
                if (dep_crate->has_api && dep_crate->modules[MODULE_API].dir_path[0] != '\0'
                    && dep_include_count < 64) {
                    strncpy(dev_dep_inc_buf[dep_include_count],
                            dep_crate->modules[MODULE_API].dir_path, 259);
                    dep_include_paths[dep_include_count] = dev_dep_inc_buf[dep_include_count];
                    dep_include_count++;
                }

                // Link against dev-dependency's library artifact
                if (dep_crate->has_lib) {
                    char dep_build_dir[260];
                    build_dir_for_crate(&ws, dep_crate, profile, dep_build_dir, sizeof(dep_build_dir));

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
                                     cache_active ? &ws.cache_config : NULL,
                                     &cache_stats, opts->no_cache,
                                     progress, &completed_units);
            if (rc != 0) {
                // Req 7.8: Finalize progress bar at current count before reporting error
                if (progress) {
                    progress_finish(progress);
                    progress = NULL;
                }
                failed = 1;
                break;
            }
            // Module-based crate fully built; skip legacy src/ flow
            continue;
        }

        // --- Legacy src/-based build path (crates without module directories) ---
        {
            LegacyBuildCtx legacy_ctx = {0};
            legacy_ctx.ws = &ws;
            legacy_ctx.crate = crate;
            legacy_ctx.compiler = &compiler;
            legacy_ctx.profile = profile;
            legacy_ctx.build_prof = &build_profile;
            legacy_ctx.jobs = jobs;
            legacy_ctx.coverage_flags = coverage_flags;
            legacy_ctx.coverage_flag_count = coverage_flag_count;
            legacy_ctx.cache_config = cache_active ? &ws.cache_config : NULL;
            legacy_ctx.cache_stats = &cache_stats;
            legacy_ctx.no_cache = opts->no_cache;
            legacy_ctx.progress = progress;
            legacy_ctx.completed_units = &completed_units;
            legacy_ctx.crate_full_path = crate_full_path;
            legacy_ctx.build_dir = build_dir;
            legacy_ctx.dep_include_paths = dep_include_paths;
            legacy_ctx.dep_include_count = dep_include_count;
            legacy_ctx.dep_lib_paths = dep_lib_paths;
            legacy_ctx.dep_lib_count = dep_lib_count;
            legacy_ctx.dep_link_libs = dep_link_libs;
            legacy_ctx.dep_link_lib_count = dep_link_lib_count;

            int legacy_rc = build_legacy_crate(&legacy_ctx);
            if (legacy_rc == 1) {
                // Req 7.8: Finalize progress bar at current count before reporting error
                if (progress) {
                    progress_finish(progress);
                    progress = NULL;
                }
                failed = 1;
                break;
            }
            // legacy_rc == -1 means skip (no sources), legacy_rc == 0 means success
        }
    }

    // --- Finish (sequential path) ---
    progress_finish(progress);

build_done:
    // --- Workspace post-build hook ---
    // In DAG path, hooks are embedded in the DAG and already executed.
    // In sequential path, run the post-build hook explicitly here.
    if (!failed && !dag_path_used) {
        HookEnv ws_hook_env = {0};
        ws_hook_env.ws_root = ws.root_path;
        ws_hook_env.profile = profile;
        char ws_build_dir[260];
        snprintf(ws_build_dir, sizeof(ws_build_dir), "%s/build/%s", ws.root_path, profile);
        ws_hook_env.build_dir = ws_build_dir;

        if (hook_execute(&ws.ws_hooks.hooks[HOOK_POST_BUILD], &ws_hook_env) != 0) {
            cdo_warn("Workspace post-build hook failed (artifacts preserved)");
            failed = 1;
        }
    }

    // Run cache eviction if cache is over max size (Req 4.4)
    if (cache_active && !failed) {
        cache_evict(&ws.cache_config);
    }

    if (failed) {
        cdo_error("build failed");
    } else {
        // Print cache summary (Req 5.1, 5.2, 5.3)
        if (cache_active) {
            int total = cache_stats.hits + cache_stats.misses;
            if (total > 0) {
                int hit_rate = (cache_stats.hits * 100) / total;
                cdo_info("Cache: %d hits, %d misses (%d%% hit rate)", cache_stats.hits, cache_stats.misses, hit_rate);
            }
        }

        double elapsed_s = (double)(pal_time_ms() - build_start_ms) / 1000.0;
        cdo_info("Build completed in %.2fs", elapsed_s);
    }

    build_profile_free(&build_profile);
    build_lock_release(lock);
    workspace_free(&ws);
    return failed ? 1 : 0;
}