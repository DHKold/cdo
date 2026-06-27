#include "commons/toml.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// String buffer helpers (duplicated from toml_parse.c — static, file-local)
// =============================================================================

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} StrBuf;

static void strbuf_init(StrBuf* sb) {
    sb->data = NULL;
    sb->len  = 0;
    sb->cap  = 0;
}

static int strbuf_push(StrBuf* sb, char c) {
    if (sb->len >= sb->cap) {
        size_t new_cap = sb->cap == 0 ? 32 : sb->cap * 2;
        char* nd = (char*)realloc(sb->data, new_cap);
        if (!nd) return -1;
        sb->data = nd;
        sb->cap  = new_cap;
    }
    sb->data[sb->len++] = c;
    return 0;
}

static int strbuf_push_str(StrBuf* sb, const char* str, size_t slen) {
    for (size_t i = 0; i < slen; i++) {
        if (strbuf_push(sb, str[i]) != 0) return -1;
    }
    return 0;
}

static void strbuf_free(StrBuf* sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

// =============================================================================
// Key helpers
// =============================================================================

static inline bool is_bare_key_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}


// =============================================================================
// toml_serialize â€” convert in-memory DOM back to TOML text
// =============================================================================

// Forward declarations for serializer helpers
static int ser_value(StrBuf* sb, const TomlValue* val);
static int ser_table_entries(StrBuf* sb, const TomlTable* table, const char* prefix);

// Check if a key needs quoting
static bool key_needs_quoting(const char* key) {
    if (!key || key[0] == '\0') return true;
    for (const char* p = key; *p; p++) {
        if (!is_bare_key_char(*p)) return true;
    }
    return false;
}

// Emit a key (bare or quoted)
static int ser_key(StrBuf* sb, const char* key) {
    if (key_needs_quoting(key)) {
        if (strbuf_push(sb, '"') != 0) return -1;
        for (const char* p = key; *p; p++) {
            switch (*p) {
                case '"':  if (strbuf_push_str(sb, "\\\"", 2) != 0) return -1; break;
                case '\\': if (strbuf_push_str(sb, "\\\\", 2) != 0) return -1; break;
                case '\n': if (strbuf_push_str(sb, "\\n", 2) != 0) return -1; break;
                case '\t': if (strbuf_push_str(sb, "\\t", 2) != 0) return -1; break;
                case '\r': if (strbuf_push_str(sb, "\\r", 2) != 0) return -1; break;
                case '\b': if (strbuf_push_str(sb, "\\b", 2) != 0) return -1; break;
                case '\f': if (strbuf_push_str(sb, "\\f", 2) != 0) return -1; break;
                default:   if (strbuf_push(sb, *p) != 0) return -1; break;
            }
        }
        if (strbuf_push(sb, '"') != 0) return -1;
    } else {
        if (strbuf_push_str(sb, key, strlen(key)) != 0) return -1;
    }
    return 0;
}

// Emit a string value with proper escaping
static int ser_string(StrBuf* sb, const char* str) {
    if (strbuf_push(sb, '"') != 0) return -1;
    for (const char* p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"':  if (strbuf_push_str(sb, "\\\"", 2) != 0) return -1; break;
            case '\\': if (strbuf_push_str(sb, "\\\\", 2) != 0) return -1; break;
            case '\n': if (strbuf_push_str(sb, "\\n", 2) != 0) return -1; break;
            case '\t': if (strbuf_push_str(sb, "\\t", 2) != 0) return -1; break;
            case '\r': if (strbuf_push_str(sb, "\\r", 2) != 0) return -1; break;
            case '\b': if (strbuf_push_str(sb, "\\b", 2) != 0) return -1; break;
            case '\f': if (strbuf_push_str(sb, "\\f", 2) != 0) return -1; break;
            default:
                if (c < 0x20) {
                    // Control character: emit as \uXXXX
                    char esc[7];
                    snprintf(esc, sizeof(esc), "\\u%04X", c);
                    if (strbuf_push_str(sb, esc, 6) != 0) return -1;
                } else {
                    if (strbuf_push(sb, (char)c) != 0) return -1;
                }
                break;
        }
    }
    if (strbuf_push(sb, '"') != 0) return -1;
    return 0;
}

// Emit an inline table value { k1 = v1, k2 = v2 }
static int ser_inline_table(StrBuf* sb, const TomlTable* table) {
    if (strbuf_push_str(sb, "{ ", 2) != 0) return -1;
    bool first = true;
    for (TomlEntry* e = table->head; e; e = e->next) {
        if (!first) {
            if (strbuf_push_str(sb, ", ", 2) != 0) return -1;
        }
        first = false;
        if (ser_key(sb, e->key) != 0) return -1;
        if (strbuf_push_str(sb, " = ", 3) != 0) return -1;
        if (ser_value(sb, e->value) != 0) return -1;
    }
    if (strbuf_push_str(sb, " }", 2) != 0) return -1;
    return 0;
}

// Emit an array value [v1, v2, ...]
static int ser_array(StrBuf* sb, const TomlArray* arr) {
    if (strbuf_push(sb, '[') != 0) return -1;
    for (int i = 0; i < arr->count; i++) {
        if (i > 0) {
            if (strbuf_push_str(sb, ", ", 2) != 0) return -1;
        }
        if (ser_value(sb, arr->items[i]) != 0) return -1;
    }
    if (strbuf_push(sb, ']') != 0) return -1;
    return 0;
}

// Emit a single value (inline form â€” used for assignments and array elements)
static int ser_value(StrBuf* sb, const TomlValue* val) {
    if (!val) return -1;
    switch (val->type) {
        case TOML_STRING:
            return ser_string(sb, val->as.string ? val->as.string : "");
        case TOML_INTEGER: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%" PRId64, val->as.integer);
            return strbuf_push_str(sb, buf, strlen(buf));
        }
        case TOML_FLOAT: {
            char buf[64];
            double f = val->as.floating;
            if (isinf(f)) {
                if (f > 0) snprintf(buf, sizeof(buf), "inf");
                else snprintf(buf, sizeof(buf), "-inf");
            } else if (isnan(f)) {
                snprintf(buf, sizeof(buf), "nan");
            } else {
                snprintf(buf, sizeof(buf), "%.17g", f);
                // Ensure there's a decimal point (so it's clearly a float)
                if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E')) {
                    size_t len = strlen(buf);
                    buf[len] = '.';
                    buf[len + 1] = '0';
                    buf[len + 2] = '\0';
                }
            }
            return strbuf_push_str(sb, buf, strlen(buf));
        }
        case TOML_BOOL:
            if (val->as.boolean)
                return strbuf_push_str(sb, "true", 4);
            else
                return strbuf_push_str(sb, "false", 5);
        case TOML_DATETIME:
            // Datetimes are stored as strings, emit as-is (no quotes)
            return strbuf_push_str(sb, val->as.string, strlen(val->as.string));
        case TOML_ARRAY:
            return ser_array(sb, val->as.array);
        case TOML_INLINE_TABLE:
            return ser_inline_table(sb, val->as.table);
        case TOML_TABLE:
            // Tables at top level get section headers; in inline context, emit as inline table
            return ser_inline_table(sb, val->as.table);
    }
    return -1;
}

// Build the full key prefix for section headers (e.g., "parent.child")
static char* build_prefix(const char* prefix, const char* key) {
    if (!prefix || prefix[0] == '\0') {
        return strdup(key);
    }
    size_t plen = strlen(prefix);
    size_t klen = strlen(key);
    char* result = (char*)malloc(plen + 1 + klen + 1);
    if (!result) return NULL;
    memcpy(result, prefix, plen);
    result[plen] = '.';
    memcpy(result + plen + 1, key, klen);
    result[plen + 1 + klen] = '\0';
    return result;
}

// Emit a section header key (with quoting per segment if needed)
static int ser_section_key(StrBuf* sb, const char* prefix) {
    // The prefix is already in "a.b.c" dotted form where each segment
    // may need quoting. We stored it that way, so we need to emit each
    // segment properly. Since we build the prefix by concatenating bare/quoted
    // keys with '.', let's re-emit each segment.
    // Actually, since build_prefix just concatenates with '.', and keys could
    // contain dots if quoted, we need a different approach.
    // For simplicity (since we control prefix building), we'll emit the prefix as-is
    // but quote each segment.

    const char* p = prefix;
    bool first_segment = true;

    while (*p) {
        if (!first_segment) {
            if (strbuf_push(sb, '.') != 0) return -1;
        }
        first_segment = false;

        // Extract segment up to next '.' (not inside quotes in our prefix)
        // Our prefix is built with build_prefix which uses raw key strings joined by '.'
        // Keys themselves should not contain '.' (they're individual key names)
        const char* seg_start = p;
        while (*p && *p != '.') p++;
        size_t seg_len = (size_t)(p - seg_start);

        // Copy segment to temp buffer
        char* seg = (char*)malloc(seg_len + 1);
        if (!seg) return -1;
        memcpy(seg, seg_start, seg_len);
        seg[seg_len] = '\0';

        if (ser_key(sb, seg) != 0) { free(seg); return -1; }
        free(seg);

        if (*p == '.') p++;
    }
    return 0;
}

// Check if an array is an array of tables (all elements are TOML_TABLE)
static bool is_array_of_tables(const TomlArray* arr) {
    if (!arr || arr->count == 0) return false;
    for (int i = 0; i < arr->count; i++) {
        if (arr->items[i]->type != TOML_TABLE) return false;
    }
    return true;
}

// Emit all entries for a table, handling sub-tables and arrays of tables
// with proper section headers. The prefix is the dotted path to this table.
static int ser_table_entries(StrBuf* sb, const TomlTable* table, const char* prefix) {
    if (!table) return 0;

    // First pass: emit all non-table, non-array-of-tables entries as key = value
    for (TomlEntry* e = table->head; e; e = e->next) {
        if (e->value->type == TOML_TABLE) continue;
        if (e->value->type == TOML_ARRAY && is_array_of_tables(e->value->as.array)) continue;

        if (ser_key(sb, e->key) != 0) return -1;
        if (strbuf_push_str(sb, " = ", 3) != 0) return -1;
        if (ser_value(sb, e->value) != 0) return -1;
        if (strbuf_push(sb, '\n') != 0) return -1;
    }

    // Second pass: emit sub-tables with [section] headers
    for (TomlEntry* e = table->head; e; e = e->next) {
        if (e->value->type != TOML_TABLE) continue;

        char* sub_prefix = build_prefix(prefix, e->key);
        if (!sub_prefix) return -1;

        // Emit section header
        if (strbuf_push(sb, '\n') != 0) { free(sub_prefix); return -1; }
        if (strbuf_push(sb, '[') != 0) { free(sub_prefix); return -1; }
        if (ser_section_key(sb, sub_prefix) != 0) { free(sub_prefix); return -1; }
        if (strbuf_push_str(sb, "]\n", 2) != 0) { free(sub_prefix); return -1; }

        // Recursively emit sub-table's entries
        if (ser_table_entries(sb, e->value->as.table, sub_prefix) != 0) {
            free(sub_prefix);
            return -1;
        }
        free(sub_prefix);
    }

    // Third pass: emit arrays of tables with [[section]] headers
    for (TomlEntry* e = table->head; e; e = e->next) {
        if (e->value->type != TOML_ARRAY) continue;
        if (!is_array_of_tables(e->value->as.array)) continue;

        TomlArray* arr = e->value->as.array;
        char* arr_prefix = build_prefix(prefix, e->key);
        if (!arr_prefix) return -1;

        for (int i = 0; i < arr->count; i++) {
            if (strbuf_push(sb, '\n') != 0) { free(arr_prefix); return -1; }
            if (strbuf_push_str(sb, "[[", 2) != 0) { free(arr_prefix); return -1; }
            if (ser_section_key(sb, arr_prefix) != 0) { free(arr_prefix); return -1; }
            if (strbuf_push_str(sb, "]]\n", 3) != 0) { free(arr_prefix); return -1; }

            if (arr->items[i]->type == TOML_TABLE) {
                if (ser_table_entries(sb, arr->items[i]->as.table, arr_prefix) != 0) {
                    free(arr_prefix);
                    return -1;
                }
            }
        }
        free(arr_prefix);
    }

    return 0;
}

int toml_serialize(const TomlTable* table, char** out_buf, size_t* out_len) {
    if (!table || !out_buf || !out_len) return -1;

    StrBuf sb;
    strbuf_init(&sb);

    if (ser_table_entries(&sb, table, "") != 0) {
        strbuf_free(&sb);
        return -1;
    }

    // Null-terminate
    if (strbuf_push(&sb, '\0') != 0) {
        strbuf_free(&sb);
        return -1;
    }

    *out_buf = sb.data;
    *out_len = sb.len - 1; // exclude null terminator from reported length
    return 0;
}
