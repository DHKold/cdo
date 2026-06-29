/**
 * validate.c - Type coercion and constraint checking for parsed arguments.
 *
 * Provides built-in validation for: bool, string (length), int (range, numeric),
 * enum (set membership). Also supports custom parse and validate function pointers.
 *
 * Called from parse.c after an argument value has been resolved, before storing
 * in the result buffer.
 */
#include "cmd_internal.h"
#include "../../api/cli_errors.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

/**
 * Validate and coerce a single argument value.
 * Called after the raw string value has been extracted from argv.
 *
 * For int: parses the string, checks range constraints.
 * For enum: checks membership in the allowed values set.
 * For string: checks length constraints.
 * For bool: no validation needed (presence = true).
 *
 * On success: stores the coerced value in `av` and returns CLI_OK.
 * On failure: writes error message to err_msg and returns CLI_ERR_VALIDATE.
 */
int cli_cmd_validate_arg(const CliArgSpec* arg, const char* raw_value, CliArgValue* av, char* err_msg, int err_msg_size) {
    /* If custom parse function is provided, invoke it first */
    if (arg->custom_parse != NULL) {
        int rc = arg->custom_parse(raw_value, &av->value, err_msg, err_msg_size, arg->custom_ctx);
        if (rc != 0) return CLI_ERR_VALIDATE;
        /* Custom parse succeeded; still run custom validate if present */
        if (arg->custom_validate != NULL) {
            rc = arg->custom_validate(&av->value, err_msg, err_msg_size, arg->custom_ctx);
            if (rc != 0) return CLI_ERR_VALIDATE;
        }
        return CLI_OK;
    }

    switch (arg->type) {
        case CLI_ARG_BOOL:
            /* Bool args don't need value validation (presence = true) */
            break;

        case CLI_ARG_STRING:
            /* Validate string length constraints */
            if (raw_value != NULL) {
                int len = (int)strlen(raw_value);
                if (arg->str_min_len > 0 && len < arg->str_min_len) {
                    snprintf(err_msg, err_msg_size, "argument '--%s': minimum length %d required (got: %d chars)", arg->long_name, arg->str_min_len, len);
                    return CLI_ERR_VALIDATE;
                }
                if (arg->str_max_len > 0 && len > arg->str_max_len) {
                    snprintf(err_msg, err_msg_size, "argument '--%s': maximum length %d exceeded (got: %d chars)", arg->long_name, arg->str_max_len, len);
                    return CLI_ERR_VALIDATE;
                }
                av->value.str_val = raw_value;
            }
            break;

        case CLI_ARG_INT: {
            /* Validate numeric and range */
            if (raw_value == NULL || raw_value[0] == '\0') {
                snprintf(err_msg, err_msg_size, "argument '--%s': value required", arg->long_name);
                return CLI_ERR_VALIDATE;
            }
            /* Check if the string is a valid integer */
            char* endptr = NULL;
            errno = 0;
            long val = strtol(raw_value, &endptr, 10);
            if (endptr == raw_value || *endptr != '\0' || errno == ERANGE) {
                snprintf(err_msg, err_msg_size, "argument '--%s': not a valid integer (got: '%s')", arg->long_name, raw_value);
                return CLI_ERR_VALIDATE;
            }
            /* Check range constraints (only if min != max, i.e., constraints are set) */
            if (arg->int_min != 0 || arg->int_max != 0) {
                if (val < arg->int_min || val > arg->int_max) {
                    snprintf(err_msg, err_msg_size, "argument '--%s': value must be between %d and %d (got: %s)", arg->long_name, arg->int_min, arg->int_max, raw_value);
                    return CLI_ERR_VALIDATE;
                }
            }
            av->value.int_val = (int)val;
            break;
        }

        case CLI_ARG_ENUM: {
            /* Validate enum membership */
            if (raw_value == NULL) {
                snprintf(err_msg, err_msg_size, "argument '--%s': value required", arg->long_name);
                return CLI_ERR_VALIDATE;
            }
            if (arg->enum_values != NULL) {
                bool found = false;
                for (int i = 0; arg->enum_values[i] != NULL; i++) {
                    if (strcmp(raw_value, arg->enum_values[i]) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    snprintf(err_msg, err_msg_size, "argument '--%s': invalid value (got: '%s')", arg->long_name, raw_value);
                    return CLI_ERR_VALIDATE;
                }
            }
            av->value.str_val = raw_value;
            break;
        }
    }

    /* If custom validate function is provided (without custom parse), invoke it */
    if (arg->custom_validate != NULL) {
        int rc = arg->custom_validate(&av->value, err_msg, err_msg_size, arg->custom_ctx);
        if (rc != 0) return CLI_ERR_VALIDATE;
    }

    return CLI_OK;
}
