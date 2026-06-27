#include "cmd_build_internal.h"
#include "commands/build_lock.h"
#include "core/compiler.h"
#include "core/scanner.h"
#include "core/module.h"
#include "commons/toml.h"
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
                                     progress, &completed_units);
            if (rc != 0) {
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
                failed = 1;
                break;
            }
            // legacy_rc == -1 means skip (no sources), legacy_rc == 0 means success
        }
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
    build_lock_release(lock);
    workspace_free(&ws);
    return failed ? 1 : 0;
}