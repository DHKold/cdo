#ifndef CDO_COMMONS_CHECKSUM_H
#define CDO_COMMONS_CHECKSUM_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHECKSUM_SHA256,
    CHECKSUM_SHA384,
    CHECKSUM_SHA512,
} ChecksumAlgorithm;

typedef struct {
    ChecksumAlgorithm algorithm;
    char              hex_digest[129]; /* max 128 chars (sha512) + null */
} ChecksumSpec;

/// Parse a checksum string in "algorithm:hex_digest" format.
/// Returns 0 on success, non-zero if malformed.
int checksum_parse(const char* str, ChecksumSpec* out);

/// Validate a checksum spec (correct algorithm, correct digest length, valid hex chars).
/// Returns 0 if valid, non-zero if invalid.
int checksum_validate_format(const ChecksumSpec* spec);

/// Compute the hash of a byte buffer.
/// Writes the hex digest to out_hex (must be at least 129 bytes).
/// Returns 0 on success.
int checksum_compute(const void* data, size_t data_len,
                     ChecksumAlgorithm algorithm, char* out_hex);

/// Verify that a buffer's content matches the expected checksum.
/// Computes the hash of the data and compares to the expected digest.
/// Returns 0 if matches, non-zero if mismatch or error.
/// On mismatch, actual_hex is filled with the computed digest (must be at least 129 bytes).
int checksum_verify_buffer(const ChecksumSpec* expected,
                           const unsigned char* data, size_t data_len,
                           char* actual_hex);

/// Verify that a file matches the expected checksum.
/// Reads the file, computes the hash, and compares against expected.
/// On mismatch: deletes the file, reports expected vs actual, returns non-zero.
/// On success: returns 0.
/// On read error: returns non-zero without deleting.
int checksum_verify_file(const char* filepath, const ChecksumSpec* expected);

#ifdef __cplusplus
}
#endif

#endif /* CDO_COMMONS_CHECKSUM_H */
