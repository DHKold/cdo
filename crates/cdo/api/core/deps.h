#ifndef CDO_CORE_DEPS_H
#define CDO_CORE_DEPS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Dependency source types.
 */
typedef enum {
    DEP_REGISTRY,
    DEP_GIT,
    DEP_LOCAL,
} DepSourceKind;

/**
 * Metadata format provided by a resolved dependency.
 * Determines how the build system discovers include/lib paths.
 */
typedef enum {
    DEP_META_NONE,          /* No metadata detected (use directory layout convention) */
    DEP_META_PKGCONFIG,     /* Provides .pc files for pkg-config */
    DEP_META_CMAKE,         /* Provides CMake package config (*Config.cmake / *-config.cmake) */
    DEP_META_CDO_NATIVE,    /* Provides cdo-package.toml (CDo-native metadata) */
} DepMetadataKind;

/**
 * Specification of a single dependency (from manifest or lock file).
 */
typedef struct {
    char            name[64];
    char            version[32];
    DepSourceKind   source;
    char            url[512];       /* registry URL, git URL, or local path */
    char            git_ref[128];   /* tag, branch, or commit (for git deps) */
    char            checksum[128];  /* e.g. "sha256:abcdef1234..." */
    DepMetadataKind metadata_kind;  /* How this dep provides build metadata */
} DepSpec;

/**
 * A resolved dependency with paths and link information ready for the build.
 */
typedef struct {
    char            include_dir[260];
    char            lib_dir[260];
    char**          link_libs;
    int             link_lib_count;
    char**          runtime_dlls;
    int             runtime_dll_count;
} ResolvedDep;

/**
 * Resolve a dependency. Checks local cache first, downloads if needed.
 *
 * For DEP_REGISTRY: downloads the archive from spec->url and extracts it.
 * For DEP_GIT: clones the repository at the specified ref.
 * For DEP_LOCAL: uses the local path directly (no caching).
 *
 * Populates `out` with include_dir, lib_dir, link_libs, and runtime_dlls
 * based on the resolved dependency layout.
 *
 * @param spec      Dependency specification.
 * @param cache_dir Base cache directory (e.g., "~/.cdo/cache/").
 * @param out       Filled with resolved paths and link info on success.
 * @return 0 on success, non-zero on failure.
 */
int dep_resolve(const DepSpec* spec, const char* cache_dir, ResolvedDep* out);

/**
 * Free resources allocated inside a ResolvedDep.
 */
void dep_resolved_free(ResolvedDep* dep);

/**
 * Write a lock file from a set of dependency specs.
 * (Stub — full implementation in task 15.2)
 *
 * @return 0 on success, non-zero on failure.
 */
int dep_lock_write(const char* lock_path, const DepSpec* specs, int count);

/**
 * Read a lock file into a set of dependency specs.
 * Caller frees the returned specs array.
 * (Stub — full implementation in task 15.2)
 *
 * @return 0 on success, non-zero on failure.
 */
int dep_lock_read(const char* lock_path, DepSpec** specs, int* count);

/**
 * Detect what metadata format a resolved dependency provides by probing
 * its directory structure. Checks for (in priority order):
 *   1. CDo-native: cdo-package.toml in the root
 *   2. CMake: *Config.cmake or *-config.cmake in lib/cmake/ or share/cmake/
 *   3. pkg-config: *.pc files in lib/pkgconfig/ or share/pkgconfig/
 *
 * @param dep_path  Root directory of the resolved dependency.
 * @return The detected metadata kind (DEP_META_NONE if nothing found).
 */
DepMetadataKind dep_detect_metadata(const char* dep_path);

#ifdef __cplusplus
}
#endif

#endif /* CDO_CORE_DEPS_H */
