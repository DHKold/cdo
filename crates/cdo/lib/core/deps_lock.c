#include "core/deps.h"
#include "core/output.h"
#include "commons/toml.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Internal helpers ---------- */

/**
 * Build the "source" string for a lock file entry.
 * Format: "registry+<url>", "git+<url>#<ref>", or "path+<url>"
 */
static int dep_build_source_string(const DepSpec* spec, char* buf, size_t buf_size)
{
    const char* prefix;
    switch (spec->source) {
        case DEP_REGISTRY: prefix = "registry+"; break;
        case DEP_GIT:      prefix = "git+"; break;
        case DEP_LOCAL:    prefix = "path+"; break;
        default:           return -1;
    }

    int n;
    if (spec->source == DEP_GIT && spec->git_ref[0]) {
        n = snprintf(buf, buf_size, "%s%s#%s", prefix, spec->url, spec->git_ref);
    } else {
        n = snprintf(buf, buf_size, "%s%s", prefix, spec->url);
    }
    if (n < 0 || (size_t)n >= buf_size) return -1;
    return 0;
}

/**
 * Parse a "source" string from a lock file back into DepSourceKind, url, and git_ref.
 */
static int dep_parse_source_string(const char* source_str, DepSpec* spec)
{
    if (strncmp(source_str, "registry+", 9) == 0) {
        spec->source = DEP_REGISTRY;
        strncpy(spec->url, source_str + 9, sizeof(spec->url) - 1);
        spec->url[sizeof(spec->url) - 1] = '\0';
        spec->git_ref[0] = '\0';
    } else if (strncmp(source_str, "git+", 4) == 0) {
        spec->source = DEP_GIT;
        const char* url_start = source_str + 4;
        const char* hash = strrchr(url_start, '#');
        if (hash) {
            size_t url_len = (size_t)(hash - url_start);
            if (url_len >= sizeof(spec->url)) url_len = sizeof(spec->url) - 1;
            memcpy(spec->url, url_start, url_len);
            spec->url[url_len] = '\0';
            strncpy(spec->git_ref, hash + 1, sizeof(spec->git_ref) - 1);
            spec->git_ref[sizeof(spec->git_ref) - 1] = '\0';
        } else {
            strncpy(spec->url, url_start, sizeof(spec->url) - 1);
            spec->url[sizeof(spec->url) - 1] = '\0';
            spec->git_ref[0] = '\0';
        }
    } else if (strncmp(source_str, "path+", 5) == 0) {
        spec->source = DEP_LOCAL;
        strncpy(spec->url, source_str + 5, sizeof(spec->url) - 1);
        spec->url[sizeof(spec->url) - 1] = '\0';
        spec->git_ref[0] = '\0';
    } else {
        return -1;
    }
    return 0;
}

/* ---------- Public API ---------- */

int dep_lock_write(const char* lock_path, const DepSpec* specs, int count)
{
    if (!lock_path || (!specs && count > 0)) return -1;
    if (count == 0) {
        /* Write an empty file */
        return pal_file_write(lock_path, "", 0);
    }

    /* Build TOML text manually for the lock file format:
     * [[package]]
     * name = "..."
     * version = "..."
     * source = "..."
     * checksum = "..."
     * metadata = "..."
     */
    size_t buf_cap = (size_t)count * 512 + 64;
    char* buf = (char*)malloc(buf_cap);
    if (!buf) return -1;

    size_t pos = 0;

    for (int i = 0; i < count; i++) {
        char source_str[700];
        if (dep_build_source_string(&specs[i], source_str, sizeof(source_str)) != 0) {
            free(buf);
            return -1;
        }

        /* Map metadata_kind to string */
        const char* meta_str;
        switch (specs[i].metadata_kind) {
            case DEP_META_PKGCONFIG:  meta_str = "pkg-config"; break;
            case DEP_META_CMAKE:      meta_str = "cmake"; break;
            case DEP_META_CDO_NATIVE: meta_str = "cdo-native"; break;
            default:                  meta_str = "none"; break;
        }

        int n = snprintf(buf + pos, buf_cap - pos,
            "[[package]]\nname = \"%s\"\nversion = \"%s\"\nsource = \"%s\"\nchecksum = \"%s\"\nmetadata = \"%s\"\n\n",
            specs[i].name, specs[i].version, source_str, specs[i].checksum, meta_str);

        if (n < 0 || (size_t)n >= buf_cap - pos) {
            free(buf);
            return -1;
        }
        pos += (size_t)n;
    }

    int rc = pal_file_write(lock_path, buf, pos);
    free(buf);
    return rc;
}

int dep_lock_read(const char* lock_path, DepSpec** specs, int* count)
{
    if (!lock_path || !specs || !count) return -1;
    *specs = NULL;
    *count = 0;

    /* Read file contents */
    char* file_buf = NULL;
    size_t file_len = 0;
    int rc = pal_file_read(lock_path, &file_buf, &file_len);
    if (rc != 0) return rc;

    /* Empty file = no packages */
    if (file_len == 0) {
        free(file_buf);
        return 0;
    }

    /* Parse as TOML */
    TomlTable* root = NULL;
    TomlError err = {0};
    rc = toml_parse(file_buf, file_len, &root, &err);
    free(file_buf);
    if (rc != 0) return -1;

    /* Look for "package" key — should be an array of tables */
    const TomlValue* pkg_val = toml_get(root, "package");
    if (!pkg_val || pkg_val->type != TOML_ARRAY) {
        toml_free(root);
        /* No packages — not an error */
        return 0;
    }

    const TomlArray* pkg_array = pkg_val->as.array;
    if (pkg_array->count == 0) {
        toml_free(root);
        return 0;
    }

    /* Allocate output array */
    DepSpec* result = (DepSpec*)calloc((size_t)pkg_array->count, sizeof(DepSpec));
    if (!result) {
        toml_free(root);
        return -1;
    }

    int valid_count = 0;
    for (int i = 0; i < pkg_array->count; i++) {
        TomlValue* item = pkg_array->items[i];
        if (item->type != TOML_TABLE) continue;

        TomlTable* tbl = item->as.table;
        DepSpec* s = &result[valid_count];
        memset(s, 0, sizeof(DepSpec));

        /* Extract fields from the table */
        for (TomlEntry* e = tbl->head; e; e = e->next) {
            if (e->value->type != TOML_STRING) continue;

            if (strcmp(e->key, "name") == 0) {
                strncpy(s->name, e->value->as.string, sizeof(s->name) - 1);
            } else if (strcmp(e->key, "version") == 0) {
                strncpy(s->version, e->value->as.string, sizeof(s->version) - 1);
            } else if (strcmp(e->key, "source") == 0) {
                dep_parse_source_string(e->value->as.string, s);
            } else if (strcmp(e->key, "checksum") == 0) {
                strncpy(s->checksum, e->value->as.string, sizeof(s->checksum) - 1);
            } else if (strcmp(e->key, "metadata") == 0) {
                const char* meta = e->value->as.string;
                if (strcmp(meta, "pkg-config") == 0) {
                    s->metadata_kind = DEP_META_PKGCONFIG;
                } else if (strcmp(meta, "cmake") == 0) {
                    s->metadata_kind = DEP_META_CMAKE;
                } else if (strcmp(meta, "cdo-native") == 0) {
                    s->metadata_kind = DEP_META_CDO_NATIVE;
                } else {
                    s->metadata_kind = DEP_META_NONE;
                }
            }
        }

        /* Validate that we got at least a name */
        if (s->name[0] != '\0') {
            valid_count++;
        }
    }

    toml_free(root);

    if (valid_count == 0) {
        free(result);
        return 0;
    }

    *specs = result;
    *count = valid_count;
    return 0;
}
