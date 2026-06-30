#include "model/deps.h"
#include "core/log.h"
#include "commons/http.h"
#include "commons/archive.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Internal helpers ---------- */

/**
 * Build the cache path for a dependency: <cache_dir>/<name>-<version>/
 * Returns 0 on success, non-zero if the path would overflow.
 */
static int dep_cache_path(char* dest, size_t dest_size,
                          const char* cache_dir,
                          const char* name, const char* version)
{
    char rel[128];
    int n = snprintf(rel, sizeof(rel), "%s-%s", name, version);
    if (n < 0 || (size_t)n >= sizeof(rel)) return -1;

    return pal_path_join(dest, dest_size, cache_dir, rel);
}

/**
 * Check whether a cache directory looks valid (exists and has content).
 * Returns 1 if usable, 0 if not.
 */
static int dep_cache_valid(const char* cache_path)
{
    return (pal_path_exists(cache_path) == 0);
}

/**
 * Determine the archive extension from a URL.
 * Returns pointer to ".zip", ".tar.gz", or ".tgz" within url, or NULL.
 */
static const char* url_archive_ext(const char* url)
{
    size_t len = strlen(url);
    if (len >= 7 && strcmp(url + len - 7, ".tar.gz") == 0) return ".tar.gz";
    if (len >= 4 && strcmp(url + len - 4, ".tgz") == 0)    return ".tgz";
    if (len >= 4 && strcmp(url + len - 4, ".zip") == 0)     return ".zip";
    return NULL;
}

/**
 * Download a registry dependency: fetch archive, extract to cache.
 */
static int dep_fetch_registry(const DepSpec* spec, const char* cache_path)
{
    /* Determine archive extension and temp download path */
    const char* ext = url_archive_ext(spec->url);
    if (!ext) {
        /* Default to .zip if extension cannot be determined */
        ext = ".zip";
    }

    char archive_path[520];
    int n = snprintf(archive_path, sizeof(archive_path), "%s%s", cache_path, ext);
    if (n < 0 || (size_t)n >= sizeof(archive_path)) return -1;

    cdo_log_info("Downloading %s %s ...", spec->name, spec->version);

    /* Download the archive */
    int rc = http_download(spec->url, archive_path, 3, NULL, NULL);
    if (rc != 0) {
        cdo_log_error("Failed to download dependency '%s' from %s", spec->name, spec->url);
        return rc;
    }

    /* Create the cache directory */
    rc = pal_mkdir_p(cache_path);
    if (rc != 0) {
        cdo_log_error("Failed to create cache directory: %s", cache_path);
        return rc;
    }

    /* Extract the archive */
    cdo_log_debug("Extracting %s to %s", archive_path, cache_path);
    if (strcmp(ext, ".zip") == 0) {
        rc = archive_extract_zip(archive_path, cache_path);
    } else {
        /* .tar.gz or .tgz */
        rc = archive_extract_targz(archive_path, cache_path);
    }

    if (rc != 0) {
        cdo_log_error("Failed to extract archive for dependency '%s'", spec->name);
    }

    return rc;
}

/**
 * Clone a git repository dependency to the cache path.
 */
static int dep_fetch_git(const DepSpec* spec, const char* cache_path)
{
    cdo_log_info("Cloning %s (ref: %s) ...", spec->name,
             spec->git_ref[0] ? spec->git_ref : "HEAD");

    /* Build git clone args */
    const char* args[16];
    int argc = 0;
    args[argc++] = "clone";
    args[argc++] = "--depth";
    args[argc++] = "1";
    if (spec->git_ref[0]) {
        args[argc++] = "--branch";
        args[argc++] = spec->git_ref;
    }
    args[argc++] = spec->url;
    args[argc++] = cache_path;

    PalSpawnOpts opts = {0};
    opts.program = "git";
    opts.args = args;
    opts.arg_count = argc;
    opts.capture_output = true;

    PalSpawnResult result = {0};
    int rc = pal_spawn(&opts, &result);
    if (rc != 0 || result.exit_code != 0) {
        cdo_log_error("Git clone failed for dependency '%s'", spec->name);
        if (result.stderr_buf) {
            cdo_log_error("  %s", result.stderr_buf);
        }
        pal_spawn_result_free(&result);
        return -1;
    }

    pal_spawn_result_free(&result);
    return 0;
}

/* Context for directory scanning callbacks */
typedef struct {
    char**  entries;
    int     count;
    int     capacity;
} ScanCtx;

static void scan_libs_cb(const char* path, bool is_dir, void* ctx)
{
    if (is_dir) return;
    ScanCtx* sc = (ScanCtx*)ctx;

    const char* ext = pal_path_ext(path);
    if (!ext) return;

    /* Check for static library extensions */
    bool is_lib = (strcmp(ext, ".a") == 0 || strcmp(ext, ".lib") == 0);
    if (!is_lib) return;

    /* Grow array if needed */
    if (sc->count >= sc->capacity) {
        int new_cap = sc->capacity == 0 ? 8 : sc->capacity * 2;
        char** new_arr = (char**)realloc(sc->entries, sizeof(char*) * (size_t)new_cap);
        if (!new_arr) return;
        sc->entries = new_arr;
        sc->capacity = new_cap;
    }

    /* Extract library name from the file path */
    const char* filename = path;
    const char* sep = strrchr(path, '/');
    if (sep) filename = sep + 1;
#ifdef _WIN32
    const char* bsep = strrchr(path, '\\');
    if (bsep && bsep > filename - 1) filename = bsep + 1;
#endif

    /* Strip "lib" prefix and extension to get link name */
    char lib_name[128];
    size_t fname_len = strlen(filename);
    size_t ext_len = strlen(ext);
    size_t base_len = fname_len - ext_len;

    const char* start = filename;
    if (base_len > 3 && strncmp(filename, "lib", 3) == 0) {
        start = filename + 3;
        base_len -= 3;
    }

    if (base_len >= sizeof(lib_name)) base_len = sizeof(lib_name) - 1;
    memcpy(lib_name, start, base_len);
    lib_name[base_len] = '\0';

    sc->entries[sc->count] = (char*)malloc(strlen(lib_name) + 1);
    if (sc->entries[sc->count]) {
        strcpy(sc->entries[sc->count], lib_name);
        sc->count++;
    }
}

static void scan_dlls_cb(const char* path, bool is_dir, void* ctx)
{
    if (is_dir) return;
    ScanCtx* sc = (ScanCtx*)ctx;

    const char* ext = pal_path_ext(path);
    if (!ext) return;

    bool is_dll = (strcmp(ext, ".dll") == 0 || strcmp(ext, ".so") == 0);
    if (!is_dll) return;

    /* Grow array if needed */
    if (sc->count >= sc->capacity) {
        int new_cap = sc->capacity == 0 ? 8 : sc->capacity * 2;
        char** new_arr = (char**)realloc(sc->entries, sizeof(char*) * (size_t)new_cap);
        if (!new_arr) return;
        sc->entries = new_arr;
        sc->capacity = new_cap;
    }

    sc->entries[sc->count] = (char*)malloc(strlen(path) + 1);
    if (sc->entries[sc->count]) {
        strcpy(sc->entries[sc->count], path);
        sc->count++;
    }
}

/**
 * Populate a ResolvedDep from a resolved dependency path by scanning
 * for include/, lib/, and bin/ subdirectories.
 */
static int dep_populate_resolved(const char* dep_path, ResolvedDep* out)
{
    memset(out, 0, sizeof(ResolvedDep));

    /* Look for include/ subdirectory */
    char include_path[520];
    if (pal_path_join(include_path, sizeof(include_path), dep_path, "include") == 0) {
        if (pal_path_exists(include_path) == 0) {
            strncpy(out->include_dir, include_path, sizeof(out->include_dir) - 1);
        }
    }

    /* If no include/ dir, use the dep_path itself as include dir */
    if (out->include_dir[0] == '\0') {
        strncpy(out->include_dir, dep_path, sizeof(out->include_dir) - 1);
    }

    /* Look for lib/ subdirectory */
    char lib_path[520];
    if (pal_path_join(lib_path, sizeof(lib_path), dep_path, "lib") == 0) {
        if (pal_path_exists(lib_path) == 0) {
            strncpy(out->lib_dir, lib_path, sizeof(out->lib_dir) - 1);
        }
    }

    /* Scan for static libraries in lib/ */
    if (out->lib_dir[0] != '\0') {
        ScanCtx lib_ctx = {0};
        pal_dir_walk(out->lib_dir, scan_libs_cb, &lib_ctx);
        out->link_libs = lib_ctx.entries;
        out->link_lib_count = lib_ctx.count;
    }

    /* Scan for runtime DLLs in bin/ first, then lib/ */
    char bin_path[520];
    ScanCtx dll_ctx = {0};

    if (pal_path_join(bin_path, sizeof(bin_path), dep_path, "bin") == 0) {
        if (pal_path_exists(bin_path) == 0) {
            pal_dir_walk(bin_path, scan_dlls_cb, &dll_ctx);
        }
    }

    /* Also check lib/ for shared libraries */
    if (out->lib_dir[0] != '\0') {
        pal_dir_walk(out->lib_dir, scan_dlls_cb, &dll_ctx);
    }

    out->runtime_dlls = dll_ctx.entries;
    out->runtime_dll_count = dll_ctx.count;

    return 0;
}

/* ---------- Public API ---------- */

int dep_resolve(const DepSpec* spec, const char* cache_dir, ResolvedDep* out)
{
    if (!spec || !out) return -1;

    memset(out, 0, sizeof(ResolvedDep));

    /* For local dependencies, just point to the local path directly */
    if (spec->source == DEP_LOCAL) {
        cdo_log_debug("Resolving local dependency '%s' at '%s'", spec->name, spec->url);
        return dep_populate_resolved(spec->url, out);
    }

    /* Compute cache key path */
    if (!cache_dir || cache_dir[0] == '\0') {
        cdo_log_error("No cache directory specified for dependency '%s'", spec->name);
        return -1;
    }

    char cache_path[520];
    if (dep_cache_path(cache_path, sizeof(cache_path),
                       cache_dir, spec->name, spec->version) != 0) {
        cdo_log_error("Cache path too long for dependency '%s'", spec->name);
        return -1;
    }

    /* Check if already cached */
    if (dep_cache_valid(cache_path)) {
        cdo_log_debug("Using cached dependency '%s' at '%s'", spec->name, cache_path);
        return dep_populate_resolved(cache_path, out);
    }

    /* Not cached â€” fetch based on source kind */
    int rc;
    switch (spec->source) {
        case DEP_REGISTRY:
            rc = dep_fetch_registry(spec, cache_path);
            break;
        case DEP_GIT:
            rc = dep_fetch_git(spec, cache_path);
            break;
        default:
            cdo_log_error("Unknown dependency source kind for '%s'", spec->name);
            return -1;
    }

    if (rc != 0) return rc;

    /* Populate resolved info from the newly fetched cache */
    return dep_populate_resolved(cache_path, out);
}

void dep_resolved_free(ResolvedDep* dep)
{
    if (!dep) return;

    if (dep->link_libs) {
        for (int i = 0; i < dep->link_lib_count; i++) {
            free(dep->link_libs[i]);
        }
        free(dep->link_libs);
        dep->link_libs = NULL;
        dep->link_lib_count = 0;
    }

    if (dep->runtime_dlls) {
        for (int i = 0; i < dep->runtime_dll_count; i++) {
            free(dep->runtime_dlls[i]);
        }
        free(dep->runtime_dlls);
        dep->runtime_dlls = NULL;
        dep->runtime_dll_count = 0;
    }
}

/* ---------- Metadata Detection ---------- */

/* Callback context for detecting specific file patterns */
typedef struct {
    bool found;
    const char* ext;        /* Extension to look for (e.g. ".pc", ".cmake") */
    const char* suffix;     /* Filename suffix pattern (e.g. "Config.cmake") */
} MetaDetectCtx;

static void detect_file_cb(const char* path, bool is_dir, void* ctx)
{
    if (is_dir) return;
    MetaDetectCtx* dc = (MetaDetectCtx*)ctx;
    if (dc->found) return;  /* Short-circuit once found */

    const char* filename = path;
    const char* sep = strrchr(path, '/');
    if (sep) filename = sep + 1;
#ifdef _WIN32
    const char* bsep = strrchr(path, '\\');
    if (bsep && bsep > sep) filename = bsep + 1;
#endif

    if (dc->ext) {
        const char* ext = pal_path_ext(path);
        if (ext && strcmp(ext, dc->ext) == 0) {
            dc->found = true;
        }
    }

    if (dc->suffix) {
        size_t fname_len = strlen(filename);
        size_t suffix_len = strlen(dc->suffix);
        if (fname_len >= suffix_len) {
            if (strcmp(filename + fname_len - suffix_len, dc->suffix) == 0) {
                dc->found = true;
            }
        }
    }
}

DepMetadataKind dep_detect_metadata(const char* dep_path)
{
    if (!dep_path) return DEP_META_NONE;

    /* 1. Check for CDo-native: cdo-package.toml in the root */
    char probe_path[520];
    if (pal_path_join(probe_path, sizeof(probe_path), dep_path, "cdo-package.toml") == 0) {
        if (pal_path_exists(probe_path) == 0) {
            return DEP_META_CDO_NATIVE;
        }
    }

    /* 2. Check for CMake package config in lib/cmake/ or share/cmake/ */
    {
        char cmake_dir[520];
        MetaDetectCtx ctx = { .found = false, .ext = NULL, .suffix = "Config.cmake" };

        if (pal_path_join(cmake_dir, sizeof(cmake_dir), dep_path, "lib/cmake") == 0) {
            if (pal_path_exists(cmake_dir) == 0) {
                pal_dir_walk(cmake_dir, detect_file_cb, &ctx);
                if (ctx.found) return DEP_META_CMAKE;
            }
        }

        /* Also check for -config.cmake variant */
        ctx.suffix = "-config.cmake";
        if (pal_path_join(cmake_dir, sizeof(cmake_dir), dep_path, "lib/cmake") == 0) {
            if (pal_path_exists(cmake_dir) == 0) {
                pal_dir_walk(cmake_dir, detect_file_cb, &ctx);
                if (ctx.found) return DEP_META_CMAKE;
            }
        }

        /* Check share/cmake/ */
        ctx.found = false;
        ctx.suffix = "Config.cmake";
        if (pal_path_join(cmake_dir, sizeof(cmake_dir), dep_path, "share/cmake") == 0) {
            if (pal_path_exists(cmake_dir) == 0) {
                pal_dir_walk(cmake_dir, detect_file_cb, &ctx);
                if (ctx.found) return DEP_META_CMAKE;
            }
        }

        ctx.suffix = "-config.cmake";
        if (pal_path_join(cmake_dir, sizeof(cmake_dir), dep_path, "share/cmake") == 0) {
            if (pal_path_exists(cmake_dir) == 0) {
                pal_dir_walk(cmake_dir, detect_file_cb, &ctx);
                if (ctx.found) return DEP_META_CMAKE;
            }
        }
    }

    /* 3. Check for pkg-config: *.pc in lib/pkgconfig/ or share/pkgconfig/ */
    {
        char pc_dir[520];
        MetaDetectCtx ctx = { .found = false, .ext = ".pc", .suffix = NULL };

        if (pal_path_join(pc_dir, sizeof(pc_dir), dep_path, "lib/pkgconfig") == 0) {
            if (pal_path_exists(pc_dir) == 0) {
                pal_dir_walk(pc_dir, detect_file_cb, &ctx);
                if (ctx.found) return DEP_META_PKGCONFIG;
            }
        }

        ctx.found = false;
        if (pal_path_join(pc_dir, sizeof(pc_dir), dep_path, "share/pkgconfig") == 0) {
            if (pal_path_exists(pc_dir) == 0) {
                pal_dir_walk(pc_dir, detect_file_cb, &ctx);
                if (ctx.found) return DEP_META_PKGCONFIG;
            }
        }
    }

    return DEP_META_NONE;
}
