/**
 * registry.c - Command tree management.
 *
 * Manages the command registry: creation, destruction, registration
 * of top-level commands and subcommands with duplicate detection.
 *
 * The registry is an opaque type (forward-declared in cli_cmd.h). Internally it
 * holds a dynamic array of CmdNode structs, each of which may have its own
 * dynamic array of children (subcommands). This forms an N-ary tree that can
 * nest to arbitrary depth.
 */
#include "cmd_internal.h"
#include "../../api/cli_errors.h"

#include <stdlib.h>
#include <string.h>

/* Use INITIAL_CAPACITY from cmd_internal.h (CMD_INITIAL_CAPACITY) */
#define INITIAL_CAPACITY CMD_INITIAL_CAPACITY

/* --- Internal Helpers --- */

/**
 * Recursively free a CmdNode's children (depth-first), then free the children array itself.
 * Does NOT free the node pointer itself (caller owns that memory, e.g. as part of an array).
 */
static void cmd_node_free_children(CmdNode* node) {
    if (node == NULL || node->children == NULL) return;
    for (int i = 0; i < node->child_count; i++) {
        cmd_node_free_children(&node->children[i]);
    }
    free(node->children);
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;
}

/**
 * Find a child node by name within a given node's children array.
 * Returns pointer to the child, or NULL if not found.
 */
static CmdNode* find_child(CmdNode* node, const char* name) {
    for (int i = 0; i < node->child_count; i++) {
        if (strcmp(node->children[i].spec.name, name) == 0) {
            return &node->children[i];
        }
    }
    return NULL;
}

/**
 * Find a top-level command node by name.
 * Returns pointer to the node, or NULL if not found.
 */
static CmdNode* find_root(CliCmdRegistry* reg, const char* name) {
    for (int i = 0; i < reg->command_count; i++) {
        if (strcmp(reg->root_commands[i].spec.name, name) == 0) {
            return &reg->root_commands[i];
        }
    }
    return NULL;
}

/**
 * Ensure capacity in the root_commands array, growing by 2x when full.
 * Returns 0 on success, CLI_ERR_ALLOC on failure.
 */
static int ensure_root_capacity(CliCmdRegistry* reg) {
    if (reg->command_count < reg->capacity) return CLI_OK;
    int new_cap = (reg->capacity == 0) ? INITIAL_CAPACITY : reg->capacity * 2;
    CmdNode* new_buf = realloc(reg->root_commands, (size_t)new_cap * sizeof(CmdNode));
    if (new_buf == NULL) return CLI_ERR_ALLOC;
    reg->root_commands = new_buf;
    reg->capacity = new_cap;
    return CLI_OK;
}

/**
 * Ensure capacity in a node's children array, growing by 2x when full.
 * Returns 0 on success, CLI_ERR_ALLOC on failure.
 */
static int ensure_child_capacity(CmdNode* node) {
    if (node->child_count < node->child_capacity) return CLI_OK;
    int new_cap = (node->child_capacity == 0) ? INITIAL_CAPACITY : node->child_capacity * 2;
    CmdNode* new_buf = realloc(node->children, (size_t)new_cap * sizeof(CmdNode));
    if (new_buf == NULL) return CLI_ERR_ALLOC;
    node->children = new_buf;
    node->child_capacity = new_cap;
    return CLI_OK;
}

/**
 * Walk a dot-separated path (e.g. "deps.add") starting from the registry roots.
 * Returns the final CmdNode matching the full path, or NULL if any segment is not found.
 */
static CmdNode* resolve_path(CliCmdRegistry* reg, const char* path) {
    /* We need a mutable copy to tokenize with strtok-style splitting. */
    size_t len = strlen(path);
    if (len == 0) return NULL;

    /* Stack buffer for path copy (paths are short command names, 256 is generous). */
    char buf[256];
    if (len >= sizeof(buf)) return NULL;
    memcpy(buf, path, len + 1);

    CmdNode* current = NULL;
    char* token = buf;
    char* dot;

    /* First segment: look in root_commands */
    dot = strchr(token, '.');
    if (dot != NULL) *dot = '\0';

    current = find_root(reg, token);
    if (current == NULL) return NULL;

    /* Subsequent segments: walk children */
    while (dot != NULL) {
        token = dot + 1;
        dot = strchr(token, '.');
        if (dot != NULL) *dot = '\0';

        current = find_child(current, token);
        if (current == NULL) return NULL;
    }

    return current;
}

/* --- Public API --- */

CliCmdRegistry* cli_cmd_registry_create(void) {
    CliCmdRegistry* reg = malloc(sizeof(CliCmdRegistry));
    if (reg == NULL) return NULL;
    reg->root_commands = NULL;
    reg->command_count = 0;
    reg->capacity = 0;
    return reg;
}

void cli_cmd_registry_destroy(CliCmdRegistry* reg) {
    if (reg == NULL) return;
    for (int i = 0; i < reg->command_count; i++) {
        cmd_node_free_children(&reg->root_commands[i]);
    }
    free(reg->root_commands);
    free(reg);
}

int cli_cmd_register(CliCmdRegistry* reg, const CliCmdSpec* spec) {
    if (reg == NULL || spec == NULL) return CLI_ERR_NOT_FOUND;

    /* Duplicate detection at root level */
    if (find_root(reg, spec->name) != NULL) {
        return CLI_ERR_DUPLICATE;
    }

    /* Grow array if needed */
    int rc = ensure_root_capacity(reg);
    if (rc != CLI_OK) return rc;

    /* Append new node (shallow copy of spec) */
    CmdNode* node = &reg->root_commands[reg->command_count];
    node->spec = *spec;
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;
    reg->command_count++;

    return CLI_OK;
}

int cli_cmd_register_sub(CliCmdRegistry* reg, const char* parent_path, const CliCmdSpec* spec) {
    if (reg == NULL || parent_path == NULL || spec == NULL) return CLI_ERR_NOT_FOUND;

    /* Resolve the parent node from the dot-separated path */
    CmdNode* parent = resolve_path(reg, parent_path);
    if (parent == NULL) return CLI_ERR_NOT_FOUND;

    /* Duplicate detection among parent's children */
    if (find_child(parent, spec->name) != NULL) {
        return CLI_ERR_DUPLICATE;
    }

    /* Grow children array if needed */
    int rc = ensure_child_capacity(parent);
    if (rc != CLI_OK) return rc;

    /* Append new child node (shallow copy of spec) */
    CmdNode* child = &parent->children[parent->child_count];
    child->spec = *spec;
    child->children = NULL;
    child->child_count = 0;
    child->child_capacity = 0;
    parent->child_count++;

    return CLI_OK;
}
