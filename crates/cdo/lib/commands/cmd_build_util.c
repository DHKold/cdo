#include "cmd_build_internal.h"
#include "commons/toml.h"
#include "core/log.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Build Profile
// ---------------------------------------------------------------------------

void build_profile_free(BuildProfile* p) {
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

int build_profile_load(const char* ws_root, const char* profile_name,
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
        // No cdo.toml found â€” use built-in defaults (not an error for profile loading)
        out->loaded = false;
        return 0;
    }

    TomlTable* root = NULL;
    TomlError err;
    if (toml_parse(buf, buf_len, &root, &err) != 0) {
        free(buf);
        out->loaded = false;
        return 0; // Parse error â€” use defaults silently
    }
    free(buf);

    // Look up [workspace.profiles.<profile_name>]
    char key_path[128];
    snprintf(key_path, sizeof(key_path), "workspace.profiles.%s", profile_name);

    const TomlValue* profile_val = toml_get(root, key_path);
    if (!profile_val || (profile_val->type != TOML_TABLE &&
                         profile_val->type != TOML_INLINE_TABLE)) {
        // Profile not defined in manifest â€” keep built-in defaults
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

    // Read "defines" (array of strings) â€” overrides defaults
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

    // Read "flags" (array of strings) â€” extra compiler flags
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
// Path helpers
// ---------------------------------------------------------------------------

void build_dir_for_crate(const Workspace* ws, const Crate* crate,
                         const char* profile, char* out, size_t out_size) {
    char tmp[260];
    pal_path_join(tmp, sizeof(tmp), ws->root_path, "build");

    char tmp2[260];
    pal_path_join(tmp2, sizeof(tmp2), tmp, profile);

    pal_path_join(out, out_size, tmp2, crate->name);
}

void output_path_for_crate(const Workspace* ws, const Crate* crate,
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

void object_path_from_source(const char* source, const char* build_dir,
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
// Misc utilities
// ---------------------------------------------------------------------------

int resolve_jobs_raw(int jobs_value) {
    if (jobs_value > 0) {
        return jobs_value;
    }
    int cpus = pal_cpu_count();
    return (cpus > 0) ? cpus : 1;
}

int deploy_catalog_files(const char* ws_root, const char* build_dir) {
    char src_dir[520];
    if (pal_path_join(src_dir, sizeof(src_dir), ws_root, "catalogs") != 0) {
        return -1;
    }

    /* Check if workspace has a catalogs/ directory */
    if (pal_path_exists(src_dir) != 0) {
        return 0; /* No catalogs to deploy â€” not an error */
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

        if (pal_path_exists(src_path) != 0) {
            continue; /* File doesn't exist â€” skip */
        }

        char* buf = NULL;
        size_t buf_len = 0;
        if (pal_file_read(src_path, &buf, &buf_len) != 0) {
            cdo_log_warn("failed to read catalog file '%s' for deployment", src_path);
            continue;
        }

        char dest_path[520];
        if (pal_path_join(dest_path, sizeof(dest_path), dest_dir, catalog_files[i]) != 0) {
            free(buf);
            continue;
        }

        if (pal_file_write(dest_path, buf, buf_len) != 0) {
            cdo_log_warn("failed to write catalog file '%s'", dest_path);
            free(buf);
            continue;
        }

        free(buf);
        copied++;
    }

    return copied;
}

