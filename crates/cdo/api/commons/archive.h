#ifndef CDO_COMMONS_ARCHIVE_H
#define CDO_COMMONS_ARCHIVE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Extract a ZIP archive to dest_dir. Preserves directory structure.
 * Supports STORE (method 0) and DEFLATE (method 8) compression.
 * Returns 0 on success, non-zero on error.
 */
int archive_extract_zip(const char* archive_path, const char* dest_dir);

/**
 * Extract a tar.gz archive to dest_dir.
 * Returns 0 on success, non-zero on error.
 */
int archive_extract_targz(const char* archive_path, const char* dest_dir);

#ifdef __cplusplus
}
#endif

#endif /* CDO_COMMONS_ARCHIVE_H */
