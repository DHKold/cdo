#include "commons/checksum.h"

#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ============================================================
 * Internal error/warning helpers (replaces core/output.h dependency)
 * ============================================================ */

static void checksum_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void checksum_warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "warning: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

/* ============================================================
 * Minimal SHA-256 / SHA-384 / SHA-512 implementation
 * (portable C, no external dependencies)
 *
 * SHA-384 and SHA-512 share the same core (64-bit words).
 * SHA-256 uses 32-bit words.
 * ============================================================ */

/* --- SHA-256 --- */

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
} Sha256Ctx;

static inline uint32_t rotr32(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_transform(Sha256Ctx* ctx, const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i-15], 7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr32(w[i-2], 17) ^ rotr32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(Sha256Ctx* ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
    memset(ctx->buf, 0, sizeof(ctx->buf));
}

static void sha256_update(Sha256Ctx* ctx, const uint8_t* data, size_t len) {
    size_t buf_used = (size_t)(ctx->count % 64);
    ctx->count += len;

    if (buf_used > 0) {
        size_t space = 64 - buf_used;
        if (len < space) {
            memcpy(ctx->buf + buf_used, data, len);
            return;
        }
        memcpy(ctx->buf + buf_used, data, space);
        sha256_transform(ctx, ctx->buf);
        data += space;
        len -= space;
    }

    while (len >= 64) {
        sha256_transform(ctx, data);
        data += 64;
        len -= 64;
    }

    if (len > 0) {
        memcpy(ctx->buf, data, len);
    }
}

static void sha256_final(Sha256Ctx* ctx, uint8_t digest[32]) {
    uint64_t bits = ctx->count * 8;
    size_t buf_used = (size_t)(ctx->count % 64);

    ctx->buf[buf_used++] = 0x80;
    if (buf_used > 56) {
        memset(ctx->buf + buf_used, 0, 64 - buf_used);
        sha256_transform(ctx, ctx->buf);
        buf_used = 0;
    }
    memset(ctx->buf + buf_used, 0, 56 - buf_used);

    for (int i = 0; i < 8; i++) {
        ctx->buf[56 + i] = (uint8_t)(bits >> (56 - i * 8));
    }
    sha256_transform(ctx, ctx->buf);

    for (int i = 0; i < 8; i++) {
        digest[i*4]   = (uint8_t)(ctx->state[i] >> 24);
        digest[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4+2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i*4+3] = (uint8_t)(ctx->state[i]);
    }
}


/* --- SHA-512 / SHA-384 (shared 64-bit core) --- */

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

typedef struct {
    uint64_t state[8];
    uint64_t count_lo;
    uint64_t count_hi;
    uint8_t  buf[128];
} Sha512Ctx;

static inline uint64_t rotr64(uint64_t x, int n) {
    return (x >> n) | (x << (64 - n));
}

static void sha512_transform(Sha512Ctx* ctx, const uint8_t block[128]) {
    uint64_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint64_t)block[i*8] << 56) |
               ((uint64_t)block[i*8+1] << 48) |
               ((uint64_t)block[i*8+2] << 40) |
               ((uint64_t)block[i*8+3] << 32) |
               ((uint64_t)block[i*8+4] << 24) |
               ((uint64_t)block[i*8+5] << 16) |
               ((uint64_t)block[i*8+6] << 8) |
               ((uint64_t)block[i*8+7]);
    }
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = rotr64(w[i-15], 1) ^ rotr64(w[i-15], 8) ^ (w[i-15] >> 7);
        uint64_t s1 = rotr64(w[i-2], 19) ^ rotr64(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint64_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint64_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

    for (int i = 0; i < 80; i++) {
        uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t temp1 = h + S1 + ch + sha512_k[i] + w[i];
        uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha512_init(Sha512Ctx* ctx) {
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->count_lo = 0;
    ctx->count_hi = 0;
    memset(ctx->buf, 0, sizeof(ctx->buf));
}

static void sha384_init(Sha512Ctx* ctx) {
    ctx->state[0] = 0xcbbb9d5dc1059ed8ULL;
    ctx->state[1] = 0x629a292a367cd507ULL;
    ctx->state[2] = 0x9159015a3070dd17ULL;
    ctx->state[3] = 0x152fecd8f70e5939ULL;
    ctx->state[4] = 0x67332667ffc00b31ULL;
    ctx->state[5] = 0x8eb44a8768581511ULL;
    ctx->state[6] = 0xdb0c2e0d64f98fa7ULL;
    ctx->state[7] = 0x47b5481dbefa4fa4ULL;
    ctx->count_lo = 0;
    ctx->count_hi = 0;
    memset(ctx->buf, 0, sizeof(ctx->buf));
}

static void sha512_update(Sha512Ctx* ctx, const uint8_t* data, size_t len) {
    size_t buf_used = (size_t)(ctx->count_lo % 128);

    /* Update 128-bit counter */
    uint64_t old_lo = ctx->count_lo;
    ctx->count_lo += len;
    if (ctx->count_lo < old_lo) {
        ctx->count_hi++;
    }

    if (buf_used > 0) {
        size_t space = 128 - buf_used;
        if (len < space) {
            memcpy(ctx->buf + buf_used, data, len);
            return;
        }
        memcpy(ctx->buf + buf_used, data, space);
        sha512_transform(ctx, ctx->buf);
        data += space;
        len -= space;
    }

    while (len >= 128) {
        sha512_transform(ctx, data);
        data += 128;
        len -= 128;
    }

    if (len > 0) {
        memcpy(ctx->buf, data, len);
    }
}

static void sha512_final(Sha512Ctx* ctx, uint8_t* digest, int digest_len) {
    uint64_t bits_lo = ctx->count_lo * 8;
    uint64_t bits_hi = ctx->count_hi * 8 + (ctx->count_lo >> 61);
    size_t buf_used = (size_t)(ctx->count_lo % 128);

    ctx->buf[buf_used++] = 0x80;
    if (buf_used > 112) {
        memset(ctx->buf + buf_used, 0, 128 - buf_used);
        sha512_transform(ctx, ctx->buf);
        buf_used = 0;
    }
    memset(ctx->buf + buf_used, 0, 112 - buf_used);

    /* Append 128-bit length (big-endian) */
    for (int i = 0; i < 8; i++) {
        ctx->buf[112 + i] = (uint8_t)(bits_hi >> (56 - i * 8));
    }
    for (int i = 0; i < 8; i++) {
        ctx->buf[120 + i] = (uint8_t)(bits_lo >> (56 - i * 8));
    }
    sha512_transform(ctx, ctx->buf);

    int words = digest_len / 8;
    for (int i = 0; i < words; i++) {
        digest[i*8]   = (uint8_t)(ctx->state[i] >> 56);
        digest[i*8+1] = (uint8_t)(ctx->state[i] >> 48);
        digest[i*8+2] = (uint8_t)(ctx->state[i] >> 40);
        digest[i*8+3] = (uint8_t)(ctx->state[i] >> 32);
        digest[i*8+4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i*8+5] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*8+6] = (uint8_t)(ctx->state[i] >> 8);
        digest[i*8+7] = (uint8_t)(ctx->state[i]);
    }
}


/* ============================================================
 * Helper: convert raw bytes to hex string
 * ============================================================ */

static void bytes_to_hex(const uint8_t* bytes, int byte_count, char* out) {
    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < byte_count; i++) {
        out[i*2]     = hex_chars[(bytes[i] >> 4) & 0x0f];
        out[i*2 + 1] = hex_chars[bytes[i] & 0x0f];
    }
    out[byte_count * 2] = '\0';
}

/* ============================================================
 * Helper: check if a character is a valid lowercase hex digit
 * ============================================================ */

static bool is_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

/* ============================================================
 * Public API implementation
 * ============================================================ */

int checksum_parse(const char* str, ChecksumSpec* out) {
    if (!str || !out) return -1;

    /* Find the colon separator */
    const char* colon = strchr(str, ':');
    if (!colon) {
        checksum_error("malformed checksum: expected 'algorithm:hex_digest' format, got '%s'", str);
        return -1;
    }

    size_t algo_len = (size_t)(colon - str);
    const char* digest = colon + 1;

    /* Parse algorithm */
    if (algo_len == 6 && strncmp(str, "sha256", 6) == 0) {
        out->algorithm = CHECKSUM_SHA256;
    } else if (algo_len == 6 && strncmp(str, "sha384", 6) == 0) {
        out->algorithm = CHECKSUM_SHA384;
    } else if (algo_len == 6 && strncmp(str, "sha512", 6) == 0) {
        out->algorithm = CHECKSUM_SHA512;
    } else {
        checksum_error("unsupported checksum algorithm: '%.*s' (supported: sha256, sha384, sha512)",
                  (int)algo_len, str);
        return -1;
    }

    /* Validate and copy hex digest */
    size_t digest_len = strlen(digest);
    size_t expected_len = 0;
    switch (out->algorithm) {
        case CHECKSUM_SHA256: expected_len = 64; break;
        case CHECKSUM_SHA384: expected_len = 96; break;
        case CHECKSUM_SHA512: expected_len = 128; break;
    }

    if (digest_len != expected_len) {
        checksum_error("checksum hex_digest length mismatch: expected %zu characters for %.*s, got %zu",
                  expected_len, (int)algo_len, str, digest_len);
        return -1;
    }

    /* Validate all characters are lowercase hex */
    for (size_t i = 0; i < digest_len; i++) {
        if (!is_hex_char(digest[i])) {
            checksum_error("checksum hex_digest contains invalid character '%c' at position %zu", digest[i], i);
            return -1;
        }
    }

    memcpy(out->hex_digest, digest, digest_len);
    out->hex_digest[digest_len] = '\0';
    return 0;
}

int checksum_validate_format(const ChecksumSpec* spec) {
    if (!spec) return -1;

    /* Validate expected digest length for algorithm */
    size_t expected_len = 0;
    switch (spec->algorithm) {
        case CHECKSUM_SHA256: expected_len = 64; break;
        case CHECKSUM_SHA384: expected_len = 96; break;
        case CHECKSUM_SHA512: expected_len = 128; break;
        default: return -1;
    }

    size_t actual_len = strlen(spec->hex_digest);
    if (actual_len != expected_len) {
        return -1;
    }

    /* Validate all characters are lowercase hex */
    for (size_t i = 0; i < actual_len; i++) {
        if (!is_hex_char(spec->hex_digest[i])) {
            return -1;
        }
    }

    return 0;
}

int checksum_compute(const void* data, size_t data_len,
                     ChecksumAlgorithm algorithm, char* out_hex) {
    if (!data && data_len > 0) return -1;
    if (!out_hex) return -1;

    switch (algorithm) {
        case CHECKSUM_SHA256: {
            Sha256Ctx ctx;
            uint8_t digest[32];
            sha256_init(&ctx);
            sha256_update(&ctx, (const uint8_t*)data, data_len);
            sha256_final(&ctx, digest);
            bytes_to_hex(digest, 32, out_hex);
            return 0;
        }
        case CHECKSUM_SHA384: {
            Sha512Ctx ctx;
            uint8_t digest[48];
            sha384_init(&ctx);
            sha512_update(&ctx, (const uint8_t*)data, data_len);
            sha512_final(&ctx, digest, 48);
            bytes_to_hex(digest, 48, out_hex);
            return 0;
        }
        case CHECKSUM_SHA512: {
            Sha512Ctx ctx;
            uint8_t digest[64];
            sha512_init(&ctx);
            sha512_update(&ctx, (const uint8_t*)data, data_len);
            sha512_final(&ctx, digest, 64);
            bytes_to_hex(digest, 64, out_hex);
            return 0;
        }
        default:
            return -1;
    }
}

int checksum_verify_buffer(const ChecksumSpec* expected,
                           const unsigned char* data, size_t data_len,
                           char* actual_hex) {
    if (!expected || (!data && data_len > 0) || !actual_hex) return -1;

    /* Validate the expected spec format first */
    if (checksum_validate_format(expected) != 0) {
        checksum_error("invalid checksum spec format");
        return -1;
    }

    /* Compute actual hash */
    int rc = checksum_compute(data, data_len, expected->algorithm, actual_hex);
    if (rc != 0) {
        checksum_error("failed to compute hash");
        return -1;
    }

    /* Compare digests */
    if (strcmp(actual_hex, expected->hex_digest) != 0) {
        return 1; /* mismatch */
    }

    return 0; /* match */
}

int checksum_verify_file(const char* filepath, const ChecksumSpec* expected) {
    if (!filepath || !expected) return -1;

    /* Validate checksum format before reading the file */
    if (checksum_validate_format(expected) != 0) {
        checksum_error("invalid checksum format for file '%s'", filepath);
        return -1;
    }

    /* Read the file into memory */
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        checksum_error("cannot open file for checksum verification: '%s'", filepath);
        return -1;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 0) {
        fclose(f);
        checksum_error("cannot determine file size: '%s'", filepath);
        return -1;
    }

    unsigned char* buf = NULL;
    if (file_size > 0) {
        buf = (unsigned char*)malloc((size_t)file_size);
        if (!buf) {
            fclose(f);
            checksum_error("out of memory reading file for checksum: '%s'", filepath);
            return -1;
        }
        size_t read = fread(buf, 1, (size_t)file_size, f);
        if (read != (size_t)file_size) {
            free(buf);
            fclose(f);
            checksum_error("failed to read file for checksum: '%s'", filepath);
            return -1;
        }
    }
    fclose(f);

    /* Compute and compare */
    char actual_hex[129];
    int rc = checksum_verify_buffer(expected, buf, (size_t)file_size, actual_hex);
    free(buf);

    if (rc == 1) {
        /* Mismatch — delete the file and report */
        checksum_error("checksum mismatch for '%s'", filepath);
        checksum_error("  expected: %s", expected->hex_digest);
        checksum_error("  actual:   %s", actual_hex);
        if (remove(filepath) != 0) {
            checksum_warn("failed to delete archive with mismatched checksum: '%s'", filepath);
        }
        return 1;
    } else if (rc != 0) {
        /* Other error (don't delete) */
        return rc;
    }

    return 0; /* success */
}
