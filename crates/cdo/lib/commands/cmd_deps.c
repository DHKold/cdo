#include "cmd_deps_internal.h"
#include "model/deps.h"
#include "core/log.h"
#include "commons/toml.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Shared helpers (non-static, declared in cmd_deps_internal.h)                */
/* -------------------------------------------------------------------------- */

int cmd_deps_manifest_load(TomlTable** table) {
    char* buf = NULL;
    size_t len = 0;
    int rc = pal_file_read(CRATE_MANIFEST, &buf, &len);
    if (rc != PAL_OK) {
        cdo_log_error("Could not read '%s' â€” are you in a crate directory?", CRATE_MANIFEST);
        return 1;
    }

    TomlError err = {0};
    rc = toml_parse(buf, len, table, &err);
    free(buf);
    if (rc != 0) {
        cdo_log_error("Failed to parse '%s': line %d col %d: %s",
                  CRATE_MANIFEST, err.line, err.col, err.message);
        return 1;
    }
    return 0;
}

int cmd_deps_manifest_save(const TomlTable* table) {
    char* buf = NULL;
    size_t len = 0;
    int rc = toml_serialize(table, &buf, &len);
    if (rc != 0) {
        cdo_log_error("Failed to serialize manifest");
        return 1;
    }

    rc = pal_file_write(CRATE_MANIFEST, buf, len);
    free(buf);
    if (rc != PAL_OK) {
        cdo_log_error("Failed to write '%s'", CRATE_MANIFEST);
        return 1;
    }
    return 0;
}

bool cmd_deps_has(const TomlTable* deps, const char* name) {
    for (const TomlEntry* e = deps->head; e != NULL; e = e->next) {
        if (strcmp(e->key, name) == 0) {
            return true;
        }
    }
    return false;
}

int cmd_deps_get_cache_dir(char* buf, size_t buf_size) {
    char home[260];
    int rc = pal_get_home_dir(home, sizeof(home));
    if (rc != PAL_OK) {
        cdo_log_error("Could not determine home directory");
        return 1;
    }
    rc = pal_path_join(buf, buf_size, home, ".cdo/cache");
    if (rc != 0) {
        cdo_log_error("Cache path too long");
        return 1;
    }
    return 0;
}

int cmd_deps_collect_dep_specs(const TomlTable* deps, DepSpec** out_specs, int* out_count) {
    int count = deps->count;
    if (count == 0) {
        *out_specs = NULL;
        *out_count = 0;
        return 0;
    }

    DepSpec* specs = (DepSpec*)calloc((size_t)count, sizeof(DepSpec));
    if (!specs) return 1;

    int i = 0;
    for (const TomlEntry* e = deps->head; e != NULL; e = e->next) {
        snprintf(specs[i].name, sizeof(specs[i].name), "%s", e->key);
        if (e->value && e->value->type == TOML_STRING) {
            snprintf(specs[i].version, sizeof(specs[i].version), "%s", e->value->as.string);
        }
        specs[i].source = DEP_REGISTRY;
        i++;
    }

    *out_specs = specs;
    *out_count = count;
    return 0;
}

int cmd_deps_regenerate_lock(const TomlTable* deps) {
    DepSpec* specs = NULL;
    int count = 0;
    int rc = cmd_deps_collect_dep_specs(deps, &specs, &count);
    if (rc != 0) {
        cdo_log_error("Failed to collect dependency specs for lock file");
        return 1;
    }

    rc = dep_lock_write(LOCK_FILE, specs, count);
    free(specs);
    if (rc != 0) {
        cdo_log_error("Failed to write lock file");
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* deps list (cdo deps list)                                                   */
/* -------------------------------------------------------------------------- */

/// Get the version string from a dependency entry value.
static const char* get_dep_version(const TomlValue* val) {
    if (!val) return "*";
    if (val->type == TOML_STRING) {
        return val->as.string;
    }
    if (val->type == TOML_TABLE || val->type == TOML_INLINE_TABLE) {
        const TomlValue* ver = toml_get(val->as.table, "version");
        if (ver && ver->type == TOML_STRING) {
            return ver->as.string;
        }
    }
    return "*";
}

int cmd_deps_list(const CliParseResult* result, void* ctx) {
    (void)result;
    (void)ctx;

    /* Load the crate manifest */
    TomlTable* root = NULL;
    if (cmd_deps_manifest_load(&root) != 0) {
        return 1;
    }

    bool printed_any = false;

    /* Print [dependencies] entries with [normal] label */
    const TomlValue* normal_val = toml_get(root, DEPS_SECTION);
    if (normal_val && (normal_val->type == TOML_TABLE || normal_val->type == TOML_INLINE_TABLE)) {
        const TomlTable* deps = normal_val->as.table;
        for (const TomlEntry* e = deps->head; e != NULL; e = e->next) {
            const char* version = get_dep_version(e->value);
            printf("  %s %s [normal]\n", e->key, version);
            printed_any = true;
        }
    }

    /* Print [dev-dependencies] entries with [dev] label */
    const TomlValue* dev_val = toml_get(root, DEV_DEPS_SECTION);
    if (dev_val && (dev_val->type == TOML_TABLE || dev_val->type == TOML_INLINE_TABLE)) {
        const TomlTable* dev_deps = dev_val->as.table;
        for (const TomlEntry* e = dev_deps->head; e != NULL; e = e->next) {
            const char* version = get_dep_version(e->value);
            printf("  %s %s [dev]\n", e->key, version);
            printed_any = true;
        }
    }

    if (!printed_any) {
        cdo_log_info("No dependencies found");
    }

    toml_free(root);
    return 0;
}

/* -------------------------------------------------------------------------- */
// End of cmd_deps.c
