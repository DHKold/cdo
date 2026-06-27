#include "cmd_deps_internal.h"
#include "core/deps.h"
#include "core/output.h"
#include "commons/toml.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Shared helpers (non-static, declared in cmd_deps_internal.h)                */
/* -------------------------------------------------------------------------- */

int cmd_deps_manifest_load(TomlTable** table) {
    char* buf = NULL;
    size_t len = 0;
    int rc = pal_file_read(CRATE_MANIFEST, &buf, &len);
    if (rc != PAL_OK) {
        cdo_error("Could not read '%s' — are you in a crate directory?", CRATE_MANIFEST);
        return 1;
    }

    TomlError err = {0};
    rc = toml_parse(buf, len, table, &err);
    free(buf);
    if (rc != 0) {
        cdo_error("Failed to parse '%s': line %d col %d: %s",
                  CRATE_MANIFEST, err.line, err.col, err.message);
        return 1;
    }
    return 0;
}

int cmd_deps_manifest_save(const TomlTable* table) {
    char* buf = NULL;
    size_t len = 0;
    int rc = toml_serialize(table, &buf, &len);
    if (rc != 0) {
        cdo_error("Failed to serialize manifest");
        return 1;
    }

    rc = pal_file_write(CRATE_MANIFEST, buf, len);
    free(buf);
    if (rc != PAL_OK) {
        cdo_error("Failed to write '%s'", CRATE_MANIFEST);
        return 1;
    }
    return 0;
}

bool cmd_deps_has(const TomlTable* deps, const char* name) {
    for (const TomlEntry* e = deps->head; e != NULL; e = e->next) {
        if (strcmp(e->key, name) == 0) {
            return true;
        }
    }
    return false;
}

int cmd_deps_get_cache_dir(char* buf, size_t buf_size) {
    char home[260];
    int rc = pal_get_home_dir(home, sizeof(home));
    if (rc != PAL_OK) {
        cdo_error("Could not determine home directory");
        return 1;
    }
    rc = pal_path_join(buf, buf_size, home, ".cdo/cache");
    if (rc != 0) {
        cdo_error("Cache path too long");
        return 1;
    }
    return 0;
}

int cmd_deps_collect_dep_specs(const TomlTable* deps, DepSpec** out_specs, int* out_count) {
    int count = deps->count;
    if (count == 0) {
        *out_specs = NULL;
        *out_count = 0;
        return 0;
    }

    DepSpec* specs = (DepSpec*)calloc((size_t)count, sizeof(DepSpec));
    if (!specs) return 1;

    int i = 0;
    for (const TomlEntry* e = deps->head; e != NULL; e = e->next) {
        snprintf(specs[i].name, sizeof(specs[i].name), "%s", e->key);
        if (e->value && e->value->type == TOML_STRING) {
            snprintf(specs[i].version, sizeof(specs[i].version), "%s", e->value->as.string);
        }
        specs[i].source = DEP_REGISTRY;
        i++;
    }

    *out_specs = specs;
    *out_count = count;
    return 0;
}

int cmd_deps_regenerate_lock(const TomlTable* deps) {
    DepSpec* specs = NULL;
    int count = 0;
    int rc = cmd_deps_collect_dep_specs(deps, &specs, &count);
    if (rc != 0) {
        cdo_error("Failed to collect dependency specs for lock file");
        return 1;
    }

    rc = dep_lock_write(LOCK_FILE, specs, count);
    free(specs);
    if (rc != 0) {
        cdo_error("Failed to write lock file");
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* cmd_deps (dispatcher: cdo deps <add|remove|list>)                           */
/* -------------------------------------------------------------------------- */

int cmd_deps(const CdoOptions* opts) {
    if (opts->help || opts->positional_count < 1) {
        cdo_cli_print_help(CDO_CMD_DEPS, stdout);
        return opts->help ? 0 : 1;
    }

    const char* subcmd = opts->positional_args[0];

    if (strcmp(subcmd, "add") == 0) {
        return deps_add(opts);
    } else if (strcmp(subcmd, "remove") == 0) {
        return deps_remove(opts);
    } else if (strcmp(subcmd, "list") == 0) {
        return deps_list(opts);
    } else {
        cdo_error("Unknown subcommand '%s'", subcmd);
        cdo_info("Available subcommands: add, remove, list");
        cdo_info("Run 'cdo deps --help' for usage information.");
        return 1;
    }
}
