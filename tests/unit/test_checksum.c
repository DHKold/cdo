// crates/cdo_pbt/src/unit/test_checksum.c
#include "test_harness.h"
#include "core/checksum.h"

/*
 * Known SHA-256 values:
 *   empty string → e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
 *   "hello"     → 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
 */

/* Requirement 5.1: parse valid "sha256:..." spec */
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

/* Requirement 5.2: missing colon separator returns non-zero */
TEST(checksum_parse_missing_colon) {
    const char *input = "sha256abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
    ChecksumSpec spec;
    int rc = checksum_parse(input, &spec);

    TEST_ASSERT(rc != 0);

    return 0;
}

/* Requirement 5.3: incorrect digest length returns non-zero */
TEST(checksum_validate_format_wrong_length) {
    ChecksumSpec spec;
    spec.algorithm = CHECKSUM_SHA256;
    /* SHA-256 digest should be 64 hex chars; provide only 10 */
    strcpy(spec.hex_digest, "abcdef1234");

    int rc = checksum_validate_format(&spec);

    TEST_ASSERT(rc != 0);

    return 0;
}

/* Requirement 5.4: compute SHA-256 of known input, verify expected digest */
TEST(checksum_compute_known) {
    const char *data = "hello";
    const char *expected = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";
    char out_hex[129] = {0};

    int rc = checksum_compute(data, 5, CHECKSUM_SHA256, out_hex);

    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_STR_EQ(out_hex, expected);

    return 0;
}

/* Requirement 5.5: matching data and checksum returns 0 */
TEST(checksum_verify_buffer_match) {
    const char *data = "hello";
    char computed[129] = {0};

    /* First compute the checksum of the data */
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

/* Requirement 5.6: mismatched data returns non-zero with actual_hex populated */
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
    TEST_ASSERT(strlen(actual_hex) == 64);
    /* Verify it's not the same as the "hello" digest */
    TEST_ASSERT(strcmp(actual_hex, computed) != 0);

    return 0;
}
