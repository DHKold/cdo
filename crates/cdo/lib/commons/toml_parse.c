#include "commons/toml.h"

#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// Internal types
// =============================================================================

typedef struct {
    const char* src;
    size_t      len;
    size_t      pos;
    int         line;
    int         col;
} Scanner;

// =============================================================================
// Memory helpers
// =============================================================================

static TomlTable* table_new(void) {
    TomlTable* t = (TomlTable*)calloc(1, sizeof(TomlTable));
    return t;
}

static TomlArray* array_new(void) {
    TomlArray* a = (TomlArray*)calloc(1, sizeof(TomlArray));
    return a;
}

static TomlValue* value_new(TomlType type) {
    TomlValue* v = (TomlValue*)calloc(1, sizeof(TomlValue));
    if (v) v->type = type;
    return v;
}

static int array_push(TomlArray* arr, TomlValue* val) {
    if (arr->count >= arr->capacity) {
        int new_cap = arr->capacity == 0 ? 4 : arr->capacity * 2;
        TomlValue** new_items = (TomlValue**)realloc(arr->items, (size_t)new_cap * sizeof(TomlValue*));
        if (!new_items) return -1;
        arr->items = new_items;
        arr->capacity = new_cap;
    }
    arr->items[arr->count++] = val;
    return 0;
}

// Insert or find an entry in a table. Returns the entry (creates if not found).
static TomlEntry* table_find(TomlTable* t, const char* key) {
    for (TomlEntry* e = t->head; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return e;
    }
    return NULL;
}

static int table_set(TomlTable* t, const char* key, TomlValue* val) {
    TomlEntry* existing = table_find(t, key);
    if (existing) {
        // Replace value
        toml_value_free(existing->value);
        existing->value = val;
        return 0;
    }
    TomlEntry* e = (TomlEntry*)calloc(1, sizeof(TomlEntry));
    if (!e) return -1;
    e->key = strdup(key);
    if (!e->key) { free(e); return -1; }
    e->value = val;
    e->next = NULL;
    if (t->tail) {
        t->tail->next = e;
    } else {
        t->head = e;
    }
    t->tail = e;
    t->count++;
    return 0;
}

// =============================================================================
// Scanner helpers
// =============================================================================

static void scanner_init(Scanner* s, const char* src, size_t len) {
    s->src  = src;
    s->len  = len;
    s->pos  = 0;
    s->line = 1;
    s->col  = 1;
}

static inline bool scanner_eof(const Scanner* s) {
    return s->pos >= s->len;
}

static inline char scanner_peek(const Scanner* s) {
    if (s->pos >= s->len) return '\0';
    return s->src[s->pos];
}

static inline char scanner_peek_at(const Scanner* s, int offset) {
    size_t idx = s->pos + (size_t)offset;
    if (idx >= s->len) return '\0';
    return s->src[idx];
}

static inline char scanner_advance(Scanner* s) {
    if (s->pos >= s->len) return '\0';
    char c = s->src[s->pos++];
    if (c == '\n') {
        s->line++;
        s->col = 1;
    } else {
        s->col++;
    }
    return c;
}

static void scanner_skip_whitespace(Scanner* s) {
    while (!scanner_eof(s)) {
        char c = scanner_peek(s);
        if (c == ' ' || c == '\t') {
            scanner_advance(s);
        } else {
            break;
        }
    }
}

static void scanner_skip_whitespace_and_newlines(Scanner* s) {
    while (!scanner_eof(s)) {
        char c = scanner_peek(s);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            scanner_advance(s);
        } else if (c == '#') {
            // Skip comment to end of line
            while (!scanner_eof(s) && scanner_peek(s) != '\n') {
                scanner_advance(s);
            }
        } else {
            break;
        }
    }
}

static void scanner_skip_to_newline(Scanner* s) {
    while (!scanner_eof(s)) {
        char c = scanner_peek(s);
        if (c == '#') {
            while (!scanner_eof(s) && scanner_peek(s) != '\n') {
                scanner_advance(s);
            }
            return;
        }
        if (c == '\n' || c == '\r') return;
        if (c == ' ' || c == '\t') {
            scanner_advance(s);
        } else {
            return;
        }
    }
}

// =============================================================================
// Error reporting
// =============================================================================

static int parse_error(Scanner* s, TomlError* err, const char* fmt, ...) {
    if (err) {
        err->line = s->line;
        err->col  = s->col;
        va_list args;
        va_start(args, fmt);
        vsnprintf(err->message, sizeof(err->message), fmt, args);
        va_end(args);
    }
    return -1;
}

// =============================================================================
// String parsing
// =============================================================================

// Append character to a dynamic string buffer
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

static char* strbuf_finish(StrBuf* sb) {
    strbuf_push(sb, '\0');
    return sb->data;
}

static void strbuf_free(StrBuf* sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

// Encode a Unicode code point as UTF-8 into the string buffer
static int strbuf_push_utf8(StrBuf* sb, uint32_t cp) {
    if (cp <= 0x7F) {
        return strbuf_push(sb, (char)cp);
    } else if (cp <= 0x7FF) {
        if (strbuf_push(sb, (char)(0xC0 | (cp >> 6))) != 0) return -1;
        return strbuf_push(sb, (char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        if (strbuf_push(sb, (char)(0xE0 | (cp >> 12))) != 0) return -1;
        if (strbuf_push(sb, (char)(0x80 | ((cp >> 6) & 0x3F))) != 0) return -1;
        return strbuf_push(sb, (char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        if (strbuf_push(sb, (char)(0xF0 | (cp >> 18))) != 0) return -1;
        if (strbuf_push(sb, (char)(0x80 | ((cp >> 12) & 0x3F))) != 0) return -1;
        if (strbuf_push(sb, (char)(0x80 | ((cp >> 6) & 0x3F))) != 0) return -1;
        return strbuf_push(sb, (char)(0x80 | (cp & 0x3F)));
    }
    return -1; // Invalid code point
}

static int hex_digit_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

// Parse a basic string (double-quoted, supports escapes)
static int parse_basic_string(Scanner* s, TomlError* err, char** out) {
    StrBuf sb;
    strbuf_init(&sb);

    // Opening quote already consumed? No - consume it here.
    scanner_advance(s); // consume opening "

    while (!scanner_eof(s)) {
        char c = scanner_peek(s);
        if (c == '"') {
            scanner_advance(s);
            *out = strbuf_finish(&sb);
            return 0;
        }
        if (c == '\n' || c == '\r') {
            strbuf_free(&sb);
            return parse_error(s, err, "newline in basic string");
        }
        if (c == '\\') {
            scanner_advance(s); // consume backslash
            char esc = scanner_advance(s);
            switch (esc) {
                case 'b':  strbuf_push(&sb, '\b'); break;
                case 't':  strbuf_push(&sb, '\t'); break;
                case 'n':  strbuf_push(&sb, '\n'); break;
                case 'f':  strbuf_push(&sb, '\f'); break;
                case 'r':  strbuf_push(&sb, '\r'); break;
                case '"':  strbuf_push(&sb, '"');  break;
                case '\\': strbuf_push(&sb, '\\'); break;
                case 'u': case 'U': {
                    int digits = (esc == 'u') ? 4 : 8;
                    uint32_t cp = 0;
                    for (int i = 0; i < digits; i++) {
                        int d = hex_digit_val(scanner_peek(s));
                        if (d < 0) {
                            strbuf_free(&sb);
                            return parse_error(s, err, "invalid unicode escape");
                        }
                        cp = (cp << 4) | (uint32_t)d;
                        scanner_advance(s);
                    }
                    if (strbuf_push_utf8(&sb, cp) != 0) {
                        strbuf_free(&sb);
                        return parse_error(s, err, "invalid unicode code point");
                    }
                    break;
                }
                default:
                    strbuf_free(&sb);
                    return parse_error(s, err, "invalid escape sequence '\\%c'", esc);
            }
        } else {
            scanner_advance(s);
            strbuf_push(&sb, c);
        }
    }
    strbuf_free(&sb);
    return parse_error(s, err, "unterminated basic string");
}

// Parse a multi-line basic string (""" ... """)
static int parse_ml_basic_string(Scanner* s, TomlError* err, char** out) {
    StrBuf sb;
    strbuf_init(&sb);

    // Skip the opening """
    scanner_advance(s); scanner_advance(s); scanner_advance(s);

    // A newline immediately after """ is trimmed
    if (scanner_peek(s) == '\r') scanner_advance(s);
    if (scanner_peek(s) == '\n') scanner_advance(s);

    while (!scanner_eof(s)) {
        char c = scanner_peek(s);
        if (c == '"' && scanner_peek_at(s, 1) == '"' && scanner_peek_at(s, 2) == '"') {
            scanner_advance(s); scanner_advance(s); scanner_advance(s);
            *out = strbuf_finish(&sb);
            return 0;
        }
        if (c == '\\') {
            scanner_advance(s);
            char esc = scanner_peek(s);
            // Line ending backslash: trim whitespace/newlines
            if (esc == '\n' || esc == '\r') {
                while (!scanner_eof(s)) {
                    char w = scanner_peek(s);
                    if (w == ' ' || w == '\t' || w == '\n' || w == '\r') {
                        scanner_advance(s);
                    } else {
                        break;
                    }
                }
            } else {
                // Same escape handling as basic string
                scanner_advance(s);
                switch (esc) {
                    case 'b':  strbuf_push(&sb, '\b'); break;
                    case 't':  strbuf_push(&sb, '\t'); break;
                    case 'n':  strbuf_push(&sb, '\n'); break;
                    case 'f':  strbuf_push(&sb, '\f'); break;
                    case 'r':  strbuf_push(&sb, '\r'); break;
                    case '"':  strbuf_push(&sb, '"');  break;
                    case '\\': strbuf_push(&sb, '\\'); break;
                    case 'u': case 'U': {
                        int digits = (esc == 'u') ? 4 : 8;
                        uint32_t cp = 0;
                        for (int i = 0; i < digits; i++) {
                            int d = hex_digit_val(scanner_peek(s));
                            if (d < 0) { strbuf_free(&sb); return parse_error(s, err, "invalid unicode escape"); }
                            cp = (cp << 4) | (uint32_t)d;
                            scanner_advance(s);
                        }
                        strbuf_push_utf8(&sb, cp);
                        break;
                    }
                    default:
                        strbuf_free(&sb);
                        return parse_error(s, err, "invalid escape '\\%c'", esc);
                }
            }
        } else {
            scanner_advance(s);
            strbuf_push(&sb, c);
        }
    }
    strbuf_free(&sb);
    return parse_error(s, err, "unterminated multi-line basic string");
}

// Parse a literal string (single-quoted, no escapes)
static int parse_literal_string(Scanner* s, TomlError* err, char** out) {
    StrBuf sb;
    strbuf_init(&sb);
    scanner_advance(s); // consume opening '

    while (!scanner_eof(s)) {
        char c = scanner_peek(s);
        if (c == '\'') {
            scanner_advance(s);
            *out = strbuf_finish(&sb);
            return 0;
        }
        if (c == '\n' || c == '\r') {
            strbuf_free(&sb);
            return parse_error(s, err, "newline in literal string");
        }
        scanner_advance(s);
        strbuf_push(&sb, c);
    }
    strbuf_free(&sb);
    return parse_error(s, err, "unterminated literal string");
}

// Parse a multi-line literal string (''' ... ''')
static int parse_ml_literal_string(Scanner* s, TomlError* err, char** out) {
    StrBuf sb;
    strbuf_init(&sb);

    scanner_advance(s); scanner_advance(s); scanner_advance(s); // consume '''

    // A newline immediately after ''' is trimmed
    if (scanner_peek(s) == '\r') scanner_advance(s);
    if (scanner_peek(s) == '\n') scanner_advance(s);

    while (!scanner_eof(s)) {
        char c = scanner_peek(s);
        if (c == '\'' && scanner_peek_at(s, 1) == '\'' && scanner_peek_at(s, 2) == '\'') {
            scanner_advance(s); scanner_advance(s); scanner_advance(s);
            *out = strbuf_finish(&sb);
            return 0;
        }
        scanner_advance(s);
        strbuf_push(&sb, c);
    }
    strbuf_free(&sb);
    return parse_error(s, err, "unterminated multi-line literal string");
}

// Parse any string value (basic, literal, multi-line variants)
static int parse_string(Scanner* s, TomlError* err, char** out) {
    char c = scanner_peek(s);
    if (c == '"') {
        if (scanner_peek_at(s, 1) == '"' && scanner_peek_at(s, 2) == '"') {
            return parse_ml_basic_string(s, err, out);
        }
        return parse_basic_string(s, err, out);
    }
    if (c == '\'') {
        if (scanner_peek_at(s, 1) == '\'' && scanner_peek_at(s, 2) == '\'') {
            return parse_ml_literal_string(s, err, out);
        }
        return parse_literal_string(s, err, out);
    }
    return parse_error(s, err, "expected string");
}

// =============================================================================
// Key parsing
// =============================================================================

static inline bool is_bare_key_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// Parse a single key (bare or quoted)
static int parse_simple_key(Scanner* s, TomlError* err, char** out) {
    char c = scanner_peek(s);
    if (c == '"' || c == '\'') {
        return parse_string(s, err, out);
    }
    // Bare key
    if (!is_bare_key_char(c)) {
        return parse_error(s, err, "expected key");
    }
    StrBuf sb;
    strbuf_init(&sb);
    while (!scanner_eof(s) && is_bare_key_char(scanner_peek(s))) {
        strbuf_push(&sb, scanner_peek(s));
        scanner_advance(s);
    }
    *out = strbuf_finish(&sb);
    return 0;
}

// Parse a dotted key into an array of key parts.
// Returns the number of parts, or -1 on error.
// Caller must free each part and the array.
#define MAX_KEY_PARTS 64

static int parse_key(Scanner* s, TomlError* err, char** parts, int* count) {
    *count = 0;
    char* first = NULL;
    if (parse_simple_key(s, err, &first) != 0) return -1;
    parts[(*count)++] = first;

    while (scanner_peek(s) == '.') {
        scanner_advance(s); // consume '.'
        if (*count >= MAX_KEY_PARTS) {
            return parse_error(s, err, "key has too many dotted parts");
        }
        char* part = NULL;
        if (parse_simple_key(s, err, &part) != 0) return -1;
        parts[(*count)++] = part;
    }
    return 0;
}

// =============================================================================
// Number parsing
// =============================================================================

static int parse_number(Scanner* s, TomlError* err, TomlValue** out) {
    // Collect the token
    StrBuf sb;
    strbuf_init(&sb);

    bool is_negative = false;
    bool is_positive = false;
    char c = scanner_peek(s);

    if (c == '+' || c == '-') {
        if (c == '-') is_negative = true;
        else is_positive = true;
        strbuf_push(&sb, c);
        scanner_advance(s);
        c = scanner_peek(s);
    }

    // Check for special float values: inf, nan
    if (c == 'i' || c == 'n') {
        char word[4] = {0};
        for (int i = 0; i < 3 && !scanner_eof(s); i++) {
            word[i] = scanner_peek(s);
            scanner_advance(s);
        }
        if (strcmp(word, "inf") == 0) {
            strbuf_free(&sb);
            TomlValue* v = value_new(TOML_FLOAT);
            v->as.floating = is_negative ? -INFINITY : INFINITY;
            *out = v;
            return 0;
        }
        if (strcmp(word, "nan") == 0) {
            strbuf_free(&sb);
            TomlValue* v = value_new(TOML_FLOAT);
            v->as.floating = NAN;
            *out = v;
            return 0;
        }
        strbuf_free(&sb);
        return parse_error(s, err, "invalid number");
    }

    // Check prefix for hex, octal, binary
    bool is_hex = false, is_oct = false, is_bin = false;
    if (c == '0' && !is_negative && !is_positive) {
        char next = scanner_peek_at(s, 1);
        if (next == 'x' || next == 'X') { is_hex = true; scanner_advance(s); scanner_advance(s); sb.len = 0; }
        else if (next == 'o' || next == 'O') { is_oct = true; scanner_advance(s); scanner_advance(s); sb.len = 0; }
        else if (next == 'b' || next == 'B') { is_bin = true; scanner_advance(s); scanner_advance(s); sb.len = 0; }
    } else if (c == '0') {
        char next = scanner_peek_at(s, 1);
        if (next == 'x' || next == 'X') { is_hex = true; scanner_advance(s); scanner_advance(s); sb.len = 1; /* keep sign */ }
        else if (next == 'o' || next == 'O') { is_oct = true; scanner_advance(s); scanner_advance(s); sb.len = 1; }
        else if (next == 'b' || next == 'B') { is_bin = true; scanner_advance(s); scanner_advance(s); sb.len = 1; }
    }

    // Collect digits (skipping underscores)
    bool has_dot = false, has_exp = false;
    while (!scanner_eof(s)) {
        c = scanner_peek(s);
        if (c == '_') {
            scanner_advance(s);
            continue;
        }
        if (is_hex) {
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                strbuf_push(&sb, c); scanner_advance(s);
            } else break;
        } else if (is_oct) {
            if (c >= '0' && c <= '7') { strbuf_push(&sb, c); scanner_advance(s); }
            else break;
        } else if (is_bin) {
            if (c == '0' || c == '1') { strbuf_push(&sb, c); scanner_advance(s); }
            else break;
        } else {
            if (c >= '0' && c <= '9') {
                strbuf_push(&sb, c); scanner_advance(s);
            } else if (c == '.' && !has_dot && !has_exp) {
                has_dot = true;
                strbuf_push(&sb, c); scanner_advance(s);
            } else if ((c == 'e' || c == 'E') && !has_exp) {
                has_exp = true;
                strbuf_push(&sb, c); scanner_advance(s);
                // Optional sign after exponent
                if (scanner_peek(s) == '+' || scanner_peek(s) == '-') {
                    strbuf_push(&sb, scanner_peek(s)); scanner_advance(s);
                }
            } else {
                break;
            }
        }
    }

    strbuf_push(&sb, '\0');
    char* num_str = sb.data;

    if (has_dot || has_exp) {
        // Float
        TomlValue* v = value_new(TOML_FLOAT);
        v->as.floating = strtod(num_str, NULL);
        *out = v;
    } else if (is_hex) {
        TomlValue* v = value_new(TOML_INTEGER);
        v->as.integer = (int64_t)strtoull(num_str, NULL, 16);
        if (is_negative) v->as.integer = -v->as.integer;
        *out = v;
    } else if (is_oct) {
        TomlValue* v = value_new(TOML_INTEGER);
        v->as.integer = (int64_t)strtoull(num_str, NULL, 8);
        if (is_negative) v->as.integer = -v->as.integer;
        *out = v;
    } else if (is_bin) {
        TomlValue* v = value_new(TOML_INTEGER);
        v->as.integer = (int64_t)strtoull(num_str, NULL, 2);
        if (is_negative) v->as.integer = -v->as.integer;
        *out = v;
    } else {
        TomlValue* v = value_new(TOML_INTEGER);
        v->as.integer = strtoll(num_str, NULL, 10);
        *out = v;
    }

    strbuf_free(&sb);
    return 0;
}

// =============================================================================
// Datetime parsing
// =============================================================================

// Checks if current position looks like a datetime (starts with digit, has -)
static bool looks_like_datetime(Scanner* s) {
    // TOML datetimes: YYYY-MM-DD or time HH:MM:SS
    // Check for 4 digits followed by '-'
    if (s->pos + 4 >= s->len) return false;
    for (int i = 0; i < 4; i++) {
        char c = s->src[s->pos + (size_t)i];
        if (c < '0' || c > '9') return false;
    }
    return s->src[s->pos + 4] == '-';
}

static int parse_datetime(Scanner* s, TomlError* err, char** out) {
    // Collect the datetime string as-is (we store it as a string)
    StrBuf sb;
    strbuf_init(&sb);

    while (!scanner_eof(s)) {
        char c = scanner_peek(s);
        if ((c >= '0' && c <= '9') || c == '-' || c == ':' || c == '.' ||
            c == 'T' || c == 't' || c == 'Z' || c == 'z' || c == '+' || c == ' ') {
            // Space is only valid between date and time parts
            if (c == ' ') {
                // Check if next char looks like time part
                char next = scanner_peek_at(s, 1);
                if (next < '0' || next > '9') break;
            }
            strbuf_push(&sb, c);
            scanner_advance(s);
        } else {
            break;
        }
    }

    *out = strbuf_finish(&sb);
    return 0;
}

// =============================================================================
// Value parsing (forward declaration needed for recursion)
// =============================================================================

static int parse_value(Scanner* s, TomlError* err, TomlValue** out);

// Parse an inline table { key = val, ... }
static int parse_inline_table(Scanner* s, TomlError* err, TomlValue** out) {
    scanner_advance(s); // consume '{'
    TomlTable* t = table_new();
    if (!t) return parse_error(s, err, "out of memory");

    scanner_skip_whitespace(s);
    if (scanner_peek(s) == '}') {
        scanner_advance(s);
        TomlValue* v = value_new(TOML_INLINE_TABLE);
        v->as.table = t;
        *out = v;
        return 0;
    }

    while (true) {
        scanner_skip_whitespace(s);

        // Parse key (possibly dotted)
        char* parts[MAX_KEY_PARTS];
        int part_count = 0;
        if (parse_key(s, err, parts, &part_count) != 0) {
            toml_free(t);
            return -1;
        }

        scanner_skip_whitespace(s);
        if (scanner_peek(s) != '=') {
            for (int i = 0; i < part_count; i++) free(parts[i]);
            toml_free(t);
            return parse_error(s, err, "expected '=' in inline table");
        }
        scanner_advance(s); // consume '='
        scanner_skip_whitespace(s);

        TomlValue* val = NULL;
        if (parse_value(s, err, &val) != 0) {
            for (int i = 0; i < part_count; i++) free(parts[i]);
            toml_free(t);
            return -1;
        }

        // Navigate dotted key
        TomlTable* target = t;
        for (int i = 0; i < part_count - 1; i++) {
            TomlEntry* entry = table_find(target, parts[i]);
            if (entry && (entry->value->type == TOML_TABLE || entry->value->type == TOML_INLINE_TABLE)) {
                target = entry->value->as.table;
            } else {
                // Create intermediate table
                TomlTable* sub = table_new();
                TomlValue* sv = value_new(TOML_TABLE);
                sv->as.table = sub;
                table_set(target, parts[i], sv);
                target = sub;
            }
        }
        table_set(target, parts[part_count - 1], val);
        for (int i = 0; i < part_count; i++) free(parts[i]);

        scanner_skip_whitespace(s);
        if (scanner_peek(s) == ',') {
            scanner_advance(s);
            continue;
        }
        if (scanner_peek(s) == '}') {
            scanner_advance(s);
            break;
        }
        toml_free(t);
        return parse_error(s, err, "expected ',' or '}' in inline table");
    }

    TomlValue* v = value_new(TOML_INLINE_TABLE);
    v->as.table = t;
    *out = v;
    return 0;
}

// Parse an array [ val, val, ... ]
static int parse_array(Scanner* s, TomlError* err, TomlValue** out) {
    scanner_advance(s); // consume '['
    TomlArray* arr = array_new();
    if (!arr) return parse_error(s, err, "out of memory");

    scanner_skip_whitespace_and_newlines(s);
    if (scanner_peek(s) == ']') {
        scanner_advance(s);
        TomlValue* v = value_new(TOML_ARRAY);
        v->as.array = arr;
        *out = v;
        return 0;
    }

    while (true) {
        scanner_skip_whitespace_and_newlines(s);

        TomlValue* val = NULL;
        if (parse_value(s, err, &val) != 0) {
            // Free partial array
            for (int i = 0; i < arr->count; i++) toml_value_free(arr->items[i]);
            free(arr->items);
            free(arr);
            return -1;
        }
        array_push(arr, val);

        scanner_skip_whitespace_and_newlines(s);
        if (scanner_peek(s) == ',') {
            scanner_advance(s);
            scanner_skip_whitespace_and_newlines(s);
            // Trailing comma before ]
            if (scanner_peek(s) == ']') {
                scanner_advance(s);
                break;
            }
            continue;
        }
        if (scanner_peek(s) == ']') {
            scanner_advance(s);
            break;
        }
        for (int i = 0; i < arr->count; i++) toml_value_free(arr->items[i]);
        free(arr->items);
        free(arr);
        return parse_error(s, err, "expected ',' or ']' in array");
    }

    TomlValue* v = value_new(TOML_ARRAY);
    v->as.array = arr;
    *out = v;
    return 0;
}

// Parse a value (string, number, bool, datetime, array, inline table)
static int parse_value(Scanner* s, TomlError* err, TomlValue** out) {
    scanner_skip_whitespace(s);
    char c = scanner_peek(s);

    // String
    if (c == '"' || c == '\'') {
        char* str = NULL;
        if (parse_string(s, err, &str) != 0) return -1;
        TomlValue* v = value_new(TOML_STRING);
        v->as.string = str;
        *out = v;
        return 0;
    }

    // Boolean
    if (c == 't' && scanner_peek_at(s, 1) == 'r' && scanner_peek_at(s, 2) == 'u' && scanner_peek_at(s, 3) == 'e') {
        scanner_advance(s); scanner_advance(s); scanner_advance(s); scanner_advance(s);
        TomlValue* v = value_new(TOML_BOOL);
        v->as.boolean = true;
        *out = v;
        return 0;
    }
    if (c == 'f' && scanner_peek_at(s, 1) == 'a' && scanner_peek_at(s, 2) == 'l' &&
        scanner_peek_at(s, 3) == 's' && scanner_peek_at(s, 4) == 'e') {
        scanner_advance(s); scanner_advance(s); scanner_advance(s); scanner_advance(s); scanner_advance(s);
        TomlValue* v = value_new(TOML_BOOL);
        v->as.boolean = false;
        *out = v;
        return 0;
    }

    // Inline table
    if (c == '{') {
        return parse_inline_table(s, err, out);
    }

    // Array
    if (c == '[') {
        return parse_array(s, err, out);
    }

    // Number or datetime
    if (c == '+' || c == '-' || (c >= '0' && c <= '9')) {
        // Check for datetime first
        if ((c >= '0' && c <= '9') && looks_like_datetime(s)) {
            char* dt = NULL;
            if (parse_datetime(s, err, &dt) != 0) return -1;
            TomlValue* v = value_new(TOML_DATETIME);
            v->as.string = dt;
            *out = v;
            return 0;
        }
        return parse_number(s, err, out);
    }

    // inf, nan without sign
    if (c == 'i' || c == 'n') {
        return parse_number(s, err, out);
    }

    return parse_error(s, err, "unexpected character '%c'", c);
}

// =============================================================================
// Document-level parsing
// =============================================================================

// Navigate to (or create) a nested table given a dotted key path.
// Used for [table] and [[array]] headers.
static TomlTable* navigate_to_table(TomlTable* root, char** parts, int count,
                                    bool create_arrays, TomlError* err, Scanner* s) {
    TomlTable* current = root;
    for (int i = 0; i < count; i++) {
        TomlEntry* entry = table_find(current, parts[i]);
        if (!entry) {
            if (create_arrays && i == count - 1) {
                // For [[array]], create an array and add a table
                TomlArray* arr = array_new();
                TomlTable* new_t = table_new();
                TomlValue* tv = value_new(TOML_TABLE);
                tv->as.table = new_t;
                array_push(arr, tv);
                TomlValue* av = value_new(TOML_ARRAY);
                av->as.array = arr;
                table_set(current, parts[i], av);
                return new_t;
            }
            // Create intermediate table
            TomlTable* new_t = table_new();
            TomlValue* v = value_new(TOML_TABLE);
            v->as.table = new_t;
            table_set(current, parts[i], v);
            current = new_t;
        } else if (entry->value->type == TOML_TABLE || entry->value->type == TOML_INLINE_TABLE) {
            current = entry->value->as.table;
            if (create_arrays && i == count - 1) {
                // Convert existing table into an array-of-tables entry? No.
                // This is an error: key already exists as a table.
                parse_error(s, err, "key '%s' is already defined as a table", parts[i]);
                return NULL;
            }
        } else if (entry->value->type == TOML_ARRAY) {
            TomlArray* arr = entry->value->as.array;
            if (create_arrays && i == count - 1) {
                // Append a new table to the array
                TomlTable* new_t = table_new();
                TomlValue* tv = value_new(TOML_TABLE);
                tv->as.table = new_t;
                array_push(arr, tv);
                return new_t;
            }
            // Navigate into the last element of the array
            if (arr->count == 0) {
                parse_error(s, err, "empty array at key '%s'", parts[i]);
                return NULL;
            }
            TomlValue* last = arr->items[arr->count - 1];
            if (last->type != TOML_TABLE && last->type != TOML_INLINE_TABLE) {
                parse_error(s, err, "array element at key '%s' is not a table", parts[i]);
                return NULL;
            }
            current = last->as.table;
        } else {
            parse_error(s, err, "key '%s' already exists as a non-table value", parts[i]);
            return NULL;
        }
    }
    return current;
}

// Main parse function
int toml_parse(const char* input, size_t len, TomlTable** out, TomlError* err) {
    if (!input || !out) return -1;
    if (err) { err->line = 0; err->col = 0; err->message[0] = '\0'; }

    Scanner scanner;
    Scanner* s = &scanner;
    scanner_init(s, input, len);

    TomlTable* root = table_new();
    if (!root) {
        if (err) snprintf(err->message, sizeof(err->message), "out of memory");
        return -1;
    }

    TomlTable* current_table = root;

    while (!scanner_eof(s)) {
        scanner_skip_whitespace_and_newlines(s);
        if (scanner_eof(s)) break;

        char c = scanner_peek(s);

        // Table header [key] or [[key]]
        if (c == '[') {
            bool is_array_table = false;
            scanner_advance(s); // consume first '['
            if (scanner_peek(s) == '[') {
                is_array_table = true;
                scanner_advance(s); // consume second '['
            }

            scanner_skip_whitespace(s);

            char* parts[MAX_KEY_PARTS];
            int part_count = 0;
            if (parse_key(s, err, parts, &part_count) != 0) {
                toml_free(root);
                return -1;
            }

            scanner_skip_whitespace(s);

            if (is_array_table) {
                if (scanner_peek(s) != ']' || scanner_peek_at(s, 1) != ']') {
                    for (int i = 0; i < part_count; i++) free(parts[i]);
                    toml_free(root);
                    return parse_error(s, err, "expected ']]'");
                }
                scanner_advance(s); scanner_advance(s);
            } else {
                if (scanner_peek(s) != ']') {
                    for (int i = 0; i < part_count; i++) free(parts[i]);
                    toml_free(root);
                    return parse_error(s, err, "expected ']'");
                }
                scanner_advance(s);
            }

            scanner_skip_to_newline(s);

            if (is_array_table) {
                current_table = navigate_to_table(root, parts, part_count, true, err, s);
            } else {
                current_table = navigate_to_table(root, parts, part_count, false, err, s);
            }

            for (int i = 0; i < part_count; i++) free(parts[i]);

            if (!current_table) {
                toml_free(root);
                return -1;
            }
            continue;
        }

        // Comment line (already handled by skip, but just in case)
        if (c == '#') {
            while (!scanner_eof(s) && scanner_peek(s) != '\n') scanner_advance(s);
            continue;
        }

        // Key = Value pair
        char* parts[MAX_KEY_PARTS];
        int part_count = 0;
        if (parse_key(s, err, parts, &part_count) != 0) {
            toml_free(root);
            return -1;
        }

        scanner_skip_whitespace(s);
        if (scanner_peek(s) != '=') {
            for (int i = 0; i < part_count; i++) free(parts[i]);
            toml_free(root);
            return parse_error(s, err, "expected '='");
        }
        scanner_advance(s); // consume '='
        scanner_skip_whitespace(s);

        TomlValue* val = NULL;
        if (parse_value(s, err, &val) != 0) {
            for (int i = 0; i < part_count; i++) free(parts[i]);
            toml_free(root);
            return -1;
        }

        // Navigate dotted key and set value
        TomlTable* target = current_table;
        for (int i = 0; i < part_count - 1; i++) {
            TomlEntry* entry = table_find(target, parts[i]);
            if (entry && (entry->value->type == TOML_TABLE || entry->value->type == TOML_INLINE_TABLE)) {
                target = entry->value->as.table;
            } else if (entry && entry->value->type == TOML_ARRAY) {
                // Navigate into the last table element of an array-of-tables
                TomlArray* arr = entry->value->as.array;
                if (arr->count > 0 && (arr->items[arr->count-1]->type == TOML_TABLE ||
                                        arr->items[arr->count-1]->type == TOML_INLINE_TABLE)) {
                    target = arr->items[arr->count-1]->as.table;
                } else {
                    for (int j = 0; j < part_count; j++) free(parts[j]);
                    toml_value_free(val);
                    toml_free(root);
                    return parse_error(s, err, "key '%s' is not a table", parts[i]);
                }
            } else if (!entry) {
                // Create intermediate table
                TomlTable* sub = table_new();
                TomlValue* sv = value_new(TOML_TABLE);
                sv->as.table = sub;
                table_set(target, parts[i], sv);
                target = sub;
            } else {
                for (int j = 0; j < part_count; j++) free(parts[j]);
                toml_value_free(val);
                toml_free(root);
                return parse_error(s, err, "key '%s' is not a table", parts[i]);
            }
        }

        table_set(target, parts[part_count - 1], val);
        for (int i = 0; i < part_count; i++) free(parts[i]);

        scanner_skip_to_newline(s);
    }

    *out = root;
    return 0;
}

// =============================================================================
// toml_get â€” dotted key path lookup
// =============================================================================

const TomlValue* toml_get(const TomlTable* table, const char* key_path) {
    if (!table || !key_path) return NULL;

    // Split key_path on '.'
    // Handle quoted segments? For simplicity, split on unquoted dots.
    const TomlTable* current = table;
    const char* p = key_path;

    while (*p) {
        // Extract next key segment
        char segment[256];
        int seg_len = 0;

        if (*p == '"') {
            // Quoted key segment
            p++; // skip opening quote
            while (*p && *p != '"' && seg_len < 255) {
                segment[seg_len++] = *p++;
            }
            if (*p == '"') p++; // skip closing quote
        } else {
            while (*p && *p != '.' && seg_len < 255) {
                segment[seg_len++] = *p++;
            }
        }
        segment[seg_len] = '\0';

        // Skip the dot separator
        if (*p == '.') p++;

        // Look up segment in current table
        TomlEntry* entry = NULL;
        for (TomlEntry* e = current->head; e; e = e->next) {
            if (strcmp(e->key, segment) == 0) {
                entry = e;
                break;
            }
        }
        if (!entry) return NULL;

        // If there are more segments, descend into the table
        if (*p) {
            if (entry->value->type == TOML_TABLE || entry->value->type == TOML_INLINE_TABLE) {
                current = entry->value->as.table;
            } else if (entry->value->type == TOML_ARRAY) {
                // For array-of-tables, look at the last element
                TomlArray* arr = entry->value->as.array;
                if (arr->count == 0) return NULL;
                TomlValue* last = arr->items[arr->count - 1];
                if (last->type == TOML_TABLE || last->type == TOML_INLINE_TABLE) {
                    current = last->as.table;
                } else {
                    return NULL;
                }
            } else {
                return NULL;
            }
        } else {
            return entry->value;
        }
    }
    return NULL;
}

// =============================================================================
// toml_free â€” recursive memory cleanup
// =============================================================================

void toml_value_free(TomlValue* val) {
    if (!val) return;
    switch (val->type) {
        case TOML_STRING:
        case TOML_DATETIME:
            free(val->as.string);
            break;
        case TOML_INTEGER:
        case TOML_FLOAT:
        case TOML_BOOL:
            break;
        case TOML_ARRAY: {
            TomlArray* arr = val->as.array;
            if (arr) {
                for (int i = 0; i < arr->count; i++) {
                    toml_value_free(arr->items[i]);
                }
                free(arr->items);
                free(arr);
            }
            break;
        }
        case TOML_TABLE:
        case TOML_INLINE_TABLE: {
            TomlTable* t = val->as.table;
            if (t) {
                TomlEntry* e = t->head;
                while (e) {
                    TomlEntry* next = e->next;
                    free(e->key);
                    toml_value_free(e->value);
                    free(e);
                    e = next;
                }
                free(t);
            }
            break;
        }
    }
    free(val);
}

void toml_free(TomlTable* table) {
    if (!table) return;
    TomlEntry* e = table->head;
    while (e) {
        TomlEntry* next = e->next;
        free(e->key);
        toml_value_free(e->value);
        free(e);
        e = next;
    }
    free(table);
}
