#include "workspace_internal.h"
#include "model/workspace.h"
#include "model/scanner.h"
#include "model/hooks.h"
#include "commons/toml.h"
#include "core/log.h"
#include "pal/pal.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// Internal helpers
// =============================================================================

/// Parse a size string with suffix (KB, MB, GB) into bytes.
/// Supports case-insensitive suffixes. Returns bytes on success, -1 on invalid input.
/// Examples: "2GB" â†’ 2147483648, "500MB" â†’ 524288000, "100KB" â†’ 102400
static int64_t parse_size_string(const char* size_str) {
    if (!size_str || !*size_str) return -1;

    char* end = NULL;
    double value = strtod(size_str, &end);
    if (end == size_str || value < 0) return -1;

    // Skip whitespace between number and suffix
    while (*end == ' ') end++;

    int64_t multiplier = 1;
    if ((*end == 'G' || *end == 'g') && (*(end + 1) == 'B' || *(end + 1) == 'b')) {
        multiplier = (int64_t)1024 * 1024 * 1024;
    } else if ((*end == 'M' || *end == 'm') && (*(end + 1) == 'B' || *(end + 1) == 'b')) {
        multiplier = (int64_t)1024 * 1024;
    } else if ((*end == 'K' || *end == 'k') && (*(end + 1) == 'B' || *(end + 1) == 'b')) {
        multiplier = (int64_t)1024;
    } else if (*end == '\0') {
        multiplier = 1; // Plain bytes if no suffix
    } else {
        return -1; // Unknown suffix
    }

    return (int64_t)(value * (double)multiplier);
}

/// Try to read a config file with fallback order: .toml, .yaml, .json
/// Returns 0 on success with buf/len populated. Caller frees buf.
/// base_name is e.g. "cdo" or "crate" (without extension).
int ws_read_config_file(const char* dir, const char* base_name,
                        char** buf, size_t* len) {
    static const char* extensions[] = { ".toml", ".yaml", ".json" };
    static const int ext_count = 3;

    char path[520];

    for (int i = 0; i < ext_count; i++) {
        if (pal_path_join(path, sizeof(path), dir, base_name) != 0) {
            continue;
        }
        size_t path_len = strlen(path);
        size_t ext_len = strlen(extensions[i]);
        if (path_len + ext_len >= sizeof(path)) {
            continue;
        }
        memcpy(path + path_len, extensions[i], ext_len + 1);

        if (pal_file_read(path, buf, len) == 0) {
            return 0;
        }
    }

    return -1; // No config file found
}

/// Parse a crate type string into the CrateType enum.
static CrateType parse_crate_type(const char* type_str) {
    if (!type_str) return CRATE_EXECUTABLE;

    if (strcmp(type_str, "executable") == 0) return CRATE_EXECUTABLE;
    if (strcmp(type_str, "static-library") == 0) return CRATE_STATIC_LIB;
    if (strcmp(type_str, "shared-library") == 0) return CRATE_SHARED_LIB;
    if (strcmp(type_str, "test") == 0) return CRATE_TEST;

    // Default to executable for unknown types
    return CRATE_EXECUTABLE;
}

// --- Dynamic array for collecting discovered crate directories ---
typedef struct {
    char** paths;
    int    count;
    int    capacity;
} PathList;

static void pathlist_init(PathList* pl) {
    pl->paths = NULL;
    pl->count = 0;
    pl->capacity = 0;
}

static int pathlist_push(PathList* pl, const char* path) {
    if (pl->count >= pl->capacity) {
        int new_cap = pl->capacity == 0 ? 8 : pl->capacity * 2;
        char** new_paths = (char**)realloc(pl->paths, (size_t)new_cap * sizeof(char*));
        if (!new_paths) return -1;
        pl->paths = new_paths;
        pl->capacity = new_cap;
    }
    pl->paths[pl->count] = strdup(path);
    if (!pl->paths[pl->count]) return -1;
    pl->count++;
    return 0;
}

static void pathlist_free(PathList* pl) {
    for (int i = 0; i < pl->count; i++) {
        free(pl->paths[i]);
    }
    free(pl->paths);
    pl->paths = NULL;
    pl->count = 0;
    pl->capacity = 0;
}

// --- Context for pal_dir_walk callback when expanding member globs ---
typedef struct {
    const char* root_path;
    PathList*   results;
} GlobWalkCtx;

/// Callback for pal_dir_walk: collect directories that contain a crate manifest.
static void glob_walk_callback(const char* entry_path, bool is_dir, void* ctx) {
    if (!is_dir) return;

    GlobWalkCtx* gctx = (GlobWalkCtx*)ctx;
    (void)gctx->root_path;

    // Check if this directory contains a crate manifest (crate.toml, .yaml, .json)
    char check_path[520];
    static const char* exts[] = { ".toml", ".yaml", ".json" };

    for (int i = 0; i < 3; i++) {
        if (pal_path_join(check_path, sizeof(check_path), entry_path, "crate") != 0) {
            continue;
        }
        size_t check_len = strlen(check_path);
        size_t ext_len = strlen(exts[i]);
        if (check_len + ext_len >= sizeof(check_path)) continue;
        memcpy(check_path + check_len, exts[i], ext_len + 1);

        if (pal_path_exists(check_path) == 0) {
            pathlist_push(gctx->results, entry_path);
            return; // Found a manifest, no need to check more extensions
        }
    }
}

/// Expand a single member pattern (e.g., "crates/*") into actual crate directories.
/// Supports:
///   - "path/*" â€” enumerate immediate subdirectories of "path" that contain crate manifests
///   - "explicit/path" â€” a direct path to a crate directory
static int expand_member_pattern(const char* root_path, const char* pattern,
                                 PathList* results) {
    // Build the full path
    char full_path[520];

    // Check if pattern ends with "/*" (glob pattern)
    size_t pat_len = strlen(pattern);
    if (pat_len >= 2 && pattern[pat_len - 2] == '/' && pattern[pat_len - 1] == '*') {
        // Glob pattern: enumerate subdirectories
        // Trim the "/*" suffix to get the parent directory
        char parent_rel[260];
        if (pat_len - 2 >= sizeof(parent_rel)) return -1;
        memcpy(parent_rel, pattern, pat_len - 2);
        parent_rel[pat_len - 2] = '\0';

        if (pal_path_join(full_path, sizeof(full_path), root_path, parent_rel) != 0) {
            return -1;
        }

        GlobWalkCtx gctx;
        gctx.root_path = root_path;
        gctx.results = results;

        pal_dir_walk(full_path, glob_walk_callback, &gctx);

    } else {
        // Explicit path: just check if it has a crate manifest
        if (pal_path_join(full_path, sizeof(full_path), root_path, pattern) != 0) {
            return -1;
        }

        char check_path[520];
        static const char* exts[] = { ".toml", ".yaml", ".json" };
        for (int i = 0; i < 3; i++) {
            if (pal_path_join(check_path, sizeof(check_path), full_path, "crate") != 0) {
                continue;
            }
            size_t check_len = strlen(check_path);
            size_t ext_len = strlen(exts[i]);
            if (check_len + ext_len >= sizeof(check_path)) continue;
            memcpy(check_path + check_len, exts[i], ext_len + 1);

            if (pal_path_exists(check_path) == 0) {
                pathlist_push(results, full_path);
                break;
            }
        }
    }

    return 0;
}

/// Parse a single crate manifest and populate the Crate struct.
/// crate_dir is the absolute path to the crate directory.
/// root_path is the workspace root (used to compute relative path).
static int parse_crate_manifest(const char* crate_dir, const char* root_path,
                                Crate* crate, int default_c_std, int default_cpp_std) {
    char* buf = NULL;
    size_t len = 0;

    if (ws_read_config_file(crate_dir, "crate", &buf, &len) != 0) {
        cdo_log_error("Failed to read crate manifest in: %s", crate_dir);
        return -1;
    }

    // Parse as TOML (primary format)
    TomlTable* root = NULL;
    TomlError err;
    if (toml_parse(buf, len, &root, &err) != 0) {
        cdo_log_error("Failed to parse crate manifest in %s: line %d, col %d: %s",
                  crate_dir, err.line, err.col, err.message);
        free(buf);
        return -1;
    }
    free(buf);

    // Initialize crate with defaults
    memset(crate, 0, sizeof(Crate));
    crate->c_standard = default_c_std;
    crate->cpp_standard = default_cpp_std;
    crate->type = CRATE_EXECUTABLE;
    memcpy(crate->version, "0.0.0", 6);
    memcpy(crate->resource_base, ".", 2);
    memcpy(crate->shader_base, ".", 2);

    // Compute relative path from workspace root
    size_t root_len = strlen(root_path);
    const char* rel = crate_dir;
    if (strncmp(crate_dir, root_path, root_len) == 0) {
        rel = crate_dir + root_len;
        // Skip leading separator
        if (*rel == '/' || *rel == '\\') rel++;
    }
    size_t rel_len = strlen(rel);
    if (rel_len >= sizeof(crate->path)) rel_len = sizeof(crate->path) - 1;
    memcpy(crate->path, rel, rel_len);
    crate->path[rel_len] = '\0';
    // Normalize path separators
    pal_path_normalize(crate->path);

    // Parse [crate] section
    const TomlValue* name_val = toml_get(root, "crate.name");
    if (name_val && name_val->type == TOML_STRING && name_val->as.string) {
        size_t name_len = strlen(name_val->as.string);
        if (name_len >= sizeof(crate->name)) name_len = sizeof(crate->name) - 1;
        memcpy(crate->name, name_val->as.string, name_len);
        crate->name[name_len] = '\0';
    } else {
        // Fall back to directory name
        const char* dir_name = crate_dir;
        const char* last_sep = strrchr(crate_dir, '/');
        const char* last_bsep = strrchr(crate_dir, '\\');
        if (last_bsep && (!last_sep || last_bsep > last_sep)) last_sep = last_bsep;
        if (last_sep) dir_name = last_sep + 1;
        size_t name_len = strlen(dir_name);
        if (name_len >= sizeof(crate->name)) name_len = sizeof(crate->name) - 1;
        memcpy(crate->name, dir_name, name_len);
        crate->name[name_len] = '\0';
    }

    // Parse type
    const TomlValue* type_val = toml_get(root, "crate.type");
    if (type_val && type_val->type == TOML_STRING && type_val->as.string) {
        crate->type = parse_crate_type(type_val->as.string);
    }

    // Parse c-standard
    const TomlValue* c_std_val = toml_get(root, "crate.c-standard");
    if (c_std_val && c_std_val->type == TOML_INTEGER) {
        crate->c_standard = (int)c_std_val->as.integer;
    }

    // Parse cpp-standard
    const TomlValue* cpp_std_val = toml_get(root, "crate.cpp-standard");
    if (cpp_std_val && cpp_std_val->type == TOML_INTEGER) {
        crate->cpp_standard = (int)cpp_std_val->as.integer;
    }

    // Parse crate.version (optional, defaults to "0.0.0")
    const TomlValue* version_val = toml_get(root, "crate.version");
    if (version_val && version_val->type == TOML_STRING && version_val->as.string) {
        size_t ver_len = strlen(version_val->as.string);
        if (ver_len >= sizeof(crate->version)) ver_len = sizeof(crate->version) - 1;
        memcpy(crate->version, version_val->as.string, ver_len);
        crate->version[ver_len] = '\0';
        cdo_log_debug("Crate '%s': version = '%s'", crate->name, crate->version);
    } else {
        cdo_log_debug("Crate '%s': no version specified, defaulting to '0.0.0'", crate->name);
    }

    // Parse [install] section (optional, defaults: resource-base=".", shader-base=".")
    const TomlValue* res_base_val = toml_get(root, "install.resource-base");
    if (res_base_val && res_base_val->type == TOML_STRING && res_base_val->as.string) {
        size_t rb_len = strlen(res_base_val->as.string);
        if (rb_len >= sizeof(crate->resource_base)) rb_len = sizeof(crate->resource_base) - 1;
        memcpy(crate->resource_base, res_base_val->as.string, rb_len);
        crate->resource_base[rb_len] = '\0';
        cdo_log_debug("Crate '%s': install.resource-base = '%s'", crate->name, crate->resource_base);
    } else {
        cdo_log_debug("Crate '%s': no install.resource-base specified, defaulting to '.'", crate->name);
    }

    const TomlValue* shd_base_val = toml_get(root, "install.shader-base");
    if (shd_base_val && shd_base_val->type == TOML_STRING && shd_base_val->as.string) {
        size_t sb_len = strlen(shd_base_val->as.string);
        if (sb_len >= sizeof(crate->shader_base)) sb_len = sizeof(crate->shader_base) - 1;
        memcpy(crate->shader_base, shd_base_val->as.string, sb_len);
        crate->shader_base[sb_len] = '\0';
        cdo_log_debug("Crate '%s': install.shader-base = '%s'", crate->name, crate->shader_base);
    } else {
        cdo_log_debug("Crate '%s': no install.shader-base specified, defaulting to '.'", crate->name);
    }

    // Parse dependencies (just collect names for now; indices resolved later)
    const TomlValue* deps_val = toml_get(root, "dependencies");
    if (deps_val && (deps_val->type == TOML_TABLE || deps_val->type == TOML_INLINE_TABLE)) {
        TomlTable* deps_table = deps_val->as.table;
        crate->dep_count = deps_table->count;
        if (crate->dep_count > 0) {
            crate->dep_indices = (int*)calloc((size_t)crate->dep_count, sizeof(int));
            if (!crate->dep_indices) {
                crate->dep_count = 0;
            } else {
                for (int i = 0; i < crate->dep_count; i++) {
                    crate->dep_indices[i] = -1;
                }
            }
        }
    }

    // Parse dev-dependencies
    const TomlValue* dev_deps_val = toml_get(root, "dev-dependencies");
    if (dev_deps_val && (dev_deps_val->type == TOML_TABLE || dev_deps_val->type == TOML_INLINE_TABLE)) {
        TomlTable* dev_deps_table = dev_deps_val->as.table;
        crate->dev_dep_count = dev_deps_table->count;
        if (crate->dev_dep_count > 0) {
            crate->dev_dep_indices = (int*)calloc((size_t)crate->dev_dep_count, sizeof(int));
            if (!crate->dev_dep_indices) {
                crate->dev_dep_count = 0;
            } else {
                for (int i = 0; i < crate->dev_dep_count; i++) {
                    crate->dev_dep_indices[i] = -1;
                }
            }
        }
    }

    // Parse link-libs (platform link libraries)
    const TomlValue* libs_val = toml_get(root, "build.link-libs");
    if (libs_val && libs_val->type == TOML_ARRAY && libs_val->as.array) {
        TomlArray* arr = libs_val->as.array;
        if (arr->count > 0) {
            crate->link_libs = (char**)calloc((size_t)arr->count, sizeof(char*));
            if (crate->link_libs) {
                int count = 0;
                for (int i = 0; i < arr->count; i++) {
                    if (arr->items[i] && arr->items[i]->type == TOML_STRING &&
                        arr->items[i]->as.string) {
                        crate->link_libs[count] = strdup(arr->items[i]->as.string);
                        if (crate->link_libs[count]) count++;
                    }
                }
                crate->link_lib_count = count;
            }
        }
    }

    // Parse build.defines (crate-level preprocessor defines)
    const TomlValue* defs_val = toml_get(root, "build.defines");
    if (defs_val && defs_val->type == TOML_ARRAY && defs_val->as.array) {
        TomlArray* arr = defs_val->as.array;
        if (arr->count > 0) {
            crate->defines = (char**)calloc((size_t)arr->count, sizeof(char*));
            if (crate->defines) {
                int count = 0;
                for (int i = 0; i < arr->count; i++) {
                    if (arr->items[i] && arr->items[i]->type == TOML_STRING &&
                        arr->items[i]->as.string) {
                        crate->defines[count] = strdup(arr->items[i]->as.string);
                        if (crate->defines[count]) count++;
                    }
                }
                crate->define_count = count;
            }
        }
    }

    // Parse [hooks] section for crate lifecycle hooks
    const TomlValue* hooks_val = toml_get(root, "hooks");
    if (hooks_val && (hooks_val->type == TOML_TABLE || hooks_val->type == TOML_INLINE_TABLE)) {
        if (hooks_parse_table(hooks_val->as.table, &crate->hooks) != 0) {
            cdo_log_error("Failed to parse [hooks] in crate '%s'", crate->name);
            toml_free(root);
            return -1;
        }
    } else {
        memset(&crate->hooks, 0, sizeof(HookSet));
    }

    toml_free(root);
    return 0;
}

// =============================================================================
// Public API
// =============================================================================

int workspace_load(const char* root_path, Workspace* ws) {
    if (!root_path || !ws) return -1;

    memset(ws, 0, sizeof(Workspace));

    // Store root path (normalized)
    size_t root_len = strlen(root_path);
    if (root_len >= sizeof(ws->root_path)) root_len = sizeof(ws->root_path) - 1;
    memcpy(ws->root_path, root_path, root_len);
    ws->root_path[root_len] = '\0';
    pal_path_normalize(ws->root_path);

    // Strip trailing slash from root_path for consistent joining
    root_len = strlen(ws->root_path);
    while (root_len > 0 && ws->root_path[root_len - 1] == '/') {
        ws->root_path[root_len - 1] = '\0';
        root_len--;
    }

    // Read workspace config file (cdo.toml, cdo.yaml, cdo.json)
    char* buf = NULL;
    size_t buf_len = 0;
    if (ws_read_config_file(ws->root_path, "cdo", &buf, &buf_len) != 0) {
        cdo_log_error("No workspace manifest found (tried cdo.toml, cdo.yaml, cdo.json) in: %s",
                  ws->root_path);
        return -1;
    }

    // Parse workspace manifest as TOML
    TomlTable* root = NULL;
    TomlError err;
    if (toml_parse(buf, buf_len, &root, &err) != 0) {
        cdo_log_error("Failed to parse workspace manifest: line %d, col %d: %s",
                  err.line, err.col, err.message);
        free(buf);
        return -1;
    }
    free(buf);

    // Extract workspace settings (defaults for crates)
    int default_c_std = 17;
    int default_cpp_std = 20;

    const TomlValue* c_std_val = toml_get(root, "workspace.settings.c-standard");
    if (c_std_val && c_std_val->type == TOML_INTEGER) {
        default_c_std = (int)c_std_val->as.integer;
    }

    const TomlValue* cpp_std_val = toml_get(root, "workspace.settings.cpp-standard");
    if (cpp_std_val && cpp_std_val->type == TOML_INTEGER) {
        default_cpp_std = (int)cpp_std_val->as.integer;
    }

    // Parse [workspace.settings.cache] section with defaults
    ws->cache_config.enabled = true;
    strncpy(ws->cache_config.path, ".cdo/cache/objects", sizeof(ws->cache_config.path) - 1);
    ws->cache_config.path[sizeof(ws->cache_config.path) - 1] = '\0';
    ws->cache_config.max_size_bytes = (int64_t)2 * 1024 * 1024 * 1024; // 2GB
    strncpy(ws->cache_config.backend, "builtin", sizeof(ws->cache_config.backend) - 1);
    ws->cache_config.backend[sizeof(ws->cache_config.backend) - 1] = '\0';

    const TomlValue* cache_enabled_val = toml_get(root, "workspace.settings.cache.enabled");
    if (cache_enabled_val && cache_enabled_val->type == TOML_BOOL) {
        ws->cache_config.enabled = cache_enabled_val->as.boolean;
    }

    const TomlValue* cache_path_val = toml_get(root, "workspace.settings.cache.path");
    if (cache_path_val && cache_path_val->type == TOML_STRING && cache_path_val->as.string) {
        strncpy(ws->cache_config.path, cache_path_val->as.string, sizeof(ws->cache_config.path) - 1);
        ws->cache_config.path[sizeof(ws->cache_config.path) - 1] = '\0';
    }

    const TomlValue* cache_size_val = toml_get(root, "workspace.settings.cache.max-size");
    if (cache_size_val && cache_size_val->type == TOML_STRING && cache_size_val->as.string) {
        int64_t parsed_size = parse_size_string(cache_size_val->as.string);
        if (parsed_size > 0) {
            ws->cache_config.max_size_bytes = parsed_size;
        } else {
            cdo_log_warn("Invalid cache max-size '%s', using default 2GB", cache_size_val->as.string);
        }
    }

    const TomlValue* cache_backend_val = toml_get(root, "workspace.settings.cache.backend");
    if (cache_backend_val && cache_backend_val->type == TOML_STRING && cache_backend_val->as.string) {
        strncpy(ws->cache_config.backend, cache_backend_val->as.string, sizeof(ws->cache_config.backend) - 1);
        ws->cache_config.backend[sizeof(ws->cache_config.backend) - 1] = '\0';
    }

    // Parse [workspace.settings.format] section with defaults
    memset(&ws->format_settings, 0, sizeof(FmtSettings));

    const TomlValue* fmt_tool_path_val = toml_get(root, "workspace.settings.format.tool-path");
    if (fmt_tool_path_val && fmt_tool_path_val->type == TOML_STRING && fmt_tool_path_val->as.string) {
        strncpy(ws->format_settings.tool_path, fmt_tool_path_val->as.string, sizeof(ws->format_settings.tool_path) - 1);
        ws->format_settings.tool_path[sizeof(ws->format_settings.tool_path) - 1] = '\0';
    }

    const TomlValue* fmt_style_val = toml_get(root, "workspace.settings.format.style");
    if (fmt_style_val && fmt_style_val->type == TOML_STRING && fmt_style_val->as.string) {
        strncpy(ws->format_settings.style, fmt_style_val->as.string, sizeof(ws->format_settings.style) - 1);
        ws->format_settings.style[sizeof(ws->format_settings.style) - 1] = '\0';
    }

    const TomlValue* fmt_exclude_val = toml_get(root, "workspace.settings.format.exclude");
    if (fmt_exclude_val && fmt_exclude_val->type == TOML_ARRAY && fmt_exclude_val->as.array) {
        TomlArray* exclude_arr = fmt_exclude_val->as.array;
        int count = exclude_arr->count;
        if (count > 32) count = 32;
        for (int i = 0; i < count; i++) {
            TomlValue* item = exclude_arr->items[i];
            if (item && item->type == TOML_STRING && item->as.string) {
                strncpy(ws->format_settings.exclude_patterns[ws->format_settings.exclude_count], item->as.string, 259);
                ws->format_settings.exclude_patterns[ws->format_settings.exclude_count][259] = '\0';
                ws->format_settings.exclude_count++;
            }
        }
    }

    // Parse [workspace.hooks] section
    const TomlValue* ws_hooks_val = toml_get(root, "workspace.hooks");
    if (ws_hooks_val && (ws_hooks_val->type == TOML_TABLE || ws_hooks_val->type == TOML_INLINE_TABLE)) {
        if (hooks_parse_table(ws_hooks_val->as.table, &ws->ws_hooks) != 0) {
            cdo_log_error("Failed to parse [workspace.hooks]");
            toml_free(root);
            return -1;
        }
    } else {
        memset(&ws->ws_hooks, 0, sizeof(HookSet));
    }

    // Extract members array
    const TomlValue* members_val = toml_get(root, "workspace.members");
    if (!members_val || members_val->type != TOML_ARRAY) {
        cdo_log_error("Workspace manifest missing [workspace] members array");
        toml_free(root);
        return -1;
    }

    TomlArray* members_arr = members_val->as.array;

    // Expand all member patterns to discover crate directories
    PathList crate_dirs;
    pathlist_init(&crate_dirs);

    for (int i = 0; i < members_arr->count; i++) {
        TomlValue* item = members_arr->items[i];
        if (!item || item->type != TOML_STRING || !item->as.string) continue;

        expand_member_pattern(ws->root_path, item->as.string, &crate_dirs);
    }

    toml_free(root);

    if (crate_dirs.count == 0) {
        cdo_log_warn("No crates discovered in workspace");
        pathlist_free(&crate_dirs);
        return 0; // Empty workspace is valid but has no crates
    }

    // Allocate crates array
    ws->crates = (Crate*)calloc((size_t)crate_dirs.count, sizeof(Crate));
    if (!ws->crates) {
        pathlist_free(&crate_dirs);
        return -1;
    }

    // Parse each crate manifest
    int loaded = 0;
    for (int i = 0; i < crate_dirs.count; i++) {
        if (parse_crate_manifest(crate_dirs.paths[i], ws->root_path,
                                 &ws->crates[loaded],
                                 default_c_std, default_cpp_std) == 0) {
            loaded++;
        }
    }
    ws->crate_count = loaded;

    pathlist_free(&crate_dirs);

    // Scan each crate for module directories (lib/, exe/, dyn/, tst/, api/).
    for (int i = 0; i < ws->crate_count; i++) {
        char crate_abs[520];
        if (pal_path_join(crate_abs, sizeof(crate_abs),
                          ws->root_path, ws->crates[i].path) != 0) {
            continue;
        }

        int scan_result = scanner_scan_modules(crate_abs, &ws->crates[i], NULL, 0);
        if (scan_result != 0) {
            cdo_log_warn("Crate '%s': no module directories found (lib/, exe/, dyn/, tst/, api/)",
                     ws->crates[i].name);
        } else {
            cdo_log_debug("Crate '%s': discovered %d module(s)",
                      ws->crates[i].name, ws->crates[i].module_count);
        }
    }

    // Resolve dependency name references to crate indices
    resolve_dep_indices(ws);

    // Validate inter-crate module dependencies
    if (workspace_resolve_module_deps(ws) != 0) {
        // Non-fatal at load time: errors have been reported via cdo_log_error().
    }

    return 0;
}
