#include "commands/cmd_deps.h"
#include "core/deps.h"
#include "core/http.h"
#include "core/output.h"
#include "core/toml.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRATE_MANIFEST  "crate.toml"
#define LOCK_FILE       "cdo.lock"
#define DEPS_SECTION    "dependencies"

/* Default registry URL (fallback when none configured in workspace manifest) */
#define DEFAULT_REGISTRY "https://registry.cdo.dev/v1/packages"

/* -------------------------------------------------------------------------- */
/* Helpers                                                                     */
/* -------------------------------------------------------------------------- */

/// Read and parse the crate manifest in the current directory.
/// Caller must free *table via toml_free() on success.
static int manifest_load(TomlTable** table) {
    char* buf = NULL;
    size_t len = 0;
    int rc = pal_file_read(CRATE_MANIFEST, &buf, &len);
    if (rc != PAL_OK) {
        cdo_error("Could not read '%s' — are you in a crate directory?", CRATE_MANIFEST);
        return 1;
    }

    TomlError err = {0};
    rc = toml_parse(buf, len, table, &err);
    free(buf);
    if (rc != 0) {
        cdo_error("Failed to parse '%s': line %d col %d: %s",
                  CRATE_MANIFEST, err.line, err.col, err.message);
        return 1;
    }
    return 0;
}

/// Serialize and write the manifest table back to crate.toml.
static int manifest_save(const TomlTable* table) {
    char* buf = NULL;
    size_t len = 0;
    int rc = toml_serialize(table, &buf, &len);
    if (rc != 0) {
        cdo_error("Failed to serialize manifest");
        return 1;
    }

    rc = pal_file_write(CRATE_MANIFEST, buf, len);
    free(buf);
    if (rc != PAL_OK) {
        cdo_error("Failed to write '%s'", CRATE_MANIFEST);
        return 1;
    }
    return 0;
}

/// Get or create the [dependencies] table in the manifest.
/// Returns a mutable pointer to the dependencies table, or NULL on failure.
static TomlTable* manifest_get_or_create_deps(TomlTable* root) {
    /* Look for existing [dependencies] section */
    const TomlValue* val = toml_get(root, DEPS_SECTION);
    if (val != NULL) {
        if (val->type == TOML_TABLE || val->type == TOML_INLINE_TABLE) {
            return val->as.table;
        }
        cdo_error("[dependencies] exists but is not a table");
        return NULL;
    }

    /* Create a new empty [dependencies] table and insert into root */
    TomlValue* new_val = (TomlValue*)calloc(1, sizeof(TomlValue));
    if (!new_val) return NULL;
    new_val->type = TOML_TABLE;
    new_val->as.table = (TomlTable*)calloc(1, sizeof(TomlTable));
    if (!new_val->as.table) { free(new_val); return NULL; }

    TomlEntry* entry = (TomlEntry*)calloc(1, sizeof(TomlEntry));
    if (!entry) { free(new_val->as.table); free(new_val); return NULL; }

    entry->key = strdup(DEPS_SECTION);
    if (!entry->key) { free(entry); free(new_val->as.table); free(new_val); return NULL; }
    entry->value = new_val;
    entry->next = NULL;

    /* Append to root table's linked list */
    if (root->tail) {
        root->tail->next = entry;
    } else {
        root->head = entry;
    }
    root->tail = entry;
    root->count++;

    return new_val->as.table;
}

/// Check if a dependency with the given name already exists in the deps table.
static bool deps_has(const TomlTable* deps, const char* name) {
    for (const TomlEntry* e = deps->head; e != NULL; e = e->next) {
        if (strcmp(e->key, name) == 0) {
            return true;
        }
    }
    return false;
}

/// Add a dependency entry (name = version_string) to the deps table.
static int deps_add_entry(TomlTable* deps, const char* name, const char* version) {
    TomlValue* val = (TomlValue*)calloc(1, sizeof(TomlValue));
    if (!val) return 1;
    val->type = TOML_STRING;
    val->as.string = strdup(version);
    if (!val->as.string) { free(val); return 1; }

    TomlEntry* entry = (TomlEntry*)calloc(1, sizeof(TomlEntry));
    if (!entry) { free(val->as.string); free(val); return 1; }
    entry->key = strdup(name);
    if (!entry->key) { free(entry); free(val->as.string); free(val); return 1; }
    entry->value = val;
    entry->next = NULL;

    if (deps->tail) {
        deps->tail->next = entry;
    } else {
        deps->head = entry;
    }
    deps->tail = entry;
    deps->count++;
    return 0;
}

/// Remove a dependency entry from the deps table. Returns true if found and removed.
static bool deps_remove_entry(TomlTable* deps, const char* name) {
    TomlEntry* prev = NULL;
    for (TomlEntry* e = deps->head; e != NULL; prev = e, e = e->next) {
        if (strcmp(e->key, name) == 0) {
            /* Unlink */
            if (prev) {
                prev->next = e->next;
            } else {
                deps->head = e->next;
            }
            if (deps->tail == e) {
                deps->tail = prev;
            }
            deps->count--;

            /* Free the entry */
            free(e->key);
            toml_value_free(e->value);
            free(e);
            return true;
        }
    }
    return false;
}

/// Build the cache directory path (~/.cdo/cache).
static int get_cache_dir(char* buf, size_t buf_size) {
    char home[260];
    int rc = pal_get_home_dir(home, sizeof(home));
    if (rc != PAL_OK) {
        cdo_error("Could not determine home directory");
        return 1;
    }
    rc = pal_path_join(buf, buf_size, home, ".cdo/cache");
    if (rc != 0) {
        cdo_error("Cache path too long");
        return 1;
    }
    return 0;
}

/// Collect all dependency specs from the manifest's [dependencies] table
/// for lock file generation. Caller frees the returned array.
static int collect_dep_specs(const TomlTable* deps, DepSpec** out_specs, int* out_count) {
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

/// Regenerate the lock file from the current dependencies table.
static int regenerate_lock(const TomlTable* deps) {
    DepSpec* specs = NULL;
    int count = 0;
    int rc = collect_dep_specs(deps, &specs, &count);
    if (rc != 0) {
        cdo_error("Failed to collect dependency specs for lock file");
        return 1;
    }

    rc = dep_lock_write(LOCK_FILE, specs, count);
    free(specs);
    if (rc != 0) {
        cdo_error("Failed to write lock file");
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* cmd_add                                                                     */
/* -------------------------------------------------------------------------- */

int cmd_add(const CdoOptions* opts) {
    if (opts->positional_count == 0) {
        cdo_error("Usage: cdo add <package> [package...]");
        return 1;
    }

    /* Load the crate manifest */
    TomlTable* root = NULL;
    if (manifest_load(&root) != 0) {
        return 1;
    }

    /* Get or create [dependencies] */
    TomlTable* deps = manifest_get_or_create_deps(root);
    if (!deps) {
        toml_free(root);
        return 1;
    }

    /* Determine cache directory */
    char cache_dir[512];
    if (get_cache_dir(cache_dir, sizeof(cache_dir)) != 0) {
        toml_free(root);
        return 1;
    }

    /* Ensure cache directory exists */
    pal_mkdir_p(cache_dir);

    int result = 0;

    for (int i = 0; i < opts->positional_count; i++) {
        const char* pkg_name = opts->positional_args[i];

        /* Skip if already present */
        if (deps_has(deps, pkg_name)) {
            cdo_info("'%s' is already in dependencies — skipping", pkg_name);
            continue;
        }

        /* Resolve the dependency (checks cache, downloads if needed) */
        DepSpec spec = {0};
        snprintf(spec.name, sizeof(spec.name), "%s", pkg_name);
        spec.source = DEP_REGISTRY;
        snprintf(spec.url, sizeof(spec.url), "%s/%s", DEFAULT_REGISTRY, pkg_name);

        ResolvedDep resolved = {0};
        int rc = dep_resolve(&spec, cache_dir, &resolved);
        if (rc != 0) {
            cdo_error("Failed to resolve package '%s'", pkg_name);
            result = 1;
            dep_resolved_free(&resolved);
            continue;
        }

        /* Use resolved version if available, otherwise use "*" */
        const char* version = (spec.version[0] != '\0') ? spec.version : "*";

        /* Add to [dependencies] */
        rc = deps_add_entry(deps, pkg_name, version);
        if (rc != 0) {
            cdo_error("Failed to add '%s' to manifest", pkg_name);
            result = 1;
            dep_resolved_free(&resolved);
            continue;
        }

        cdo_info("Added '%s' version %s", pkg_name, version);
        dep_resolved_free(&resolved);
    }

    /* Write updated manifest */
    if (result == 0) {
        if (manifest_save(root) != 0) {
            toml_free(root);
            return 1;
        }

        /* Regenerate lock file */
        if (regenerate_lock(deps) != 0) {
            toml_free(root);
            return 1;
        }
    }

    toml_free(root);
    return result;
}

/* -------------------------------------------------------------------------- */
/* cmd_remove                                                                  */
/* -------------------------------------------------------------------------- */

int cmd_remove(const CdoOptions* opts) {
    if (opts->positional_count == 0) {
        cdo_error("Usage: cdo remove <package> [package...]");
        return 1;
    }

    /* Load the crate manifest */
    TomlTable* root = NULL;
    if (manifest_load(&root) != 0) {
        return 1;
    }

    /* Get [dependencies] — if missing, nothing to remove */
    const TomlValue* val = toml_get(root, DEPS_SECTION);
    if (!val || (val->type != TOML_TABLE && val->type != TOML_INLINE_TABLE)) {
        cdo_warn("No [dependencies] section found in '%s'", CRATE_MANIFEST);
        toml_free(root);
        return 0;
    }

    TomlTable* deps = val->as.table;
    bool any_removed = false;

    for (int i = 0; i < opts->positional_count; i++) {
        const char* pkg_name = opts->positional_args[i];

        if (deps_remove_entry(deps, pkg_name)) {
            cdo_info("Removed '%s'", pkg_name);
            any_removed = true;
        } else {
            cdo_warn("'%s' not found in dependencies", pkg_name);
        }
    }

    if (any_removed) {
        /* Write updated manifest */
        if (manifest_save(root) != 0) {
            toml_free(root);
            return 1;
        }

        /* Regenerate lock file */
        if (regenerate_lock(deps) != 0) {
            toml_free(root);
            return 1;
        }
    }

    toml_free(root);
    return 0;
}
