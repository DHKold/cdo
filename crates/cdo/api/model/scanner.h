#ifndef CDO_MODEL_SCANNER_H
#define CDO_MODEL_SCANNER_H

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration to avoid circular include with workspace.h
typedef struct Crate Crate;

/**
 * Dynamic list of file paths discovered by the scanner.
 */
typedef struct {
    char**  paths;
    int     count;
    int     capacity;
} FileList;

/**
 * Scan a crate's src/ directory for source files (.c, .cpp, .h, .hpp).
 * Excludes files matching any of the provided glob patterns.
 *
 * @param crate_path      Root path of the crate (containing src/ directory)
 * @param exclude_patterns Array of glob patterns to exclude (relative to crate root)
 * @param exclude_count   Number of exclude patterns
 * @param out             Output file list (caller must free with filelist_free)
 * @return 0 on success, non-zero on error
 */
int scanner_scan_sources(const char* crate_path, const char** exclude_patterns,
                         int exclude_count, FileList* out);

/**
 * Scan a crate's include/ directory for header files (.h, .hpp).
 *
 * @param crate_path  Root path of the crate (containing include/ directory)
 * @param out         Output file list (caller must free with filelist_free)
 * @return 0 on success, non-zero on error
 */
int scanner_scan_headers(const char* crate_path, FileList* out);

/**
 * Scan a crate directory for module subdirectories and their source files.
 * Populates the crate's modules[] array based on which well-known
 * directories (lib/, exe/, dyn/, tst/, api/) exist.
 *
 * @param crate_path        Root path of the crate directory
 * @param crate             Crate struct to populate (modules[], module_count, has_lib, has_api)
 * @param exclude_patterns  Array of glob patterns to exclude (may be NULL)
 * @param exclude_count     Number of exclude patterns
 * @return 0 on success, non-zero on error (e.g., no modules found)
 */
int scanner_scan_modules(const char* crate_path, Crate* crate,
                         const char** exclude_patterns, int exclude_count);

/**
 * Scan a single module directory for source files recursively.
 * For MODULE_API (kind == 4), only scans for header files (.h, .hpp).
 * For all other module kinds, scans for compilable source files (.c, .cpp).
 * Applies exclude patterns relative to the module directory root.
 *
 * @param module_dir        Absolute path to the module directory (e.g., <crate>/lib/)
 * @param kind              Module kind (ModuleKind enum value cast to int)
 * @param exclude_patterns  Array of glob patterns to exclude (relative to module dir, may be NULL)
 * @param exclude_count     Number of exclude patterns
 * @param out               Output file list (caller must free with filelist_free)
 * @return 0 on success, non-zero on error
 */
int scanner_scan_module_sources(const char* module_dir, int kind,
                                const char** exclude_patterns,
                                int exclude_count, FileList* out);

/**
 * Free all memory associated with a FileList.
 */
void filelist_free(FileList* fl);

#ifdef __cplusplus
}
#endif

#endif // CDO_MODEL_SCANNER_H
