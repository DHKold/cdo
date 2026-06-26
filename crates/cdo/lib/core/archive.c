#include "core/archive.h"
#include "pal/pal.h"
#include "core/output.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#define ZIP_LFH_SIG   0x04034b50u
#define ZIP_CD_SIG    0x02014b50u
#define ZIP_EOCD_SIG  0x06054b50u
#define ZIP_STORE     0
#define ZIP_DEFLATE   8
#define ZIP_EOCD_MAX  (65535 + 22)

static inline uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define INF_MAXBITS 15
#define INF_MAXLIT  288
#define INF_MAXCLEN 19

typedef struct { uint16_t cnt[INF_MAXBITS+1]; uint16_t sym[INF_MAXLIT]; } Huff;
typedef struct {
    const uint8_t* src; size_t slen; size_t spos;
    uint32_t bits; int bcnt;
    uint8_t* dst; size_t dcap; size_t dpos;
} Inf;

static int inf_bits(Inf* s, int n, uint32_t* v) {
    while (s->bcnt < n) {
        if (s->spos >= s->slen) return -1;
        s->bits |= (uint32_t)s->src[s->spos++] << s->bcnt;
        s->bcnt += 8;
    }
    *v = s->bits & ((1u << n) - 1);
    s->bits >>= n; s->bcnt -= n;
    return 0;
}

static int inf_emit(Inf* s, uint8_t b) {
    if (s->dpos >= s->dcap) {
        size_t nc = s->dcap ? s->dcap * 2 : 4096;
        uint8_t* nb = (uint8_t*)realloc(s->dst, nc);
        if (!nb) return -1;
        s->dst = nb; s->dcap = nc;
    }
    s->dst[s->dpos++] = b;
    return 0;
}

static int huff_build(Huff* h, const uint16_t* lens, int n) {
    uint16_t off[INF_MAXBITS+1];
    int i;
    memset(h->cnt, 0, sizeof(h->cnt));
    for (i = 0; i < n; i++) {
        if (lens[i] > INF_MAXBITS) return -1;
        h->cnt[lens[i]]++;
    }
    h->cnt[0] = 0;
    off[0] = 0; off[1] = 0;
    for (i = 1; i < INF_MAXBITS; i++) off[i+1] = off[i] + h->cnt[i];
    memset(h->sym, 0, sizeof(h->sym));
    for (i = 0; i < n; i++)
        if (lens[i]) h->sym[off[lens[i]]++] = (uint16_t)i;
    return 0;
}

static int huff_dec(Inf* s, const Huff* h, uint16_t* out) {
    int code = 0, first = 0, idx = 0, len;
    for (len = 1; len <= INF_MAXBITS; len++) {
        uint32_t bit;
        if (inf_bits(s, 1, &bit)) return -1;
        code = (code << 1) | (int)bit;
        int c = h->cnt[len];
        if (code - c < first) {
            *out = h->sym[idx + (code - first)];
            return 0;
        }
        idx += c;
        first = (first + c) << 1;
    }
    return -1;
}

static int inf_fixed(Huff* lit, Huff* dist) {
    uint16_t l[INF_MAXLIT];
    int i;
    for (i = 0;   i <= 143; i++) l[i] = 8;
    for (i = 144; i <= 255; i++) l[i] = 9;
    for (i = 256; i <= 279; i++) l[i] = 7;
    for (i = 280; i <= 287; i++) l[i] = 8;
    if (huff_build(lit, l, 288)) return -1;
    for (i = 0; i < 32; i++) l[i] = 5;
    if (huff_build(dist, l, 32)) return -1;
    return 0;
}

static const uint16_t LB[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint16_t LE[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
    3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t DB[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,
    8193,12289,16385,24577};
static const uint16_t DE[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int inf_codes(Inf* s, const Huff* lit, const Huff* dist) {
    for (;;) {
        uint16_t sym;
        if (huff_dec(s, lit, &sym)) return -1;
        if (sym < 256) {
            if (inf_emit(s, (uint8_t)sym)) return -1;
        } else if (sym == 256) {
            return 0;
        } else {
            int li = sym - 257;
            if (li < 0 || li >= 29) return -1;
            uint32_t ex, length = LB[li];
            if (LE[li]) { if (inf_bits(s, LE[li], &ex)) return -1; length += ex; }
            uint16_t ds;
            if (huff_dec(s, dist, &ds)) return -1;
            if (ds >= 30) return -1;
            uint32_t distance = DB[ds];
            if (DE[ds]) { if (inf_bits(s, DE[ds], &ex)) return -1; distance += ex; }
            if ((size_t)distance > s->dpos) return -1;
            size_t sp = s->dpos - distance;
            for (uint32_t i = 0; i < length; i++) {
                uint8_t b = s->dst[sp + i];
                if (inf_emit(s, b)) return -1;
            }
        }
    }
}

static const int CLEN_ORD[INF_MAXCLEN] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

static int inf_dynamic(Inf* s, Huff* lit, Huff* dist) {
    uint32_t v;
    int i;
    if (inf_bits(s, 5, &v)) return -1; int hlit = (int)v + 257;
    if (inf_bits(s, 5, &v)) return -1; int hdist = (int)v + 1;
    if (inf_bits(s, 4, &v)) return -1; int hclen = (int)v + 4;
    if (hlit > 286 || hdist > 30) return -1;

    uint16_t cl[INF_MAXCLEN];
    memset(cl, 0, sizeof(cl));
    for (i = 0; i < hclen; i++) {
        if (inf_bits(s, 3, &v)) return -1;
        cl[CLEN_ORD[i]] = (uint16_t)v;
    }
    Huff ch;
    if (huff_build(&ch, cl, INF_MAXCLEN)) return -1;

    int total = hlit + hdist;
    uint16_t lens[INF_MAXLIT + 32];
    memset(lens, 0, sizeof(lens));
    i = 0;
    while (i < total) {
        uint16_t sym;
        if (huff_dec(s, &ch, &sym)) return -1;
        if (sym < 16) {
            lens[i++] = sym;
        } else if (sym == 16) {
            if (!i) return -1;
            if (inf_bits(s, 2, &v)) return -1;
            int r = (int)v + 3;
            uint16_t p = lens[i-1];
            while (r-- && i < total) lens[i++] = p;
        } else if (sym == 17) {
            if (inf_bits(s, 3, &v)) return -1;
            int r = (int)v + 3;
            while (r-- && i < total) lens[i++] = 0;
        } else if (sym == 18) {
            if (inf_bits(s, 7, &v)) return -1;
            int r = (int)v + 11;
            while (r-- && i < total) lens[i++] = 0;
        } else {
            return -1;
        }
    }
    if (huff_build(lit, lens, hlit)) return -1;
    if (huff_build(dist, lens + hlit, hdist)) return -1;
    return 0;
}

static int inflate_raw(const uint8_t* src, size_t slen,
                       uint8_t** out, size_t* olen) {
    Inf s;
    memset(&s, 0, sizeof(s));
    s.src = src; s.slen = slen;
    s.dcap = slen > 1024 ? slen * 3 : 4096;
    s.dst = (uint8_t*)malloc(s.dcap);
    if (!s.dst) return -1;

    int fin = 0;
    while (!fin) {
        uint32_t v;
        if (inf_bits(&s, 1, &v)) goto fail;
        fin = (int)v;
        if (inf_bits(&s, 2, &v)) goto fail;
        int bt = (int)v;
        if (bt == 0) {
            s.bits = 0; s.bcnt = 0;
            if (s.spos + 4 > s.slen) goto fail;
            uint16_t len = rd16(s.src + s.spos);
            uint16_t nlen = rd16(s.src + s.spos + 2);
            s.spos += 4;
            if ((uint16_t)(~nlen) != len) goto fail;
            if (s.spos + len > s.slen) goto fail;
            for (uint16_t k = 0; k < len; k++)
                if (inf_emit(&s, s.src[s.spos++])) goto fail;
        } else if (bt == 1) {
            Huff lit, dist;
            if (inf_fixed(&lit, &dist)) goto fail;
            if (inf_codes(&s, &lit, &dist)) goto fail;
        } else if (bt == 2) {
            Huff lit, dist;
            if (inf_dynamic(&s, &lit, &dist)) goto fail;
            if (inf_codes(&s, &lit, &dist)) goto fail;
        } else {
            goto fail;
        }
    }
    *out = s.dst; *olen = s.dpos;
    return 0;
fail:
    free(s.dst);
    return -1;
}

static const uint8_t* zip_find_eocd(const uint8_t* d, size_t len) {
    if (len < 22) return NULL;
    size_t lo = len > (size_t)ZIP_EOCD_MAX ? len - (size_t)ZIP_EOCD_MAX : 0;
    for (size_t i = len - 22; ; i--) {
        if (rd32(d + i) == ZIP_EOCD_SIG) return d + i;
        if (i <= lo) break;
    }
    return NULL;
}

static int zip_mkparent(const char* path) {
    const char* sep = NULL;
    for (const char* p = path; *p; p++)
        if (*p == '/' || *p == '\\') sep = p;
    if (!sep) return 0;
    size_t n = (size_t)(sep - path);
    char* dir = (char*)malloc(n + 1);
    if (!dir) return -1;
    memcpy(dir, path, n); dir[n] = '\0';
    int rc = pal_mkdir_p(dir);
    free(dir);
    return rc;
}

int archive_extract_zip(const char* archive_path, const char* dest_dir) {
    if (!archive_path || !dest_dir) return -1;

    char* raw = NULL; size_t rlen = 0;
    if (pal_file_read(archive_path, &raw, &rlen) != 0) {
        cdo_error("archive: cannot read '%s'", archive_path);
        return -1;
    }
    const uint8_t* d = (const uint8_t*)raw;

    const uint8_t* eocd = zip_find_eocd(d, rlen);
    if (!eocd) {
        cdo_error("archive: corrupted ZIP (no EOCD) '%s'", archive_path);
        free(raw); return -1;
    }

    uint16_t nent = rd16(eocd + 10);
    uint32_t cdoff = rd32(eocd + 16);
    if ((size_t)cdoff >= rlen) {
        cdo_error("archive: corrupted ZIP (bad CD offset) '%s'", archive_path);
        free(raw); return -1;
    }

    if (pal_mkdir_p(dest_dir) != 0) {
        cdo_error("archive: cannot create dest '%s'", dest_dir);
        free(raw); return -1;
    }

    const uint8_t* cd = d + cdoff;
    for (int i = 0; i < (int)nent; i++) {
        if (cd + 46 > d + rlen || rd32(cd) != ZIP_CD_SIG) {
            cdo_error("archive: bad central directory '%s'", archive_path);
            free(raw); return -1;
        }

        uint16_t method  = rd16(cd + 10);
        uint32_t csz     = rd32(cd + 20);
        uint32_t usz     = rd32(cd + 24);
        uint16_t nlen    = rd16(cd + 28);
        uint16_t elen    = rd16(cd + 30);
        uint16_t clen    = rd16(cd + 32);
        uint32_t loff    = rd32(cd + 42);

        if (cd + 46 + nlen > d + rlen) {
            cdo_error("archive: truncated entry '%s'", archive_path);
            free(raw); return -1;
        }
        const char* name = (const char*)(cd + 46);
        cd += 46 + nlen + elen + clen;

        size_t dlen = strlen(dest_dir);
        char* op = (char*)malloc(dlen + 1 + nlen + 1);
        if (!op) { free(raw); return -1; }
        memcpy(op, dest_dir, dlen);
        op[dlen] = '/';
        memcpy(op + dlen + 1, name, nlen);
        op[dlen + 1 + nlen] = '\0';
        pal_path_normalize(op);

        if (nlen > 0 && name[nlen - 1] == '/') {
            pal_mkdir_p(op); free(op); continue;
        }

        if ((size_t)loff + 30 > rlen || rd32(d + loff) != ZIP_LFH_SIG) {
            cdo_error("archive: bad local header '%s'", archive_path);
            free(op); free(raw); return -1;
        }
        uint16_t ln = rd16(d + loff + 26);
        uint16_t le = rd16(d + loff + 28);
        size_t foff = (size_t)loff + 30 + ln + le;
        if (foff + csz > rlen) {
            cdo_error("archive: data overflow '%s'", archive_path);
            free(op); free(raw); return -1;
        }
        const uint8_t* fd = d + foff;

        if (zip_mkparent(op) != 0) {
            cdo_error("archive: mkdir failed '%s'", op);
            free(op); free(raw); return -1;
        }

        if (method == ZIP_STORE) {
            if (pal_file_write(op, (const char*)fd, csz) != 0) {
                cdo_error("archive: write failed '%s'", op);
                free(op); free(raw); return -1;
            }
        } else if (method == ZIP_DEFLATE) {
            uint8_t* dec = NULL; size_t dlen2 = 0;
            if (inflate_raw(fd, csz, &dec, &dlen2) != 0) {
                cdo_error("archive: inflate failed in '%s'", archive_path);
                free(op); free(raw); return -1;
            }
            if (dlen2 != usz) {
                cdo_error("archive: size mismatch in '%s'", archive_path);
                free(dec); free(op); free(raw); return -1;
            }
            if (pal_file_write(op, (const char*)dec, dlen2) != 0) {
                cdo_error("archive: write failed '%s'", op);
                free(dec); free(op); free(raw); return -1;
            }
            free(dec);
        } else {
            cdo_error("archive: unsupported method %d in '%s'", method, archive_path);
            free(op); free(raw); return -1;
        }
        free(op);
    }

    free(raw);
    return 0;
}

/* ===========================================================================
 * Gzip decompression (RFC 1952)
 * =========================================================================== */
#define GZ_FEXTRA   0x04
#define GZ_FNAME    0x08
#define GZ_FCOMMENT 0x10
#define GZ_FHCRC    0x02

static int gzip_decompress(const uint8_t* data, size_t len,
                           uint8_t** out, size_t* out_len) {
    if (len < 18) return -1;
    if (data[0] != 0x1F || data[1] != 0x8B) return -1;
    if (data[2] != 0x08) return -1; /* only DEFLATE method */

    uint8_t flg = data[3];
    size_t pos = 10;

    if (flg & GZ_FEXTRA) {
        if (pos + 2 > len) return -1;
        uint16_t xlen = rd16(data + pos);
        pos += 2 + xlen;
    }
    if (flg & GZ_FNAME) {
        while (pos < len && data[pos]) pos++;
        pos++;
    }
    if (flg & GZ_FCOMMENT) {
        while (pos < len && data[pos]) pos++;
        pos++;
    }
    if (flg & GZ_FHCRC) pos += 2;
    if (pos >= len) return -1;

    /* Compressed data = everything between header and 8-byte trailer */
    size_t clen = len - pos;
    if (clen > 8) clen -= 8;

    return inflate_raw(data + pos, clen, out, out_len);
}

/* ===========================================================================
 * Tar parsing (POSIX / USTAR)
 * =========================================================================== */
#define TAR_BLOCK 512

static size_t tar_octal(const char* s, int n) {
    size_t v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] == '\0' || s[i] == ' ') break;
        if (s[i] >= '0' && s[i] <= '7')
            v = (v << 3) | (size_t)(s[i] - '0');
    }
    return v;
}

static bool tar_zero_block(const uint8_t* b) {
    for (int i = 0; i < TAR_BLOCK; i++)
        if (b[i]) return false;
    return true;
}

static int tar_entry_path(char* dst, size_t dsz,
                          const char* prefix, int pmax,
                          const char* name, int nmax) {
    int plen = 0, nlen = 0;
    for (int i = 0; i < pmax && prefix[i]; i++) plen++;
    for (int i = 0; i < nmax && name[i]; i++) nlen++;
    size_t total = (plen > 0) ? (size_t)plen + 1 + (size_t)nlen : (size_t)nlen;
    if (total >= dsz) return -1;
    if (plen > 0) {
        memcpy(dst, prefix, (size_t)plen);
        dst[plen] = '/';
        memcpy(dst + plen + 1, name, (size_t)nlen);
    } else {
        memcpy(dst, name, (size_t)nlen);
    }
    dst[total] = '\0';
    return 0;
}

static int tar_mkparent(const char* path) {
    const char* sep = NULL;
    for (const char* p = path; *p; p++)
        if (*p == '/' || *p == '\\') sep = p;
    if (!sep) return 0;
    size_t n = (size_t)(sep - path);
    char* dir = (char*)malloc(n + 1);
    if (!dir) return -1;
    memcpy(dir, path, n); dir[n] = '\0';
    int rc = pal_mkdir_p(dir);
    free(dir);
    return rc;
}

static int tar_extract(const uint8_t* tar_data, size_t tar_len,
                       const char* dest_dir) {
    size_t pos = 0;
    int zero_blocks = 0;
    size_t dlen = strlen(dest_dir);

    while (pos + TAR_BLOCK <= tar_len) {
        const uint8_t* hdr = tar_data + pos;

        if (tar_zero_block(hdr)) {
            zero_blocks++;
            if (zero_blocks >= 2) break;
            pos += TAR_BLOCK;
            continue;
        }
        zero_blocks = 0;

        /* Parse header fields */
        const char* h_name   = (const char*)(hdr);
        const char* h_mode   = (const char*)(hdr + 100);
        const char* h_size   = (const char*)(hdr + 124);
        char typeflag        = (char)hdr[156];
        const char* h_magic  = (const char*)(hdr + 257);
        const char* h_prefix = (const char*)(hdr + 345);

        size_t file_size = tar_octal(h_size, 12);
        size_t mode      = tar_octal(h_mode, 8);

        /* Build entry path */
        char entry[1024];
        bool ustar = (memcmp(h_magic, "ustar", 5) == 0);
        if (ustar) {
            if (tar_entry_path(entry, sizeof(entry), h_prefix, 155, h_name, 100) < 0) {
                pos += TAR_BLOCK;
                continue;
            }
        } else {
            if (tar_entry_path(entry, sizeof(entry), "", 0, h_name, 100) < 0) {
                pos += TAR_BLOCK;
                continue;
            }
        }
        if (entry[0] == '\0') { pos += TAR_BLOCK; continue; }

        /* Full destination path */
        size_t elen = strlen(entry);
        char* full = (char*)malloc(dlen + 1 + elen + 1);
        if (!full) return -1;
        memcpy(full, dest_dir, dlen);
        full[dlen] = '/';
        memcpy(full + dlen + 1, entry, elen);
        full[dlen + 1 + elen] = '\0';
        pal_path_normalize(full);

        pos += TAR_BLOCK; /* advance past header */

        if (typeflag == '5') {
            /* Directory */
            pal_mkdir_p(full);
        } else if (typeflag == '0' || typeflag == '\0') {
            /* Regular file */
            tar_mkparent(full);
            if (file_size > 0) {
                if (pos + file_size > tar_len) { free(full); return -1; }
                if (pal_file_write(full, (const char*)(tar_data + pos),
                                   file_size) != 0) {
                    free(full); return -1;
                }
            } else {
                if (pal_file_write(full, "", 0) != 0) {
                    free(full); return -1;
                }
            }
#ifndef _WIN32
            if (mode != 0)
                chmod(full, (mode_t)(mode & 0777));
#endif
            /* Advance past data blocks (padded to TAR_BLOCK) */
            size_t blocks = (file_size + TAR_BLOCK - 1) / TAR_BLOCK;
            pos += blocks * TAR_BLOCK;
        } else if (typeflag == '2') {
            /* Symlink - skip (no data blocks) */
        } else {
            /* Other types - skip data blocks */
            size_t blocks = (file_size + TAR_BLOCK - 1) / TAR_BLOCK;
            pos += blocks * TAR_BLOCK;
        }
        free(full);
    }
    return 0;
}

/* ===========================================================================
 * tar.gz extraction - public API
 * =========================================================================== */
int archive_extract_targz(const char* archive_path, const char* dest_dir) {
    if (!archive_path || !dest_dir) return -1;

    /* Read the .tar.gz file */
    char* raw = NULL; size_t raw_len = 0;
    if (pal_file_read(archive_path, &raw, &raw_len) != 0) {
        cdo_error("archive: cannot read '%s'", archive_path);
        return -1;
    }

    /* Decompress gzip layer to get raw tar data */
    uint8_t* tar_data = NULL; size_t tar_len = 0;
    int rc = gzip_decompress((const uint8_t*)raw, raw_len, &tar_data, &tar_len);
    free(raw);
    if (rc != 0) {
        cdo_error("archive: gzip decompress failed '%s'", archive_path);
        return -1;
    }

    /* Create destination directory */
    if (pal_mkdir_p(dest_dir) != 0) {
        cdo_error("archive: cannot create dest '%s'", dest_dir);
        free(tar_data);
        return -1;
    }

    /* Extract tar contents */
    rc = tar_extract(tar_data, tar_len, dest_dir);
    free(tar_data);
    if (rc != 0) {
        cdo_error("archive: tar extraction failed '%s'", archive_path);
    }
    return rc;
}