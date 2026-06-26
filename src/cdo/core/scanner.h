#ifndef CDO_SCANNER_H
#define CDO_SCANNER_H

#ifdef __cplusplus
extern "C" {
#endif

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
 * Free all memory associated with a FileList.
 */
void filelist_free(FileList* fl);

#ifdef __cplusplus
}
#endif

#endif // CDO_SCANNER_H
