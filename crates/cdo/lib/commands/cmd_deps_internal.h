#ifndef CDO_CMD_DEPS_INTERNAL_H
#define CDO_CMD_DEPS_INTERNAL_H

#include "commands/cmd_deps.h"
#include "core/catalog.h"
#include "core/cli.h"
#include "core/deps.h"
#include "commons/toml.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Constants shared across cmd_deps_*.c files                                  */
/* -------------------------------------------------------------------------- */

#define CRATE_MANIFEST      "crate.toml"
#define LOCK_FILE           "cdo.lock"
#define DEPS_SECTION        "dependencies"
#define DEV_DEPS_SECTION    "dev-dependencies"

/* Default registry URL (fallback when catalog resolution fails) */
#define DEFAULT_REGISTRY "https://registry.cdo.dev/v1/packages"

/* -------------------------------------------------------------------------- */
/* Shared helper functions (defined in cmd_deps.c)                             */
/* -------------------------------------------------------------------------- */

/// Read and parse the crate manifest in the current directory.
/// Caller must free *table via toml_free() on success.
int cmd_deps_manifest_load(TomlTable** table);

/// Serialize and write the manifest table back to crate.toml.
int cmd_deps_manifest_save(const TomlTable* table);

/// Check if a dependency with the given name already exists in the deps table.
bool cmd_deps_has(const TomlTable* deps, const char* name);

/// Build the cache directory path (~/.cdo/cache).
int cmd_deps_get_cache_dir(char* buf, size_t buf_size);

/// Collect all dependency specs from the manifest's [dependencies] table
/// for lock file generation. Caller frees the returned array.
int cmd_deps_collect_dep_specs(const TomlTable* deps, DepSpec** out_specs, int* out_count);

/// Regenerate the lock file from the current dependencies table.
int cmd_deps_regenerate_lock(const TomlTable* deps);

/* -------------------------------------------------------------------------- */
/* Subcommand entry points (defined in respective cmd_deps_*.c files)          */
/* -------------------------------------------------------------------------- */

/// The deps add subcommand implementation.
int deps_add(const CdoOptions* opts);

/// The deps remove subcommand implementation.
int deps_remove(const CdoOptions* opts);

/// The deps list subcommand implementation.
int deps_list(const CdoOptions* opts);

#ifdef __cplusplus
}
#endif

#endif /* CDO_CMD_DEPS_INTERNAL_H */
