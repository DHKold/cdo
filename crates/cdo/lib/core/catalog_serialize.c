/*
 * catalog_serialize.c — Serialize a Catalog to TOML v1.0 text.
 *
 * Produces valid TOML with [[tool]] and [[package]] array-of-tables entries,
 * preserving array ordering and key-value pair ordering.
 */

#include "core/catalog.h"
#include "core/output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Dynamic String Buffer (local to this file)
 * -------------------------------------------------------------------------- */

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} CatBuf;

static void catbuf_init(CatBuf* b)
{
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

static void catbuf_free(CatBuf* b)
{
    free(b->data);
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

static int catbuf_grow(CatBuf* b, size_t needed)
{
    if (b->len + needed <= b->cap) return 0;

    size_t new_cap = b->cap == 0 ? 1024 : b->cap;
    while (new_cap < b->len + needed) {
        new_cap *= 2;
    }

    char* new_data = (char*)realloc(b->data, new_cap);
    if (!new_data) return 1;

    b->data = new_data;
    b->cap  = new_cap;
    return 0;
}

static int catbuf_append(CatBuf* b, const char* str)
{
    if (!str) return 0;
    size_t slen = strlen(str);
    if (slen == 0) return 0;

    if (catbuf_grow(b, slen) != 0) return 1;

    memcpy(b->data + b->len, str, slen);
    b->len += slen;
    return 0;
}

static int catbuf_append_char(CatBuf* b, char c)
{
    if (catbuf_grow(b, 1) != 0) return 1;
    b->data[b->len++] = c;
    return 0;
}

/* --------------------------------------------------------------------------
 * TOML String Escaping
 * -------------------------------------------------------------------------- */

/**
 * Append a TOML basic string (double-quoted) with proper escaping.
 * Escapes: \, ", \n, \r, \t, and control characters.
 */
static int catbuf_append_quoted_string(CatBuf* b, const char* str)
{
    if (catbuf_append_char(b, '"') != 0) return 1;

    if (str) {
        for (const char* p = str; *p; p++) {
            unsigned char c = (unsigned char)*p;
            switch (c) {
                case '"':  if (catbuf_append(b, "\\\"") != 0) return 1; break;
                case '\\': if (catbuf_append(b, "\\\\") != 0) return 1; break;
                case '\n': if (catbuf_append(b, "\\n") != 0) return 1;  break;
                case '\r': if (catbuf_append(b, "\\r") != 0) return 1;  break;
                case '\t': if (catbuf_append(b, "\\t") != 0) return 1;  break;
                default:
                    if (c < 0x20) {
                        /* Escape other control characters as \uXXXX */
                        char esc[8];
                        snprintf(esc, sizeof(esc), "\\u%04X", c);
                        if (catbuf_append(b, esc) != 0) return 1;
                    } else {
                        if (catbuf_append_char(b, (char)c) != 0) return 1;
                    }
                    break;
            }
        }
    }

    if (catbuf_append_char(b, '"') != 0) return 1;
    return 0;
}

/* --------------------------------------------------------------------------
 * Key-Value Helpers
 * -------------------------------------------------------------------------- */

/**
 * Append: key = "value"\n
 */
static int catbuf_append_kv_string(CatBuf* b, const char* key, const char* value)
{
    if (catbuf_append(b, key) != 0) return 1;
    if (catbuf_append(b, " = ") != 0) return 1;
    if (catbuf_append_quoted_string(b, value) != 0) return 1;
    if (catbuf_append_char(b, '\n') != 0) return 1;
    return 0;
}

/**
 * Append: key = ["item1", "item2", ...]\n
 */
static int catbuf_append_kv_string_array(CatBuf* b, const char* key,
                                          char* const* items, int count)
{
    if (catbuf_append(b, key) != 0) return 1;
    if (catbuf_append(b, " = [") != 0) return 1;

    for (int i = 0; i < count; i++) {
        if (i > 0) {
            if (catbuf_append(b, ", ") != 0) return 1;
        }
        if (catbuf_append_quoted_string(b, items[i]) != 0) return 1;
    }

    if (catbuf_append(b, "]\n") != 0) return 1;
    return 0;
}

/* --------------------------------------------------------------------------
 * Tool Entry Serialization
 * -------------------------------------------------------------------------- */

static int serialize_tool_entry(CatBuf* b, const CatalogToolEntry* tool,
                                bool leading_newline)
{
    /* Blank line separator between entries */
    if (leading_newline) {
        if (catbuf_append_char(b, '\n') != 0) return 1;
    }

    /* [[tool]] header */
    if (catbuf_append(b, "[[tool]]\n") != 0) return 1;

    /* name = "..." */
    if (catbuf_append_kv_string(b, "name", tool->name) != 0) return 1;

    /* version = "..." */
    if (catbuf_append_kv_string(b, "version", tool->version) != 0) return 1;

    /* description = "..." (only if non-empty) */
    if (tool->description[0] != '\0') {
        if (catbuf_append_kv_string(b, "description", tool->description) != 0) return 1;
    }

    /* Platform sub-tables */
    for (int p = 0; p < tool->platform_count; p++) {
        const CatalogPlatformEntry* pe = &tool->platforms[p];

        if (catbuf_append_char(b, '\n') != 0) return 1;

        /* [tool.platforms.<triple>] */
        if (catbuf_append(b, "[tool.platforms.") != 0) return 1;
        if (catbuf_append(b, pe->triple) != 0) return 1;
        if (catbuf_append(b, "]\n") != 0) return 1;

        /* url = "..." */
        if (catbuf_append_kv_string(b, "url", pe->url) != 0) return 1;

        /* checksum = "..." (only if non-empty) */
        if (pe->checksum[0] != '\0') {
            if (catbuf_append_kv_string(b, "checksum", pe->checksum) != 0) return 1;
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * Package Entry Serialization
 * -------------------------------------------------------------------------- */

static int serialize_package_entry(CatBuf* b, const CatalogPackageEntry* pkg,
                                   bool leading_newline)
{
    /* Blank line separator between entries */
    if (leading_newline) {
        if (catbuf_append_char(b, '\n') != 0) return 1;
    }

    /* [[package]] header */
    if (catbuf_append(b, "[[package]]\n") != 0) return 1;

    /* name = "..." */
    if (catbuf_append_kv_string(b, "name", pkg->name) != 0) return 1;

    /* version = "..." */
    if (catbuf_append_kv_string(b, "version", pkg->version) != 0) return 1;

    /* description = "..." (only if non-empty) */
    if (pkg->description[0] != '\0') {
        if (catbuf_append_kv_string(b, "description", pkg->description) != 0) return 1;
    }

    /* include_dirs = [...] (omitted if empty) */
    if (pkg->include_dir_count > 0) {
        if (catbuf_append_kv_string_array(b, "include_dirs",
                                           pkg->include_dirs,
                                           pkg->include_dir_count) != 0) return 1;
    }

    /* link_libs = [...] (omitted if empty) */
    if (pkg->link_lib_count > 0) {
        if (catbuf_append_kv_string_array(b, "link_libs",
                                           pkg->link_libs,
                                           pkg->link_lib_count) != 0) return 1;
    }

    /* defines = [...] (omitted if empty) */
    if (pkg->define_count > 0) {
        if (catbuf_append_kv_string_array(b, "defines",
                                           pkg->defines,
                                           pkg->define_count) != 0) return 1;
    }

    /* Platform sub-tables */
    for (int p = 0; p < pkg->platform_count; p++) {
        const CatalogPlatformEntry* pe = &pkg->platforms[p];

        if (catbuf_append_char(b, '\n') != 0) return 1;

        /* [package.platforms.<triple>] */
        if (catbuf_append(b, "[package.platforms.") != 0) return 1;
        if (catbuf_append(b, pe->triple) != 0) return 1;
        if (catbuf_append(b, "]\n") != 0) return 1;

        /* url = "..." */
        if (catbuf_append_kv_string(b, "url", pe->url) != 0) return 1;

        /* checksum = "..." (only if non-empty) */
        if (pe->checksum[0] != '\0') {
            if (catbuf_append_kv_string(b, "checksum", pe->checksum) != 0) return 1;
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * Public API: catalog_serialize
 * -------------------------------------------------------------------------- */

int catalog_serialize(const Catalog* cat, char** out_buf, size_t* out_len)
{
    if (!cat || !out_buf || !out_len) {
        cdo_error("catalog_serialize: invalid arguments (NULL pointer)");
        if (out_buf) *out_buf = NULL;
        if (out_len) *out_len = 0;
        return 1;
    }

    /* Initialize output to safe values immediately */
    *out_buf = NULL;
    *out_len = 0;

    CatBuf buf;
    catbuf_init(&buf);

    bool first_entry = true;

    /* Serialize all tool entries in array order */
    for (int i = 0; i < cat->tool_count; i++) {
        if (serialize_tool_entry(&buf, &cat->tools[i], !first_entry) != 0) {
            catbuf_free(&buf);
            cdo_error("catalog_serialize: out of memory while serializing tool '%s'",
                      cat->tools[i].name);
            return 1;
        }
        first_entry = false;
    }

    /* Serialize all package entries in array order */
    for (int i = 0; i < cat->package_count; i++) {
        if (serialize_package_entry(&buf, &cat->packages[i], !first_entry) != 0) {
            catbuf_free(&buf);
            cdo_error("catalog_serialize: out of memory while serializing package '%s'",
                      cat->packages[i].name);
            return 1;
        }
        first_entry = false;
    }

    /* Null-terminate the buffer */
    if (catbuf_grow(&buf, 1) != 0) {
        catbuf_free(&buf);
        cdo_error("catalog_serialize: out of memory finalizing output");
        return 1;
    }
    buf.data[buf.len] = '\0';

    /* Transfer ownership to caller */
    *out_buf = buf.data;
    *out_len = buf.len;

    return 0;
}
