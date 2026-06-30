# Implementation Plan: `cdo install` — System-Wide Binary Installation

## Overview

The core install/uninstall implementation (`cmd_install.c`, `cmd_install_manifest.c`, `bundle.c`) already exists. This plan focuses on wiring the handler function pointers, adding missing `crate.toml` parsing (version field, `[install]` section), fixing the `has_resources`/`has_shaders` detection logic, and adding comprehensive unit and e2e tests.

## Tasks

- [x] 1. Wire handler function pointers and add Crate model fields
  - [x] 1.1 Wire `cmd_install` and `cmd_uninstall` handlers in `registry_setup.c`
    - Add `.handler = cmd_install` to `spec_install` and `.handler = cmd_uninstall` to `spec_uninstall` in `cdo_registry_create()`
    - Add `#include "commands/cmd_install.h"` at the top of `registry_setup.c`
    - _Requirements: REQ-INSTALL-1, REQ-INSTALL-8_

  - [x] 1.2 Add `version`, `resource_base`, and `shader_base` fields to the `Crate` struct
    - In `crates/cdo/api/model/workspace.h`, add `char version[32]`, `char resource_base[64]`, and `char shader_base[64]` fields to `struct Crate`
    - Default `version` to `"0.0.0"`, `resource_base` to `"."`, `shader_base` to `"."`
    - _Requirements: REQ-INSTALL-12, REQ-INSTALL-2_

  - [x] 1.3 Parse `version` field and `[install]` section from `crate.toml`
    - In `crates/cdo/lib/model/workspace_load.c` (`parse_crate_manifest`), after existing `[crate]` section parsing:
      - Read `crate.version` string into `crate->version` (default `"0.0.0"`)
      - Read `install.resource-base` string into `crate->resource_base` (default `"."`)
      - Read `install.shader-base` string into `crate->shader_base` (default `"."`)
    - _Requirements: REQ-INSTALL-12, REQ-INSTALL-2_

- [x] 2. Fix version usage and resource detection in `cmd_install.c`
  - [x] 2.1 Replace hardcoded `"0.0.0"` version with `crate->version`
    - In `cmd_install.c`, replace the `const char* version = "0.0.0";` TODO line with `const char* version = crate->version;`
    - Also update the reinstall log message to use the actual old/new version from manifests
    - _Requirements: REQ-INSTALL-12, REQ-INSTALL-6_

  - [x] 2.2 Fix `has_resources`/`has_shaders` detection logic
    - The current code checks for `res/` and `shd/` subdirectories inside `app_dir`, but resources are flattened to app root (no `res/` or `shd/` folders exist after bundling)
    - Replace the check: instead of looking for `res/` and `shd/` directories in the app bundle, use `crate->has_res` and `crate->has_shd` flags from the workspace model (which reflect whether the crate has `res/` and `shd/` modules)
    - _Requirements: REQ-INSTALL-6, REQ-INSTALL-2_

  - [x] 2.3 Pass `BundleOpts` with `resource_base`/`shader_base` from crate config to `bundle_prepare`
    - In `cmd_install.c`, construct a `BundleOpts` with `resource_base = crate->resource_base` and `shader_base = crate->shader_base` instead of passing `NULL`
    - _Requirements: REQ-INSTALL-2_

  - [x] 2.4 Add `resource_base` and `shader_base` to manifest write/read
    - In `cmd_install_internal.h`, add `char resource_base[64]` and `char shader_base[64]` to `InstallManifest`
    - In `cmd_install_manifest.c`, write `resource_base` and `shader_base` fields in `install_write_manifest` under `[contents]`
    - In `install_read_manifest`, parse `contents.resource_base` and `contents.shader_base`
    - In `cmd_install.c`, populate `manifest.resource_base` and `manifest.shader_base` from crate config
    - _Requirements: REQ-INSTALL-6_

- [x] 3. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Unit tests for install command
  - [x] 4.1 Create `test_install_manifest.c` — unit tests for manifest read/write and global index
    - Create `crates/cdo/tst/unit/test_install_manifest.c`
    - Test `install_write_manifest` writes correct TOML format
    - Test `install_read_manifest` parses all fields correctly
    - Test round-trip: write then read produces identical manifest
    - Test `install_update_global_index` adds new entry
    - Test `install_update_global_index` replaces existing entry (reinstall)
    - Test `install_remove_from_global_index` removes entry
    - Test `install_remove_from_global_index` with non-existent name is no-op
    - Test `install_manifest_set_timestamp` produces valid ISO-8601 format
    - _Requirements: REQ-INSTALL-6, REQ-INSTALL-7_

  - [x] 4.2 Create `test_install_command.c` — unit tests for cmd_install/cmd_uninstall logic
    - Create `crates/cdo/tst/unit/test_install_command.c`
    - Test `cmd_install` with `--list` flag on empty index prints "No applications installed."
    - Test `cmd_install` with `--list` flag parses and prints entries from a pre-written install.toml
    - Test `cmd_uninstall` on a non-existent app returns 0 with info message
    - Test `cmd_uninstall` removes app dir, launcher, and index entry
    - Test overwrite protection: different workspace without `--force` returns error
    - Test overwrite protection: same workspace proceeds silently
    - Test overwrite protection: `--force` bypasses check
    - _Requirements: REQ-INSTALL-1, REQ-INSTALL-8, REQ-INSTALL-9, REQ-INSTALL-10_

  - [x] 4.3 Create `test_install_paths.c` — unit tests for path resolution helpers
    - Create `crates/cdo/tst/unit/test_install_paths.c`
    - Expose `resolve_base_dir` and `resolve_bin_dir` as internal testable functions (move to a `cmd_install_internal.h` declaration or create a test-only wrapper)
    - Test default base dir resolves to `~/.cdo/`
    - Test `--path /custom/dir` resolves to the provided path
    - Test `--global` resolves to platform-specific system dir
    - Test `resolve_bin_dir` default returns `<base>/bin/`
    - Test `resolve_bin_dir` with `--global` on Unix returns `/usr/local/bin/`
    - _Requirements: REQ-INSTALL-3, REQ-INSTALL-4_

  - [x] 4.4 Create `test_crate_version.c` — unit tests for version and [install] section parsing
    - Create `crates/cdo/tst/unit/test_crate_version.c`
    - Test crate.toml with `version = "1.2.3"` under `[crate]` populates `crate->version`
    - Test crate.toml without `version` defaults to `"0.0.0"`
    - Test `[install]` section with `resource-base = "data"` and `shader-base = "shaders"` populates correctly
    - Test missing `[install]` section defaults to `resource_base = "."` and `shader_base = "."`
    - _Requirements: REQ-INSTALL-12, REQ-INSTALL-2_

- [x] 5. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 6. End-to-end tests for install/uninstall
  - [x] 6.1 Create e2e workspace `e2e/install_basic/` for install e2e tests
    - Create `e2e/install_basic/` with a minimal workspace containing one executable crate (e.g., `hello`)
    - Include a `crate.toml` with `version = "1.0.0"` and an `[install]` section
    - Include a simple `exe/main.c` that compiles successfully
    - _Requirements: REQ-INSTALL-1_

  - [x] 6.2 Create `test_e2e_install.c` — e2e tests for install and uninstall flow
    - Create `crates/cdo/tst/unit/test_e2e_install.c` (using cdo_ut framework, marked TEST_SERIAL)
    - Test full install flow: run `cdo install` in the e2e workspace, verify app dir created, manifest written, launcher exists, global index updated
    - Test `cdo install --list` shows the installed app
    - Test reinstall from same workspace succeeds without `--force`
    - Test `cdo uninstall hello` removes app dir, launcher, and index entry
    - Test `cdo install --path <tmpdir>` installs to custom directory
    - Use a temporary install path (`--path`) to avoid polluting the real `~/.cdo/`
    - _Requirements: REQ-INSTALL-1, REQ-INSTALL-3, REQ-INSTALL-8, REQ-INSTALL-9_

- [x] 7. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- The core implementation (`cmd_install.c`, `cmd_install_manifest.c`, `bundle.c`, `bundle.h`, `cmd_install.h`, `cmd_install_internal.h`) is already complete. This plan focuses on the remaining integration gaps and testing.
- No PBT — using extensive unit tests per workspace steering rules.
- E2e tests use `--path <tmpdir>` to avoid side effects on the developer's real `~/.cdo/` directory.
- The `has_resources`/`has_shaders` fix is critical: the current code looks for `res/` and `shd/` directories in the app bundle, but since resources are flattened to the app root, those directories will never exist. The fix is to use `crate->has_res` / `crate->has_shd` from the workspace model instead.
- Tasks marked with `*` are optional and can be skipped for faster MVP.
- Each task references specific requirements for traceability.
- Checkpoints ensure incremental validation.

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1", "1.2"] },
    { "id": 1, "tasks": ["1.3"] },
    { "id": 2, "tasks": ["2.1", "2.2", "2.3", "2.4"] },
    { "id": 3, "tasks": ["4.1", "4.3", "4.4"] },
    { "id": 4, "tasks": ["4.2", "6.1"] },
    { "id": 5, "tasks": ["6.2"] }
  ]
}
```
