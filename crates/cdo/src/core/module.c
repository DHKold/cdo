#include "core/module.h"
#include "core/workspace.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Public API ---

const char* module_kind_to_string(ModuleKind kind) {
    switch (kind) {
    case MODULE_LIB: return "lib";
    case MODULE_EXE: return "exe";
    case MODULE_DYN: return "dyn";
    case MODULE_TST: return "tst";
    case MODULE_API: return "api";
    }
    return "unknown";
}

const char* module_artifact_extension(ModuleKind kind) {
    switch (kind) {
#ifdef _WIN32
    case MODULE_LIB: return ".lib";
    case MODULE_EXE: return ".exe";
    case MODULE_DYN: return ".dll";
    case MODULE_TST: return ".exe";
#else
    case MODULE_LIB: return ".a";
    case MODULE_EXE: return "";
    case MODULE_DYN: return ".so";
    case MODULE_TST: return "";
#endif
    case MODULE_API: return NULL;
    }
    return NULL;
}

int module_artifact_name(const char* crate_name, ModuleKind kind,
                         char* buf, int buf_size) {
    if (!crate_name || !buf || buf_size <= 0) return 1;
    if (kind == MODULE_API) return 1;

    int written = 0;

    switch (kind) {
#ifdef _WIN32
    case MODULE_LIB:
        written = snprintf(buf, buf_size, "%s.lib", crate_name);
        break;
    case MODULE_EXE:
        written = snprintf(buf, buf_size, "%s.exe", crate_name);
        break;
    case MODULE_DYN:
        written = snprintf(buf, buf_size, "%s.dll", crate_name);
        break;
    case MODULE_TST:
        written = snprintf(buf, buf_size, "%s_test.exe", crate_name);
        break;
#else
    case MODULE_LIB:
        written = snprintf(buf, buf_size, "lib%s.a", crate_name);
        break;
    case MODULE_EXE:
        written = snprintf(buf, buf_size, "%s", crate_name);
        break;
    case MODULE_DYN:
        written = snprintf(buf, buf_size, "lib%s.so", crate_name);
        break;
    case MODULE_TST:
        written = snprintf(buf, buf_size, "%s_test", crate_name);
        break;
#endif
    case MODULE_API:
        return 1;
    }

    if (written < 0 || written >= buf_size) return 1;
    return 0;
}

int module_compute_artifact_path(const char* ws_root, const char* crate_name,
                                 ModuleKind kind, const char* profile,
                                 char* out_artifact_path, int artifact_path_size,
                                 char* out_obj_dir, int obj_dir_size) {
    if (!ws_root || !crate_name || !profile) return 1;
    if (!out_artifact_path || artifact_path_size <= 0) return 1;
    if (!out_obj_dir || obj_dir_size <= 0) return 1;
    if (kind == MODULE_API) return 1; // API modules produce no artifact

    // Compute: build/<profile>/<crate_name>/
    char tmp1[260];
    pal_path_join(tmp1, sizeof(tmp1), ws_root, "build");

    char tmp2[260];
    pal_path_join(tmp2, sizeof(tmp2), tmp1, profile);

    char crate_build_dir[260];
    pal_path_join(crate_build_dir, sizeof(crate_build_dir), tmp2, crate_name);

    // Create the crate-level output directory
    pal_mkdir_p(crate_build_dir);

    // Compute the module's object-file subdirectory: build/<profile>/<crate_name>/<kind>/
    const char* kind_str = module_kind_to_string(kind);
    pal_path_join(out_obj_dir, (size_t)obj_dir_size, crate_build_dir, kind_str);

    // Create the object-file subdirectory
    pal_mkdir_p(out_obj_dir);

    // Compute the artifact filename
    char artifact_name[128];
    int rc = module_artifact_name(crate_name, kind, artifact_name, sizeof(artifact_name));
    if (rc != 0) return 1;

    // Final artifact path: build/<profile>/<crate_name>/<artifact_filename>
    pal_path_join(out_artifact_path, (size_t)artifact_path_size,
                  crate_build_dir, artifact_name);

    return 0;
}

int module_include_paths(const Crate* crate, ModuleKind kind,
                         const struct Workspace* ws, char*** paths, int* count) {
    if (!crate || !paths || !count) return 1;

    // Maximum possible paths:
    // 1 (own dir) + 1 (lib/) + 1 (api/) + dep_count (inter-crate)
    int max_paths = 3 + crate->dep_count;
    char** result = (char**)malloc(max_paths * sizeof(char*));
    if (!result) return 1;

    int n = 0;

    // 1. Always add the module's own directory first
    const Module* self_mod = &crate->modules[kind];
    if (self_mod->present && self_mod->dir_path[0] != '\0') {
        result[n] = strdup(self_mod->dir_path);
        if (!result[n]) goto fail;
        n++;
    }

    // 2. If kind != MODULE_LIB and lib/ is present, add lib/ directory
    if (kind != MODULE_LIB && crate->has_lib) {
        const Module* lib_mod = &crate->modules[MODULE_LIB];
        if (lib_mod->dir_path[0] != '\0') {
            result[n] = strdup(lib_mod->dir_path);
            if (!result[n]) goto fail;
            n++;
        }
    }

    // 3. If api/ is present, add api/ directory; otherwise fallback to include/
    if (crate->has_api) {
        const Module* api_mod = &crate->modules[MODULE_API];
        if (api_mod->dir_path[0] != '\0') {
            result[n] = strdup(api_mod->dir_path);
            if (!result[n]) goto fail;
            n++;
        }
    } else if (ws) {
        // Intra-crate fallback: if no api/ but include/ exists, use it (Req 6.3)
        char crate_abs[260];
        if (pal_path_join(crate_abs, sizeof(crate_abs),
                          ws->root_path, crate->path) == 0) {
            char include_path[260];
            if (pal_path_join(include_path, sizeof(include_path),
                              crate_abs, "include") == 0) {
                if (pal_path_exists(include_path) == 0) {
                    char** grown = (char**)realloc(result, (max_paths + 1) * sizeof(char*));
                    if (!grown) goto fail;
                    result = grown;
                    max_paths++;

                    result[n] = strdup(include_path);
                    if (!result[n]) goto fail;
                    n++;
                }
            }
        }
    }

    // 4. Inter-crate dependencies: add target crate's api/ or fallback to include/
    if (ws) {
        for (int i = 0; i < crate->dep_count; i++) {
            int dep_idx = crate->dep_indices[i];
            if (dep_idx < 0 || dep_idx >= ws->crate_count) continue;

            const Crate* dep = &ws->crates[dep_idx];

            if (dep->has_api) {
                // Use the dep crate's api/ directory
                const Module* dep_api = &dep->modules[MODULE_API];
                if (dep_api->dir_path[0] != '\0') {
                    // Grow result array if needed
                    char** grown = (char**)realloc(result, (max_paths + 1) * sizeof(char*));
                    if (!grown) goto fail;
                    result = grown;
                    max_paths++;

                    result[n] = strdup(dep_api->dir_path);
                    if (!result[n]) goto fail;
                    n++;
                }
            } else {
                // Fallback: check for include/ directory in dep crate's path.
                // dep->path is relative to workspace root, so we must resolve
                // it to an absolute path first (Req 6.3).
                char dep_abs[260];
                if (pal_path_join(dep_abs, sizeof(dep_abs),
                                  ws->root_path, dep->path) == 0) {
                    char include_path[260];
                    if (pal_path_join(include_path, sizeof(include_path),
                                      dep_abs, "include") == 0) {
                        if (pal_path_exists(include_path) == 0) {
                            char** grown = (char**)realloc(result, (max_paths + 1) * sizeof(char*));
                            if (!grown) goto fail;
                            result = grown;
                            max_paths++;

                            result[n] = strdup(include_path);
                            if (!result[n]) goto fail;
                            n++;
                        }
                    }
                }
            }
        }
    }

    *paths = result;
    *count = n;
    return 0;

fail:
    for (int i = 0; i < n; i++) {
        free(result[i]);
    }
    free(result);
    *paths = NULL;
    *count = 0;
    return 1;
}
