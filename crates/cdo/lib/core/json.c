#include "core/json.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

// --- Internal Structures ---

typedef struct {
    char*       key;
    JsonValue*  value;
} JsonObjectEntry;

struct JsonValue {
    JsonType type;
    union {
        bool            bool_val;
        double          number_val;
        char*           string_val;
        struct {
            JsonValue** items;
            int         count;
            int         capacity;
        } array;
        struct {
            JsonObjectEntry*    entries;
            int                 count;
            int                 capacity;
        } object;
    } data;
};

// --- Parser State ---

typedef struct {
    const char* input;
    size_t      len;
    size_t      pos;
    int         line;
    int         col;
    JsonError*  err;
} Parser;

// --- Forward Declarations ---

static JsonValue* parse_value(Parser* p);
static JsonValue* parse_null(Parser* p);
static JsonValue* parse_bool(Parser* p);
static JsonValue* parse_number(Parser* p);
static JsonValue* parse_string_value(Parser* p);
static JsonValue* parse_array(Parser* p);
static JsonValue* parse_object(Parser* p);
static char*      parse_string_raw(Parser* p);
static void       skip_whitespace(Parser* p);
static bool       parser_eof(const Parser* p);
static char       parser_peek(const Parser* p);
static char       parser_advance(Parser* p);
static bool       parser_match(Parser* p, char c);
static void       parser_error(Parser* p, const char* fmt, ...);

// --- Allocation Helpers ---

static JsonValue* json_alloc(JsonType type) {
    JsonValue* val = (JsonValue*)calloc(1, sizeof(JsonValue));
    if (val) {
        val->type = type;
    }
    return val;
}

// --- Public API ---

int json_parse(const char* input, size_t len, JsonValue** out, JsonError* err) {
    if (!input || !out) {
        if (err) {
            err->line = 1;
            err->col = 1;
            snprintf(err->message, sizeof(err->message), "null input or output pointer");
        }
        return -1;
    }

    Parser p = {
        .input = input,
        .len   = len,
        .pos   = 0,
        .line  = 1,
        .col   = 1,
        .err   = err,
    };

    if (err) {
        err->line = 0;
        err->col = 0;
        err->message[0] = '\0';
    }

    skip_whitespace(&p);

    if (parser_eof(&p)) {
        parser_error(&p, "unexpected end of input");
        return -1;
    }

    JsonValue* val = parse_value(&p);
    if (!val) {
        return -1;
    }

    // Check for trailing content
    skip_whitespace(&p);
    if (!parser_eof(&p)) {
        parser_error(&p, "unexpected content after JSON value");
        json_free(val);
        return -1;
    }

    *out = val;
    return 0;
}

void json_free(JsonValue* val) {
    if (!val) return;

    switch (val->type) {
        case JSON_STRING:
            free(val->data.string_val);
            break;
        case JSON_ARRAY:
            for (int i = 0; i < val->data.array.count; i++) {
                json_free(val->data.array.items[i]);
            }
            free(val->data.array.items);
            break;
        case JSON_OBJECT:
            for (int i = 0; i < val->data.object.count; i++) {
                free(val->data.object.entries[i].key);
                json_free(val->data.object.entries[i].value);
            }
            free(val->data.object.entries);
            break;
        default:
            break;
    }

    free(val);
}

const JsonValue* json_get(const JsonValue* val, const char* key) {
    if (!val || !key || val->type != JSON_OBJECT) {
        return NULL;
    }

    for (int i = 0; i < val->data.object.count; i++) {
        if (strcmp(val->data.object.entries[i].key, key) == 0) {
            return val->data.object.entries[i].value;
        }
    }

    return NULL;
}

JsonType json_type(const JsonValue* val) {
    return val ? val->type : JSON_NULL;
}

bool json_bool(const JsonValue* val) {
    return val && val->type == JSON_BOOL ? val->data.bool_val : false;
}

double json_number(const JsonValue* val) {
    return val && val->type == JSON_NUMBER ? val->data.number_val : 0.0;
}

const char* json_string(const JsonValue* val) {
    return val && val->type == JSON_STRING ? val->data.string_val : NULL;
}

int json_array_len(const JsonValue* val) {
    return val && val->type == JSON_ARRAY ? val->data.array.count : 0;
}

const JsonValue* json_array_get(const JsonValue* val, int index) {
    if (!val || val->type != JSON_ARRAY) return NULL;
    if (index < 0 || index >= val->data.array.count) return NULL;
    return val->data.array.items[index];
}

int json_object_len(const JsonValue* val) {
    return val && val->type == JSON_OBJECT ? val->data.object.count : 0;
}

// --- Parser Internals ---

static bool parser_eof(const Parser* p) {
    return p->pos >= p->len;
}

static char parser_peek(const Parser* p) {
    if (parser_eof(p)) return '\0';
    return p->input[p->pos];
}

static char parser_advance(Parser* p) {
    if (parser_eof(p)) return '\0';
    char c = p->input[p->pos++];
    if (c == '\n') {
        p->line++;
        p->col = 1;
    } else {
        p->col++;
    }
    return c;
}

static bool parser_match(Parser* p, char c) {
    if (!parser_eof(p) && p->input[p->pos] == c) {
        parser_advance(p);
        return true;
    }
    return false;
}

static void parser_error(Parser* p, const char* fmt, ...) {
    if (!p->err) return;
    p->err->line = p->line;
    p->err->col = p->col;

    va_list args;
    va_start(args, fmt);
    vsnprintf(p->err->message, sizeof(p->err->message), fmt, args);
    va_end(args);
}

static void skip_whitespace(Parser* p) {
    while (!parser_eof(p)) {
        char c = parser_peek(p);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            parser_advance(p);
        } else {
            break;
        }
    }
}

// --- Value Parsing ---

static JsonValue* parse_value(Parser* p) {
    if (parser_eof(p)) {
        parser_error(p, "unexpected end of input");
        return NULL;
    }

    char c = parser_peek(p);

    switch (c) {
        case 'n': return parse_null(p);
        case 't': // fall through
        case 'f': return parse_bool(p);
        case '"': return parse_string_value(p);
        case '[': return parse_array(p);
        case '{': return parse_object(p);
        default:
            if (c == '-' || (c >= '0' && c <= '9')) {
                return parse_number(p);
            }
            parser_error(p, "unexpected character '%c'", c);
            return NULL;
    }
}

static JsonValue* parse_null(Parser* p) {
    if (p->pos + 4 <= p->len &&
        p->input[p->pos]     == 'n' &&
        p->input[p->pos + 1] == 'u' &&
        p->input[p->pos + 2] == 'l' &&
        p->input[p->pos + 3] == 'l') {
        for (int i = 0; i < 4; i++) parser_advance(p);
        return json_alloc(JSON_NULL);
    }
    parser_error(p, "expected 'null'");
    return NULL;
}

static JsonValue* parse_bool(Parser* p) {
    if (parser_peek(p) == 't') {
        if (p->pos + 4 <= p->len &&
            p->input[p->pos]     == 't' &&
            p->input[p->pos + 1] == 'r' &&
            p->input[p->pos + 2] == 'u' &&
            p->input[p->pos + 3] == 'e') {
            for (int i = 0; i < 4; i++) parser_advance(p);
            JsonValue* val = json_alloc(JSON_BOOL);
            if (val) val->data.bool_val = true;
            return val;
        }
        parser_error(p, "expected 'true'");
        return NULL;
    }

    if (p->pos + 5 <= p->len &&
        p->input[p->pos]     == 'f' &&
        p->input[p->pos + 1] == 'a' &&
        p->input[p->pos + 2] == 'l' &&
        p->input[p->pos + 3] == 's' &&
        p->input[p->pos + 4] == 'e') {
        for (int i = 0; i < 5; i++) parser_advance(p);
        JsonValue* val = json_alloc(JSON_BOOL);
        if (val) val->data.bool_val = false;
        return val;
    }
    parser_error(p, "expected 'false'");
    return NULL;
}

static JsonValue* parse_number(Parser* p) {
    size_t start = p->pos;

    // Optional negative sign
    if (parser_peek(p) == '-') {
        parser_advance(p);
    }

    // Integer part
    if (parser_peek(p) == '0') {
        parser_advance(p);
    } else if (parser_peek(p) >= '1' && parser_peek(p) <= '9') {
        while (!parser_eof(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9') {
            parser_advance(p);
        }
    } else {
        parser_error(p, "expected digit in number");
        return NULL;
    }

    // Fractional part
    if (!parser_eof(p) && parser_peek(p) == '.') {
        parser_advance(p);
        if (parser_eof(p) || parser_peek(p) < '0' || parser_peek(p) > '9') {
            parser_error(p, "expected digit after decimal point");
            return NULL;
        }
        while (!parser_eof(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9') {
            parser_advance(p);
        }
    }

    // Exponent part
    if (!parser_eof(p) && (parser_peek(p) == 'e' || parser_peek(p) == 'E')) {
        parser_advance(p);
        if (!parser_eof(p) && (parser_peek(p) == '+' || parser_peek(p) == '-')) {
            parser_advance(p);
        }
        if (parser_eof(p) || parser_peek(p) < '0' || parser_peek(p) > '9') {
            parser_error(p, "expected digit in exponent");
            return NULL;
        }
        while (!parser_eof(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9') {
            parser_advance(p);
        }
    }

    // Convert to double
    size_t num_len = p->pos - start;
    char buf[64];
    if (num_len >= sizeof(buf)) {
        parser_error(p, "number too long");
        return NULL;
    }
    memcpy(buf, p->input + start, num_len);
    buf[num_len] = '\0';

    char* end = NULL;
    double val_num = strtod(buf, &end);
    if (end != buf + num_len) {
        parser_error(p, "invalid number");
        return NULL;
    }

    JsonValue* val = json_alloc(JSON_NUMBER);
    if (val) val->data.number_val = val_num;
    return val;
}

// --- String Parsing ---

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int parse_unicode_escape(Parser* p, unsigned int* codepoint) {
    *codepoint = 0;
    for (int i = 0; i < 4; i++) {
        if (parser_eof(p)) {
            parser_error(p, "unexpected end of input in unicode escape");
            return -1;
        }
        int d = hex_digit(parser_peek(p));
        if (d < 0) {
            parser_error(p, "invalid hex digit in unicode escape");
            return -1;
        }
        *codepoint = (*codepoint << 4) | (unsigned int)d;
        parser_advance(p);
    }
    return 0;
}

static int encode_utf8(unsigned int codepoint, char* buf) {
    if (codepoint <= 0x7F) {
        buf[0] = (char)codepoint;
        return 1;
    } else if (codepoint <= 0x7FF) {
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint <= 0xFFFF) {
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint <= 0x10FFFF) {
        buf[0] = (char)(0xF0 | (codepoint >> 18));
        buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

static char* parse_string_raw(Parser* p) {
    if (!parser_match(p, '"')) {
        parser_error(p, "expected '\"'");
        return NULL;
    }

    // Dynamic buffer for the string
    size_t capacity = 64;
    size_t length = 0;
    char* buf = (char*)malloc(capacity);
    if (!buf) {
        parser_error(p, "out of memory");
        return NULL;
    }

    while (!parser_eof(p)) {
        char c = parser_peek(p);

        if (c == '"') {
            parser_advance(p);
            // Null-terminate and return
            if (length + 1 > capacity) {
                capacity = length + 1;
                char* tmp = (char*)realloc(buf, capacity);
                if (!tmp) { free(buf); parser_error(p, "out of memory"); return NULL; }
                buf = tmp;
            }
            buf[length] = '\0';
            return buf;
        }

        if (c == '\\') {
            parser_advance(p);
            if (parser_eof(p)) {
                free(buf);
                parser_error(p, "unexpected end of input in string escape");
                return NULL;
            }
            char esc = parser_advance(p);
            char replacement = 0;
            switch (esc) {
                case '"':  replacement = '"';  break;
                case '\\': replacement = '\\'; break;
                case '/':  replacement = '/';  break;
                case 'b':  replacement = '\b'; break;
                case 'f':  replacement = '\f'; break;
                case 'n':  replacement = '\n'; break;
                case 'r':  replacement = '\r'; break;
                case 't':  replacement = '\t'; break;
                case 'u': {
                    unsigned int cp = 0;
                    if (parse_unicode_escape(p, &cp) != 0) {
                        free(buf);
                        return NULL;
                    }
                    // Handle surrogate pairs
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // High surrogate — expect \uXXXX low surrogate
                        if (parser_eof(p) || parser_peek(p) != '\\') {
                            free(buf);
                            parser_error(p, "expected low surrogate after high surrogate");
                            return NULL;
                        }
                        parser_advance(p); // consume '\'
                        if (parser_eof(p) || parser_peek(p) != 'u') {
                            free(buf);
                            parser_error(p, "expected '\\u' for low surrogate");
                            return NULL;
                        }
                        parser_advance(p); // consume 'u'
                        unsigned int low = 0;
                        if (parse_unicode_escape(p, &low) != 0) {
                            free(buf);
                            return NULL;
                        }
                        if (low < 0xDC00 || low > 0xDFFF) {
                            free(buf);
                            parser_error(p, "invalid low surrogate");
                            return NULL;
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        free(buf);
                        parser_error(p, "unexpected low surrogate");
                        return NULL;
                    }

                    char utf8[4];
                    int utf8_len = encode_utf8(cp, utf8);
                    // Ensure capacity
                    while (length + (size_t)utf8_len > capacity) {
                        capacity *= 2;
                        char* tmp = (char*)realloc(buf, capacity);
                        if (!tmp) { free(buf); parser_error(p, "out of memory"); return NULL; }
                        buf = tmp;
                    }
                    memcpy(buf + length, utf8, (size_t)utf8_len);
                    length += (size_t)utf8_len;
                    continue; // skip the common append below
                }
                default:
                    free(buf);
                    parser_error(p, "invalid escape character '\\%c'", esc);
                    return NULL;
            }
            // Append the replacement character
            if (length + 1 > capacity) {
                capacity *= 2;
                char* tmp = (char*)realloc(buf, capacity);
                if (!tmp) { free(buf); parser_error(p, "out of memory"); return NULL; }
                buf = tmp;
            }
            buf[length++] = replacement;
        } else if ((unsigned char)c < 0x20) {
            // Control characters not allowed in JSON strings
            free(buf);
            parser_error(p, "unescaped control character in string");
            return NULL;
        } else {
            parser_advance(p);
            if (length + 1 > capacity) {
                capacity *= 2;
                char* tmp = (char*)realloc(buf, capacity);
                if (!tmp) { free(buf); parser_error(p, "out of memory"); return NULL; }
                buf = tmp;
            }
            buf[length++] = c;
        }
    }

    free(buf);
    parser_error(p, "unterminated string");
    return NULL;
}

static JsonValue* parse_string_value(Parser* p) {
    char* str = parse_string_raw(p);
    if (!str) return NULL;

    JsonValue* val = json_alloc(JSON_STRING);
    if (!val) {
        free(str);
        parser_error(p, "out of memory");
        return NULL;
    }
    val->data.string_val = str;
    return val;
}

// --- Array Parsing ---

static JsonValue* parse_array(Parser* p) {
    parser_advance(p); // consume '['

    JsonValue* arr = json_alloc(JSON_ARRAY);
    if (!arr) {
        parser_error(p, "out of memory");
        return NULL;
    }
    arr->data.array.items = NULL;
    arr->data.array.count = 0;
    arr->data.array.capacity = 0;

    skip_whitespace(p);

    // Empty array
    if (!parser_eof(p) && parser_peek(p) == ']') {
        parser_advance(p);
        return arr;
    }

    while (true) {
        skip_whitespace(p);
        JsonValue* item = parse_value(p);
        if (!item) {
            json_free(arr);
            return NULL;
        }

        // Grow if needed
        if (arr->data.array.count >= arr->data.array.capacity) {
            int new_cap = arr->data.array.capacity == 0 ? 4 : arr->data.array.capacity * 2;
            JsonValue** tmp = (JsonValue**)realloc(arr->data.array.items,
                                                   (size_t)new_cap * sizeof(JsonValue*));
            if (!tmp) {
                json_free(item);
                json_free(arr);
                parser_error(p, "out of memory");
                return NULL;
            }
            arr->data.array.items = tmp;
            arr->data.array.capacity = new_cap;
        }
        arr->data.array.items[arr->data.array.count++] = item;

        skip_whitespace(p);

        if (parser_eof(p)) {
            json_free(arr);
            parser_error(p, "unterminated array");
            return NULL;
        }

        if (parser_peek(p) == ']') {
            parser_advance(p);
            return arr;
        }

        if (!parser_match(p, ',')) {
            json_free(arr);
            parser_error(p, "expected ',' or ']' in array");
            return NULL;
        }
    }
}

// --- Object Parsing ---

static JsonValue* parse_object(Parser* p) {
    parser_advance(p); // consume '{'

    JsonValue* obj = json_alloc(JSON_OBJECT);
    if (!obj) {
        parser_error(p, "out of memory");
        return NULL;
    }
    obj->data.object.entries = NULL;
    obj->data.object.count = 0;
    obj->data.object.capacity = 0;

    skip_whitespace(p);

    // Empty object
    if (!parser_eof(p) && parser_peek(p) == '}') {
        parser_advance(p);
        return obj;
    }

    while (true) {
        skip_whitespace(p);

        // Parse key (must be a string)
        if (parser_eof(p) || parser_peek(p) != '"') {
            json_free(obj);
            parser_error(p, "expected string key in object");
            return NULL;
        }

        char* key = parse_string_raw(p);
        if (!key) {
            json_free(obj);
            return NULL;
        }

        skip_whitespace(p);

        if (!parser_match(p, ':')) {
            free(key);
            json_free(obj);
            parser_error(p, "expected ':' after object key");
            return NULL;
        }

        skip_whitespace(p);

        JsonValue* value = parse_value(p);
        if (!value) {
            free(key);
            json_free(obj);
            return NULL;
        }

        // Grow if needed
        if (obj->data.object.count >= obj->data.object.capacity) {
            int new_cap = obj->data.object.capacity == 0 ? 4 : obj->data.object.capacity * 2;
            JsonObjectEntry* tmp = (JsonObjectEntry*)realloc(obj->data.object.entries,
                                                              (size_t)new_cap * sizeof(JsonObjectEntry));
            if (!tmp) {
                free(key);
                json_free(value);
                json_free(obj);
                parser_error(p, "out of memory");
                return NULL;
            }
            obj->data.object.entries = tmp;
            obj->data.object.capacity = new_cap;
        }
        obj->data.object.entries[obj->data.object.count].key = key;
        obj->data.object.entries[obj->data.object.count].value = value;
        obj->data.object.count++;

        skip_whitespace(p);

        if (parser_eof(p)) {
            json_free(obj);
            parser_error(p, "unterminated object");
            return NULL;
        }

        if (parser_peek(p) == '}') {
            parser_advance(p);
            return obj;
        }

        if (!parser_match(p, ',')) {
            json_free(obj);
            parser_error(p, "expected ',' or '}' in object");
            return NULL;
        }
    }
}
