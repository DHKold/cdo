#ifndef CDO_CORE_TOML_H
#define CDO_CORE_TOML_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- TOML Value Types ---
typedef enum {
    TOML_STRING,
    TOML_INTEGER,
    TOML_FLOAT,
    TOML_BOOL,
    TOML_DATETIME,
    TOML_ARRAY,
    TOML_TABLE,
    TOML_INLINE_TABLE,
} TomlType;

// --- Forward declarations ---
typedef struct TomlValue TomlValue;
typedef struct TomlTable TomlTable;
typedef struct TomlArray TomlArray;

// --- Table entry (key-value pair) ---
typedef struct TomlEntry {
    char*             key;
    TomlValue*        value;
    struct TomlEntry* next;
} TomlEntry;

// --- Table: ordered list of key-value pairs ---
struct TomlTable {
    TomlEntry* head;
    TomlEntry* tail;
    int        count;
};

// --- Array: list of values ---
struct TomlArray {
    TomlValue** items;
    int         count;
    int         capacity;
};

// --- Value: tagged union ---
struct TomlValue {
    TomlType type;
    union {
        char*      string;      // TOML_STRING, TOML_DATETIME
        int64_t    integer;     // TOML_INTEGER
        double     floating;    // TOML_FLOAT
        bool       boolean;     // TOML_BOOL
        TomlArray* array;       // TOML_ARRAY
        TomlTable* table;       // TOML_TABLE, TOML_INLINE_TABLE
    } as;
};

// --- Error reporting ---
typedef struct {
    int  line;
    int  col;
    char message[256];
} TomlError;

/// Parse a TOML document from a UTF-8 string.
/// On success, *out points to the root table. Caller frees with toml_free().
/// On failure, returns non-zero and populates err with line/col/message.
int toml_parse(const char* input, size_t len, TomlTable** out, TomlError* err);

/// Serialize a table back to TOML text.
/// On success, *out_buf is heap-allocated (caller frees), *out_len is the length.
/// Returns 0 on success, non-zero on failure.
int toml_serialize(const TomlTable* table, char** out_buf, size_t* out_len);

/// Free a parsed TOML tree (root table and all descendants).
void toml_free(TomlTable* table);

/// Lookup a value by dotted key path (e.g. "workspace.settings.c-standard").
/// Returns NULL if the path does not exist or any intermediate key is not a table.
const TomlValue* toml_get(const TomlTable* table, const char* key_path);

/// Free a single TomlValue and its children.
void toml_value_free(TomlValue* val);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_TOML_H
