#include "cmd_deps_internal.h"
#include "model/deps.h"
#include "commons/http.h"
#include "core/output.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Get or create a named table section in the manifest.
/// Returns a mutable pointer to the table, or NULL on failure.
static TomlTable* manifest_get_or_create_section(TomlTable* root, const char* section) {
    const TomlValue* val = toml_get(root, section);
    if (val != NULL) {
        if (val->type == TOML_TABLE || val->type == TOML_INLINE_TABLE) {
            return val->as.table;
        }
        cdo_error("[%s] exists but is not a table", section);
        return NULL;
    }

    /* Create a new empty table and insert into root */
    TomlValue* new_val = (TomlValue*)calloc(1, sizeof(TomlValue));
    if (!new_val) return NULL;
    new_val->type = TOML_TABLE;
    new_val->as.table = (TomlTable*)calloc(1, sizeof(TomlTable));
    if (!new_val->as.table) { free(new_val); return NULL; }

    TomlEntry* entry = (TomlEntry*)calloc(1, sizeof(TomlEntry));
    if (!entry) { free(new_val->as.table); free(new_val); return NULL; }

    entry->key = strdup(section);
    if (!entry->key) { free(entry); free(new_val->as.table); free(new_val); return NULL; }
    entry->value = new_val;
    entry->next = NULL;

    if (root->tail) {
        root->tail->next = entry;
    } else {
        root->head = entry;
    }
    root->tail = entry;
    root->count++;

    return new_val->as.table;
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

/// Add a dependency entry as an inline table: name = { version = "x.y.z", source = "..." }
static int deps_add_inline_entry(TomlTable* deps, const char* name,
                                  const char* version, const char* source,
                                  const char* url) {
    /* Create the inline table */
    TomlTable* inline_tbl = (TomlTable*)calloc(1, sizeof(TomlTable));
    if (!inline_tbl) return 1;

    /* Add "version" entry */
    {
        TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
        if (!v) { free(inline_tbl); return 1; }
        v->type = TOML_STRING;
        v->as.string = strdup(version);
        if (!v->as.string) { free(v); free(inline_tbl); return 1; }

        TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
        if (!e) { free(v->as.string); free(v); free(inline_tbl); return 1; }
        e->key = strdup("version");
        e->value = v;
        e->next = NULL;

        inline_tbl->head = e;
        inline_tbl->tail = e;
        inline_tbl->count = 1;
    }

    /* Add "source" entry */
    {
        TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
        if (!v) return 1;
        v->type = TOML_STRING;
        v->as.string = strdup(source);
        if (!v->as.string) { free(v); return 1; }

        TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
        if (!e) { free(v->as.string); free(v); return 1; }
        e->key = strdup("source");
        e->value = v;
        e->next = NULL;

        inline_tbl->tail->next = e;
        inline_tbl->tail = e;
        inline_tbl->count++;
    }

    /* Add "url" entry if provided */
    if (url && url[0] != '\0') {
        TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
        if (!v) return 1;
        v->type = TOML_STRING;
        v->as.string = strdup(url);
        if (!v->as.string) { free(v); return 1; }

        TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
        if (!e) { free(v->as.string); free(v); return 1; }
        e->key = strdup("url");
        e->value = v;
        e->next = NULL;

        inline_tbl->tail->next = e;
        inline_tbl->tail = e;
        inline_tbl->count++;
    }

    /* Wrap inline table in a TomlValue */
    TomlValue* tbl_val = (TomlValue*)calloc(1, sizeof(TomlValue));
    if (!tbl_val) return 1;
    tbl_val->type = TOML_INLINE_TABLE;
    tbl_val->as.table = inline_tbl;

    /* Add entry to deps table */
    TomlEntry* entry = (TomlEntry*)calloc(1, sizeof(TomlEntry));
    if (!entry) { free(tbl_val); return 1; }
    entry->key = strdup(name);
    if (!entry->key) { free(entry); free(tbl_val); return 1; }
    entry->value = tbl_val;
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

/// Parse a package name with optional @version suffix.
static void parse_name_version(const char* input, char* name_buf, size_t name_size,
                               char* version_buf, size_t version_size) {
    const char* at = strchr(input, '@');
    if (at) {
        size_t name_len = (size_t)(at - input);
        if (name_len >= name_size) name_len = name_size - 1;
        memcpy(name_buf, input, name_len);
        name_buf[name_len] = '\0';

        const char* ver = at + 1;
        size_t ver_len = strlen(ver);
        if (ver_len >= version_size) ver_len = version_size - 1;
        memcpy(version_buf, ver, ver_len);
        version_buf[ver_len] = '\0';
    } else {
        size_t len = strlen(input);
        if (len >= name_size) len = name_size - 1;
        memcpy(name_buf, input, len);
        name_buf[len] = '\0';
        version_buf[0] = '\0';
    }
}

/// Check if "--dev" flag is present in positional args or argv_rest.
static bool has_dev_flag(const CdoOptions* opts) {
    for (int i = 0; i < opts->positional_count; i++) {
        if (strcmp(opts->positional_args[i], "--dev") == 0) {
            return true;
        }
    }
    for (int i = 0; i < opts->argc_rest; i++) {
        if (strcmp(opts->argv_rest[i], "--dev") == 0) {
            return true;
        }
    }
    return false;
}

/// Helper: append a string array as a TOML array entry in an inline table.
static int append_string_array(TomlTable* tbl, const char* key,
                               char* const* strings, int count) {
    if (count <= 0) return 0;
    TomlArray* arr = (TomlArray*)calloc(1, sizeof(TomlArray));
    if (!arr) return 1;
    arr->items = (TomlValue**)calloc((size_t)count, sizeof(TomlValue*));
    if (!arr->items) { free(arr); return 1; }
    arr->count = count;
    arr->capacity = count;
    for (int i = 0; i < count; i++) {
        TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
        if (!v) return 1;
        v->type = TOML_STRING;
        v->as.string = strdup(strings[i]);
        arr->items[i] = v;
    }
    TomlValue* arr_val = (TomlValue*)calloc(1, sizeof(TomlValue));
    if (!arr_val) return 1;
    arr_val->type = TOML_ARRAY;
    arr_val->as.array = arr;
    TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
    if (!e) return 1;
    e->key = strdup(key);
    e->value = arr_val;
    e->next = NULL;
    tbl->tail->next = e;
    tbl->tail = e;
    tbl->count++;
    return 0;
}

/// Persist build metadata (include_dirs, link_libs, defines) as arrays in the inline table.
static int deps_persist_build_metadata(TomlTable* dep_inline_tbl,
                                        const CatalogResolveResult* resolved) {
    if (append_string_array(dep_inline_tbl, "include_dirs",
                            resolved->include_dirs, resolved->include_dir_count) != 0)
        return 1;
    if (append_string_array(dep_inline_tbl, "link_libs",
                            resolved->link_libs, resolved->link_lib_count) != 0)
        return 1;
    if (append_string_array(dep_inline_tbl, "defines",
                            resolved->defines, resolved->define_count) != 0)
        return 1;
    return 0;
}

/// Add a catalog-resolved dependency entry with full build metadata to the deps table.
static int deps_add_catalog_entry(TomlTable* deps, const char* name,
                                   const CatalogResolveResult* resolved) {
    /* Create the inline table */
    TomlTable* inline_tbl = (TomlTable*)calloc(1, sizeof(TomlTable));
    if (!inline_tbl) return 1;

    /* Add "version" entry */
    {
        TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
        if (!v) { free(inline_tbl); return 1; }
        v->type = TOML_STRING;
        v->as.string = strdup(resolved->version);
        if (!v->as.string) { free(v); free(inline_tbl); return 1; }

        TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
        if (!e) { free(v->as.string); free(v); free(inline_tbl); return 1; }
        e->key = strdup("version");
        e->value = v;
        e->next = NULL;

        inline_tbl->head = e;
        inline_tbl->tail = e;
        inline_tbl->count = 1;
    }

    /* Add "source" entry */
    {
        TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
        if (!v) return 1;
        v->type = TOML_STRING;
        v->as.string = strdup("catalog");
        if (!v->as.string) { free(v); return 1; }

        TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
        if (!e) { free(v->as.string); free(v); return 1; }
        e->key = strdup("source");
        e->value = v;
        e->next = NULL;

        inline_tbl->tail->next = e;
        inline_tbl->tail = e;
        inline_tbl->count++;
    }

    /* Add "url" entry if present */
    if (resolved->url[0] != '\0') {
        TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
        if (!v) return 1;
        v->type = TOML_STRING;
        v->as.string = strdup(resolved->url);
        if (!v->as.string) { free(v); return 1; }

        TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
        if (!e) { free(v->as.string); free(v); return 1; }
        e->key = strdup("url");
        e->value = v;
        e->next = NULL;

        inline_tbl->tail->next = e;
        inline_tbl->tail = e;
        inline_tbl->count++;
    }

    /* Add "checksum" entry if present */
    if (resolved->checksum[0] != '\0') {
        TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
        if (!v) return 1;
        v->type = TOML_STRING;
        v->as.string = strdup(resolved->checksum);
        if (!v->as.string) { free(v); return 1; }

        TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
        if (!e) { free(v->as.string); free(v); return 1; }
        e->key = strdup("checksum");
        e->value = v;
        e->next = NULL;

        inline_tbl->tail->next = e;
        inline_tbl->tail = e;
        inline_tbl->count++;
    }

    /* Persist build metadata (include_dirs, link_libs, defines) */
    if (deps_persist_build_metadata(inline_tbl, resolved) != 0) {
        return 1;
    }

    /* Wrap inline table in a TomlValue */
    TomlValue* tbl_val = (TomlValue*)calloc(1, sizeof(TomlValue));
    if (!tbl_val) return 1;
    tbl_val->type = TOML_INLINE_TABLE;
    tbl_val->as.table = inline_tbl;

    /* Add entry to deps table */
    TomlEntry* entry = (TomlEntry*)calloc(1, sizeof(TomlEntry));
    if (!entry) { free(tbl_val); return 1; }
    entry->key = strdup(name);
    if (!entry->key) { free(entry); free(tbl_val); return 1; }
    entry->value = tbl_val;
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


int deps_add(const CdoOptions* opts) {
    if (opts->positional_count == 0) {
        cdo_error("Usage: cdo deps add <package[@version]> [--dev]");
        return 1;
    }

    /* positional_args[0] == "add" (from cmd_deps dispatch), skip it */
    int start_idx = 1;

    /* Check if we have any actual package arguments */
    bool has_packages = false;
    for (int i = start_idx; i < opts->positional_count; i++) {
        if (strcmp(opts->positional_args[i], "--dev") != 0) {
            has_packages = true;
            break;
        }
    }
    if (!has_packages) {
        cdo_error("Usage: cdo deps add <package[@version]> [--dev]");
        return 1;
    }

    /* Detect --dev flag */
    bool dev_mode = has_dev_flag(opts);

    /* Load the crate manifest */
    TomlTable* root = NULL;
    if (cmd_deps_manifest_load(&root) != 0) {
        return 1;
    }

    /* Get or create [dependencies] table (always needed for conflict check) */
    TomlTable* deps = manifest_get_or_create_section(root, DEPS_SECTION);
    if (!deps) {
        toml_free(root);
        return 1;
    }

    /* Get or create [dev-dependencies] table if --dev is set */
    TomlTable* dev_deps = NULL;
    if (dev_mode) {
        dev_deps = manifest_get_or_create_section(root, DEV_DEPS_SECTION);
        if (!dev_deps) {
            toml_free(root);
            return 1;
        }
    }

    /* The target table for insertion */
    TomlTable* target_deps = dev_mode ? dev_deps : deps;

    /* Load catalog for resolution */
    Catalog catalog = {0};
    int cat_rc = catalog_load(&catalog, ".");
    if (cat_rc != 0) {
        cdo_warn("Failed to load catalog — falling back to registry lookup");
    }

    /* Detect platform */
    CatalogPlatform platform = {0};
    int plat_rc = catalog_detect_platform(&platform);
    if (plat_rc != 0) {
        cdo_warn("Could not detect platform — catalog resolution may fail");
    }

    /* Determine cache directory */
    char cache_dir[512];
    if (cmd_deps_get_cache_dir(cache_dir, sizeof(cache_dir)) != 0) {
        catalog_free(&catalog);
        toml_free(root);
        return 1;
    }

    /* Ensure cache directory exists */
    pal_mkdir_p(cache_dir);

    int result = 0;

    for (int i = start_idx; i < opts->positional_count; i++) {
        const char* arg = opts->positional_args[i];

        /* Skip --dev flag in positional args */
        if (strcmp(arg, "--dev") == 0) {
            continue;
        }

        /* Parse name@version suffix */
        char pkg_name[128];
        char version_constraint[64];
        parse_name_version(arg, pkg_name, sizeof(pkg_name),
                           version_constraint, sizeof(version_constraint));

        /* Check for conflict: --dev but already in [dependencies] */
        if (dev_mode && cmd_deps_has(deps, pkg_name)) {
            cdo_error("'%s' already exists in [dependencies] — cannot add as dev-dependency",
                      pkg_name);
            result = 1;
            continue;
        }

        /* Check if already in the target section */
        if (cmd_deps_has(target_deps, pkg_name)) {
            cdo_info("'%s' is already in %s — skipping",
                     pkg_name, dev_mode ? "dev-dependencies" : "dependencies");
            continue;
        }

        /* Try catalog resolution */
        CatalogResolveResult resolved = {0};
        const char* constraint = (version_constraint[0] != '\0') ? version_constraint : NULL;
        int resolve_rc = catalog_resolve_package(&catalog, pkg_name, constraint,
                                                  &platform, &resolved);

        if (resolve_rc == 0) {
            /* Catalog resolution succeeded — add with full metadata */
            int rc = deps_add_catalog_entry(target_deps, pkg_name, &resolved);
            if (rc != 0) {
                cdo_error("Failed to add '%s' to manifest", pkg_name);
                result = 1;
                catalog_resolve_result_free(&resolved);
                continue;
            }

            cdo_info("Added '%s' version %s (from catalog)%s",
                     pkg_name, resolved.version,
                     dev_mode ? " [dev]" : "");
            catalog_resolve_result_free(&resolved);
        } else {
            /* Catalog resolution failed — fall back to registry */
            cdo_warn("Package '%s' not found in catalog — using registry fallback", pkg_name);

            DepSpec spec = {0};
            snprintf(spec.name, sizeof(spec.name), "%s", pkg_name);
            spec.source = DEP_REGISTRY;
            snprintf(spec.url, sizeof(spec.url), "%s/%s", DEFAULT_REGISTRY, pkg_name);

            ResolvedDep resolved_dep = {0};
            int rc = dep_resolve(&spec, cache_dir, &resolved_dep);
            if (rc != 0) {
                cdo_error("Failed to resolve package '%s'", pkg_name);
                result = 1;
                dep_resolved_free(&resolved_dep);
                continue;
            }

            /* Use resolved version if available, otherwise use "*" */
            const char* version = (spec.version[0] != '\0') ? spec.version : "*";

            rc = deps_add_entry(target_deps, pkg_name, version);
            if (rc != 0) {
                cdo_error("Failed to add '%s' to manifest", pkg_name);
                result = 1;
                dep_resolved_free(&resolved_dep);
                continue;
            }

            cdo_info("Added '%s' version %s%s",
                     pkg_name, version,
                     dev_mode ? " [dev]" : "");
            dep_resolved_free(&resolved_dep);
        }
    }

    /* Write updated manifest */
    if (result == 0) {
        if (cmd_deps_manifest_save(root) != 0) {
            catalog_free(&catalog);
            toml_free(root);
            return 1;
        }

        /* Regenerate lock file from [dependencies] */
        if (cmd_deps_regenerate_lock(deps) != 0) {
            catalog_free(&catalog);
            toml_free(root);
            return 1;
        }
    }

    catalog_free(&catalog);
    toml_free(root);
    return result;
}
