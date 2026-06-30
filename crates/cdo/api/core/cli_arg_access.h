/**
 * cli_arg_access.h - Inline helpers for reading typed values from CliParseResult.
 *
 * These provide a convenient api to extract named argument values from
 * the parse result produced by cli_cmd_parse().
 */
#ifndef CDO_CORE_CLI_ARG_ACCESS_H
#define CDO_CORE_CLI_ARG_ACCESS_H

#include "cmd/cli_cmd.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Find a named argument in the parse result. Returns NULL if not found.
static inline const CliArgValue* cli_arg_find(const CliParseResult* result, const char* name) {
    if (!result || !name) return NULL;
    for (int i = 0; i < result->arg_value_count; i++) {
        if (result->arg_values[i].name && strcmp(result->arg_values[i].name, name) == 0) {
            return &result->arg_values[i];
        }
    }
    return NULL;
}

/// Get a bool argument value. Returns false if not present.
static inline bool cli_arg_get_bool(const CliParseResult* result, const char* name) {
    const CliArgValue* v = cli_arg_find(result, name);
    return (v && v->present && v->type == CLI_ARG_BOOL) ? v->value.bool_val : false;
}

/// Get an int argument value. Returns default_val if not present.
static inline int cli_arg_get_int(const CliParseResult* result, const char* name, int default_val) {
    const CliArgValue* v = cli_arg_find(result, name);
    return (v && v->present && v->type == CLI_ARG_INT) ? v->value.int_val : default_val;
}

/// Get a string argument value. Returns NULL if not present.
static inline const char* cli_arg_get_str(const CliParseResult* result, const char* name) {
    const CliArgValue* v = cli_arg_find(result, name);
    return (v && v->present && (v->type == CLI_ARG_STRING || v->type == CLI_ARG_ENUM)) ? v->value.str_val : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* CDO_CORE_CLI_ARG_ACCESS_H */
