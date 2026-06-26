/*
 * catalog.c — Catalog registry: platform detection, TOML loading, and resolution.
 */

#include "catalog.h"
#include "output.h"
#include "semver.h"
#include "toml.h"
#include "../pal/pal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Platform Detection
 * -------------------------------------------------------------------------- */

int catalog_detect_platform(CatalogPlatform* out)
{
    const char* os   = NULL;
    const char* arch = NULL;

    if (!out) {
        return 1;
    }

    /* Detect operating system */
#if defined(_WIN32)
    os = "windows";
#elif defined(__linux__)
    os = "linux";
#elif defined(__APPLE__)
    os = "macos";
#else
    cdo_error("unsupported operating system — catalog resolution requires windows, linux, or macos");
    return 1;
#endif

    /* Detect CPU architecture */
#if defined(_M_X64) || defined(__x86_64__)
    arch = "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    arch = "arm64";
#else
    cdo_error("unsupported CPU architecture — catalog resolution requires x86_64 or arm64");
    return 1;
#endif

    /* Copy OS and arch into output struct */
    snprintf(out->os, sizeof(out->os), "%s", os);
    snprintf(out->arch, sizeof(out->arch), "%s", arch);

    /* Construct the platform triple: {os}-{arch} */
    snprintf(out->triple, sizeof(out->triple), "%s-%s", os, arch);

    return 0;
}

/* --------------------------------------------------------------------------
 * Dynamic Array Helpers
 * -------------------------------------------------------------------------- */

#define CATALOG_INITIAL_CAPACITY 16

static int catalog_grow_tools(Catalog* cat)
{
    int new_cap = cat->tool_capacity == 0 ? CATALOG_INITIAL_CAPACITY
                                          : cat->tool_capacity * 2;
    CatalogToolEntry* new_arr = (CatalogToolEntry*)realloc(
        cat->tools, (size_t)new_cap * sizeof(CatalogToolEntry));
    if (!new_arr) {
        return 1;
    }
    cat->tools = new_arr;
    cat->tool_capacity = new_cap;
    return 0;
}

static int catalog_grow_packages(Catalog* cat)
{
    int new_cap = cat->package_capacity == 0 ? CATALOG_INITIAL_CAPACITY
                                             : cat->package_capacity * 2;
    CatalogPackageEntry* new_arr = (CatalogPackageEntry*)realloc(
        cat->packages, (size_t)new_cap * sizeof(CatalogPackageEntry));
    if (!new_arr) {
        return 1;
    }
    cat->packages = new_arr;
    cat->package_capacity = new_cap;
    return 0;
}

/* --------------------------------------------------------------------------
 * Name Validation
 * -------------------------------------------------------------------------- */

/**
 * Validate a catalog entry name.
 * Valid names: 1-128 characters, lowercase alphanumeric + hyphens + underscores.
 * Returns true if valid, false otherwise.
 */
static bool catalog_validate_name(const char* name)
{
    if (!name) return false;

    size_t len = strlen(name);
    if (len == 0 || len > CATALOG_MAX_NAME) return false;

    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') continue;
        if (c >= '0' && c <= '9') continue;
        if (c == '-' || c == '_') continue;
        return false;
    }
    return true;
}

/* --------------------------------------------------------------------------
 * Platform Sub-Table Parsing
 * -------------------------------------------------------------------------- */

/**
 * Parse platform sub-tables from an entry.
 * Looks for keys under "platforms" table within the entry's table.
 * Each platform key is a triple (e.g. "windows-x86_64") containing url and checksum.
 *
 * Returns the number of platforms parsed (0 if none or error).
 */
static int catalog_parse_platforms(const TomlTable* platforms_table,
                                   CatalogPlatformEntry* out_platforms,
                                   int max_platforms,
                                   const char* filepath,
                                   int entry_index)
{
    if (!platforms_table) return 0;

    int count = 0;
    TomlEntry* entry = platforms_table->head;

    while (entry && count < max_platforms) {
        /* Each key in platforms table is a triple like "windows-x86_64" */
        if (!entry->key || !entry->value) {
            entry = entry->next;
            continue;
        }

        if (entry->value->type != TOML_TABLE &&
            entry->value->type != TOML_INLINE_TABLE) {
            entry = entry->next;
            continue;
        }

        TomlTable* plat_table = entry->value->as.table;
        CatalogPlatformEntry* pe = &out_platforms[count];
        memset(pe, 0, sizeof(*pe));

        /* Copy triple */
        snprintf(pe->triple, sizeof(pe->triple), "%s", entry->key);

        /* url (required) */
        const TomlValue* url_val = toml_get(plat_table, "url");
        if (!url_val || url_val->type != TOML_STRING) {
            cdo_warn("%s: entry [%d] platform '%s' missing required 'url', skipping platform",
                     filepath, entry_index, entry->key);
            entry = entry->next;
            continue;
        }
        snprintf(pe->url, sizeof(pe->url), "%s", url_val->as.string);

        /* checksum (optional) */
        const TomlValue* checksum_val = toml_get(plat_table, "checksum");
        if (checksum_val && checksum_val->type == TOML_STRING) {
            snprintf(pe->checksum, sizeof(pe->checksum), "%s", checksum_val->as.string);
        }

        count++;
        entry = entry->next;
    }

    return count;
}

/* --------------------------------------------------------------------------
 * String Array Parsing Helper
 * -------------------------------------------------------------------------- */

/**
 * Parse a TOML array of strings into a char* array.
 * Allocates copies of each string. Caller must free them.
 * Returns the number of items parsed (capped at max_items).
 */
static int catalog_parse_string_array(const TomlValue* array_val,
                                      char** out_arr,
                                      int max_items)
{
    if (!array_val || array_val->type != TOML_ARRAY) return 0;

    TomlArray* arr = array_val->as.array;
    if (!arr) return 0;

    int count = 0;
    for (int i = 0; i < arr->count && count < max_items; i++) {
        TomlValue* item = arr->items[i];
        if (!item || item->type != TOML_STRING) continue;
        if (!item->as.string) continue;

        out_arr[count] = _strdup(item->as.string);
        if (!out_arr[count]) break; /* allocation failure */
        count++;
    }
    return count;
}

/* --------------------------------------------------------------------------
 * Tool Entry Parsing
 * -------------------------------------------------------------------------- */

/**
 * Parse [[tool]] entries from a TOML root table and append to catalog.
 */
static void catalog_parse_tools(Catalog* cat, const TomlTable* root,
                                const char* filepath, int precedence)
{
    const TomlValue* tool_array_val = toml_get(root, "tool");
    if (!tool_array_val) return;
    if (tool_array_val->type != TOML_ARRAY) return;

    TomlArray* tool_array = tool_array_val->as.array;
    if (!tool_array) return;

    for (int i = 0; i < tool_array->count; i++) {
        TomlValue* entry_val = tool_array->items[i];
        if (!entry_val || (entry_val->type != TOML_TABLE &&
                           entry_val->type != TOML_INLINE_TABLE)) {
            continue;
        }

        TomlTable* entry_table = entry_val->as.table;

        /* name (required) */
        const TomlValue* name_val = toml_get(entry_table, "name");
        if (!name_val || name_val->type != TOML_STRING) {
            cdo_warn("%s: tool entry [%d] missing required field 'name', skipping",
                     filepath, i);
            continue;
        }
        const char* name = name_val->as.string;
        if (!catalog_validate_name(name)) {
            cdo_warn("%s: tool entry [%d] has invalid name '%s' "
                     "(must be 1-128 chars, lowercase alphanumeric/hyphen/underscore), skipping",
                     filepath, i, name ? name : "");
            continue;
        }

        /* version (required) */
        const TomlValue* version_val = toml_get(entry_table, "version");
        if (!version_val || version_val->type != TOML_STRING) {
            cdo_warn("%s: tool entry [%d] missing required field 'version', skipping",
                     filepath, i);
            continue;
        }
        const char* version_str = version_val->as.string;
        Semver sv;
        if (semver_parse(version_str, &sv) != 0) {
            cdo_warn("%s: tool entry [%d] has invalid version '%s' "
                     "(must be valid semver), skipping",
                     filepath, i, version_str);
            continue;
        }

        /* Ensure capacity */
        if (cat->tool_count >= cat->tool_capacity) {
            if (catalog_grow_tools(cat) != 0) {
                cdo_error("out of memory growing tool catalog");
                return;
            }
        }

        CatalogToolEntry* tool = &cat->tools[cat->tool_count];
        memset(tool, 0, sizeof(*tool));

        snprintf(tool->name, sizeof(tool->name), "%s", name);
        snprintf(tool->version, sizeof(tool->version), "%s", version_str);

        /* description (optional) */
        const TomlValue* desc_val = toml_get(entry_table, "description");
        if (desc_val && desc_val->type == TOML_STRING && desc_val->as.string) {
            snprintf(tool->description, sizeof(tool->description), "%s",
                     desc_val->as.string);
        }

        /* platforms sub-table */
        const TomlValue* plat_val = toml_get(entry_table, "platforms");
        if (plat_val && (plat_val->type == TOML_TABLE ||
                         plat_val->type == TOML_INLINE_TABLE)) {
            tool->platform_count = catalog_parse_platforms(
                plat_val->as.table, tool->platforms, CATALOG_MAX_PLATFORMS,
                filepath, i);
        }

        /* Set precedence and source file for deduplication */
        tool->_precedence = precedence;
        snprintf(tool->_source_file, sizeof(tool->_source_file), "%s", filepath);

        cat->tool_count++;
    }
}

/* --------------------------------------------------------------------------
 * Package Entry Parsing
 * -------------------------------------------------------------------------- */

/**
 * Parse [[package]] entries from a TOML root table and append to catalog.
 */
static void catalog_parse_packages(Catalog* cat, const TomlTable* root,
                                   const char* filepath, int precedence)
{
    const TomlValue* pkg_array_val = toml_get(root, "package");
    if (!pkg_array_val) return;
    if (pkg_array_val->type != TOML_ARRAY) return;

    TomlArray* pkg_array = pkg_array_val->as.array;
    if (!pkg_array) return;

    for (int i = 0; i < pkg_array->count; i++) {
        TomlValue* entry_val = pkg_array->items[i];
        if (!entry_val || (entry_val->type != TOML_TABLE &&
                           entry_val->type != TOML_INLINE_TABLE)) {
            continue;
        }

        TomlTable* entry_table = entry_val->as.table;

        /* name (required) */
        const TomlValue* name_val = toml_get(entry_table, "name");
        if (!name_val || name_val->type != TOML_STRING) {
            cdo_warn("%s: package entry [%d] missing required field 'name', skipping",
                     filepath, i);
            continue;
        }
        const char* name = name_val->as.string;
        if (!catalog_validate_name(name)) {
            cdo_warn("%s: package entry [%d] has invalid name '%s' "
                     "(must be 1-128 chars, lowercase alphanumeric/hyphen/underscore), skipping",
                     filepath, i, name ? name : "");
            continue;
        }

        /* version (required) */
        const TomlValue* version_val = toml_get(entry_table, "version");
        if (!version_val || version_val->type != TOML_STRING) {
            cdo_warn("%s: package entry [%d] missing required field 'version', skipping",
                     filepath, i);
            continue;
        }
        const char* version_str = version_val->as.string;
        Semver sv;
        if (semver_parse(version_str, &sv) != 0) {
            cdo_warn("%s: package entry [%d] has invalid version '%s' "
                     "(must be valid semver), skipping",
                     filepath, i, version_str);
            continue;
        }

        /* Ensure capacity */
        if (cat->package_count >= cat->package_capacity) {
            if (catalog_grow_packages(cat) != 0) {
                cdo_error("out of memory growing package catalog");
                return;
            }
        }

        CatalogPackageEntry* pkg = &cat->packages[cat->package_count];
        memset(pkg, 0, sizeof(*pkg));

        snprintf(pkg->name, sizeof(pkg->name), "%s", name);
        snprintf(pkg->version, sizeof(pkg->version), "%s", version_str);

        /* description (optional) */
        const TomlValue* desc_val = toml_get(entry_table, "description");
        if (desc_val && desc_val->type == TOML_STRING && desc_val->as.string) {
            snprintf(pkg->description, sizeof(pkg->description), "%s",
                     desc_val->as.string);
        }

        /* include_dirs (optional array of strings) */
        const TomlValue* inc_val = toml_get(entry_table, "include_dirs");
        pkg->include_dir_count = catalog_parse_string_array(
            inc_val, pkg->include_dirs, CATALOG_MAX_ARRAY_ITEMS);

        /* link_libs (optional array of strings) */
        const TomlValue* lib_val = toml_get(entry_table, "link_libs");
        pkg->link_lib_count = catalog_parse_string_array(
            lib_val, pkg->link_libs, CATALOG_MAX_ARRAY_ITEMS);

        /* defines (optional array of strings) */
        const TomlValue* def_val = toml_get(entry_table, "defines");
        pkg->define_count = catalog_parse_string_array(
            def_val, pkg->defines, CATALOG_MAX_ARRAY_ITEMS);

        /* platforms sub-table */
        const TomlValue* plat_val = toml_get(entry_table, "platforms");
        if (plat_val && (plat_val->type == TOML_TABLE ||
                         plat_val->type == TOML_INLINE_TABLE)) {
            pkg->platform_count = catalog_parse_platforms(
                plat_val->as.table, pkg->platforms, CATALOG_MAX_PLATFORMS,
                filepath, i);
        }

        /* Set precedence and source file for deduplication */
        pkg->_precedence = precedence;
        snprintf(pkg->_source_file, sizeof(pkg->_source_file), "%s", filepath);

        cat->package_count++;
    }
}

/* --------------------------------------------------------------------------
 * Catalog File Parsing (single file)
 * -------------------------------------------------------------------------- */

/**
 * Parse a single catalog TOML file's content and append entries to the catalog.
 * On TOML parse failure: reports error with file path, line, description; skips file.
 * On missing required fields: reports warning; skips entry.
 */
int catalog_parse_file(Catalog* cat, const char* filepath,
                       const char* content, size_t content_len,
                       int precedence)
{
    if (!cat || !filepath || !content) return 1;

    TomlTable* root = NULL;
    TomlError err;
    memset(&err, 0, sizeof(err));

    int rc = toml_parse(content, content_len, &root, &err);
    if (rc != 0 || !root) {
        cdo_error("%s:%d: TOML parse error: %s", filepath, err.line, err.message);
        if (root) toml_free(root);
        return 1;
    }

    /* Parse [[tool]] entries */
    catalog_parse_tools(cat, root, filepath, precedence);

    /* Parse [[package]] entries */
    catalog_parse_packages(cat, root, filepath, precedence);

    toml_free(root);
    return 0;
}

/* --------------------------------------------------------------------------
 * Deduplication
 * -------------------------------------------------------------------------- */

/**
 * Case-insensitive string comparison helper.
 */
static int catalog_stricmp(const char* a, const char* b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/**
 * Free heap-allocated strings in a package entry (include_dirs, link_libs, defines).
 */
static void catalog_free_package_strings(CatalogPackageEntry* pkg)
{
    for (int j = 0; j < pkg->include_dir_count; j++) {
        free(pkg->include_dirs[j]);
        pkg->include_dirs[j] = NULL;
    }
    for (int j = 0; j < pkg->link_lib_count; j++) {
        free(pkg->link_libs[j]);
        pkg->link_libs[j] = NULL;
    }
    for (int j = 0; j < pkg->define_count; j++) {
        free(pkg->defines[j]);
        pkg->defines[j] = NULL;
    }
}

/**
 * Determine which of two entries should be kept when they share the same
 * name+version (case-insensitive name, exact version match).
 *
 * Rules:
 *   1. Different precedence levels: keep higher precedence (lower _precedence value)
 *   2. Same precedence, different files: keep the one from the lexicographically-last file
 *   3. Same precedence, same file: keep the last occurrence (later array index) and emit warning
 *
 * Returns: index of the entry to REMOVE (the loser).
 * Sets *emit_warning to true if both entries are from the same file (intra-file duplicate).
 */
static int catalog_pick_loser(int idx_a, int prec_a, const char* file_a,
                              int idx_b, int prec_b, const char* file_b,
                              bool* emit_warning)
{
    *emit_warning = false;

    /* Rule 1: different precedence — lower value wins (higher priority) */
    if (prec_a != prec_b) {
        return (prec_a < prec_b) ? idx_b : idx_a;
    }

    /* Same precedence level */
    int file_cmp = strcmp(file_a, file_b);

    if (file_cmp != 0) {
        /* Rule 2: different files at same level — lex-last file wins.
         * If file_a > file_b lexicographically, file_a wins => remove idx_b. */
        return (file_cmp > 0) ? idx_b : idx_a;
    }

    /* Rule 3: same file — last occurrence (higher index) wins, emit warning */
    *emit_warning = true;
    /* Higher index wins => lower index is the loser */
    return (idx_a < idx_b) ? idx_a : idx_b;
}

/**
 * Deduplicate tool entries in the catalog.
 * For entries with the same name (case-insensitive) and version (exact match):
 *   - Keep according to precedence/file rules
 *   - Emit warning for intra-file duplicates
 * Entries to remove are compacted out of the array.
 */
static void catalog_deduplicate_tools(Catalog* cat)
{
    if (cat->tool_count <= 1) return;

    /* Mark entries for removal. true = remove this entry. */
    bool* remove = (bool*)calloc((size_t)cat->tool_count, sizeof(bool));
    if (!remove) return;

    for (int i = 0; i < cat->tool_count; i++) {
        if (remove[i]) continue;
        for (int j = i + 1; j < cat->tool_count; j++) {
            if (remove[j]) continue;

            /* Check if same name (case-insensitive) and same version (exact) */
            if (catalog_stricmp(cat->tools[i].name, cat->tools[j].name) != 0) continue;
            if (strcmp(cat->tools[i].version, cat->tools[j].version) != 0) continue;

            /* Duplicate found — determine which to remove */
            bool warn = false;
            int loser = catalog_pick_loser(
                i, cat->tools[i]._precedence, cat->tools[i]._source_file,
                j, cat->tools[j]._precedence, cat->tools[j]._source_file,
                &warn);

            if (warn) {
                int winner = (loser == i) ? j : i;
                cdo_warn("%s: duplicate tool entry '%s' version '%s' "
                         "(keeping last occurrence)",
                         cat->tools[winner]._source_file,
                         cat->tools[winner].name,
                         cat->tools[winner].version);
            }

            remove[loser] = true;
        }
    }

    /* Compact: remove marked entries */
    int write_idx = 0;
    for (int read_idx = 0; read_idx < cat->tool_count; read_idx++) {
        if (!remove[read_idx]) {
            if (write_idx != read_idx) {
                cat->tools[write_idx] = cat->tools[read_idx];
            }
            write_idx++;
        }
    }
    cat->tool_count = write_idx;

    free(remove);
}

/**
 * Deduplicate package entries in the catalog.
 * Same rules as tools: precedence > lex-last file > last occurrence + warning.
 */
static void catalog_deduplicate_packages(Catalog* cat)
{
    if (cat->package_count <= 1) return;

    bool* remove = (bool*)calloc((size_t)cat->package_count, sizeof(bool));
    if (!remove) return;

    for (int i = 0; i < cat->package_count; i++) {
        if (remove[i]) continue;
        for (int j = i + 1; j < cat->package_count; j++) {
            if (remove[j]) continue;

            if (catalog_stricmp(cat->packages[i].name, cat->packages[j].name) != 0) continue;
            if (strcmp(cat->packages[i].version, cat->packages[j].version) != 0) continue;

            bool warn = false;
            int loser = catalog_pick_loser(
                i, cat->packages[i]._precedence, cat->packages[i]._source_file,
                j, cat->packages[j]._precedence, cat->packages[j]._source_file,
                &warn);

            if (warn) {
                int winner = (loser == i) ? j : i;
                cdo_warn("%s: duplicate package entry '%s' version '%s' "
                         "(keeping last occurrence)",
                         cat->packages[winner]._source_file,
                         cat->packages[winner].name,
                         cat->packages[winner].version);
            }

            /* Free heap strings of the losing package entry before removing */
            catalog_free_package_strings(&cat->packages[loser]);

            remove[loser] = true;
        }
    }

    /* Compact: remove marked entries */
    int write_idx = 0;
    for (int read_idx = 0; read_idx < cat->package_count; read_idx++) {
        if (!remove[read_idx]) {
            if (write_idx != read_idx) {
                cat->packages[write_idx] = cat->packages[read_idx];
            }
            write_idx++;
        }
    }
    cat->package_count = write_idx;

    free(remove);
}

/**
 * Deduplicate all entries in the catalog after loading from all sources.
 * Applies precedence and file-ordering rules per Requirements 2.3 and 8.5.
 */
static void catalog_deduplicate(Catalog* cat)
{
    catalog_deduplicate_tools(cat);
    catalog_deduplicate_packages(cat);
}

/* --------------------------------------------------------------------------
 * Catalog Loading — Multi-Location Discovery
 * -------------------------------------------------------------------------- */

/** Maximum number of .toml files we'll collect from a single directory. */
#define CATALOG_MAX_DIR_FILES 256

/** Context for the directory walk callback that collects .toml file paths. */
typedef struct {
    char** paths;
    int    count;
    int    capacity;
} CatalogDirCtx;

/**
 * Callback for pal_dir_walk: collects full paths of .toml files.
 * Skips directories and non-.toml files.
 */
static void catalog_collect_toml_cb(const char* path, bool is_dir, void* ctx)
{
    if (is_dir) return;

    CatalogDirCtx* dc = (CatalogDirCtx*)ctx;
    if (dc->count >= CATALOG_MAX_DIR_FILES) return;

    /* Check for .toml extension */
    const char* ext = pal_path_ext(path);
    if (!ext || strcmp(ext, ".toml") != 0) return;

    /* Grow array if needed */
    if (dc->count >= dc->capacity) {
        int new_cap = dc->capacity == 0 ? 16 : dc->capacity * 2;
        if (new_cap > CATALOG_MAX_DIR_FILES) new_cap = CATALOG_MAX_DIR_FILES;
        char** new_arr = (char**)realloc(dc->paths, sizeof(char*) * (size_t)new_cap);
        if (!new_arr) return;
        dc->paths = new_arr;
        dc->capacity = new_cap;
    }

    dc->paths[dc->count] = _strdup(path);
    if (dc->paths[dc->count]) {
        dc->count++;
    }
}

/**
 * Comparison function for qsort: sort filenames lexicographically.
 * Compares only the filename portion (after last path separator).
 */
static int catalog_path_cmp(const void* a, const void* b)
{
    const char* pa = *(const char**)a;
    const char* pb = *(const char**)b;

    /* Extract filename from full path for comparison */
    const char* fa = pa;
    const char* fb = pb;
    const char* sep;

    sep = strrchr(pa, '/');
    if (sep) fa = sep + 1;
#ifdef _WIN32
    sep = strrchr(pa, '\\');
    if (sep && (sep + 1) > fa) fa = sep + 1;
#endif

    sep = strrchr(pb, '/');
    if (sep) fb = sep + 1;
#ifdef _WIN32
    sep = strrchr(pb, '\\');
    if (sep && (sep + 1) > fb) fb = sep + 1;
#endif

    return strcmp(fa, fb);
}

/**
 * Load all .toml files from a single directory into the catalog.
 * Files are sorted lexicographically by filename before parsing.
 * Returns the number of files successfully loaded.
 */
static int catalog_load_directory(Catalog* cat, const char* dir_path, int precedence)
{
    CatalogDirCtx dc;
    memset(&dc, 0, sizeof(dc));

    int rc = pal_dir_walk(dir_path, catalog_collect_toml_cb, &dc);
    if (rc != 0) {
        /* If the walk itself fails, just skip — we already checked existence */
        for (int i = 0; i < dc.count; i++) free(dc.paths[i]);
        free(dc.paths);
        return 0;
    }

    if (dc.count == 0) {
        free(dc.paths);
        return 0;
    }

    /* Sort collected paths lexicographically by filename */
    qsort(dc.paths, (size_t)dc.count, sizeof(char*), catalog_path_cmp);

    int files_loaded = 0;

    for (int i = 0; i < dc.count; i++) {
        char* file_buf = NULL;
        size_t file_len = 0;

        int read_rc = pal_file_read(dc.paths[i], &file_buf, &file_len);
        if (read_rc != 0) {
            cdo_warn("catalog: failed to read '%s', skipping", dc.paths[i]);
            free(dc.paths[i]);
            continue;
        }

        catalog_parse_file(cat, dc.paths[i], file_buf, file_len, precedence);
        files_loaded++;

        free(file_buf);
        free(dc.paths[i]);
    }

    free(dc.paths);
    return files_loaded;
}

/**
 * Load all catalog files from the three search locations.
 * Applies precedence: workspace > user-global > built-in.
 * Returns 0 on success (even if no catalogs found — emits warning).
 */
int catalog_load(Catalog* out, const char* workspace_root)
{
    if (!out) return 1;

    /* Zero out the catalog struct */
    memset(out, 0, sizeof(Catalog));

    int total_files = 0;
    char path_buf[1024];

    /* --- 1. Workspace catalogs: {workspace_root}/.cdo/catalogs/ --- */
    if (workspace_root && workspace_root[0] != '\0') {
        if (pal_path_join(path_buf, sizeof(path_buf), workspace_root, ".cdo/catalogs") == 0) {
            if (pal_path_exists(path_buf) == 0) {
                total_files += catalog_load_directory(out, path_buf, 0);
            }
        }
    }

    /* --- 2. User-global catalogs: ~/.cdo/catalogs/ --- */
    char home_dir[512];
    if (pal_get_home_dir(home_dir, sizeof(home_dir)) == 0) {
        if (pal_path_join(path_buf, sizeof(path_buf), home_dir, ".cdo/catalogs") == 0) {
            if (pal_path_exists(path_buf) == 0) {
                total_files += catalog_load_directory(out, path_buf, 1);
            }
        }
    }

    /* --- 3. Built-in catalogs: catalogs/ relative to CWD --- */
    /* The built-in catalogs are deployed alongside the binary.
     * We look for a "catalogs/" directory relative to the current working
     * directory, which is the standard deployment layout. */
    if (pal_path_exists("catalogs") == 0) {
        total_files += catalog_load_directory(out, "catalogs", 2);
    }

    /* If no catalog files found anywhere, emit warning */
    if (total_files == 0) {
        cdo_warn("no catalog files found; use --url to specify download URLs manually");
    }

    /* Deduplicate entries across all loaded files */
    catalog_deduplicate(out);

    return 0;
}

/* --------------------------------------------------------------------------
 * Tool Resolution
 * -------------------------------------------------------------------------- */

int catalog_resolve_tool(const Catalog* cat, const char* name,
                         const char* version_constraint,
                         const CatalogPlatform* platform,
                         CatalogResolveResult* out)
{
    if (!cat || !name || !platform || !out) return 1;

    memset(out, 0, sizeof(*out));

    /* Parse version constraint if provided */
    SemverConstraint constraint;
    bool has_constraint = false;

    if (version_constraint && version_constraint[0] != '\0') {
        if (semver_constraint_parse(version_constraint, &constraint) != 0) {
            cdo_error("invalid version constraint '%s'", version_constraint);
            cdo_info("  supported formats: 1.2.3, ^1.2.3, ~1.2.3, >=1.2.3, <2.0.0, *");
            return 1;
        }
        has_constraint = true;
    }

    /* Find the best matching entry:
     * - Case-insensitive name match
     * - Version satisfies constraint (or any version if no constraint)
     * - Highest version among matches
     */
    int best_idx = -1;
    Semver best_version;
    memset(&best_version, 0, sizeof(best_version));

    for (int i = 0; i < cat->tool_count; i++) {
        /* Case-insensitive name comparison */
        if (catalog_stricmp(cat->tools[i].name, name) != 0) {
            continue;
        }

        /* Parse this entry's version */
        Semver entry_ver;
        if (semver_parse(cat->tools[i].version, &entry_ver) != 0) {
            continue; /* skip entries with unparseable versions */
        }

        /* Check version constraint if provided */
        if (has_constraint) {
            if (!semver_satisfies(&entry_ver, &constraint)) {
                continue;
            }
        }

        /* Check if this is a better (higher) version than current best */
        if (best_idx < 0 || semver_compare(&entry_ver, &best_version) > 0) {
            best_idx = i;
            best_version = entry_ver;
        }
    }

    /* Error: no name match at all */
    if (best_idx < 0) {
        /* Distinguish between "name not found" and "no version satisfies" */
        bool name_found = false;
        for (int i = 0; i < cat->tool_count; i++) {
            if (catalog_stricmp(cat->tools[i].name, name) == 0) {
                name_found = true;
                break;
            }
        }

        if (!name_found) {
            cdo_error("tool '%s' not found in any loaded catalog", name);
            cdo_info("  hint: use --url to specify a download URL manually");
        } else {
            cdo_error("no version of tool '%s' satisfies constraint '%s'",
                      name, version_constraint ? version_constraint : "");
        }
        return 1;
    }

    /* Find platform entry for the selected tool */
    const CatalogToolEntry* tool = &cat->tools[best_idx];
    int plat_idx = -1;

    for (int i = 0; i < tool->platform_count; i++) {
        if (strcmp(tool->platforms[i].triple, platform->triple) == 0) {
            plat_idx = i;
            break;
        }
    }

    if (plat_idx < 0) {
        cdo_error("tool '%s' is not available for platform '%s'",
                  name, platform->triple);
        cdo_info("  available platforms:");
        for (int i = 0; i < tool->platform_count; i++) {
            cdo_info("    - %s", tool->platforms[i].triple);
        }
        return 1;
    }

    /* Populate output */
    snprintf(out->url, sizeof(out->url), "%s", tool->platforms[plat_idx].url);
    snprintf(out->checksum, sizeof(out->checksum), "%s", tool->platforms[plat_idx].checksum);
    snprintf(out->version, sizeof(out->version), "%s", tool->version);

    return 0;
}

/* --------------------------------------------------------------------------
 * Package Resolution
 * -------------------------------------------------------------------------- */

/**
 * Case-insensitive substring check.
 * Returns true if `needle` is found as a substring of `haystack` (case-insensitive).
 */
static bool catalog_stristr(const char* haystack, const char* needle)
{
    if (!haystack || !needle) return false;
    if (needle[0] == '\0') return true;

    size_t hay_len = strlen(haystack);
    size_t ndl_len = strlen(needle);
    if (ndl_len > hay_len) return false;

    for (size_t i = 0; i <= hay_len - ndl_len; i++) {
        bool match = true;
        for (size_t j = 0; j < ndl_len; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

int catalog_resolve_package(const Catalog* cat, const char* name,
                            const char* version_constraint,
                            const CatalogPlatform* platform,
                            CatalogResolveResult* out)
{
    if (!cat || !name || !platform || !out) return 1;

    memset(out, 0, sizeof(*out));

    /* Parse version constraint if provided */
    SemverConstraint constraint;
    bool has_constraint = false;

    if (version_constraint && version_constraint[0] != '\0') {
        if (semver_constraint_parse(version_constraint, &constraint) != 0) {
            cdo_error("invalid version constraint '%s'", version_constraint);
            cdo_info("  supported formats: 1.2.3, ^1.2.3, ~1.2.3, >=1.2.3, <2.0.0, *");
            return 1;
        }
        has_constraint = true;
    }

    /* Find the best matching entry:
     * - Case-insensitive name match
     * - Version satisfies constraint (or any version if no constraint)
     * - Highest version among matches
     */
    int best_idx = -1;
    Semver best_version;
    memset(&best_version, 0, sizeof(best_version));

    for (int i = 0; i < cat->package_count; i++) {
        /* Case-insensitive name comparison */
        if (catalog_stricmp(cat->packages[i].name, name) != 0) {
            continue;
        }

        /* Parse this entry's version */
        Semver entry_ver;
        if (semver_parse(cat->packages[i].version, &entry_ver) != 0) {
            continue; /* skip entries with unparseable versions */
        }

        /* Check version constraint if provided */
        if (has_constraint) {
            if (!semver_satisfies(&entry_ver, &constraint)) {
                continue;
            }
        }

        /* Check if this is a better (higher) version than current best */
        if (best_idx < 0 || semver_compare(&entry_ver, &best_version) > 0) {
            best_idx = i;
            best_version = entry_ver;
        }
    }

    /* Error: no match found */
    if (best_idx < 0) {
        /* Distinguish between "name not found" and "no version satisfies" */
        bool name_found = false;
        for (int i = 0; i < cat->package_count; i++) {
            if (catalog_stricmp(cat->packages[i].name, name) == 0) {
                name_found = true;
                break;
            }
        }

        if (!name_found) {
            cdo_error("package '%s' not found in any loaded catalog", name);

            /* Suggest up to 5 packages whose name contains the query as substring */
            const char* suggestions[5];
            int suggestion_count = 0;

            for (int i = 0; i < cat->package_count && suggestion_count < 5; i++) {
                if (!catalog_stristr(cat->packages[i].name, name)) {
                    continue;
                }

                /* Check for duplicate names already in suggestions */
                bool dup = false;
                for (int s = 0; s < suggestion_count; s++) {
                    if (catalog_stricmp(suggestions[s], cat->packages[i].name) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    suggestions[suggestion_count++] = cat->packages[i].name;
                }
            }

            if (suggestion_count > 0) {
                cdo_info("  did you mean:");
                for (int s = 0; s < suggestion_count; s++) {
                    cdo_info("    - %s", suggestions[s]);
                }
            }
        } else {
            cdo_error("no version of package '%s' satisfies constraint '%s'",
                      name, version_constraint ? version_constraint : "");
        }
        return 1;
    }

    /* Find platform entry for the selected package */
    const CatalogPackageEntry* pkg = &cat->packages[best_idx];
    int plat_idx = -1;

    for (int i = 0; i < pkg->platform_count; i++) {
        if (strcmp(pkg->platforms[i].triple, platform->triple) == 0) {
            plat_idx = i;
            break;
        }
    }

    if (plat_idx < 0) {
        cdo_error("package '%s' is not available for platform '%s'",
                  name, platform->triple);
        cdo_info("  available platforms:");
        for (int i = 0; i < pkg->platform_count; i++) {
            cdo_info("    - %s", pkg->platforms[i].triple);
        }
        return 1;
    }

    /* Populate output — URL, checksum, version */
    snprintf(out->url, sizeof(out->url), "%s", pkg->platforms[plat_idx].url);
    snprintf(out->checksum, sizeof(out->checksum), "%s", pkg->platforms[plat_idx].checksum);
    snprintf(out->version, sizeof(out->version), "%s", pkg->version);

    /* Populate package-specific metadata: include_dirs, link_libs, defines */
    out->include_dir_count = 0;
    for (int i = 0; i < pkg->include_dir_count; i++) {
        if (pkg->include_dirs[i]) {
            out->include_dirs[out->include_dir_count] = _strdup(pkg->include_dirs[i]);
            if (out->include_dirs[out->include_dir_count]) {
                out->include_dir_count++;
            }
        }
    }

    out->link_lib_count = 0;
    for (int i = 0; i < pkg->link_lib_count; i++) {
        if (pkg->link_libs[i]) {
            out->link_libs[out->link_lib_count] = _strdup(pkg->link_libs[i]);
            if (out->link_libs[out->link_lib_count]) {
                out->link_lib_count++;
            }
        }
    }

    out->define_count = 0;
    for (int i = 0; i < pkg->define_count; i++) {
        if (pkg->defines[i]) {
            out->defines[out->define_count] = _strdup(pkg->defines[i]);
            if (out->defines[out->define_count]) {
                out->define_count++;
            }
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * Resolution Result Free
 * -------------------------------------------------------------------------- */

void catalog_resolve_result_free(CatalogResolveResult* result)
{
    if (!result) return;

    for (int i = 0; i < result->include_dir_count; i++) {
        free(result->include_dirs[i]);
        result->include_dirs[i] = NULL;
    }
    result->include_dir_count = 0;

    for (int i = 0; i < result->link_lib_count; i++) {
        free(result->link_libs[i]);
        result->link_libs[i] = NULL;
    }
    result->link_lib_count = 0;

    for (int i = 0; i < result->define_count; i++) {
        free(result->defines[i]);
        result->defines[i] = NULL;
    }
    result->define_count = 0;
}

/* --------------------------------------------------------------------------
 * Catalog Search
 * -------------------------------------------------------------------------- */

/**
 * Search catalog entries by query (case-insensitive substring on name/description).
 * Writes matching indices into output arrays and returns 0 on success.
 *
 * Parameters:
 *   cat              - loaded catalog to search
 *   query            - search string (case-insensitive substring match)
 *   tools_only       - if true, search only tool entries (skip packages)
 *   packages_only    - if true, search only package entries (skip tools)
 *   out_tool_indices - caller-allocated array to receive matching tool indices
 *   tool_match_count - output: number of matching tools found
 *   out_pkg_indices  - caller-allocated array to receive matching package indices
 *   pkg_match_count  - output: number of matching packages found
 *
 * Returns 0 on success (including zero matches). If query is NULL or empty,
 * returns 0 with zero match counts.
 */
int catalog_search(const Catalog* cat, const char* query,
                   bool tools_only, bool packages_only,
                   int* out_tool_indices, int* tool_match_count,
                   int* out_pkg_indices, int* pkg_match_count)
{
    if (!cat || !tool_match_count || !pkg_match_count) return 1;

    *tool_match_count = 0;
    *pkg_match_count = 0;

    /* Empty or NULL query: no matches */
    if (!query || query[0] == '\0') return 0;

    /* Search tool entries (unless packages_only is set) */
    if (!packages_only) {
        for (int i = 0; i < cat->tool_count; i++) {
            if (catalog_stristr(cat->tools[i].name, query) ||
                catalog_stristr(cat->tools[i].description, query)) {
                if (out_tool_indices) {
                    out_tool_indices[*tool_match_count] = i;
                }
                (*tool_match_count)++;
            }
        }
    }

    /* Search package entries (unless tools_only is set) */
    if (!tools_only) {
        for (int i = 0; i < cat->package_count; i++) {
            if (catalog_stristr(cat->packages[i].name, query) ||
                catalog_stristr(cat->packages[i].description, query)) {
                if (out_pkg_indices) {
                    out_pkg_indices[*pkg_match_count] = i;
                }
                (*pkg_match_count)++;
            }
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * Catalog Free
 * -------------------------------------------------------------------------- */

void catalog_free(Catalog* cat)
{
    if (!cat) return;

    /* Free heap-allocated string arrays in package entries */
    for (int i = 0; i < cat->package_count; i++) {
        CatalogPackageEntry* pkg = &cat->packages[i];
        for (int j = 0; j < pkg->include_dir_count; j++) {
            free(pkg->include_dirs[j]);
        }
        for (int j = 0; j < pkg->link_lib_count; j++) {
            free(pkg->link_libs[j]);
        }
        for (int j = 0; j < pkg->define_count; j++) {
            free(pkg->defines[j]);
        }
    }

    free(cat->tools);
    free(cat->packages);

    memset(cat, 0, sizeof(*cat));
}
