#include "commands/cmd_deps.h"
#include "core/catalog.h"
#include "core/deps.h"
#include "core/http.h"
#include "core/output.h"
#include "core/toml.h"
#include "pal/pal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRATE_MANIFEST      "crate.toml"
#define LOCK_FILE           "cdo.lock"
#define DEPS_SECTION        "dependencies"
#define DEV_DEPS_SECTION    "dev-dependencies"

/* Default registry URL (fallback when catalog resolution fails) */
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

/// Get or create the [dev-dependencies] table in the manifest.
/// Returns a mutable pointer to the dev-dependencies table, or NULL on failure.
static TomlTable* manifest_get_or_create_dev_deps(TomlTable* root) {
    /* Look for existing [dev-dependencies] section */
    const TomlValue* val = toml_get(root, DEV_DEPS_SECTION);
    if (val != NULL) {
        if (val->type == TOML_TABLE || val->type == TOML_INLINE_TABLE) {
            return val->as.table;
        }
        cdo_error("[dev-dependencies] exists but is not a table");
        return NULL;
    }

    /* Create a new empty [dev-dependencies] table and insert into root */
    TomlValue* new_val = (TomlValue*)calloc(1, sizeof(TomlValue));
    if (!new_val) return NULL;
    new_val->type = TOML_TABLE;
    new_val->as.table = (TomlTable*)calloc(1, sizeof(TomlTable));
    if (!new_val->as.table) { free(new_val); return NULL; }

    TomlEntry* entry = (TomlEntry*)calloc(1, sizeof(TomlEntry));
    if (!entry) { free(new_val->as.table); free(new_val); return NULL; }

    entry->key = strdup(DEV_DEPS_SECTION);
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

/// Add a dependency entry as an inline table with version, source, and optional url.
/// Format: name = { version = "x.y.z", source = "catalog" }
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
/// Input: "sdl3@^3.0.0" → name_buf="sdl3", version_buf="^3.0.0"
/// Input: "sdl3"         → name_buf="sdl3", version_buf=""
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

/// Persist build metadata (include_dirs, link_libs, defines) as arrays in the
/// dependency's inline table entry. This information is written as top-level
/// keys in the crate manifest under [dependencies.<name>.build] or directly
/// within the inline table for the dependency.
/// For simplicity, we write them as arrays in the inline table.
static int deps_persist_build_metadata(TomlTable* dep_inline_tbl,
                                        const CatalogResolveResult* resolved) {
    /* Add include_dirs array */
    if (resolved->include_dir_count > 0) {
        TomlArray* arr = (TomlArray*)calloc(1, sizeof(TomlArray));
        if (!arr) return 1;
        arr->items = (TomlValue**)calloc((size_t)resolved->include_dir_count, sizeof(TomlValue*));
        if (!arr->items) { free(arr); return 1; }
        arr->count = resolved->include_dir_count;
        arr->capacity = resolved->include_dir_count;
        for (int i = 0; i < resolved->include_dir_count; i++) {
            TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
            if (!v) return 1;
            v->type = TOML_STRING;
            v->as.string = strdup(resolved->include_dirs[i]);
            arr->items[i] = v;
        }
        TomlValue* arr_val = (TomlValue*)calloc(1, sizeof(TomlValue));
        if (!arr_val) return 1;
        arr_val->type = TOML_ARRAY;
        arr_val->as.array = arr;

        TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
        if (!e) return 1;
        e->key = strdup("include_dirs");
        e->value = arr_val;
        e->next = NULL;
        dep_inline_tbl->tail->next = e;
        dep_inline_tbl->tail = e;
        dep_inline_tbl->count++;
    }

    /* Add link_libs array */
    if (resolved->link_lib_count > 0) {
        TomlArray* arr = (TomlArray*)calloc(1, sizeof(TomlArray));
        if (!arr) return 1;
        arr->items = (TomlValue**)calloc((size_t)resolved->link_lib_count, sizeof(TomlValue*));
        if (!arr->items) { free(arr); return 1; }
        arr->count = resolved->link_lib_count;
        arr->capacity = resolved->link_lib_count;
        for (int i = 0; i < resolved->link_lib_count; i++) {
            TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
            if (!v) return 1;
            v->type = TOML_STRING;
            v->as.string = strdup(resolved->link_libs[i]);
            arr->items[i] = v;
        }
        TomlValue* arr_val = (TomlValue*)calloc(1, sizeof(TomlValue));
        if (!arr_val) return 1;
        arr_val->type = TOML_ARRAY;
        arr_val->as.array = arr;

        TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
        if (!e) return 1;
        e->key = strdup("link_libs");
        e->value = arr_val;
        e->next = NULL;
        dep_inline_tbl->tail->next = e;
        dep_inline_tbl->tail = e;
        dep_inline_tbl->count++;
    }

    /* Add defines array */
    if (resolved->define_count > 0) {
        TomlArray* arr = (TomlArray*)calloc(1, sizeof(TomlArray));
        if (!arr) return 1;
        arr->items = (TomlValue**)calloc((size_t)resolved->define_count, sizeof(TomlValue*));
        if (!arr->items) { free(arr); return 1; }
        arr->count = resolved->define_count;
        arr->capacity = resolved->define_count;
        for (int i = 0; i < resolved->define_count; i++) {
            TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
            if (!v) return 1;
            v->type = TOML_STRING;
            v->as.string = strdup(resolved->defines[i]);
            arr->items[i] = v;
        }
        TomlValue* arr_val = (TomlValue*)calloc(1, sizeof(TomlValue));
        if (!arr_val) return 1;
        arr_val->type = TOML_ARRAY;
        arr_val->as.array = arr;

        TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
        if (!e) return 1;
        e->key = strdup("defines");
        e->value = arr_val;
        e->next = NULL;
        dep_inline_tbl->tail->next = e;
        dep_inline_tbl->tail = e;
        dep_inline_tbl->count++;
    }

    return 0;
}

/// Add a catalog-resolved dependency entry with full build metadata to the deps table.
/// Format: name = { version = "x.y.z", source = "catalog", url = "...",
///                   include_dirs = [...], link_libs = [...], defines = [...] }
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
/* deps_add                                                                    */
/* -------------------------------------------------------------------------- */

static int deps_add(const CdoOptions* opts) {
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
    if (manifest_load(&root) != 0) {
        return 1;
    }

    /* Get or create [dependencies] table (always needed for conflict check) */
    TomlTable* deps = manifest_get_or_create_deps(root);
    if (!deps) {
        toml_free(root);
        return 1;
    }

    /* Get or create [dev-dependencies] table if --dev is set */
    TomlTable* dev_deps = NULL;
    if (dev_mode) {
        dev_deps = manifest_get_or_create_dev_deps(root);
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
    if (get_cache_dir(cache_dir, sizeof(cache_dir)) != 0) {
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
        if (dev_mode && deps_has(deps, pkg_name)) {
            cdo_error("'%s' already exists in [dependencies] — cannot add as dev-dependency",
                      pkg_name);
            result = 1;
            continue;
        }

        /* Check if already in the target section */
        if (deps_has(target_deps, pkg_name)) {
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
        if (manifest_save(root) != 0) {
            catalog_free(&catalog);
            toml_free(root);
            return 1;
        }

        /* Regenerate lock file from [dependencies] */
        if (regenerate_lock(deps) != 0) {
            catalog_free(&catalog);
            toml_free(root);
            return 1;
        }
    }

    catalog_free(&catalog);
    toml_free(root);
    return result;
}

/* -------------------------------------------------------------------------- */
/* deps_remove (cdo deps remove <name> [--dev])                                */
/* -------------------------------------------------------------------------- */

/// Check if --dev flag is present in the positional args starting at start_idx.
/// This scans remaining positional args for "--dev".
static bool has_dev_flag_from(const CdoOptions* opts, int start_idx) {
    for (int i = start_idx; i < opts->positional_count; i++) {
        if (strcmp(opts->positional_args[i], "--dev") == 0) {
            return true;
        }
    }
    /* Also check argv_rest for --dev */
    for (int i = 0; i < opts->argc_rest; i++) {
        if (strcmp(opts->argv_rest[i], "--dev") == 0) {
            return true;
        }
    }
    return false;
}

/// Get the first non-flag positional arg starting at start_idx.
/// Returns the arg string or NULL if none found.
static const char* get_dep_name(const CdoOptions* opts, int start_idx) {
    for (int i = start_idx; i < opts->positional_count; i++) {
        if (opts->positional_args[i][0] != '-') {
            return opts->positional_args[i];
        }
    }
    return NULL;
}

static int deps_remove(const CdoOptions* opts) {
    /* positional_args[0] = "remove", remaining = <name> [--dev] */
    const char* dep_name = get_dep_name(opts, 1);
    if (!dep_name) {
        cdo_error("Usage: cdo deps remove <name> [--dev]");
        return 1;
    }

    bool dev = has_dev_flag_from(opts, 1);
    const char* section_name = dev ? DEV_DEPS_SECTION : DEPS_SECTION;

    /* Load the crate manifest */
    TomlTable* root = NULL;
    if (manifest_load(&root) != 0) {
        return 1;
    }

    /* Get the targeted section */
    const TomlValue* val = toml_get(root, section_name);
    if (!val || (val->type != TOML_TABLE && val->type != TOML_INLINE_TABLE)) {
        cdo_error("dependency '%s' not found in [%s]", dep_name, section_name);
        toml_free(root);
        return 1;
    }

    TomlTable* deps = val->as.table;

    /* Attempt removal */
    if (!deps_remove_entry(deps, dep_name)) {
        cdo_error("dependency '%s' not found in [%s]", dep_name, section_name);
        toml_free(root);
        return 1;
    }

    cdo_info("Removed '%s' from [%s]", dep_name, section_name);

    /* Write updated manifest */
    if (manifest_save(root) != 0) {
        toml_free(root);
        return 1;
    }

    /* Regenerate lock file from [dependencies] (normal deps drive the lock) */
    const TomlValue* normal_val = toml_get(root, DEPS_SECTION);
    if (normal_val && (normal_val->type == TOML_TABLE || normal_val->type == TOML_INLINE_TABLE)) {
        if (regenerate_lock(normal_val->as.table) != 0) {
            toml_free(root);
            return 1;
        }
    } else {
        /* No dependencies section — write empty lock */
        TomlTable empty = { .head = NULL, .tail = NULL, .count = 0 };
        if (regenerate_lock(&empty) != 0) {
            toml_free(root);
            return 1;
        }
    }

    toml_free(root);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* deps_list (new: cdo deps list)                                              */
/* -------------------------------------------------------------------------- */

/// Get the version string from a dependency entry value.
/// Handles both simple string values ("1.0.0") and inline table values
/// ({ version = "1.0.0", source = "catalog" }).
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

static int deps_list(const CdoOptions* opts) {
    (void)opts; /* unused for now */

    /* Load the crate manifest */
    TomlTable* root = NULL;
    if (manifest_load(&root) != 0) {
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
        cdo_info("No dependencies found");
    }

    toml_free(root);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* cmd_deps (dispatcher: cdo deps <add|remove|list>)                           */
/* -------------------------------------------------------------------------- */

int cmd_deps(const CdoOptions* opts) {
    if (opts->help || opts->positional_count < 1) {
        cdo_cli_print_help(CDO_CMD_DEPS, stdout);
        return opts->help ? 0 : 1;
    }

    const char* subcmd = opts->positional_args[0];

    if (strcmp(subcmd, "add") == 0) {
        return deps_add(opts);
    } else if (strcmp(subcmd, "remove") == 0) {
        return deps_remove(opts);
    } else if (strcmp(subcmd, "list") == 0) {
        return deps_list(opts);
    } else {
        cdo_error("Unknown subcommand '%s'", subcmd);
        cdo_info("Available subcommands: add, remove, list");
        cdo_info("Run 'cdo deps --help' for usage information.");
        return 1;
    }
}
