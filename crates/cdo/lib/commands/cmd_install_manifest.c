/**
 * cmd_install_manifest.c - Per-app manifest and global index read/write.
 *
 * Handles TOML serialization for:
 *   - Per-app manifest.toml (written to each app bundle)
 *   - Global install.toml (index for fast enumeration)
 */
#include "commands/cmd_install_internal.h"
#include "commons/toml.h"
#include "core/log.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Timestamp helper
// ---------------------------------------------------------------------------

static void get_iso_timestamp(char* buf, size_t buf_size) {
    time_t now = time(NULL);
    struct tm* utc = gmtime(&now);
    if (utc) {
        strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", utc);
    } else {
        strncpy(buf, "1970-01-01T00:00:00Z", buf_size - 1);
        buf[buf_size - 1] = '\0';
    }
}

// ---------------------------------------------------------------------------
// Per-app manifest write
// ---------------------------------------------------------------------------

int install_write_manifest(const InstallManifest* manifest, const char* manifest_path) {
    // Build TOML by hand for simplicity and control over output format
    char buf[4096];
    int len = 0;

    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "[app]\n");
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "name = \"%s\"\n", manifest->name);
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "version = \"%s\"\n", manifest->version);
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "crate = \"%s\"\n", manifest->crate_name);
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "source_workspace = \"%s\"\n", manifest->source_workspace);
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "installed_at = \"%s\"\n", manifest->installed_at);
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "cdo_version = \"%s\"\n", manifest->cdo_version);
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "profile = \"%s\"\n", manifest->profile);
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "\n[contents]\n");
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "executable = \"%s\"\n", manifest->executable);
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "has_resources = %s\n", manifest->has_resources ? "true" : "false");
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "has_shaders = %s\n", manifest->has_shaders ? "true" : "false");
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "resource_base = \"%s\"\n", manifest->resource_base[0] ? manifest->resource_base : ".");
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "shader_base = \"%s\"\n", manifest->shader_base[0] ? manifest->shader_base : ".");

    if (len <= 0 || (size_t)len >= sizeof(buf)) {
        cdo_log_error("install: manifest content too large");
        return 1;
    }

    int rc = pal_file_write(manifest_path, buf, (size_t)len);
    if (rc != 0) {
        cdo_log_error("install: failed to write manifest '%s'", manifest_path);
        return 1;
    }

    cdo_log_debug("install: wrote manifest '%s'", manifest_path);
    return 0;
}

// ---------------------------------------------------------------------------
// Per-app manifest read
// ---------------------------------------------------------------------------

int install_read_manifest(const char* manifest_path, InstallManifest* out) {
    memset(out, 0, sizeof(*out));

    char* content = NULL;
    size_t content_len = 0;
    int rc = pal_file_read(manifest_path, &content, &content_len);
    if (rc != 0) return rc;

    TomlTable* root = NULL;
    TomlError err;
    rc = toml_parse(content, content_len, &root, &err);
    free(content);
    if (rc != 0) {
        cdo_log_debug("install: failed to parse manifest '%s': %s", manifest_path, err.message);
        return 1;
    }

    const TomlValue* v;

    v = toml_get(root, "app.name");
    if (v && v->type == TOML_STRING) strncpy(out->name, v->as.string, sizeof(out->name) - 1);

    v = toml_get(root, "app.version");
    if (v && v->type == TOML_STRING) strncpy(out->version, v->as.string, sizeof(out->version) - 1);

    v = toml_get(root, "app.crate");
    if (v && v->type == TOML_STRING) strncpy(out->crate_name, v->as.string, sizeof(out->crate_name) - 1);

    v = toml_get(root, "app.source_workspace");
    if (v && v->type == TOML_STRING) strncpy(out->source_workspace, v->as.string, sizeof(out->source_workspace) - 1);

    v = toml_get(root, "app.installed_at");
    if (v && v->type == TOML_STRING) strncpy(out->installed_at, v->as.string, sizeof(out->installed_at) - 1);

    v = toml_get(root, "app.cdo_version");
    if (v && v->type == TOML_STRING) strncpy(out->cdo_version, v->as.string, sizeof(out->cdo_version) - 1);

    v = toml_get(root, "app.profile");
    if (v && v->type == TOML_STRING) strncpy(out->profile, v->as.string, sizeof(out->profile) - 1);

    v = toml_get(root, "contents.executable");
    if (v && v->type == TOML_STRING) strncpy(out->executable, v->as.string, sizeof(out->executable) - 1);

    v = toml_get(root, "contents.has_resources");
    if (v && v->type == TOML_BOOL) out->has_resources = v->as.boolean;

    v = toml_get(root, "contents.has_shaders");
    if (v && v->type == TOML_BOOL) out->has_shaders = v->as.boolean;

    v = toml_get(root, "contents.resource_base");
    if (v && v->type == TOML_STRING) strncpy(out->resource_base, v->as.string, sizeof(out->resource_base) - 1);
    else strncpy(out->resource_base, ".", sizeof(out->resource_base) - 1);

    v = toml_get(root, "contents.shader_base");
    if (v && v->type == TOML_STRING) strncpy(out->shader_base, v->as.string, sizeof(out->shader_base) - 1);
    else strncpy(out->shader_base, ".", sizeof(out->shader_base) - 1);

    toml_free(root);
    return 0;
}

// ---------------------------------------------------------------------------
// Global index write
// ---------------------------------------------------------------------------

int install_update_global_index(const char* apps_dir, const InstallManifest* manifest) {
    char index_path[1024];
    if (pal_path_join(index_path, sizeof(index_path), apps_dir, "install.toml") != 0) {
        cdo_log_error("install: index path too long");
        return 1;
    }

    // Read existing index entries (if any)
    InstallIndexEntry entries[64];
    int entry_count = 0;

    char* content = NULL;
    size_t content_len = 0;
    if (pal_file_read(index_path, &content, &content_len) == 0 && content) {
        TomlTable* root = NULL;
        TomlError err;
        if (toml_parse(content, content_len, &root, &err) == 0 && root) {
            const TomlValue* arr = toml_get(root, "app");
            if (arr && arr->type == TOML_ARRAY) {
                for (int i = 0; i < arr->as.array->count && entry_count < 64; i++) {
                    TomlValue* item = arr->as.array->items[i];
                    if (item->type != TOML_TABLE) continue;

                    InstallIndexEntry* e = &entries[entry_count];
                    memset(e, 0, sizeof(*e));

                    // Read fields from inline table entries
                    for (TomlEntry* te = item->as.table->head; te; te = te->next) {
                        if (strcmp(te->key, "name") == 0 && te->value->type == TOML_STRING)
                            strncpy(e->name, te->value->as.string, sizeof(e->name) - 1);
                        else if (strcmp(te->key, "version") == 0 && te->value->type == TOML_STRING)
                            strncpy(e->version, te->value->as.string, sizeof(e->version) - 1);
                        else if (strcmp(te->key, "source_workspace") == 0 && te->value->type == TOML_STRING)
                            strncpy(e->source_workspace, te->value->as.string, sizeof(e->source_workspace) - 1);
                        else if (strcmp(te->key, "installed_at") == 0 && te->value->type == TOML_STRING)
                            strncpy(e->installed_at, te->value->as.string, sizeof(e->installed_at) - 1);
                        else if (strcmp(te->key, "path") == 0 && te->value->type == TOML_STRING)
                            strncpy(e->path, te->value->as.string, sizeof(e->path) - 1);
                    }
                    entry_count++;
                }
            }
            toml_free(root);
        }
        free(content);
    }

    // Remove existing entry for this app name (if reinstalling)
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].name, manifest->name) == 0) {
            // Shift remaining entries down
            for (int j = i; j < entry_count - 1; j++) {
                entries[j] = entries[j + 1];
            }
            entry_count--;
            break;
        }
    }

    // Add new entry
    if (entry_count < 64) {
        InstallIndexEntry* e = &entries[entry_count];
        memset(e, 0, sizeof(*e));
        strncpy(e->name, manifest->name, sizeof(e->name) - 1);
        strncpy(e->version, manifest->version, sizeof(e->version) - 1);
        strncpy(e->source_workspace, manifest->source_workspace, sizeof(e->source_workspace) - 1);
        strncpy(e->installed_at, manifest->installed_at, sizeof(e->installed_at) - 1);
        strncpy(e->path, manifest->name, sizeof(e->path) - 1);
        entry_count++;
    }

    // Serialize to TOML
    char buf[8192];
    int len = 0;

    for (int i = 0; i < entry_count; i++) {
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "[[app]]\n");
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "name = \"%s\"\n", entries[i].name);
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "version = \"%s\"\n", entries[i].version);
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "source_workspace = \"%s\"\n", entries[i].source_workspace);
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "installed_at = \"%s\"\n", entries[i].installed_at);
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "path = \"%s\"\n", entries[i].path);
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "\n");
        if ((size_t)len >= sizeof(buf)) {
            cdo_log_error("install: global index too large");
            return 1;
        }
    }

    // Write atomically: write to temp, then rename
    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", index_path);

    int rc = pal_file_write(tmp_path, buf, (size_t)len);
    if (rc != 0) {
        cdo_log_error("install: failed to write temp index '%s'", tmp_path);
        return 1;
    }

    // Rename temp to final (atomic on most filesystems)
    // On Windows, remove target first if it exists
    if (pal_path_exists(index_path) == 0) {
        remove(index_path);
    }
    if (rename(tmp_path, index_path) != 0) {
        cdo_log_error("install: failed to rename temp index to '%s'", index_path);
        remove(tmp_path);
        return 1;
    }

    cdo_log_debug("install: updated global index '%s'", index_path);
    return 0;
}

// ---------------------------------------------------------------------------
// Global index remove entry
// ---------------------------------------------------------------------------

int install_remove_from_global_index(const char* apps_dir, const char* app_name) {
    char index_path[1024];
    if (pal_path_join(index_path, sizeof(index_path), apps_dir, "install.toml") != 0) return 1;

    char* content = NULL;
    size_t content_len = 0;
    if (pal_file_read(index_path, &content, &content_len) != 0) {
        // No index file, nothing to remove
        return 0;
    }

    InstallIndexEntry entries[64];
    int entry_count = 0;

    TomlTable* root = NULL;
    TomlError err;
    if (toml_parse(content, content_len, &root, &err) == 0 && root) {
        const TomlValue* arr = toml_get(root, "app");
        if (arr && arr->type == TOML_ARRAY) {
            for (int i = 0; i < arr->as.array->count && entry_count < 64; i++) {
                TomlValue* item = arr->as.array->items[i];
                if (item->type != TOML_TABLE) continue;

                InstallIndexEntry* e = &entries[entry_count];
                memset(e, 0, sizeof(*e));

                for (TomlEntry* te = item->as.table->head; te; te = te->next) {
                    if (strcmp(te->key, "name") == 0 && te->value->type == TOML_STRING)
                        strncpy(e->name, te->value->as.string, sizeof(e->name) - 1);
                    else if (strcmp(te->key, "version") == 0 && te->value->type == TOML_STRING)
                        strncpy(e->version, te->value->as.string, sizeof(e->version) - 1);
                    else if (strcmp(te->key, "source_workspace") == 0 && te->value->type == TOML_STRING)
                        strncpy(e->source_workspace, te->value->as.string, sizeof(e->source_workspace) - 1);
                    else if (strcmp(te->key, "installed_at") == 0 && te->value->type == TOML_STRING)
                        strncpy(e->installed_at, te->value->as.string, sizeof(e->installed_at) - 1);
                    else if (strcmp(te->key, "path") == 0 && te->value->type == TOML_STRING)
                        strncpy(e->path, te->value->as.string, sizeof(e->path) - 1);
                }
                entry_count++;
            }
        }
        toml_free(root);
    }
    free(content);

    // Remove the entry
    bool found = false;
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].name, app_name) == 0) {
            for (int j = i; j < entry_count - 1; j++) {
                entries[j] = entries[j + 1];
            }
            entry_count--;
            found = true;
            break;
        }
    }

    if (!found) return 0;

    // Rewrite index
    char buf[8192];
    int len = 0;
    for (int i = 0; i < entry_count; i++) {
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "[[app]]\n");
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "name = \"%s\"\n", entries[i].name);
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "version = \"%s\"\n", entries[i].version);
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "source_workspace = \"%s\"\n", entries[i].source_workspace);
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "installed_at = \"%s\"\n", entries[i].installed_at);
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "path = \"%s\"\n", entries[i].path);
        len += snprintf(buf + len, sizeof(buf) - (size_t)len, "\n");
    }

    int rc = pal_file_write(index_path, buf, (size_t)len);
    if (rc != 0) {
        cdo_log_error("install: failed to rewrite global index");
        return 1;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Fill manifest timestamp
// ---------------------------------------------------------------------------

void install_manifest_set_timestamp(InstallManifest* manifest) {
    get_iso_timestamp(manifest->installed_at, sizeof(manifest->installed_at));
}
