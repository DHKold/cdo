#ifndef CDO_CORE_CATALOG_H
#define CDO_CORE_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CATALOG_MAX_NAME        128
#define CATALOG_MAX_VERSION     32
#define CATALOG_MAX_DESC        512
#define CATALOG_MAX_URL         512
#define CATALOG_MAX_CHECKSUM    128
#define CATALOG_MAX_ARRAY_ITEMS 64
#define CATALOG_MAX_PLATFORMS   6

/* --- Platform Triple --- */
typedef struct {
    char os[16];        /* "windows", "linux", "macos" */
    char arch[16];      /* "x86_64", "arm64" */
    char triple[32];    /* "windows-x86_64" */
} CatalogPlatform;

/* --- Platform-specific entry --- */
typedef struct {
    char triple[32];
    char url[CATALOG_MAX_URL];
    char checksum[CATALOG_MAX_CHECKSUM]; /* "sha256:abcdef..." or empty */
} CatalogPlatformEntry;

/* Maximum path length for source file tracking */
#define CATALOG_MAX_PATH 260

/* --- Tool Entry --- */
typedef struct {
    char name[CATALOG_MAX_NAME];
    char version[CATALOG_MAX_VERSION];
    char description[CATALOG_MAX_DESC];
    CatalogPlatformEntry platforms[CATALOG_MAX_PLATFORMS];
    int  platform_count;
    /* Internal fields for precedence/deduplication (not part of public API) */
    int  _precedence;               /* 0=workspace, 1=user-global, 2=built-in */
    char _source_file[CATALOG_MAX_PATH]; /* file path this entry was loaded from */
} CatalogToolEntry;

/* --- Package Entry --- */
typedef struct {
    char  name[CATALOG_MAX_NAME];
    char  version[CATALOG_MAX_VERSION];
    char  description[CATALOG_MAX_DESC];
    char* include_dirs[CATALOG_MAX_ARRAY_ITEMS];
    int   include_dir_count;
    char* link_libs[CATALOG_MAX_ARRAY_ITEMS];
    int   link_lib_count;
    char* defines[CATALOG_MAX_ARRAY_ITEMS];
    int   define_count;
    CatalogPlatformEntry platforms[CATALOG_MAX_PLATFORMS];
    int   platform_count;
    /* Internal fields for precedence/deduplication (not part of public API) */
    int   _precedence;               /* 0=workspace, 1=user-global, 2=built-in */
    char  _source_file[CATALOG_MAX_PATH]; /* file path this entry was loaded from */
} CatalogPackageEntry;

/* --- Loaded Catalog (aggregate of all sources) --- */
typedef struct {
    CatalogToolEntry*    tools;
    int                  tool_count;
    int                  tool_capacity;
    CatalogPackageEntry* packages;
    int                  package_count;
    int                  package_capacity;
} Catalog;

/* --- Resolution Result --- */
typedef struct {
    char url[CATALOG_MAX_URL];
    char checksum[CATALOG_MAX_CHECKSUM];
    char version[CATALOG_MAX_VERSION];
    /* Package-specific metadata (unused for tools) */
    char* include_dirs[CATALOG_MAX_ARRAY_ITEMS];
    int   include_dir_count;
    char* link_libs[CATALOG_MAX_ARRAY_ITEMS];
    int   link_lib_count;
    char* defines[CATALOG_MAX_ARRAY_ITEMS];
    int   define_count;
} CatalogResolveResult;

/* --- API --- */

/// Detect the current platform triple.
/// Returns 0 on success, non-zero if OS or arch is unsupported.
int catalog_detect_platform(CatalogPlatform* out);

/// Load all catalog files from the three search locations.
/// Applies precedence: workspace > user-global > built-in.
/// Returns 0 on success (even if no catalogs found — emits warning).
int catalog_load(Catalog* out, const char* workspace_root);

/// Resolve a tool by name and optional version constraint.
/// Returns 0 on success, non-zero on failure (no match, wrong platform).
int catalog_resolve_tool(const Catalog* cat, const char* name,
                         const char* version_constraint,
                         const CatalogPlatform* platform,
                         CatalogResolveResult* out);

/// Resolve a package by name and optional version constraint.
/// Returns 0 on success, non-zero on failure.
int catalog_resolve_package(const Catalog* cat, const char* name,
                            const char* version_constraint,
                            const CatalogPlatform* platform,
                            CatalogResolveResult* out);

/// Search catalog entries by query (case-insensitive substring on name/description).
/// Writes matching indices into out_indices. Returns the number of matches.
int catalog_search(const Catalog* cat, const char* query,
                   bool tools_only, bool packages_only,
                   int* out_tool_indices, int* tool_match_count,
                   int* out_pkg_indices, int* pkg_match_count);

/// Serialize a Catalog to TOML v1.0 text.
/// On success, *out_buf is heap-allocated (caller frees), *out_len is the byte length.
/// Returns 0 on success, non-zero on failure.
/// On failure: reports error via cdo_error(), sets *out_buf = NULL and *out_len = 0.
int catalog_serialize(const Catalog* cat, char** out_buf, size_t* out_len);

/// Free all heap memory in a Catalog struct.
void catalog_free(Catalog* cat);

/// Free heap memory in a CatalogResolveResult (include_dirs, link_libs, defines).
void catalog_resolve_result_free(CatalogResolveResult* result);

#ifdef __cplusplus
}
#endif

#endif /* CDO_CORE_CATALOG_H */
