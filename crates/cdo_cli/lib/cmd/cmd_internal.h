/**
 * cmd_internal.h - Internal types shared between cmd module source files.
 *
 * Defines CmdNode and CliCmdRegistry struct internals that registry.c,
 * parse.c, help.c, and complete.c all need to access.
 */
#ifndef CDO_CLI_CMD_INTERNAL_H
#define CDO_CLI_CMD_INTERNAL_H

#include "../../api/cmd/cli_cmd.h"

/* Initial capacity for dynamic node arrays. */
#define CMD_INITIAL_CAPACITY 4

/* --- Internal Types --- */

typedef struct CmdNode {
    CliCmdSpec      spec;           /* Shallow copy of the command spec (pointers not owned) */
    struct CmdNode* children;       /* Dynamic array of child nodes */
    int             child_count;
    int             child_capacity;
} CmdNode;

struct CliCmdRegistry {
    CmdNode* root_commands;     /* Dynamic array of top-level command nodes */
    int      command_count;     /* Number of registered top-level commands */
    int      capacity;          /* Allocated capacity */
};

/* --- Internal Functions (validate.c) --- */

/**
 * Validate and coerce a single argument value after parsing.
 * Returns CLI_OK on success, CLI_ERR_VALIDATE on failure (writes message to err_msg).
 */
int cli_cmd_validate_arg(const CliArgSpec* arg, const char* raw_value, CliArgValue* av, char* err_msg, int err_msg_size);

#endif /* CDO_CLI_CMD_INTERNAL_H */
