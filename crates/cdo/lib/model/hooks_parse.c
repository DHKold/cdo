#include "model/hooks.h"
#include "commons/toml.h"
#include "core/log.h"

#include <string.h>

// =============================================================================
// Lifecycle name mapping
// =============================================================================

/// Key strings used in TOML config files for each lifecycle point.
static const char* lifecycle_keys[HOOK_LIFECYCLE_COUNT] = {
    "pre-build",
    "post-build",
    "pre-test",
    "post-test",
    "pre-e2e",
    "post-e2e",
};

#define HOOK_DEFAULT_TIMEOUT 120

// =============================================================================
// hooks_parse_table â€” parse hooks from an already-parsed TomlTable
// =============================================================================

int hooks_parse_table(const TomlTable* hooks_table, HookSet* out) {
    if (!out) return -1;

    // Initialize all hooks as absent
    memset(out, 0, sizeof(HookSet));
    for (int i = 0; i < HOOK_LIFECYCLE_COUNT; i++) {
        out->hooks[i].lifecycle = (HookLifecycle)i;
        out->hooks[i].timeout_sec = HOOK_DEFAULT_TIMEOUT;
        out->hooks[i].present = false;
        out->hooks[i].command[0] = '\0';
    }

    if (!hooks_table) return 0; // No hooks section â†’ all absent, success

    // Iterate entries in the hooks table
    TomlEntry* entry = hooks_table->head;
    while (entry) {
        // Find which lifecycle this key maps to
        int lifecycle_idx = -1;
        for (int i = 0; i < HOOK_LIFECYCLE_COUNT; i++) {
            if (strcmp(entry->key, lifecycle_keys[i]) == 0) {
                lifecycle_idx = i;
                break;
            }
        }

        if (lifecycle_idx < 0) {
            cdo_log_warn("Unknown hook lifecycle key: '%s' (expected pre-build, post-build, pre-test, post-test, pre-e2e, post-e2e)", entry->key);
            entry = entry->next;
            continue;
        }

        HookDef* hook = &out->hooks[lifecycle_idx];
        TomlValue* val = entry->value;

        if (!val) {
            entry = entry->next;
            continue;
        }

        if (val->type == TOML_STRING) {
            // String shorthand: pre-build = "command"
            if (!val->as.string || val->as.string[0] == '\0') {
                // Empty string â†’ treat as absent
                entry = entry->next;
                continue;
            }
            size_t cmd_len = strlen(val->as.string);
            if (cmd_len >= sizeof(hook->command)) {
                cdo_log_error("Hook command too long for '%s' (max %d chars)", entry->key, (int)(sizeof(hook->command) - 1));
                return -1;
            }
            memcpy(hook->command, val->as.string, cmd_len + 1);
            hook->timeout_sec = HOOK_DEFAULT_TIMEOUT;
            hook->present = true;

        } else if (val->type == TOML_TABLE || val->type == TOML_INLINE_TABLE) {
            // Table form: pre-build = { command = "...", timeout = N }
            TomlTable* tbl = val->as.table;
            if (!tbl) {
                cdo_log_error("Invalid hook table for '%s'", entry->key);
                return -1;
            }

            // Look for "command" key
            bool found_command = false;
            TomlEntry* tbl_entry = tbl->head;
            while (tbl_entry) {
                if (strcmp(tbl_entry->key, "command") == 0) {
                    if (!tbl_entry->value || tbl_entry->value->type != TOML_STRING || !tbl_entry->value->as.string) {
                        cdo_log_error("Hook '%s': 'command' must be a string", entry->key);
                        return -1;
                    }
                    size_t cmd_len = strlen(tbl_entry->value->as.string);
                    if (cmd_len >= sizeof(hook->command)) {
                        cdo_log_error("Hook command too long for '%s' (max %d chars)", entry->key, (int)(sizeof(hook->command) - 1));
                        return -1;
                    }
                    memcpy(hook->command, tbl_entry->value->as.string, cmd_len + 1);
                    found_command = true;

                } else if (strcmp(tbl_entry->key, "timeout") == 0) {
                    if (!tbl_entry->value || tbl_entry->value->type != TOML_INTEGER) {
                        cdo_log_error("Hook '%s': 'timeout' must be an integer", entry->key);
                        return -1;
                    }
                    hook->timeout_sec = (int)tbl_entry->value->as.integer;
                }
                tbl_entry = tbl_entry->next;
            }

            if (!found_command) {
                cdo_log_error("Hook '%s': table form requires a 'command' key", entry->key);
                return -1;
            }

            if (hook->command[0] == '\0') {
                // Empty command string â†’ treat as absent
                entry = entry->next;
                continue;
            }

            // Note: timeout=0 explicitly means no timeout, which is valid
            hook->present = true;

        } else {
            cdo_log_error("Hook '%s': invalid type (expected string or table, got type %d)", entry->key, (int)val->type);
            return -1;
        }

        entry = entry->next;
    }

    return 0;
}

// =============================================================================
// hooks_parse â€” parse hooks from raw TOML text containing a [hooks] section
// =============================================================================

int hooks_parse(const char* toml_content, HookSet* out) {
    if (!out) return -1;

    // Initialize all hooks as absent (safe default even on early return)
    memset(out, 0, sizeof(HookSet));
    for (int i = 0; i < HOOK_LIFECYCLE_COUNT; i++) {
        out->hooks[i].lifecycle = (HookLifecycle)i;
        out->hooks[i].timeout_sec = HOOK_DEFAULT_TIMEOUT;
        out->hooks[i].present = false;
    }

    if (!toml_content || toml_content[0] == '\0') return 0; // No content â†’ no hooks

    // Parse the TOML text
    TomlTable* root = NULL;
    TomlError err;
    if (toml_parse(toml_content, strlen(toml_content), &root, &err) != 0) {
        cdo_log_error("Failed to parse TOML for hooks: line %d, col %d: %s", err.line, err.col, err.message);
        return -1;
    }

    // Look up the "hooks" key in the root table
    const TomlValue* hooks_val = toml_get(root, "hooks");
    if (!hooks_val) {
        // No [hooks] section â†’ all absent, success
        toml_free(root);
        return 0;
    }

    if (hooks_val->type != TOML_TABLE && hooks_val->type != TOML_INLINE_TABLE) {
        cdo_log_error("[hooks] must be a table");
        toml_free(root);
        return -1;
    }

    int result = hooks_parse_table(hooks_val->as.table, out);
    toml_free(root);
    return result;
}

// =============================================================================
// hook_lifecycle_name
// =============================================================================

const char* hook_lifecycle_name(HookLifecycle lifecycle) {
    if (lifecycle >= 0 && lifecycle < HOOK_LIFECYCLE_COUNT) {
        return lifecycle_keys[lifecycle];
    }
    return "unknown";
}
