#include "cmd_deps_internal.h"
#include "core/output.h"
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
/* Static helpers for deps_remove                                              */
/* -------------------------------------------------------------------------- */

/// Check if --dev flag is present in the positional args starting at start_idx.
static bool has_dev_flag_from(const CdoOptions* opts, int start_idx) {
    for (int i = start_idx; i < opts->positional_count; i++) {
        if (strcmp(opts->positional_args[i], "--dev") == 0) {
            return true;
        }
    }
    /* Also check argv_rest for --dev */
    for (int i = 0; i < opts->argc_rest; i++) {
        if (strcmp(opts->argv_rest[i], "--dev") == 0) {
            return true;
        }
    }
    return false;
}

/// Get the first non-flag positional arg starting at start_idx.
static const char* get_dep_name(const CdoOptions* opts, int start_idx) {
    for (int i = start_idx; i < opts->positional_count; i++) {
        if (opts->positional_args[i][0] != '-') {
            return opts->positional_args[i];
        }
    }
    return NULL;
}

/* -------------------------------------------------------------------------- */
/* deps_remove (cdo deps remove <name> [--dev])                                */
/* -------------------------------------------------------------------------- */

int deps_remove(const CdoOptions* opts) {
    /* positional_args[0] = "remove", remaining = <name> [--dev] */
    const char* dep_name = get_dep_name(opts, 1);
    if (!dep_name) {
        cdo_error("Usage: cdo deps remove <name> [--dev]");
        return 1;
    }

    bool dev = has_dev_flag_from(opts, 1);
    const char* section_name = dev ? DEV_DEPS_SECTION : DEPS_SECTION;

    /* Load the crate manifest */
    TomlTable* root = NULL;
    if (cmd_deps_manifest_load(&root) != 0) {
        return 1;
    }

    /* Get the targeted section */
    const TomlValue* val = toml_get(root, section_name);
    if (!val || (val->type != TOML_TABLE && val->type != TOML_INLINE_TABLE)) {
        cdo_error("dependency '%s' not found in [%s]", dep_name, section_name);
        toml_free(root);
        return 1;
    }

    TomlTable* deps = val->as.table;

    /* Attempt removal */
    if (!deps_remove_entry(deps, dep_name)) {
        cdo_error("dependency '%s' not found in [%s]", dep_name, section_name);
        toml_free(root);
        return 1;
    }

    cdo_info("Removed '%s' from [%s]", dep_name, section_name);

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
        /* No dependencies section — write empty lock */
        TomlTable empty = { .head = NULL, .tail = NULL, .count = 0 };
        if (cmd_deps_regenerate_lock(&empty) != 0) {
            toml_free(root);
            return 1;
        }
    }

    toml_free(root);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Static helpers for deps_list                                                */
/* -------------------------------------------------------------------------- */

/// Get the version string from a dependency entry value.
static const char* get_dep_version(const TomlValue* val) {
    if (!val) return "*";
    if (val->type == TOML_STRING) {
        return val->as.string;
    }
    if (val->type == TOML_TABLE || val->type == TOML_INLINE_TABLE) {
        const TomlValue* ver = toml_get(val->as.table, "version");
        if (ver && ver->type == TOML_STRING) {
            return ver->as.string;
        }
    }
    return "*";
}

/* -------------------------------------------------------------------------- */
/* deps_list (cdo deps list)                                                   */
/* -------------------------------------------------------------------------- */

int deps_list(const CdoOptions* opts) {
    (void)opts; /* unused for now */

    /* Load the crate manifest */
    TomlTable* root = NULL;
    if (cmd_deps_manifest_load(&root) != 0) {
        return 1;
    }

    bool printed_any = false;

    /* Print [dependencies] entries with [normal] label */
    const TomlValue* normal_val = toml_get(root, DEPS_SECTION);
    if (normal_val && (normal_val->type == TOML_TABLE || normal_val->type == TOML_INLINE_TABLE)) {
        const TomlTable* deps = normal_val->as.table;
        for (const TomlEntry* e = deps->head; e != NULL; e = e->next) {
            const char* version = get_dep_version(e->value);
            printf("  %s %s [normal]\n", e->key, version);
            printed_any = true;
        }
    }

    /* Print [dev-dependencies] entries with [dev] label */
    const TomlValue* dev_val = toml_get(root, DEV_DEPS_SECTION);
    if (dev_val && (dev_val->type == TOML_TABLE || dev_val->type == TOML_INLINE_TABLE)) {
        const TomlTable* dev_deps = dev_val->as.table;
        for (const TomlEntry* e = dev_deps->head; e != NULL; e = e->next) {
            const char* version = get_dep_version(e->value);
            printf("  %s %s [dev]\n", e->key, version);
            printed_any = true;
        }
    }

    if (!printed_any) {
        cdo_info("No dependencies found");
    }

    toml_free(root);
    return 0;
}
