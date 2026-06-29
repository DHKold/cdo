/**
 * parse.c - argc/argv parsing engine.
 *
 * Tokenizes command-line arguments, matches commands from the registry,
 * resolves options and positionals, and produces a CliParseResult.
 * Supports: --name=value, --name value, -x value, -xvalue, -abc combined flags,
 * -- rest separator, subcommand resolution, positional args, and error reporting.
 */
#include "cmd_internal.h"
#include "../../api/cli_errors.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* --- Internal Helpers --- */

/**
 * Find a root command node by name in the registry.
 */
static CmdNode* parse_find_root(const CliCmdRegistry* reg, const char* name) {
    for (int i = 0; i < reg->command_count; i++) {
        if (strcmp(reg->root_commands[i].spec.name, name) == 0) {
            return &reg->root_commands[i];
        }
    }
    return NULL;
}

/**
 * Find a child command node by name.
 */
static CmdNode* parse_find_child(const CmdNode* node, const char* name) {
    for (int i = 0; i < node->child_count; i++) {
        if (strcmp(node->children[i].spec.name, name) == 0) {
            return &node->children[i];
        }
    }
    return NULL;
}

/**
 * Find an argument spec by long name in the matched command.
 */
static const CliArgSpec* find_arg_by_long(const CliCmdSpec* cmd, const char* name) {
    for (int i = 0; i < cmd->arg_count; i++) {
        if (strcmp(cmd->args[i].long_name, name) == 0) {
            return &cmd->args[i];
        }
    }
    return NULL;
}

/**
 * Find an argument spec by short name in the matched command.
 */
static const CliArgSpec* find_arg_by_short(const CliCmdSpec* cmd, char ch) {
    for (int i = 0; i < cmd->arg_count; i++) {
        if (cmd->args[i].short_name == ch) {
            return &cmd->args[i];
        }
    }
    return NULL;
}

/**
 * Find or create an arg_value entry in the result buffer for the given arg spec.
 * Returns a pointer to the CliArgValue slot.
 */
static CliArgValue* get_or_create_arg_value(CliParseResult* result, CliArgValue* result_buf, int result_buf_count, const CliArgSpec* arg) {
    /* Check if already exists */
    for (int i = 0; i < result->arg_value_count; i++) {
        if (strcmp(result->arg_values[i].name, arg->long_name) == 0) {
            return &result->arg_values[i];
        }
    }
    /* Create new entry */
    if (result->arg_value_count >= result_buf_count) return NULL;
    CliArgValue* val = &result_buf[result->arg_value_count];
    memset(val, 0, sizeof(*val));
    val->name = arg->long_name;
    val->type = arg->type;
    val->present = false;
    result->arg_value_count++;
    return val;
}

/**
 * Set error state in the parse result.
 */
static void set_error(CliParseResult* result, int code, const char* token, const char* fmt, ...) {
    result->error_code = code;
    result->error_token = token;
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(result->error_msg, sizeof(result->error_msg), fmt, args);
        va_end(args);
    }
}


/* --- Public API --- */

int cli_cmd_parse(const CliCmdRegistry* reg, int argc, const char** argv, CliArgValue* result_buf, int result_buf_count, CliParseResult* result) {
    if (result == NULL) return CLI_ERR_PARSE;
    memset(result, 0, sizeof(*result));

    /* Validate inputs */
    if (reg == NULL || argc < 2 || argv == NULL) {
        set_error(result, CLI_ERR_PARSE, NULL, "no command specified");
        return result->error_code;
    }

    /* Set up the result buffer (zero-alloc fast path) */
    result->arg_values = result_buf;
    result->arg_value_count = 0;

    /* Skip argv[0] (program name), start matching from argv[1] */
    int idx = 1;

    /* --- Command matching (with subcommand traversal) --- */
    CmdNode* matched_node = parse_find_root(reg, argv[idx]);
    if (matched_node == NULL) {
        set_error(result, CLI_ERR_PARSE, argv[idx], "unknown command: '%s'", argv[idx]);
        return result->error_code;
    }
    idx++;

    /* Traverse subcommands while the next token matches a child */
    while (idx < argc && matched_node->child_count > 0) {
        const char* token = argv[idx];
        /* Don't try to match tokens that look like options as subcommands */
        if (token[0] == '-') break;
        CmdNode* child = parse_find_child(matched_node, token);
        if (child == NULL) break;
        matched_node = child;
        idx++;
    }

    result->matched_cmd = &matched_node->spec;
    const CliCmdSpec* cmd = result->matched_cmd;

    /* --- Parse remaining tokens: options, positionals, rest --- */
    /* Use static arrays for positionals and rest (reasonable limits for CLI) */
    static const char* s_positional_values[64];
    static const char* s_rest_args[128];
    int positional_idx = 0;
    int rest_idx = 0;
    bool rest_mode = false;

    while (idx < argc) {
        const char* token = argv[idx];

        /* Rest mode: everything goes into rest_args */
        if (rest_mode) {
            s_rest_args[rest_idx++] = token;
            idx++;
            continue;
        }

        /* Check for rest separator "--" */
        if (strcmp(token, "--") == 0) {
            rest_mode = true;
            idx++;
            continue;
        }

        /* Long option: --name=value or --name value */
        if (token[0] == '-' && token[1] == '-' && token[2] != '\0') {
            const char* opt_start = token + 2;
            const char* eq = strchr(opt_start, '=');

            char name_buf[128];
            const char* value = NULL;

            if (eq != NULL) {
                /* --name=value form */
                size_t name_len = (size_t)(eq - opt_start);
                if (name_len >= sizeof(name_buf)) name_len = sizeof(name_buf) - 1;
                memcpy(name_buf, opt_start, name_len);
                name_buf[name_len] = '\0';
                value = eq + 1;
            } else {
                /* --name value form (or --flag for booleans) */
                size_t name_len = strlen(opt_start);
                if (name_len >= sizeof(name_buf)) name_len = sizeof(name_buf) - 1;
                memcpy(name_buf, opt_start, name_len);
                name_buf[name_len] = '\0';
            }

            /* Look up the arg spec */
            const CliArgSpec* arg = find_arg_by_long(cmd, name_buf);
            if (arg == NULL) {
                set_error(result, CLI_ERR_PARSE, token, "unknown option: '%s'", token);
                return result->error_code;
            }

            CliArgValue* av = get_or_create_arg_value(result, result_buf, result_buf_count, arg);
            if (av == NULL) {
                set_error(result, CLI_ERR_BUFFER, token, "argument buffer full");
                return result->error_code;
            }

            av->present = true;

            if (arg->type == CLI_ARG_BOOL) {
                av->value.bool_val = true;
            } else {
                /* Need a value */
                if (value == NULL) {
                    /* Try next token as value */
                    if (idx + 1 < argc && argv[idx + 1][0] != '-') {
                        idx++;
                        value = argv[idx];
                    } else if (idx + 1 < argc) {
                        /* Next token starts with -, use it anyway (could be negative number or path) */
                        idx++;
                        value = argv[idx];
                    } else {
                        set_error(result, CLI_ERR_PARSE, token, "option '%s' requires a value", token);
                        return result->error_code;
                    }
                }
                /* Validate and coerce the value */
                int vrc = cli_cmd_validate_arg(arg, value, av, result->error_msg, sizeof(result->error_msg));
                if (vrc != CLI_OK) {
                    result->error_code = vrc;
                    return vrc;
                }
            }

            idx++;
            continue;
        }

        /* Short option: -x value, -xvalue, or -abc combined booleans */
        if (token[0] == '-' && token[1] != '\0') {
            const char* p = token + 1;

            /* Check if all chars in the token are boolean flags (combined short form) */
            bool all_bool = true;
            for (const char* c = p; *c != '\0'; c++) {
                const CliArgSpec* a = find_arg_by_short(cmd, *c);
                if (a == NULL || a->type != CLI_ARG_BOOL) {
                    all_bool = false;
                    break;
                }
            }

            if (all_bool && strlen(p) > 1) {
                /* Combined boolean flags: -abc → -a -b -c */
                for (const char* c = p; *c != '\0'; c++) {
                    const CliArgSpec* a = find_arg_by_short(cmd, *c);
                    CliArgValue* av = get_or_create_arg_value(result, result_buf, result_buf_count, a);
                    if (av == NULL) {
                        set_error(result, CLI_ERR_BUFFER, token, "argument buffer full");
                        return result->error_code;
                    }
                    av->present = true;
                    av->value.bool_val = true;
                }
                idx++;
                continue;
            }

            /* Single short option: first char is the option name */
            char short_ch = p[0];
            const CliArgSpec* arg = find_arg_by_short(cmd, short_ch);
            if (arg == NULL) {
                set_error(result, CLI_ERR_PARSE, token, "unknown option: '%s'", token);
                return result->error_code;
            }

            CliArgValue* av = get_or_create_arg_value(result, result_buf, result_buf_count, arg);
            if (av == NULL) {
                set_error(result, CLI_ERR_BUFFER, token, "argument buffer full");
                return result->error_code;
            }

            av->present = true;

            if (arg->type == CLI_ARG_BOOL) {
                av->value.bool_val = true;
            } else {
                /* Value: either attached (-ovalue) or next token (-o value) */
                const char* value = NULL;
                if (p[1] != '\0') {
                    /* Attached value: -o/tmp/out */
                    value = p + 1;
                } else {
                    /* Space-separated: -o value */
                    if (idx + 1 < argc) {
                        idx++;
                        value = argv[idx];
                    } else {
                        set_error(result, CLI_ERR_PARSE, token, "option '%s' requires a value", token);
                        return result->error_code;
                    }
                }

                /* Validate and coerce the value */
                int vrc = cli_cmd_validate_arg(arg, value, av, result->error_msg, sizeof(result->error_msg));
                if (vrc != CLI_OK) {
                    result->error_code = vrc;
                    return vrc;
                }
            }

            idx++;
            continue;
        }

        /* Positional argument */
        if (positional_idx < 64) {
            s_positional_values[positional_idx++] = token;
        }
        idx++;
    }

    /* Store positionals and rest in the result */
    result->positional_values = s_positional_values;
    result->positional_count = positional_idx;
    result->rest_args = s_rest_args;
    result->rest_count = rest_idx;

    /* --- Validate required arguments --- */
    for (int i = 0; i < cmd->arg_count; i++) {
        const CliArgSpec* arg = &cmd->args[i];
        if (!arg->required) continue;

        /* Check if this required arg was provided */
        bool found = false;
        for (int j = 0; j < result->arg_value_count; j++) {
            if (strcmp(result->arg_values[j].name, arg->long_name) == 0 && result->arg_values[j].present) {
                found = true;
                break;
            }
        }
        if (!found) {
            set_error(result, CLI_ERR_VALIDATE, NULL, "argument '--%s': required but not provided", arg->long_name);
            return result->error_code;
        }
    }

    return CLI_OK;
}
