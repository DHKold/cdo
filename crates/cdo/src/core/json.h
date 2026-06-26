#ifndef CDO_CORE_JSON_H
#define CDO_CORE_JSON_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- JSON Value Types ---
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonType;

// --- Parse Error ---
// Compatible shape with TomlError for consistency across parsers.
typedef struct {
    int     line;
    int     col;
    char    message[256];
} JsonError;

// --- JSON Value (opaque in header, defined in json.c) ---
typedef struct JsonValue JsonValue;

/// Parse a JSON document from a UTF-8 string.
/// On success, *out points to the root value. Caller frees with json_free().
/// On failure, returns non-zero and populates err with line/col/message.
int json_parse(const char* input, size_t len, JsonValue** out, JsonError* err);

/// Free a parsed JSON value tree recursively.
void json_free(JsonValue* val);

/// Lookup a key in a JSON object value.
/// Returns NULL if val is not an object or the key is not found.
const JsonValue* json_get(const JsonValue* val, const char* key);

/// Get the type of a JSON value.
JsonType json_type(const JsonValue* val);

/// Get the boolean value. Undefined if type != JSON_BOOL.
bool json_bool(const JsonValue* val);

/// Get the number value. Undefined if type != JSON_NUMBER.
double json_number(const JsonValue* val);

/// Get the string value. Undefined if type != JSON_STRING.
const char* json_string(const JsonValue* val);

/// Get the array length. Returns 0 if type != JSON_ARRAY.
int json_array_len(const JsonValue* val);

/// Get an array element by index. Returns NULL if out of bounds or not an array.
const JsonValue* json_array_get(const JsonValue* val, int index);

/// Get the number of keys in an object. Returns 0 if type != JSON_OBJECT.
int json_object_len(const JsonValue* val);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_JSON_H
