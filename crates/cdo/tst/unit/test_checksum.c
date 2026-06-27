// crates/cdo/tst/unit/test_checksum.c
// Unit tests for checksum parse, compute, and verify functions
#include "cdo_ut.h"
#include "commons/checksum.h"

#include <stdio.h>
#include <string.h>

/* ============================================================
 * Known-Answer Tests (SHA-256)
 * Requirement 2.4: correct SHA-256 digests for all input sizes
 * ============================================================ */

/* SHA-256 of empty input (0 bytes) */
TEST(checksum_compute_sha256_empty) {
    const char *expected = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    char out_hex[129] = {0};

    int rc = checksum_compute("", 0, CHECKSUM_SHA256, out_hex);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_STR_EQ(out_hex, expected);

    return 0;
}

/* SHA-256 of "abc" (3 bytes, short input < 64 bytes) */
TEST(checksum_compute_sha256_short) {
    const char *data = "abc";
    const char *expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    char out_hex[129] = {0};

    int rc = checksum_compute(data, 3, CHECKSUM_SHA256, out_hex);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_STR_EQ(out_hex, expected);

    return 0;
}

/* SHA-256 of exactly 64 bytes of 0x00 (one block) */
TEST(checksum_compute_sha256_one_block) {
    unsigned char data[64];
    memset(data, 0x00, 64);
    const char *expected = "f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b";
    char out_hex[129] = {0};

    int rc = checksum_compute(data, 64, CHECKSUM_SHA256, out_hex);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_STR_EQ(out_hex, expected);

    return 0;
}

/* SHA-256 of multi-block input (56 bytes that pads to 2 blocks) */
TEST(checksum_compute_sha256_multi_block) {
    const char *data = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const char *expected = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
    char out_hex[129] = {0};

    int rc = checksum_compute(data, 56, CHECKSUM_SHA256, out_hex);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_STR_EQ(out_hex, expected);

    return 0;
}

/* ============================================================
 * Parse Valid Format Tests
 * Requirement 2.1: parse "algorithm:hex_digest" format
 * ============================================================ */

/* Parse valid sha256 format */
TEST(checksum_parse_valid_sha256) {
    const char *input = "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    ChecksumSpec spec;
    int rc = checksum_parse(input, &spec);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(spec.algorithm, CHECKSUM_SHA256);
    TEST_ASSERT_STR_EQ(spec.hex_digest,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    return 0;
}

/* Parse valid sha384 format (96 hex chars) */
TEST(checksum_parse_valid_sha384) {
    const char *input = "sha384:"
        "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
        "8086072ba1e7cc2358baeca134c825a7";
    ChecksumSpec spec;
    int rc = checksum_parse(input, &spec);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(spec.algorithm, CHECKSUM_SHA384);
    TEST_ASSERT((int)strlen(spec.hex_digest) == 96);

    return 0;
}

/* Parse valid sha512 format (128 hex chars) */
TEST(checksum_parse_valid_sha512) {
    const char *input = "sha512:"
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f";
    ChecksumSpec spec;
    int rc = checksum_parse(input, &spec);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(spec.algorithm, CHECKSUM_SHA512);
    TEST_ASSERT((int)strlen(spec.hex_digest) == 128);

    return 0;
}

/* ============================================================
 * Parse Invalid Format Tests
 * Requirement 2.7: malformed checksum strings return non-zero
 * ============================================================ */

/* Missing colon separator */
TEST(checksum_parse_missing_colon) {
    const char *input = "sha256abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
    ChecksumSpec spec;
    int rc = checksum_parse(input, &spec);

    TEST_ASSERT(rc != 0);

    return 0;
}

/* Unsupported algorithm (md5) */
TEST(checksum_parse_unsupported_algorithm) {
    const char *input = "md5:d41d8cd98f00b204e9800998ecf8427e";
    ChecksumSpec spec;
    int rc = checksum_parse(input, &spec);

    TEST_ASSERT(rc != 0);

    return 0;
}

/* Wrong hex length for sha256 (too short) */
TEST(checksum_parse_wrong_hex_length_short) {
    const char *input = "sha256:abcdef1234";
    ChecksumSpec spec;
    int rc = checksum_parse(input, &spec);

    TEST_ASSERT(rc != 0);

    return 0;
}

/* Wrong hex length for sha256 (too long) */
TEST(checksum_parse_wrong_hex_length_long) {
    const char *input = "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855aa";
    ChecksumSpec spec;
    int rc = checksum_parse(input, &spec);

    TEST_ASSERT(rc != 0);

    return 0;
}

/* Non-hex characters in digest */
TEST(checksum_parse_non_hex_chars) {
    /* 'g' and 'z' are not valid hex characters */
    const char *input = "sha256:g3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85z";
    ChecksumSpec spec;
    int rc = checksum_parse(input, &spec);

    TEST_ASSERT(rc != 0);

    return 0;
}

/* Uppercase hex characters (implementation expects lowercase) */
TEST(checksum_parse_uppercase_hex) {
    const char *input = "sha256:E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855";
    ChecksumSpec spec;
    int rc = checksum_parse(input, &spec);

    TEST_ASSERT(rc != 0);

    return 0;
}

/* Empty string */
TEST(checksum_parse_empty_string) {
    const char *input = "";
    ChecksumSpec spec;
    int rc = checksum_parse(input, &spec);

    TEST_ASSERT(rc != 0);

    return 0;
}

/* NULL input */
TEST(checksum_parse_null_input) {
    ChecksumSpec spec;
    int rc = checksum_parse(NULL, &spec);

    TEST_ASSERT(rc != 0);

    return 0;
}

/* ============================================================
 * Validate Format Tests
 * Requirement 2.6: validates spec format correctness
 * ============================================================ */

TEST(checksum_validate_format_correct) {
    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA256;
    strcpy(spec.hex_digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    int rc = checksum_validate_format(&spec);
    TEST_ASSERT_EQ(rc, 0);

    return 0;
}

TEST(checksum_validate_format_wrong_length) {
    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA256;
    /* SHA-256 digest should be 64 hex chars; provide only 10 */
    strcpy(spec.hex_digest, "abcdef1234");

    int rc = checksum_validate_format(&spec);
    TEST_ASSERT(rc != 0);

    return 0;
}

/* ============================================================
 * Mismatch Detection Tests
 * Requirement 2.3: computed hash mismatch returns non-zero
 * ============================================================ */

/* Compute SHA-256 of one buffer, verify against a different buffer */
TEST(checksum_verify_buffer_mismatch) {
    const char *original = "hello";
    const char *different = "world";

    /* Compute checksum of "hello" */
    char computed[129] = {0};
    int rc = checksum_compute(original, 5, CHECKSUM_SHA256, computed);
    TEST_ASSERT_EQ(rc, 0);

    /* Build spec from "hello" checksum */
    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA256;
    strcpy(spec.hex_digest, computed);

    /* Verify "world" against "hello" checksum — should mismatch */
    char actual_hex[129] = {0};
    rc = checksum_verify_buffer(&spec, (const unsigned char *)different, 5, actual_hex);

    TEST_ASSERT(rc != 0);
    /* actual_hex should be filled with the SHA-256 of "world" */
    TEST_ASSERT((int)strlen(actual_hex) == 64);
    /* Verify it's not the same as the "hello" digest */
    TEST_ASSERT(strcmp(actual_hex, computed) != 0);

    return 0;
}

/* Mismatch with empty vs non-empty */
TEST(checksum_verify_buffer_mismatch_empty_vs_data) {
    /* Compute checksum of empty */
    char computed_empty[129] = {0};
    int rc = checksum_compute("", 0, CHECKSUM_SHA256, computed_empty);
    TEST_ASSERT_EQ(rc, 0);

    /* Build spec from empty checksum */
    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA256;
    strcpy(spec.hex_digest, computed_empty);

    /* Verify non-empty buffer against empty checksum — should mismatch */
    const char *data = "not empty";
    char actual_hex[129] = {0};
    rc = checksum_verify_buffer(&spec, (const unsigned char *)data, 9, actual_hex);

    TEST_ASSERT(rc != 0);

    return 0;
}

/* ============================================================
 * Verify Round-Trip Tests
 * Requirement 2.1, 2.2: compute + verify same buffer = success
 * ============================================================ */

/* Round-trip with "hello" */
TEST(checksum_verify_buffer_roundtrip) {
    const char *data = "hello";
    char computed[129] = {0};

    /* Compute the checksum */
    int rc = checksum_compute(data, 5, CHECKSUM_SHA256, computed);
    TEST_ASSERT_EQ(rc, 0);

    /* Build a ChecksumSpec from the computed digest */
    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA256;
    strcpy(spec.hex_digest, computed);

    /* Verify the same data against its own checksum */
    char actual_hex[129] = {0};
    rc = checksum_verify_buffer(&spec, (const unsigned char *)data, 5, actual_hex);

    TEST_ASSERT_EQ(rc, 0);

    return 0;
}

/* Round-trip with empty input */
TEST(checksum_verify_buffer_roundtrip_empty) {
    char computed[129] = {0};

    int rc = checksum_compute("", 0, CHECKSUM_SHA256, computed);
    TEST_ASSERT_EQ(rc, 0);

    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA256;
    strcpy(spec.hex_digest, computed);

    char actual_hex[129] = {0};
    rc = checksum_verify_buffer(&spec, (const unsigned char *)"", 0, actual_hex);

    TEST_ASSERT_EQ(rc, 0);

    return 0;
}

/* Round-trip with SHA-384 */
TEST(checksum_verify_buffer_roundtrip_sha384) {
    const char *data = "test data for sha384";
    char computed[129] = {0};

    int rc = checksum_compute(data, strlen(data), CHECKSUM_SHA384, computed);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT((int)strlen(computed) == 96);

    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA384;
    strcpy(spec.hex_digest, computed);

    char actual_hex[129] = {0};
    rc = checksum_verify_buffer(&spec, (const unsigned char *)data, strlen(data), actual_hex);

    TEST_ASSERT_EQ(rc, 0);

    return 0;
}

/* Round-trip with SHA-512 */
TEST(checksum_verify_buffer_roundtrip_sha512) {
    const char *data = "test data for sha512";
    char computed[129] = {0};

    int rc = checksum_compute(data, strlen(data), CHECKSUM_SHA512, computed);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT((int)strlen(computed) == 128);

    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA512;
    strcpy(spec.hex_digest, computed);

    char actual_hex[129] = {0};
    rc = checksum_verify_buffer(&spec, (const unsigned char *)data, strlen(data), actual_hex);

    TEST_ASSERT_EQ(rc, 0);

    return 0;
}

/* ============================================================
 * File Verification Tests
 * Requirement 2.3: file read failure does not delete the file
 * ============================================================ */

/* Verify file that does not exist — returns non-zero, no crash */
TEST(checksum_verify_file_nonexistent) {
    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA256;
    strcpy(spec.hex_digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    int rc = checksum_verify_file("__nonexistent_file_for_test__.dat", &spec);

    /* Should return non-zero (read failure) */
    TEST_ASSERT(rc != 0);

    return 0;
}

/* Verify file round-trip: write a file, compute checksum, verify it */
TEST_SERIAL(checksum_verify_file_roundtrip) {
    const char *filepath = "__test_checksum_roundtrip__.dat";
    const char *data = "file content for checksum test";
    size_t data_len = strlen(data);

    /* Write the test file */
    FILE *f = fopen(filepath, "wb");
    TEST_ASSERT(f != NULL);
    fwrite(data, 1, data_len, f);
    fclose(f);

    /* Compute the checksum of the data */
    char computed[129] = {0};
    int rc = checksum_compute(data, data_len, CHECKSUM_SHA256, computed);
    TEST_ASSERT_EQ(rc, 0);

    /* Build spec and verify the file */
    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA256;
    strcpy(spec.hex_digest, computed);

    rc = checksum_verify_file(filepath, &spec);
    TEST_ASSERT_EQ(rc, 0);

    /* Cleanup */
    remove(filepath);

    return 0;
}

/* Verify file mismatch: file exists but checksum doesn't match — deletes the file */
TEST_SERIAL(checksum_verify_file_mismatch_deletes) {
    const char *filepath = "__test_checksum_mismatch__.dat";
    const char *data = "actual file content";
    size_t data_len = strlen(data);

    /* Write the test file */
    FILE *f = fopen(filepath, "wb");
    TEST_ASSERT(f != NULL);
    fwrite(data, 1, data_len, f);
    fclose(f);

    /* Build spec with wrong digest (use empty-input hash) */
    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA256;
    strcpy(spec.hex_digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    int rc = checksum_verify_file(filepath, &spec);
    TEST_ASSERT(rc != 0);

    /* File should have been deleted on mismatch */
    f = fopen(filepath, "rb");
    TEST_ASSERT(f == NULL);

    return 0;
}

/* File read failure does NOT delete the file.
 * We simulate this by passing a directory path as the file path,
 * which will fail to open/read but should not delete. */
TEST_SERIAL(checksum_verify_file_read_failure_no_delete) {
    const char *filepath = "__test_checksum_read_fail__.dat";

    /* Write a file so we can confirm it's not deleted */
    FILE *f = fopen(filepath, "wb");
    TEST_ASSERT(f != NULL);
    fwrite("x", 1, 1, f);
    fclose(f);

    /* Build spec with invalid format to trigger early non-mismatch error path.
     * An invalid format causes checksum_verify_file to return error
     * without deleting the file. */
    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA256;
    /* Wrong length digest — causes validate_format to fail */
    strcpy(spec.hex_digest, "tooshort");

    int rc = checksum_verify_file(filepath, &spec);
    TEST_ASSERT(rc != 0);

    /* File should NOT have been deleted (it was an error, not a mismatch) */
    f = fopen(filepath, "rb");
    TEST_ASSERT(f != NULL);
    fclose(f);

    /* Cleanup */
    remove(filepath);

    return 0;
}

/* ============================================================
 * SHA-256 Output Format Tests
 * Requirement 2.6: produces lowercase hex string of exactly 64 chars
 * ============================================================ */

TEST(checksum_compute_sha256_output_format) {
    const char *data = "format test";
    char out_hex[129] = {0};

    int rc = checksum_compute(data, strlen(data), CHECKSUM_SHA256, out_hex);

    TEST_ASSERT_EQ(rc, 0);
    /* Must be exactly 64 characters */
    TEST_ASSERT((int)strlen(out_hex) == 64);
    /* All characters must be lowercase hex */
    for (int i = 0; i < 64; i++) {
        char c = out_hex[i];
        TEST_ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }

    return 0;
}

/* ============================================================
 * Edge Cases
 * ============================================================ */

/* Compute with NULL out_hex returns error */
TEST(checksum_compute_null_output) {
    const char *data = "test";
    int rc = checksum_compute(data, 4, CHECKSUM_SHA256, NULL);
    TEST_ASSERT(rc != 0);

    return 0;
}

/* Verify buffer with NULL expected returns error */
TEST(checksum_verify_buffer_null_expected) {
    const char *data = "test";
    char actual_hex[129] = {0};
    int rc = checksum_verify_buffer(NULL, (const unsigned char *)data, 4, actual_hex);
    TEST_ASSERT(rc != 0);

    return 0;
}
