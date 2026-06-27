/*
 * catalog_load.c - Catalog loading, parsing, deduplication, and free.
 */
#include "core/catalog.h"
#include "core/output.h"
#include "commons/semver.h"
#include "commons/toml.h"
#include "pal/pal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Platform Detection --- */

int catalog_detect_platform(CatalogPlatform* out)
{
    const char* os = NULL;
    const char* arch = NULL;
    if (!out) return 1;
#if defined(_WIN32)
    os = "windows";
#elif defined(__linux__)
    os = "linux";
#elif defined(__APPLE__)
    os = "macos";
#else
    cdo_error("unsupported operating system - catalog resolution requires windows, linux, or macos");
    return 1;
#endif
#if defined(_M_X64) || defined(__x86_64__)
    arch = "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    arch = "arm64";
#else
    cdo_error("unsupported CPU architecture - catalog resolution requires x86_64 or arm64");
    return 1;
#endif
    snprintf(out->os, sizeof(out->os), "%s", os);
    snprintf(out->arch, sizeof(out->arch), "%s", arch);
    snprintf(out->triple, sizeof(out->triple), "%s-%s", os, arch);
    return 0;
}

/* --- Dynamic Array Helpers --- */

#define CATALOG_INITIAL_CAPACITY 16

static int catalog_grow_tools(Catalog* cat)
{
    int new_cap = cat->tool_capacity == 0 ? CATALOG_INITIAL_CAPACITY : cat->tool_capacity * 2;
    CatalogToolEntry* p = (CatalogToolEntry*)realloc(cat->tools, (size_t)new_cap * sizeof(*p));
    if (!p) return 1;
    cat->tools = p;
    cat->tool_capacity = new_cap;
    return 0;
}

static int catalog_grow_packages(Catalog* cat)
{
    int new_cap = cat->package_capacity == 0 ? CATALOG_INITIAL_CAPACITY : cat->package_capacity * 2;
    CatalogPackageEntry* p = (CatalogPackageEntry*)realloc(cat->packages, (size_t)new_cap * sizeof(*p));
    if (!p) return 1;
    cat->packages = p;
    cat->package_capacity = new_cap;
    return 0;
}
/* --- Validation & Parsing Helpers --- */

static bool catalog_validate_name(const char* name)
{
    if (!name) return false;
    size_t len = strlen(name);
    if (len == 0 || len > CATALOG_MAX_NAME) return false;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    }
    return true;
}

static int catalog_parse_platforms(const TomlTable* pt, CatalogPlatformEntry* out,
                                   int max, const char* fp, int idx)
{
    if (!pt) return 0;
    int count = 0;
    TomlEntry* e = pt->head;
    while (e && count < max) {
        if (!e->key || !e->value) { e = e->next; continue; }
        if (e->value->type != TOML_TABLE && e->value->type != TOML_INLINE_TABLE)
            { e = e->next; continue; }
        TomlTable* t = e->value->as.table;
        CatalogPlatformEntry* pe = &out[count];
        memset(pe, 0, sizeof(*pe));
        snprintf(pe->triple, sizeof(pe->triple), "%s", e->key);
        const TomlValue* u = toml_get(t, "url");
        if (!u || u->type != TOML_STRING) {
            cdo_warn("%s: entry [%d] platform '%s' missing 'url', skipping", fp, idx, e->key);
            e = e->next; continue;
        }
        snprintf(pe->url, sizeof(pe->url), "%s", u->as.string);
        const TomlValue* cs = toml_get(t, "checksum");
        if (cs && cs->type == TOML_STRING)
            snprintf(pe->checksum, sizeof(pe->checksum), "%s", cs->as.string);
        count++;
        e = e->next;
    }
    return count;
}

static int catalog_parse_string_array(const TomlValue* av, char** out, int max)
{
    if (!av || av->type != TOML_ARRAY) return 0;
    TomlArray* arr = av->as.array;
    if (!arr) return 0;
    int count = 0;
    for (int i = 0; i < arr->count && count < max; i++) {
        TomlValue* item = arr->items[i];
        if (!item || item->type != TOML_STRING || !item->as.string) continue;
        out[count] = _strdup(item->as.string);
        if (!out[count]) break;
        count++;
    }
    return count;
}
/* --- Tool & Package Parsing --- */

static void catalog_parse_tools(Catalog* cat, const TomlTable* root,
                                const char* fp, int prec)
{
    const TomlValue* av = toml_get(root, "tool");
    if (!av || av->type != TOML_ARRAY) return;
    TomlArray* arr = av->as.array;
    if (!arr) return;
    for (int i = 0; i < arr->count; i++) {
        TomlValue* ev = arr->items[i];
        if (!ev || (ev->type != TOML_TABLE && ev->type != TOML_INLINE_TABLE)) continue;
        TomlTable* et = ev->as.table;
        const TomlValue* nv = toml_get(et, "name");
        if (!nv || nv->type != TOML_STRING) {
            cdo_warn("%s: tool entry [%d] missing required field 'name', skipping", fp, i);
            continue;
        }
        const char* name = nv->as.string;
        if (!catalog_validate_name(name)) {
            cdo_warn("%s: tool entry [%d] has invalid name '%s', skipping", fp, i, name ? name : "");
            continue;
        }
        const TomlValue* vv = toml_get(et, "version");
        if (!vv || vv->type != TOML_STRING) {
            cdo_warn("%s: tool entry [%d] missing required field 'version', skipping", fp, i);
            continue;
        }
        Semver sv;
        if (semver_parse(vv->as.string, &sv) != 0) {
            cdo_warn("%s: tool entry [%d] has invalid version '%s', skipping", fp, i, vv->as.string);
            continue;
        }
        if (cat->tool_count >= cat->tool_capacity && catalog_grow_tools(cat) != 0) {
            cdo_error("out of memory growing tool catalog"); return;
        }
        CatalogToolEntry* tool = &cat->tools[cat->tool_count];
        memset(tool, 0, sizeof(*tool));
        snprintf(tool->name, sizeof(tool->name), "%s", name);
        snprintf(tool->version, sizeof(tool->version), "%s", vv->as.string);
        const TomlValue* dv = toml_get(et, "description");
        if (dv && dv->type == TOML_STRING && dv->as.string)
            snprintf(tool->description, sizeof(tool->description), "%s", dv->as.string);
        const TomlValue* pv = toml_get(et, "platforms");
        if (pv && (pv->type == TOML_TABLE || pv->type == TOML_INLINE_TABLE))
            tool->platform_count = catalog_parse_platforms(pv->as.table, tool->platforms,
                                                           CATALOG_MAX_PLATFORMS, fp, i);
        tool->_precedence = prec;
        snprintf(tool->_source_file, sizeof(tool->_source_file), "%s", fp);
        cat->tool_count++;
    }
}
static void catalog_parse_packages(Catalog* cat, const TomlTable* root,
                                   const char* fp, int prec)
{
    const TomlValue* av = toml_get(root, "package");
    if (!av || av->type != TOML_ARRAY) return;
    TomlArray* arr = av->as.array;
    if (!arr) return;
    for (int i = 0; i < arr->count; i++) {
        TomlValue* ev = arr->items[i];
        if (!ev || (ev->type != TOML_TABLE && ev->type != TOML_INLINE_TABLE)) continue;
        TomlTable* et = ev->as.table;
        const TomlValue* nv = toml_get(et, "name");
        if (!nv || nv->type != TOML_STRING) {
            cdo_warn("%s: package entry [%d] missing required field 'name', skipping", fp, i);
            continue;
        }
        const char* name = nv->as.string;
        if (!catalog_validate_name(name)) {
            cdo_warn("%s: package entry [%d] has invalid name '%s', skipping", fp, i, name ? name : "");
            continue;
        }
        const TomlValue* vv = toml_get(et, "version");
        if (!vv || vv->type != TOML_STRING) {
            cdo_warn("%s: package entry [%d] missing required field 'version', skipping", fp, i);
            continue;
        }
        Semver sv;
        if (semver_parse(vv->as.string, &sv) != 0) {
            cdo_warn("%s: package entry [%d] has invalid version '%s', skipping", fp, i, vv->as.string);
            continue;
        }
        if (cat->package_count >= cat->package_capacity && catalog_grow_packages(cat) != 0) {
            cdo_error("out of memory growing package catalog"); return;
        }
        CatalogPackageEntry* pkg = &cat->packages[cat->package_count];
        memset(pkg, 0, sizeof(*pkg));
        snprintf(pkg->name, sizeof(pkg->name), "%s", name);
        snprintf(pkg->version, sizeof(pkg->version), "%s", vv->as.string);
        const TomlValue* dv = toml_get(et, "description");
        if (dv && dv->type == TOML_STRING && dv->as.string)
            snprintf(pkg->description, sizeof(pkg->description), "%s", dv->as.string);
        pkg->include_dir_count = catalog_parse_string_array(toml_get(et, "include_dirs"),
            pkg->include_dirs, CATALOG_MAX_ARRAY_ITEMS);
        pkg->link_lib_count = catalog_parse_string_array(toml_get(et, "link_libs"),
            pkg->link_libs, CATALOG_MAX_ARRAY_ITEMS);
        pkg->define_count = catalog_parse_string_array(toml_get(et, "defines"),
            pkg->defines, CATALOG_MAX_ARRAY_ITEMS);
        const TomlValue* pv = toml_get(et, "platforms");
        if (pv && (pv->type == TOML_TABLE || pv->type == TOML_INLINE_TABLE))
            pkg->platform_count = catalog_parse_platforms(pv->as.table, pkg->platforms,
                                                          CATALOG_MAX_PLATFORMS, fp, i);
        pkg->_precedence = prec;
        snprintf(pkg->_source_file, sizeof(pkg->_source_file), "%s", fp);
        cat->package_count++;
    }
}
/* --- Catalog File Parsing --- */

int catalog_parse_file(Catalog* cat, const char* filepath,
                       const char* content, size_t content_len, int precedence)
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
    catalog_parse_tools(cat, root, filepath, precedence);
    catalog_parse_packages(cat, root, filepath, precedence);
    toml_free(root);
    return 0;
}

/* --- Deduplication --- */

static int catalog_stricmp(const char* a, const char* b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static void catalog_free_package_strings(CatalogPackageEntry* pkg)
{
    for (int j = 0; j < pkg->include_dir_count; j++) { free(pkg->include_dirs[j]); pkg->include_dirs[j] = NULL; }
    for (int j = 0; j < pkg->link_lib_count; j++) { free(pkg->link_libs[j]); pkg->link_libs[j] = NULL; }
    for (int j = 0; j < pkg->define_count; j++) { free(pkg->defines[j]); pkg->defines[j] = NULL; }
}

static int catalog_pick_loser(int ia, int pa, const char* fa,
                              int ib, int pb, const char* fb, bool* warn)
{
    *warn = false;
    if (pa != pb) return (pa < pb) ? ib : ia;
    int cmp = strcmp(fa, fb);
    if (cmp != 0) return (cmp > 0) ? ib : ia;
    *warn = true;
    return (ia < ib) ? ia : ib;
}

static void catalog_deduplicate_tools(Catalog* cat)
{
    if (cat->tool_count <= 1) return;
    bool* rm = (bool*)calloc((size_t)cat->tool_count, sizeof(bool));
    if (!rm) return;
    for (int i = 0; i < cat->tool_count; i++) {
        if (rm[i]) continue;
        for (int j = i + 1; j < cat->tool_count; j++) {
            if (rm[j]) continue;
            if (catalog_stricmp(cat->tools[i].name, cat->tools[j].name) != 0) continue;
            if (strcmp(cat->tools[i].version, cat->tools[j].version) != 0) continue;
            bool w = false;
            int loser = catalog_pick_loser(i, cat->tools[i]._precedence, cat->tools[i]._source_file,
                                           j, cat->tools[j]._precedence, cat->tools[j]._source_file, &w);
            if (w) { int winner = (loser == i) ? j : i;
                cdo_warn("%s: duplicate tool entry '%s' version '%s' (keeping last occurrence)",
                         cat->tools[winner]._source_file, cat->tools[winner].name, cat->tools[winner].version); }
            rm[loser] = true;
        }
    }
    int wi = 0;
    for (int ri = 0; ri < cat->tool_count; ri++) {
        if (!rm[ri]) { if (wi != ri) cat->tools[wi] = cat->tools[ri]; wi++; }
    }
    cat->tool_count = wi;
    free(rm);
}
static void catalog_deduplicate_packages(Catalog* cat)
{
    if (cat->package_count <= 1) return;
    bool* rm = (bool*)calloc((size_t)cat->package_count, sizeof(bool));
    if (!rm) return;
    for (int i = 0; i < cat->package_count; i++) {
        if (rm[i]) continue;
        for (int j = i + 1; j < cat->package_count; j++) {
            if (rm[j]) continue;
            if (catalog_stricmp(cat->packages[i].name, cat->packages[j].name) != 0) continue;
            if (strcmp(cat->packages[i].version, cat->packages[j].version) != 0) continue;
            bool w = false;
            int loser = catalog_pick_loser(i, cat->packages[i]._precedence, cat->packages[i]._source_file,
                                           j, cat->packages[j]._precedence, cat->packages[j]._source_file, &w);
            if (w) { int winner = (loser == i) ? j : i;
                cdo_warn("%s: duplicate package entry '%s' version '%s' (keeping last occurrence)",
                         cat->packages[winner]._source_file, cat->packages[winner].name, cat->packages[winner].version); }
            catalog_free_package_strings(&cat->packages[loser]);
            rm[loser] = true;
        }
    }
    int wi = 0;
    for (int ri = 0; ri < cat->package_count; ri++) {
        if (!rm[ri]) { if (wi != ri) cat->packages[wi] = cat->packages[ri]; wi++; }
    }
    cat->package_count = wi;
    free(rm);
}

static void catalog_deduplicate(Catalog* cat)
{
    catalog_deduplicate_tools(cat);
    catalog_deduplicate_packages(cat);
}

/* --- Directory Loading --- */

#define CATALOG_MAX_DIR_FILES 256

typedef struct { char** paths; int count; int capacity; } CatalogDirCtx;

static void catalog_collect_toml_cb(const char* path, bool is_dir, void* ctx)
{
    if (is_dir) return;
    CatalogDirCtx* dc = (CatalogDirCtx*)ctx;
    if (dc->count >= CATALOG_MAX_DIR_FILES) return;
    const char* ext = pal_path_ext(path);
    if (!ext || strcmp(ext, ".toml") != 0) return;
    if (dc->count >= dc->capacity) {
        int nc = dc->capacity == 0 ? 16 : dc->capacity * 2;
        if (nc > CATALOG_MAX_DIR_FILES) nc = CATALOG_MAX_DIR_FILES;
        char** na = (char**)realloc(dc->paths, sizeof(char*) * (size_t)nc);
        if (!na) return;
        dc->paths = na; dc->capacity = nc;
    }
    dc->paths[dc->count] = _strdup(path);
    if (dc->paths[dc->count]) dc->count++;
}

static int catalog_path_cmp(const void* a, const void* b)
{
    const char* pa = *(const char**)a;
    const char* pb = *(const char**)b;
    const char* fa = pa; const char* fb = pb; const char* sep;
    sep = strrchr(pa, '/'); if (sep) fa = sep + 1;
#ifdef _WIN32
    sep = strrchr(pa, '\\'); if (sep && (sep + 1) > fa) fa = sep + 1;
#endif
    sep = strrchr(pb, '/'); if (sep) fb = sep + 1;
#ifdef _WIN32
    sep = strrchr(pb, '\\'); if (sep && (sep + 1) > fb) fb = sep + 1;
#endif
    return strcmp(fa, fb);
}
static int catalog_load_directory(Catalog* cat, const char* dir_path, int precedence)
{
    CatalogDirCtx dc;
    memset(&dc, 0, sizeof(dc));
    int rc = pal_dir_walk(dir_path, catalog_collect_toml_cb, &dc);
    if (rc != 0) {
        for (int i = 0; i < dc.count; i++) free(dc.paths[i]);
        free(dc.paths); return 0;
    }
    if (dc.count == 0) { free(dc.paths); return 0; }
    qsort(dc.paths, (size_t)dc.count, sizeof(char*), catalog_path_cmp);
    int loaded = 0;
    for (int i = 0; i < dc.count; i++) {
        char* buf = NULL; size_t len = 0;
        if (pal_file_read(dc.paths[i], &buf, &len) != 0) {
            cdo_warn("catalog: failed to read '%s', skipping", dc.paths[i]);
            free(dc.paths[i]); continue;
        }
        catalog_parse_file(cat, dc.paths[i], buf, len, precedence);
        loaded++;
        free(buf); free(dc.paths[i]);
    }
    free(dc.paths);
    return loaded;
}

/* --- Public: catalog_load --- */

int catalog_load(Catalog* out, const char* workspace_root)
{
    if (!out) return 1;
    memset(out, 0, sizeof(Catalog));
    int total = 0;
    char pb[1024];
    /* 1. Workspace catalogs */
    if (workspace_root && workspace_root[0] != '\0') {
        if (pal_path_join(pb, sizeof(pb), workspace_root, ".cdo/catalogs") == 0)
            if (pal_path_exists(pb) == 0) total += catalog_load_directory(out, pb, 0);
    }
    /* 2. User-global catalogs */
    char home[512];
    if (pal_get_home_dir(home, sizeof(home)) == 0) {
        if (pal_path_join(pb, sizeof(pb), home, ".cdo/catalogs") == 0)
            if (pal_path_exists(pb) == 0) total += catalog_load_directory(out, pb, 1);
    }
    /* 3. Built-in catalogs */
    if (pal_path_exists("catalogs") == 0)
        total += catalog_load_directory(out, "catalogs", 2);
    if (total == 0)
        cdo_warn("no catalog files found; use --url to specify download URLs manually");
    catalog_deduplicate(out);
    return 0;
}

/* --- Public: catalog_free --- */

void catalog_free(Catalog* cat)
{
    if (!cat) return;
    for (int i = 0; i < cat->package_count; i++) {
        CatalogPackageEntry* pkg = &cat->packages[i];
        for (int j = 0; j < pkg->include_dir_count; j++) free(pkg->include_dirs[j]);
        for (int j = 0; j < pkg->link_lib_count; j++) free(pkg->link_libs[j]);
        for (int j = 0; j < pkg->define_count; j++) free(pkg->defines[j]);
    }
    free(cat->tools);
    free(cat->packages);
    memset(cat, 0, sizeof(*cat));
}