/**
 * cli_cmd.h - Command management public API.
 *
 * Covers command registration, argument specification, parsing,
 * validation, help generation, and shell completion.
 */
#ifndef CDO_CLI_CMD_H
#define CDO_CLI_CMD_H

#include <stdbool.h>
#include <stddef.h>
#include "../term/cli_term.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct CliCmdRegistry CliCmdRegistry;
typedef struct CliParseResult CliParseResult;

/* --- Argument Types --- */
typedef enum {
    CLI_ARG_BOOL,
    CLI_ARG_STRING,
    CLI_ARG_INT,
    CLI_ARG_ENUM,
} CliArgType;

/* --- Positional Cardinality --- */
typedef enum {
    CLI_CARD_EXACTLY_ONE,   /* Required single value */
    CLI_CARD_ZERO_OR_ONE,   /* Optional single value */
    CLI_CARD_ZERO_OR_MORE,  /* Optional, variadic */
} CliCardinality;

/// Custom parse function: converts raw string to typed value.
/// Returns 0 on success, non-zero on error (writes message to err_buf).
typedef int (*CliParseFn)(const char* input, void* out, char* err_buf, int err_buf_size, void* ctx);

/// Custom validate function: checks a parsed value against constraints.
/// Returns 0 on success, non-zero on error (writes message to err_buf).
typedef int (*CliValidateFn)(const void* value, char* err_buf, int err_buf_size, void* ctx);

/// Argument specification (named option or flag).
typedef struct {
    const char*     long_name;      /* e.g. "verbose" -> --verbose */
    char            short_name;     /* e.g. 'v' -> -v (0 = none) */
    CliArgType      type;
    const char*     description;
    const char*     default_value;  /* String representation of default (NULL = no default) */
    bool            required;
    /* For CLI_ARG_INT: range constraints */
    int             int_min;        /* Inclusive minimum (ignored if int_min == int_max == 0) */
    int             int_max;        /* Inclusive maximum */
    /* For CLI_ARG_ENUM: allowed values */
    const char**    enum_values;    /* NULL-terminated array of allowed strings */
    /* For CLI_ARG_STRING: length constraints */
    int             str_min_len;    /* Minimum string length (0 = no minimum) */
    int             str_max_len;    /* Maximum string length (0 = no maximum) */
    /* Custom hooks */
    CliParseFn      custom_parse;   /* NULL = use built-in parser for type */
    CliValidateFn   custom_validate;/* NULL = use built-in validator */
    void*           custom_ctx;     /* Passed to custom_parse/custom_validate */
} CliArgSpec;

/// Positional argument specification.
typedef struct {
    const char*     name;           /* Display name for help text */
    const char*     description;
    CliCardinality  cardinality;
} CliPositionalSpec;

/// Command handler function pointer.
/// Receives the parse result for the matched command.
/// Returns 0 on success, non-zero on error.
typedef int (*CliHandlerFn)(const CliParseResult* result, void* ctx);

/// Command specification (declarative definition).
typedef struct {
    const char*         name;               /* Command name token */
    const char*         description;        /* One-line description for help */
    const char*         long_description;   /* Multi-line help text (NULL = use description) */
    CliHandlerFn        handler;            /* Handler function (NULL for group-only commands) */
    void*               handler_ctx;        /* Context passed to handler */
    const CliArgSpec*   args;               /* Array of argument specs */
    int                 arg_count;          /* Number of entries in args[] */
    const CliPositionalSpec* positionals;   /* Array of positional specs */
    int                 positional_count;   /* Number of entries in positionals[] */
} CliCmdSpec;

/* --- Registry --- */

/// Create a command registry. Returns NULL on allocation failure.
CliCmdRegistry* cli_cmd_registry_create(void);

/// Destroy a registry and free all internal memory.
void cli_cmd_registry_destroy(CliCmdRegistry* reg);

/// Register a top-level command. Returns 0 on success, non-zero on duplicate or error.
int cli_cmd_register(CliCmdRegistry* reg, const CliCmdSpec* spec);

/// Register a subcommand under a parent path (e.g. "deps" -> "deps add").
/// parent_path is the dot-separated path: "deps" for top-level, "deps.add" for nested.
/// Returns 0 on success, non-zero on error.
int cli_cmd_register_sub(CliCmdRegistry* reg, const char* parent_path, const CliCmdSpec* spec);

/* --- Parsing --- */

/// Parsed argument value (tagged union).
typedef struct {
    const char*     name;       /* Argument long_name */
    CliArgType      type;
    union {
        bool        bool_val;
        const char* str_val;    /* Points into original argv (no allocation) */
        int         int_val;
    } value;
    bool            present;    /* Was this argument explicitly provided? */
} CliArgValue;

/// Parse result produced by cli_cmd_parse().
struct CliParseResult {
    const CliCmdSpec*   matched_cmd;        /* The resolved command (NULL if not matched) */
    CliArgValue*        arg_values;         /* Array of resolved argument values */
    int                 arg_value_count;
    const char**        positional_values;  /* Positional argument strings */
    int                 positional_count;
    const char**        rest_args;          /* Tokens after "--" */
    int                 rest_count;
    /* Error reporting */
    int                 error_code;         /* 0 = success */
    char                error_msg[256];     /* Human-readable error description */
    const char*         error_token;        /* The problematic token (if applicable) */
};

/// Parse argc/argv against the registry.
/// `result_buf` is caller-provided storage for CliArgValue array (zero-alloc fast path).
/// `result_buf_count` is the capacity of result_buf.
/// Returns 0 on success, non-zero on parse error (details in result->error_msg).
int cli_cmd_parse(const CliCmdRegistry* reg, int argc, const char** argv, CliArgValue* result_buf, int result_buf_count, CliParseResult* result);

/* --- Help --- */

/// Generate and write help text for the given command to the provided buffer.
/// If cmd is NULL, generates top-level help listing all commands.
/// Returns number of bytes written (excluding NUL), or -1 on error.
int cli_cmd_help(const CliCmdRegistry* reg, const CliCmdSpec* cmd, const CliTermInfo* term, char* buf, int buf_size);

/* --- Completion --- */

/// Shell targets for completion script generation.
typedef enum {
    CLI_SHELL_BASH,
    CLI_SHELL_ZSH,
    CLI_SHELL_POWERSHELL,
} CliShellType;

/// Generate a completion script for the given shell.
/// Writes the script to `buf`. Returns bytes written or -1 on error.
int cli_cmd_completion_script(const CliCmdRegistry* reg, const char* program_name, CliShellType shell, char* buf, int buf_size);

/// Generate completion candidates for the current cursor position.
/// `argv` contains tokens typed so far, `argc` is token count, `cursor_pos` is index.
/// Writes candidates as newline-separated strings to `buf`.
/// Returns number of candidates, or -1 on error.
int cli_cmd_complete(const CliCmdRegistry* reg, int argc, const char** argv, int cursor_pos, char* buf, int buf_size);

/* --- Suggestion --- */

/// Suggest similar commands for an unrecognized token using Levenshtein distance.
/// Returns the number of suggestions written to `suggestions[]` (up to max_suggestions).
/// Each suggestion is a null-terminated string in a 32-byte buffer.
/// Uses adaptive threshold: max(2, input_length / 2) capped at 3.
int cli_cmd_suggest(const CliCmdRegistry* reg, const char* input, char suggestions[][32], int max_suggestions);

#ifdef __cplusplus
}
#endif

#endif /* CDO_CLI_CMD_H */
