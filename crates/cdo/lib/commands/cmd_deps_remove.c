#include "cmd_deps_internal.h"
#include "core/log.h"
#include "commons/toml.h"
#include "pal/pal.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* deps_remove_entry                                                           */
/* -------------------------------------------------------------------------- */

/// Remove a dependency entry from the deps table. Returns true if found and removed.
static bool deps_remove_entry(TomlTable* deps, const char* name) {
    TomlEntry* prev = NULL;
    for (TomlEntry* e = deps->head; e != NULL; prev = e, e = e->next) {
        if (strcmp(e->key, name) == 0) {
            /* Unlink */
            if (prev) {
                prev->next = e->next;
            } else {
                deps->head = e->next;
            }
            if (deps->tail == e) {
                deps->tail = prev;
            }
            deps->count--;

            /* Free the entry */
            free(e->key);
            toml_value_free(e->value);
            free(e);
            return true;
        }
    }
    return false;
}

/* -------------------------------------------------------------------------- */
/* cmd_deps_remove (cdo deps remove <name> [--dev])                            */
/* -------------------------------------------------------------------------- */

int cmd_deps_remove(const CliParseResult* result, void* ctx) {
    (void)ctx;

    /* Positional arg is the package name to remove */
    if (result->positional_count < 1) {
        cdo_log_error("Usage: cdo deps remove <name> [--dev]");
        return 1;
    }

    const char* dep_name = result->positional_values[0];
    bool dev = cli_arg_get_bool(result, "dev");
    const char* section_name = dev ? DEV_DEPS_SECTION : DEPS_SECTION;

    /* Load the crate manifest */
    TomlTable* root = NULL;
    if (cmd_deps_manifest_load(&root) != 0) {
        return 1;
    }

    /* Get the targeted section */
    const TomlValue* val = toml_get(root, section_name);
    if (!val || (val->type != TOML_TABLE && val->type != TOML_INLINE_TABLE)) {
        cdo_log_error("dependency '%s' not found in [%s]", dep_name, section_name);
        toml_free(root);
        return 1;
    }

    TomlTable* deps = val->as.table;

    /* Attempt removal */
    if (!deps_remove_entry(deps, dep_name)) {
        cdo_log_error("dependency '%s' not found in [%s]", dep_name, section_name);
        toml_free(root);
        return 1;
    }

    cdo_log_info("Removed '%s' from [%s]", dep_name, section_name);

    /* Write updated manifest */
    if (cmd_deps_manifest_save(root) != 0) {
        toml_free(root);
        return 1;
    }

    /* Regenerate lock file from [dependencies] (normal deps drive the lock) */
    const TomlValue* normal_val = toml_get(root, DEPS_SECTION);
    if (normal_val && (normal_val->type == TOML_TABLE || normal_val->type == TOML_INLINE_TABLE)) {
        if (cmd_deps_regenerate_lock(normal_val->as.table) != 0) {
            toml_free(root);
            return 1;
        }
    } else {
        /* No dependencies section â€” write empty lock */
        TomlTable empty = { .head = NULL, .tail = NULL, .count = 0 };
        if (cmd_deps_regenerate_lock(&empty) != 0) {
            toml_free(root);
            return 1;
        }
    }

    toml_free(root);
    return 0;
}
