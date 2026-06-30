#include "model/module.h"
#include "model/workspace.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// --- Public API ---

const char* module_kind_to_string(ModuleKind kind) {
    switch (kind) {
    case MODULE_LIB: return "lib";
    case MODULE_EXE: return "exe";
    case MODULE_DYN: return "dyn";
    case MODULE_TST: return "tst";
    case MODULE_API: return "api";
    case MODULE_RES: return "res";
    case MODULE_SHD: return "shd";
    case MODULE_E2E: return "e2e";
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
    case MODULE_E2E: return ".exe";
#else
    case MODULE_LIB: return ".a";
    case MODULE_EXE: return "";
    case MODULE_DYN: return ".so";
    case MODULE_TST: return "";
    case MODULE_E2E: return "";
#endif
    case MODULE_API: return NULL;
    case MODULE_RES: return NULL;
    case MODULE_SHD: return NULL;
    }
    return NULL;
}

int module_artifact_name(const char* crate_name, ModuleKind kind,
                         char* buf, int buf_size) {
    if (!crate_name || !buf || buf_size <= 0) return 1;
    if (kind == MODULE_API || kind == MODULE_RES || kind == MODULE_SHD) return 1;

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
    case MODULE_E2E:
        written = snprintf(buf, buf_size, "%s_e2e.exe", crate_name);
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
    case MODULE_E2E:
        written = snprintf(buf, buf_size, "%s_e2e", crate_name);
        break;
#endif
    case MODULE_API:
    case MODULE_RES:
    case MODULE_SHD:
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
    if (kind == MODULE_API || kind == MODULE_RES || kind == MODULE_SHD) return 1; // These modules produce no linked artifact

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

    // We may have many paths: root dirs + subdirectories of lib/ and api/ + deps.
    // Start with a generous initial allocation.
    int max_paths = 32 + crate->dep_count;
    char** result = (char**)malloc(max_paths * sizeof(char*));
    if (!result) return 1;

    int n = 0;

    // Helper: grow result array if needed
    #define GROW_IF_NEEDED() do { \
        if (n >= max_paths) { \
            char** grown = (char**)realloc(result, (max_paths * 2) * sizeof(char*)); \
            if (!grown) goto fail; \
            result = grown; \
            max_paths *= 2; \
        } \
    } while(0)

    // 1. Always add the module's own directory first
    const Module* self_mod = &crate->modules[kind];
    if (self_mod->present && self_mod->dir_path[0] != '\0') {
        GROW_IF_NEEDED();
        result[n] = strdup(self_mod->dir_path);
        if (!result[n]) goto fail;
        n++;
    }

    // 2. If kind != MODULE_LIB and lib/ is present, add lib/ directory
    if (kind != MODULE_LIB && crate->has_lib) {
        const Module* lib_mod = &crate->modules[MODULE_LIB];
        if (lib_mod->dir_path[0] != '\0') {
            GROW_IF_NEEDED();
            result[n] = strdup(lib_mod->dir_path);
            if (!result[n]) goto fail;
            n++;
        }
    }

    // 3. If api/ is present, add api/ directory
    if (crate->has_api) {
        const Module* api_mod = &crate->modules[MODULE_API];
        if (api_mod->dir_path[0] != '\0') {
            GROW_IF_NEEDED();
            result[n] = strdup(api_mod->dir_path);
            if (!result[n]) goto fail;
            n++;
        }
    }

    // 3b. Add immediate subdirectories of api/ and lib/ as include paths.
    // This allows source files to use bare #include "header.h" for headers
    // in sibling subdirectories (matching the behavior of the old -Isrc/ layout
    // where GCC's same-directory lookup found co-located headers).
    {
        const char* dirs_to_scan[2] = {NULL, NULL};
        int dir_count = 0;

        if (crate->has_api && crate->modules[MODULE_API].dir_path[0] != '\0') {
            dirs_to_scan[dir_count++] = crate->modules[MODULE_API].dir_path;
        }
        if (crate->has_lib && crate->modules[MODULE_LIB].dir_path[0] != '\0') {
            dirs_to_scan[dir_count++] = crate->modules[MODULE_LIB].dir_path;
        }

        for (int di = 0; di < dir_count; di++) {
            const char* parent_dir = dirs_to_scan[di];

            size_t plen = strlen(parent_dir);
            // Normalize trailing slash
            char norm_parent[260];
            strncpy(norm_parent, parent_dir, sizeof(norm_parent) - 1);
            norm_parent[sizeof(norm_parent) - 1] = '\0';
            size_t nplen = strlen(norm_parent);
            if (nplen > 0 && norm_parent[nplen-1] != '/' && norm_parent[nplen-1] != '\\') {
                if (nplen < sizeof(norm_parent) - 1) {
                    norm_parent[nplen] = '/';
                    norm_parent[nplen+1] = '\0';
                    nplen++;
                }
            }

            (void)plen;

#ifdef _WIN32
            {
                char pattern[260];
                snprintf(pattern, sizeof(pattern), "%s*", norm_parent);
                WIN32_FIND_DATAA fd;
                HANDLE hFind = FindFirstFileA(pattern, &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                            strcmp(fd.cFileName, ".") != 0 &&
                            strcmp(fd.cFileName, "..") != 0) {
                            char subdir[260];
                            snprintf(subdir, sizeof(subdir), "%s%s", norm_parent, fd.cFileName);

                            GROW_IF_NEEDED();
                            result[n] = strdup(subdir);
                            if (!result[n]) goto fail;
                            n++;
                        }
                    } while (FindNextFileA(hFind, &fd));
                    FindClose(hFind);
                }
            }
#else
            {
                DIR* dir = opendir(norm_parent);
                if (dir) {
                    struct dirent* entry;
                    while ((entry = readdir(dir)) != NULL) {
                        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                            continue;
                        char subdir[260];
                        snprintf(subdir, sizeof(subdir), "%s%s", norm_parent, entry->d_name);
                        // Check if it's a directory
                        struct stat st;
                        if (stat(subdir, &st) == 0 && S_ISDIR(st.st_mode)) {
                            GROW_IF_NEEDED();
                            result[n] = strdup(subdir);
                            if (!result[n]) goto fail;
                            n++;
                        }
                    }
                    closedir(dir);
                }
            }
#endif
        }
    }

    // 4. Inter-crate dependencies: add target crate's api/ or fallback to lib/
    if (ws) {
        for (int i = 0; i < crate->dep_count; i++) {
            int dep_idx = crate->dep_indices[i];
            if (dep_idx < 0 || dep_idx >= ws->crate_count) continue;

            const Crate* dep = &ws->crates[dep_idx];

            if (dep->has_api) {
                const Module* dep_api = &dep->modules[MODULE_API];
                if (dep_api->dir_path[0] != '\0') {
                    GROW_IF_NEEDED();
                    result[n] = strdup(dep_api->dir_path);
                    if (!result[n]) goto fail;
                    n++;
                }
            } else if (dep->has_lib) {
                // No api/ — fallback to lib/ for header access
                const Module* dep_lib = &dep->modules[MODULE_LIB];
                if (dep_lib->dir_path[0] != '\0') {
                    GROW_IF_NEEDED();
                    result[n] = strdup(dep_lib->dir_path);
                    if (!result[n]) goto fail;
                    n++;
                }
            }
        }
    }

    #undef GROW_IF_NEEDED

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
