/**
 * cmd_install_internal.h - Internal types and helpers for the install command.
 */
#ifndef CDO_CMD_INSTALL_INTERNAL_H
#define CDO_CMD_INSTALL_INTERNAL_H

#include "commands/cmd_install.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Per-app manifest data.
typedef struct {
    char name[64];
    char version[32];
    char crate_name[64];
    char source_workspace[512];
    char installed_at[32];
    char cdo_version[32];
    char profile[16];
    char executable[128];
    bool has_resources;
    bool has_shaders;
    char resource_base[64];
    char shader_base[64];
} InstallManifest;

/// Entry in the global install.toml index.
typedef struct {
    char name[64];
    char version[32];
    char source_workspace[512];
    char installed_at[32];
    char path[256];
} InstallIndexEntry;

/// Write a per-app manifest to disk.
int install_write_manifest(const InstallManifest* manifest, const char* manifest_path);

/// Read a per-app manifest from disk.
int install_read_manifest(const char* manifest_path, InstallManifest* out);

/// Update the global install.toml with the given manifest entry.
/// Creates or updates the entry for manifest->name.
int install_update_global_index(const char* apps_dir, const InstallManifest* manifest);

/// Remove an app entry from the global install.toml.
int install_remove_from_global_index(const char* apps_dir, const char* app_name);

/// Set the installed_at field to the current UTC timestamp.
void install_manifest_set_timestamp(InstallManifest* manifest);

/// Resolve the base install directory from CLI options.
/// Priority: --path > --global > default (~/.cdo/)
int install_resolve_base_dir(const CliParseResult* result, char* base_dir, size_t base_size);

/// Resolve the bin directory for launchers.
/// For --global on Unix, uses /usr/local/bin. Otherwise <base>/bin/.
int install_resolve_bin_dir(const CliParseResult* result, const char* base_dir, char* bin_dir, size_t bin_size);

#ifdef __cplusplus
}
#endif

#endif // CDO_CMD_INSTALL_INTERNAL_H
