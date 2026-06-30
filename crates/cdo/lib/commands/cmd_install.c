/**
 * cmd_install.c - Install/uninstall command implementation.
 *
 * Orchestrates: build -> bundle -> copy to apps dir -> strip -> write manifests -> generate launcher.
 */
#include "commands/cmd_install.h"
#include "commands/cmd_install_internal.h"
#include "commands/cmd_build.h"
#include "commands/bundle.h"
#include "commons/toml.h"
#include "core/log.h"
#include "core/cli_arg_access.h"
#include "model/workspace.h"
#include "model/module.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUILD_DIR "build"
#define CDO_VERSION "0.5.0"

// ---------------------------------------------------------------------------
// Path resolution helpers
// ---------------------------------------------------------------------------

/// Resolve the base install directory from options.
/// Priority: --path > --global > default (~/.cdo/)
int install_resolve_base_dir(const CliParseResult* result, char* base_dir, size_t base_size) {
    const char* path_opt = cli_arg_get_str(result, "path");
    bool global = cli_arg_get_bool(result, "global");

    if (path_opt && path_opt[0] != '\0') {
        strncpy(base_dir, path_opt, base_size - 1);
        base_dir[base_size - 1] = '\0';
        pal_path_normalize(base_dir);
        return 0;
    }

    if (global) {
#ifdef _WIN32
        const char* local_app_data = getenv("LOCALAPPDATA");
        if (!local_app_data) {
            cdo_log_error("install: LOCALAPPDATA environment variable not set");
            return 1;
        }
        snprintf(base_dir, base_size, "%s/Programs/cdo", local_app_data);
#else
        strncpy(base_dir, "/usr/local/lib/cdo", base_size - 1);
#endif
        pal_path_normalize(base_dir);
        return 0;
    }

    // Default: ~/.cdo/
    char home[512];
    if (pal_get_home_dir(home, sizeof(home)) != 0) {
        cdo_log_error("install: failed to determine home directory");
        return 1;
    }
    if (pal_path_join(base_dir, base_size, home, ".cdo") != 0) {
        cdo_log_error("install: base path too long");
        return 1;
    }
    return 0;
}

/// Resolve the bin directory for launchers.
/// For --global on Unix, uses /usr/local/bin. Otherwise <base>/bin/.
int install_resolve_bin_dir(const CliParseResult* result, const char* base_dir, char* bin_dir, size_t bin_size) {
#ifndef _WIN32
    bool global = cli_arg_get_bool(result, "global");
    const char* path_opt = cli_arg_get_str(result, "path");
    if (global && (!path_opt || path_opt[0] == '\0')) {
        strncpy(bin_dir, "/usr/local/bin", bin_size - 1);
        bin_dir[bin_size - 1] = '\0';
        return 0;
    }
#endif
    (void)result;
    return pal_path_join(bin_dir, bin_size, base_dir, "bin");
}

// ---------------------------------------------------------------------------
// Stripping
// ---------------------------------------------------------------------------

static void strip_binary(const char* path) {
    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    const char* args[] = { path };
    opts.program = "strip";
    opts.args = args;
    opts.arg_count = 1;
    opts.capture_output = true;
    opts.timeout_ms = 10000;

    PalSpawnResult res;
    memset(&res, 0, sizeof(res));
    int rc = pal_spawn(&opts, &res);
    if (rc != 0 || res.exit_code != 0) {
        cdo_log_debug("install: strip not available or failed for '%s' (non-fatal)", path);
    } else {
        cdo_log_debug("install: stripped '%s'", path);
    }
    pal_spawn_result_free(&res);
}

static void strip_walk_callback(const char* path, bool is_dir, void* ctx) {
    (void)ctx;
    if (is_dir) return;

    const char* ext = pal_path_ext(path);
#ifdef _WIN32
    if (strcmp(ext, ".exe") == 0 || strcmp(ext, ".dll") == 0) {
        strip_binary(path);
    }
#else
    if (strcmp(ext, ".so") == 0 || strcmp(ext, "") == 0) {
        strip_binary(path);
    }
#endif
}

static void strip_app_binaries(const char* app_dir) {
    pal_dir_walk(app_dir, strip_walk_callback, NULL);
}

// ---------------------------------------------------------------------------
// Launcher generation
// ---------------------------------------------------------------------------

static int generate_launcher(const char* bin_dir, const char* app_name) {
    if (pal_mkdir_p(bin_dir) != 0) {
        cdo_log_error("install: failed to create bin directory '%s'", bin_dir);
        return 1;
    }

    char launcher_path[1024];
#ifdef _WIN32
    char launcher_name[128];
    snprintf(launcher_name, sizeof(launcher_name), "%s.cmd", app_name);
    if (pal_path_join(launcher_path, sizeof(launcher_path), bin_dir, launcher_name) != 0) {
        cdo_log_error("install: launcher path too long");
        return 1;
    }

    char content[512];
    snprintf(content, sizeof(content), "@\"%%~dp0..\\apps\\%s\\%s.exe\" %%*\r\n", app_name, app_name);
#else
    if (pal_path_join(launcher_path, sizeof(launcher_path), bin_dir, app_name) != 0) {
        cdo_log_error("install: launcher path too long");
        return 1;
    }

    char content[512];
    snprintf(content, sizeof(content), "#!/bin/sh\nexec \"$(dirname \"$0\")/../apps/%s/%s\" \"$@\"\n", app_name, app_name);
#endif

    int rc = pal_file_write(launcher_path, content, strlen(content));
    if (rc != 0) {
        cdo_log_error("install: failed to write launcher '%s'", launcher_path);
        return 1;
    }

#ifndef _WIN32
    // Make executable
    PalSpawnOpts opts;
    memset(&opts, 0, sizeof(opts));
    const char* args[] = { "+x", launcher_path };
    opts.program = "chmod";
    opts.args = args;
    opts.arg_count = 2;
    opts.capture_output = true;
    opts.timeout_ms = 5000;
    PalSpawnResult res;
    memset(&res, 0, sizeof(res));
    pal_spawn(&opts, &res);
    pal_spawn_result_free(&res);
#endif

    cdo_log_debug("install: generated launcher '%s'", launcher_path);
    return 0;
}

static int remove_launcher(const char* bin_dir, const char* app_name) {
    char launcher_path[1024];
#ifdef _WIN32
    char launcher_name[128];
    snprintf(launcher_name, sizeof(launcher_name), "%s.cmd", app_name);
    if (pal_path_join(launcher_path, sizeof(launcher_path), bin_dir, launcher_name) != 0) return 1;
#else
    if (pal_path_join(launcher_path, sizeof(launcher_path), bin_dir, app_name) != 0) return 1;
#endif

    if (pal_path_exists(launcher_path) == 0) {
        remove(launcher_path);
        cdo_log_debug("install: removed launcher '%s'", launcher_path);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// install --list
// ---------------------------------------------------------------------------

static int install_list(const CliParseResult* result) {
    char base_dir[1024];
    if (install_resolve_base_dir(result, base_dir, sizeof(base_dir)) != 0) return 1;

    char apps_dir[1024];
    if (pal_path_join(apps_dir, sizeof(apps_dir), base_dir, "apps") != 0) return 1;

    char index_path[1024];
    if (pal_path_join(index_path, sizeof(index_path), apps_dir, "install.toml") != 0) return 1;

    char* content = NULL;
    size_t content_len = 0;
    if (pal_file_read(index_path, &content, &content_len) != 0) {
        printf("No applications installed.\n");
        return 0;
    }

    TomlTable* root = NULL;
    TomlError err;
    if (toml_parse(content, content_len, &root, &err) != 0) {
        free(content);
        printf("No applications installed.\n");
        return 0;
    }
    free(content);

    const TomlValue* arr = toml_get(root, "app");
    if (!arr || arr->type != TOML_ARRAY || arr->as.array->count == 0) {
        toml_free(root);
        printf("No applications installed.\n");
        return 0;
    }

    printf("%-20s %-12s %-40s %s\n", "Name", "Version", "Source", "Installed");
    printf("%-20s %-12s %-40s %s\n", "----", "-------", "------", "---------");

    for (int i = 0; i < arr->as.array->count; i++) {
        TomlValue* item = arr->as.array->items[i];
        if (item->type != TOML_TABLE) continue;

        const char* name = "";
        const char* version = "";
        const char* source = "";
        const char* installed = "";

        for (TomlEntry* te = item->as.table->head; te; te = te->next) {
            if (strcmp(te->key, "name") == 0 && te->value->type == TOML_STRING) name = te->value->as.string;
            else if (strcmp(te->key, "version") == 0 && te->value->type == TOML_STRING) version = te->value->as.string;
            else if (strcmp(te->key, "source_workspace") == 0 && te->value->type == TOML_STRING) source = te->value->as.string;
            else if (strcmp(te->key, "installed_at") == 0 && te->value->type == TOML_STRING) installed = te->value->as.string;
        }

        printf("%-20s %-12s %-40s %s\n", name, version, source, installed);
    }

    toml_free(root);
    return 0;
}

// ---------------------------------------------------------------------------
// cmd_install
// ---------------------------------------------------------------------------

int cmd_install(const CliParseResult* result, void* ctx) {
    // --list: just print installed apps
    if (cli_arg_get_bool(result, "list")) {
        return install_list(result);
    }

    bool release = true;  // Install always builds release
    bool force = cli_arg_get_bool(result, "force");
    bool debug_mode = cli_arg_get_bool(result, "debug");

    // Load workspace
    Workspace ws;
    memset(&ws, 0, sizeof(ws));
    int rc = workspace_load(".", &ws);
    if (rc != 0) {
        cdo_log_error("Failed to load workspace");
        return 1;
    }

    // Select crate
    const Crate* crate = bundle_select_exe_crate(&ws, result->positional_values, result->positional_count);
    if (!crate) {
        workspace_free(&ws);
        return 1;
    }

    cdo_log_info("Installing '%s'...", crate->name);

    // Build in release mode
    const char* build_positional[1];
    build_positional[0] = crate->name;

    CliArgValue build_arg_buf[4];
    int build_arg_count = 0;

    build_arg_buf[build_arg_count].name = "release";
    build_arg_buf[build_arg_count].type = CLI_ARG_BOOL;
    build_arg_buf[build_arg_count].value.bool_val = release;
    build_arg_buf[build_arg_count].present = true;
    build_arg_count++;

    CliParseResult build_result = {0};
    build_result.matched_cmd = NULL;
    build_result.arg_values = build_arg_buf;
    build_result.arg_value_count = build_arg_count;
    build_result.positional_values = build_positional;
    build_result.positional_count = 1;
    build_result.rest_args = NULL;
    build_result.rest_count = 0;

    rc = cmd_build(&build_result, ctx);
    if (rc != 0) {
        cdo_log_error("Build failed for crate '%s'", crate->name);
        workspace_free(&ws);
        return rc;
    }

    // Resolve paths
    const char* profile = "release";
    char exe_path[1024];
    {
        char profile_dir[512];
        char crate_build_dir[512];
        pal_path_join(profile_dir, sizeof(profile_dir), BUILD_DIR, profile);
        pal_path_join(crate_build_dir, sizeof(crate_build_dir), profile_dir, crate->name);

        char exe_name[128];
#ifdef _WIN32
        snprintf(exe_name, sizeof(exe_name), "%s.exe", crate->name);
#else
        snprintf(exe_name, sizeof(exe_name), "%s", crate->name);
#endif
        pal_path_join(exe_path, sizeof(exe_path), crate_build_dir, exe_name);
    }

    // Stage runtime bundle into a temp staging dir
    char staging_dir[1024];
    {
        char cdo_crate_dir[512];
        pal_path_join(cdo_crate_dir, sizeof(cdo_crate_dir), ".cdo", crate->name);
        pal_path_join(staging_dir, sizeof(staging_dir), cdo_crate_dir, "install_stage");
    }

    BundleOpts bundle_opts = { .resource_base = crate->resource_base, .shader_base = crate->shader_base };
    rc = bundle_prepare(&ws, crate, profile, staging_dir, exe_path, &bundle_opts);
    if (rc != 0) {
        cdo_log_error("Failed to stage runtime bundle for '%s'", crate->name);
        workspace_free(&ws);
        return 1;
    }

    // Resolve install directories
    char base_dir[1024];
    if (install_resolve_base_dir(result, base_dir, sizeof(base_dir)) != 0) {
        workspace_free(&ws);
        return 1;
    }

    char apps_dir[1024];
    pal_path_join(apps_dir, sizeof(apps_dir), base_dir, "apps");

    char app_dir[1024];
    pal_path_join(app_dir, sizeof(app_dir), apps_dir, crate->name);

    char bin_dir[1024];
    if (install_resolve_bin_dir(result, base_dir, bin_dir, sizeof(bin_dir)) != 0) {
        workspace_free(&ws);
        return 1;
    }

    // Check overwrite protection
    char existing_manifest_path[1024];
    pal_path_join(existing_manifest_path, sizeof(existing_manifest_path), app_dir, "manifest.toml");

    if (pal_path_exists(existing_manifest_path) == 0) {
        InstallManifest existing;
        if (install_read_manifest(existing_manifest_path, &existing) == 0) {
            // Normalize paths for comparison
            char norm_existing[512];
            char norm_current[512];
            strncpy(norm_existing, existing.source_workspace, sizeof(norm_existing) - 1);
            norm_existing[sizeof(norm_existing) - 1] = '\0';
            strncpy(norm_current, ws.root_path, sizeof(norm_current) - 1);
            norm_current[sizeof(norm_current) - 1] = '\0';
            pal_path_normalize(norm_existing);
            pal_path_normalize(norm_current);

            if (strcmp(norm_existing, norm_current) != 0 && !force) {
                cdo_log_error("App '%s' was installed from a different workspace (%s). Use --force to overwrite.", crate->name, existing.source_workspace);
                workspace_free(&ws);
                return 1;
            }
            cdo_log_info("Reinstalling '%s' (v%s -> v%s)", crate->name, existing.version, crate->version);
        }
    }

    // Remove existing app dir for clean install
    if (pal_path_exists(app_dir) == 0) {
        if (pal_rmdir_r(app_dir) != 0) {
            cdo_log_error("install: failed to remove existing app directory '%s'", app_dir);
            workspace_free(&ws);
            return 1;
        }
    }

    // Create apps dir and copy staged contents
    if (pal_mkdir_p(app_dir) != 0) {
        cdo_log_error("install: cannot write to install directory '%s'. Use --path or run with elevated privileges.", app_dir);
        workspace_free(&ws);
        return 1;
    }

    rc = bundle_copy_dir_recursive(staging_dir, app_dir);
    if (rc != 0) {
        cdo_log_error("install: failed to copy bundle to '%s'", app_dir);
        workspace_free(&ws);
        return 1;
    }

    // Strip binaries (unless --debug)
    if (!debug_mode) {
        strip_app_binaries(app_dir);
    }

    // Use version from crate.toml (populated during workspace_load, defaults to "0.0.0")
    const char* version = crate->version;

    // Write per-app manifest
    InstallManifest manifest;
    memset(&manifest, 0, sizeof(manifest));
    strncpy(manifest.name, crate->name, sizeof(manifest.name) - 1);
    strncpy(manifest.version, version, sizeof(manifest.version) - 1);
    strncpy(manifest.crate_name, crate->name, sizeof(manifest.crate_name) - 1);
    strncpy(manifest.source_workspace, ws.root_path, sizeof(manifest.source_workspace) - 1);
    install_manifest_set_timestamp(&manifest);
    strncpy(manifest.cdo_version, CDO_VERSION, sizeof(manifest.cdo_version) - 1);
    strncpy(manifest.profile, profile, sizeof(manifest.profile) - 1);

#ifdef _WIN32
    snprintf(manifest.executable, sizeof(manifest.executable), "%s.exe", crate->name);
#else
    snprintf(manifest.executable, sizeof(manifest.executable), "%s", crate->name);
#endif

    // Use workspace model flags — resources/shaders are flattened to app root, so no res/ or shd/ directories exist in the bundle
    manifest.has_resources = crate->has_res;
    manifest.has_shaders = crate->has_shd;

    // Populate resource_base and shader_base from crate config
    strncpy(manifest.resource_base, crate->resource_base[0] ? crate->resource_base : ".", sizeof(manifest.resource_base) - 1);
    manifest.resource_base[sizeof(manifest.resource_base) - 1] = '\0';
    strncpy(manifest.shader_base, crate->shader_base[0] ? crate->shader_base : ".", sizeof(manifest.shader_base) - 1);
    manifest.shader_base[sizeof(manifest.shader_base) - 1] = '\0';
    cdo_log_debug("install: resource_base='%s', shader_base='%s'", manifest.resource_base, manifest.shader_base);

    char manifest_path[1024];
    pal_path_join(manifest_path, sizeof(manifest_path), app_dir, "manifest.toml");
    rc = install_write_manifest(&manifest, manifest_path);
    if (rc != 0) {
        workspace_free(&ws);
        return 1;
    }

    // Update global index
    rc = install_update_global_index(apps_dir, &manifest);
    if (rc != 0) {
        cdo_log_error("install: failed to update global index (non-fatal)");
        // Continue - the app is installed even if the index fails
    }

    // Generate launcher
    rc = generate_launcher(bin_dir, crate->name);
    if (rc != 0) {
        cdo_log_error("install: failed to generate launcher (non-fatal)");
    }

    // Clean up staging dir
    pal_rmdir_r(staging_dir);

    workspace_free(&ws);
    cdo_log_info("Installed '%s' v%s to %s", crate->name, version, app_dir);
    return 0;
}

// ---------------------------------------------------------------------------
// cmd_uninstall
// ---------------------------------------------------------------------------

int cmd_uninstall(const CliParseResult* result, void* ctx) {
    (void)ctx;

    if (result->positional_count < 1) {
        cdo_log_error("Usage: cdo uninstall <name>");
        return 1;
    }

    const char* app_name = result->positional_values[0];

    // Resolve install base dir
    char base_dir[1024];
    if (install_resolve_base_dir(result, base_dir, sizeof(base_dir)) != 0) return 1;

    char apps_dir[1024];
    pal_path_join(apps_dir, sizeof(apps_dir), base_dir, "apps");

    char app_dir[1024];
    pal_path_join(app_dir, sizeof(app_dir), apps_dir, app_name);

    char bin_dir[1024];
    if (install_resolve_bin_dir(result, base_dir, bin_dir, sizeof(bin_dir)) != 0) return 1;

    // Check if app exists
    if (pal_path_exists(app_dir) != 0) {
        cdo_log_info("App '%s' is not installed.", app_name);
        return 0;
    }

    // Remove app directory
    if (pal_rmdir_r(app_dir) != 0) {
        cdo_log_error("uninstall: failed to remove app directory '%s'", app_dir);
        return 1;
    }

    // Remove launcher
    remove_launcher(bin_dir, app_name);

    // Remove from global index
    install_remove_from_global_index(apps_dir, app_name);

    cdo_log_info("Uninstalled '%s'", app_name);
    return 0;
}
