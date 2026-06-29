# Feature 9: `cdo install` — Implementation Tasks

## Task 1: CLI Infrastructure

- [ ] Add `CDO_CMD_INSTALL` and `CDO_CMD_UNINSTALL` to `CdoCommand` enum in `cli.h`
- [ ] Add `force` and `global` bool fields to `CdoOptions` struct
- [ ] Update `cdo_cli_parse` to recognize "install" and "uninstall" command tokens
- [ ] Parse `--path`, `--global`, `--debug`, `--force`, `--list` flags for install
- [ ] Add help text for install/uninstall commands in `cdo_cli_print_help`
- [ ] Update `main.cpp` dispatch switch with `CDO_CMD_INSTALL` and `CDO_CMD_UNINSTALL` cases
- [ ] Update `print_usage()` with install/uninstall command descriptions

## Task 2: Crate Version Field

- [ ] Add optional `version` field parsing in `crate.toml` under `[crate]`:
  ```toml
  [crate]
  name = "my-app"
  version = "0.1.0"
  ```
- [ ] Store as `char version[32]` in the `Crate` struct
- [ ] Default to "0.0.0" when not present
- [ ] Update workspace_load crate parsing to read this field

## Task 3: Command Headers

- [ ] Create `crates/cdo/api/commands/cmd_install.h`
- [ ] Declare `int cmd_install(const CdoOptions* opts)`
- [ ] Declare `int cmd_uninstall(const CdoOptions* opts)`

## Task 4: Install Directory Resolution

- [ ] Implement `install_resolve_base_dir(const CdoOptions* opts, char* out, size_t out_size)`:
  - `--path <dir>`: resolve relative to cwd, return that
  - `--global`: return platform-specific global base
  - Default: `~/.cdo/` via `pal_get_home_dir` + path join
- [ ] Implement `install_apps_dir(const char* base, const char* name, char* out, size_t size)`:
  returns `<base>/apps/<name>/`
- [ ] Implement `install_bin_dir(const char* base, char* out, size_t size)`:
  returns `<base>/bin/`
- [ ] Implement `install_global_base()` returning platform-specific system path

## Task 5: Extract Staging Logic from cmd_run

- [ ] Identify the staging folder logic in `cmd_run.c` that collects:
  - exe binary
  - transitive DLLs (from deps and workspace dyn/ modules)
  - res/ directory contents
  - shd/ directory contents
- [ ] Extract into a shared function (e.g., `build_stage_runtime(...)` in a shared file)
  - Input: workspace, crate, profile, build dir
  - Output: path to populated staging directory
- [ ] Ensure `cmd_run.c` calls the extracted function (no behavior change)
- [ ] Make the function available for `cmd_install.c` to call

## Task 6: Core Install Logic

- [ ] Create `crates/cdo/lib/commands/cmd_install.c`
- [ ] Implement `cmd_install()`:
  1. Handle `--list` early exit (delegate to `install_list`)
  2. Load workspace
  3. Determine target crate:
     - If positional arg: find crate by name
     - Else: scan for exe modules, error if 0 or >1
  4. Validate: crate has exe/ module
  5. Build in release mode (construct opts, call cmd_build)
  6. Call shared staging logic to collect runtime artifacts
  7. Resolve install base dir
  8. Compute apps_dir and bin_dir
  9. Check overwrite protection
  10. If app dir exists: `pal_rmdir_r` (clean slate)
  11. `pal_mkdir_p` apps_dir
  12. Copy staging contents to apps_dir (recursive copy)
  13. Strip binaries (unless --debug)
  14. Write per-app manifest.toml
  15. Update global install.toml
  16. Generate launcher script
  17. Log success

## Task 7: Recursive Directory Copy

- [ ] Implement `install_copy_dir(const char* src, const char* dest)`:
  - Walk src with `pal_dir_walk`
  - For each file: compute relative path, create parent dirs, `pal_file_copy`
  - Preserve directory structure
  - Return 0 on success, non-zero on any failure
- [ ] This is needed because PAL currently has `pal_file_copy` (single file) but no recursive dir copy

## Task 8: Strip Binaries

- [ ] Implement `install_strip_binaries(const char* app_dir, const CompilerInfo* info)`:
  - Walk app_dir looking for .exe and .dll files
  - For GCC/Clang: spawn `strip --strip-all <path>` on each
  - For MSVC: no-op
  - If strip not found: log debug, return success (non-fatal)
- [ ] Strip operates on the app bundle copies, never build artifacts

## Task 9: Per-App Manifest

- [ ] Create `crates/cdo/lib/commands/cmd_install_manifest.c`
- [ ] Define `InstallManifest` struct:
  ```c
  typedef struct {
      char name[64];
      char version[32];
      char crate_name[64];
      char source_workspace[260];
      char installed_at[32];      // ISO 8601
      char cdo_version[32];
      char executable[128];
      char** dlls;
      int  dll_count;
      bool has_resources;
      bool has_shaders;
  } InstallManifest;
  ```
- [ ] Implement `install_manifest_write(const char* app_dir, const InstallManifest* m)`:
  - Serialize to TOML, write to `<app_dir>/manifest.toml`
- [ ] Implement `install_manifest_read(const char* app_dir, InstallManifest* m)`:
  - Parse `<app_dir>/manifest.toml`, populate struct
  - Return non-zero if file missing or malformed
- [ ] Implement `install_manifest_free(InstallManifest* m)`: free dll array

## Task 10: Global Installation Index

- [ ] Define `InstallIndexEntry` struct (subset of manifest: name, version, source, date, path)
- [ ] Implement `install_index_read(const char* apps_dir, InstallIndexEntry** entries, int* count)`:
  - Parse `<apps_dir>/install.toml`
  - Return empty list if file doesn't exist
- [ ] Implement `install_index_write(const char* apps_dir, const InstallIndexEntry* entries, int count)`:
  - Serialize array to TOML
  - Write atomically (write to .tmp, then rename)
- [ ] Implement `install_index_add(const char* apps_dir, const InstallIndexEntry* entry)`:
  - Read existing, add/update entry by name, write back
- [ ] Implement `install_index_remove(const char* apps_dir, const char* name)`:
  - Read existing, remove entry by name, write back
- [ ] Implement `install_index_rebuild(const char* apps_dir)`:
  - Scan subdirectories for manifest.toml files
  - Rebuild install.toml from discovered manifests
  - Used when global index is corrupt/missing

## Task 11: Overwrite Protection

- [ ] Before installing, check if `<apps_dir>/<name>/manifest.toml` exists
- [ ] If exists: read it, compare `source_workspace` with current workspace root
- [ ] If different workspace and `--force` not set:
  - Error: "App 'X' was installed from 'Y'. Use --force to overwrite."
- [ ] If same workspace or --force: proceed (log "Reinstalling..." at info level)
- [ ] If app dir exists but manifest is missing: treat as foreign, require --force

## Task 12: Launcher Script Generation

- [ ] Implement `install_write_launcher(const char* bin_dir, const char* app_name)`:
  - On Windows: write `<bin_dir>/<name>.cmd` with content:
    `@"%~dp0..\apps\<name>\<name>.exe" %*`
  - On Unix: write `<bin_dir>/<name>` with content:
    ```
    #!/bin/sh
    exec "$(dirname "$0")/../apps/<name>/<name>" "$@"
    ```
  - On Unix: set executable permission (chmod +x equivalent)
- [ ] Handle the case where bin_dir doesn't exist: create it

## Task 13: Uninstall Command

- [ ] Implement `cmd_uninstall()`:
  1. Get app name from positional arg (required, error if missing)
  2. Resolve install base dir
  3. Build app path: `<base>/apps/<name>/`
  4. Check existence (`pal_path_exists`)
  5. If not found: warn "App 'X' is not installed", return 0
  6. Remove app directory: `pal_rmdir_r`
  7. Remove launcher: delete `<base>/bin/<name>.cmd` (Windows) or `<base>/bin/<name>` (Unix)
  8. Update global index: `install_index_remove`
  9. Log: "Uninstalled 'X'"

## Task 14: List Command

- [ ] Implement `install_list()`:
  1. Resolve install base dir
  2. Read global index
  3. If read fails: call `install_index_rebuild`, then re-read
  4. If empty: print "No applications installed."
  5. Else: print aligned table:
     ```
     Name          Version    Source                        Installed
     my-app        0.1.0      C:/projects/my-app            2026-06-29
     other-tool    2.0.0      C:/projects/tools             2026-06-28
     ```

## Task 15: Unit Tests

- [ ] Test install base dir resolution (default, --path, --global)
- [ ] Test per-app manifest write and read roundtrip
- [ ] Test global index add/remove/rebuild
- [ ] Test overwrite protection logic (same workspace, different workspace, missing manifest)
- [ ] Test launcher script content generation (Windows and Unix variants)
- [ ] Test crate version field parsing (present and absent)
- [ ] Test auto-detect single exe crate (1 exe, 0 exe, multiple exe)
- [ ] Test recursive directory copy
- [ ] Test strip invocation (mock pal_spawn)
- [ ] Target >90% line coverage on cmd_install.c and cmd_install_manifest.c

## Task 16: Integration / E2E Tests

- [ ] Create `e2e/install_basic/` workspace with a simple exe crate (no deps)
  - Test: `cdo install` builds and installs to temp dir
  - Test: launcher script exists and is correct
  - Test: manifest.toml is written with correct fields
  - Test: global index contains the entry
- [ ] Create `e2e/install_with_deps/` workspace with exe + dyn + res
  - Test: DLLs are included in app bundle
  - Test: res/ directory is included
  - Test: exe runs successfully from installed location
- [ ] Test `cdo install --list` shows installed apps
- [ ] Test `cdo uninstall <name>` removes everything (app dir + launcher + index entry)
- [ ] Test reinstall from same workspace succeeds without --force
- [ ] Test install from different workspace requires --force
- [ ] Test installing non-exe crate fails with clear error
