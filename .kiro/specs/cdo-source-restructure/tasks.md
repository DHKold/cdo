# Implementation Plan: CDo Source Restructure

## Overview

Two-phase restructuring of `crates/cdo/`: first split 8 large source files into focused compilation units, then move 7 generic modules into a new `lib/commons/` layer. Each individual operation is verified with `.\cdo.exe build cdo` and `.\cdo.exe test cdo` before proceeding.

## Tasks

- [x] 1. Split cmd_build.c (Phase 1 — Commands)
  - [x] 1.1 Create `lib/commands/cmd_build_internal.h` and extract `cmd_build_util.c`
    - Create internal header declaring shared types (`BuildProfile`) and utility function signatures
    - Extract `build_profile_load`, `build_dir_for_crate`, `output_path_for_crate`, `resolve_jobs`, `object_path_from_source`, `deploy_catalog_files` into `cmd_build_util.c`
    - Update `cmd_build.c` to remove extracted functions and `#include "cmd_build_internal.h"`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.3, 1.11, 1.12, 4.5_

  - [x] 1.2 Extract `cmd_build_lib.c` (library module build)
    - Move `build_library_module` and its static helpers into `cmd_build_lib.c`
    - Include `cmd_build_internal.h` for shared types
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.3, 1.11, 1.12, 4.5_

  - [x] 1.3 Extract `cmd_build_exe.c` (executable module build)
    - Move `build_executable_module` and its static helpers into `cmd_build_exe.c`
    - Include `cmd_build_internal.h` for shared types
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.3, 1.11, 1.12, 4.5_

  - [x] 1.4 Extract `cmd_build_test.c` (test module build)
    - Move `build_test_module` and its static helpers into `cmd_build_test.c`
    - Include `cmd_build_internal.h` for shared types
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.3, 1.11, 1.12, 4.5_

  - [x] 1.5 Verify `cmd_build.c` retains only orchestrator + shared-library build + `cmd_build` entry
    - Confirm remaining `cmd_build.c` contains `build_shared_library_module`, `build_crate_modules`, `cmd_build`
    - Confirm all 5 resulting files are each below 500 lines
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.3, 1.11, 1.12, 4.5_

- [x] 2. Split toml.c (Phase 1 — Core)
  - [x] 2.1 Extract `toml_parse.c` and `toml_serialize.c` from `toml.c`
    - Move `toml_parse`, `toml_value_free`, `toml_free`, all `parse_*` and `scanner_*` helpers into `toml_parse.c`
    - Move `toml_serialize` and all `ser_*` helpers into `toml_serialize.c`
    - Remove original `toml.c`
    - Both files include `"core/toml.h"` for public types
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.4, 1.11, 1.12, 4.5_

- [x] 3. Split compiler.c (Phase 1 — Core)
  - [x] 3.1 Create `lib/core/compiler_internal.h` and extract `compiler_detect.c`
    - Create internal header with shared helper signatures (`is_compilable_source`, `derive_stem`, etc.)
    - Extract `compiler_detect`, `try_compiler`, `try_msvc`, `try_vendored_tools`, `extract_version` into `compiler_detect.c`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.5, 1.11, 1.12, 4.5_

  - [x] 3.2 Extract `compiler_dirty.c` (dirty-set computation)
    - Move `compiler_compute_dirty_set`, `compiler_compute_dirty`, `parse_depfile` into `compiler_dirty.c`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.5, 1.11, 1.12, 4.5_

  - [x] 3.3 Extract `compiler_compile.c` (compilation dispatch)
    - Move `compiler_compile_batch`, `build_gcc_clang_args`, `build_msvc_args`, `compile_task` into `compiler_compile.c`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.5, 1.11, 1.12, 4.5_

  - [x] 3.4 Extract `compiler_link.c` (linking) and remove original `compiler.c`
    - Move `compiler_link`, `link_static_gcc`, `link_static_msvc`, `link_gcc_clang`, `link_msvc` into `compiler_link.c`
    - Remove original `compiler.c`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.5, 1.11, 1.12, 4.5_

- [x] 4. Split catalog.c (Phase 1 — Core)
  - [x] 4.1 Extract `catalog_load.c` and `catalog_resolve.c` from `catalog.c`
    - Move `catalog_load`, `catalog_parse_file`, `catalog_parse_*`, `catalog_deduplicate*`, `catalog_free` into `catalog_load.c`
    - Move `catalog_resolve_tool`, `catalog_resolve_package`, `catalog_search`, `catalog_resolve_result_free` into `catalog_resolve.c`
    - Remove original `catalog.c` (existing `catalog_serialize.c` remains unchanged)
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.6, 1.11, 1.12, 4.5_

- [x] 5. Split workspace.c (Phase 1 — Core)
  - [x] 5.1 Extract `workspace_load.c` and `workspace_resolve.c` from `workspace.c`
    - Move `workspace_load`, `parse_crate_manifest`, `read_config_file`, `expand_member_pattern` into `workspace_load.c`
    - Move `workspace_resolve`, `workspace_resolve_module_deps`, `workspace_free`, `resolve_dep_indices`, `dfs_find_cycle`, `compute_transitive_closure` into `workspace_resolve.c`
    - Remove original `workspace.c`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.7, 1.11, 1.12, 4.5_

- [x] 6. Split cmd_deps.c (Phase 1 — Commands)
  - [x] 6.1 Extract `cmd_deps_add.c` (add subcommand)
    - Move `deps_add`, `deps_add_entry`, `deps_add_inline_entry`, `deps_add_catalog_entry`, `parse_name_version`, `deps_persist_build_metadata` into `cmd_deps_add.c`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.8, 1.11, 1.12, 4.5_

  - [x] 6.2 Extract `cmd_deps_remove.c` (remove + list subcommands)
    - Move `deps_remove`, `deps_list`, `deps_remove_entry` into `cmd_deps_remove.c`
    - Retain `cmd_deps.c` as dispatcher with `cmd_deps`, `manifest_load`, `manifest_save`, `deps_has`, `get_cache_dir`, `collect_dep_specs`, `regenerate_lock`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.8, 1.11, 1.12, 4.5_

- [x] 7. Split cli.c and deps.c (Phase 1 — Core)
  - [x] 7.1 Extract `cli_parse.c` and `cli_suggest.c` from `cli.c`
    - Move `cdo_cli_parse`, `cdo_cli_print_help`, `match_command`, `is_option`, `parse_log_level`, `parse_color_mode` into `cli_parse.c`
    - Move `cdo_cli_suggest`, `levenshtein`, `cdo_min3` into `cli_suggest.c`
    - Remove original `cli.c`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.9, 1.11, 1.12, 4.5_

  - [x] 7.2 Extract `deps_resolve.c` and `deps_lock.c` from `deps.c`
    - Move `dep_resolve`, `dep_fetch_registry`, `dep_fetch_git`, `dep_populate_resolved`, `dep_resolved_free`, `dep_detect_metadata` into `deps_resolve.c`
    - Move `dep_lock_write`, `dep_lock_read`, `dep_build_source_string`, `dep_parse_source_string` into `deps_lock.c`
    - Remove original `deps.c`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 1.10, 1.11, 1.12, 4.5_

- [x] 8. Phase 1 Checkpoint
  - Ensure `.\cdo.exe build cdo` and `.\cdo.exe test cdo` both pass
  - Confirm all resulting `.c` files are below 500 lines
  - Ensure all tests pass, ask the user if questions arise.
  - _Requirements: 4.1, 4.2, 5.1, 5.2_

- [x] 9. Create commons directories (Phase 2 — Setup)
  - [x] 9.1 Create `lib/commons/` and `api/commons/` directories
    - Create empty directories `crates/cdo/lib/commons/` and `crates/cdo/api/commons/`
    - Verify: `.\cdo.exe build cdo` (no-op, just ensuring the build still works)
    - _Requirements: 2.1, 3.1_

- [x] 10. Move json module to commons
  - [x] 10.1 Move `json.c` and `json.h` to commons and update includes
    - Move `lib/core/json.c` → `lib/commons/json.c`
    - Move `api/core/json.h` → `api/commons/json.h`
    - Update all `#include "core/json.h"` → `#include "commons/json.h"` across the codebase
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 2.2, 2.3, 2.4, 2.8, 4.5_

- [x] 11. Move toml module to commons
  - [x] 11.1 Move `toml_parse.c`, `toml_serialize.c`, and `toml.h` to commons and update includes
    - Move `lib/core/toml_parse.c` → `lib/commons/toml_parse.c`
    - Move `lib/core/toml_serialize.c` → `lib/commons/toml_serialize.c`
    - Move `api/core/toml.h` → `api/commons/toml.h`
    - Update all `#include "core/toml.h"` → `#include "commons/toml.h"` across the codebase
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 2.2, 2.3, 2.4, 2.8, 4.5_

- [x] 12. Move checksum and threadpool modules to commons
  - [x] 12.1 Move `checksum.c` and `checksum.h` to commons and update includes
    - Move `lib/core/checksum.c` → `lib/commons/checksum.c`
    - Move `api/core/checksum.h` → `api/commons/checksum.h`
    - Update all `#include "core/checksum.h"` → `#include "commons/checksum.h"`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 2.2, 2.3, 2.4, 2.8, 4.5_

  - [x] 12.2 Move `threadpool.c` and `threadpool.h` to commons and update includes
    - Move `lib/core/threadpool.c` → `lib/commons/threadpool.c`
    - Move `api/core/threadpool.h` → `api/commons/threadpool.h`
    - Update all `#include "core/threadpool.h"` → `#include "commons/threadpool.h"`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 2.2, 2.3, 2.4, 2.8, 4.5_

- [x] 13. Move semver, http, and archive modules to commons
  - [x] 13.1 Move `semver.c` and `semver.h` to commons and update includes
    - Move `lib/core/semver.c` → `lib/commons/semver.c`
    - Move `api/core/semver.h` → `api/commons/semver.h`
    - Update all `#include "core/semver.h"` → `#include "commons/semver.h"`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 2.2, 2.3, 2.4, 2.8, 4.5_

  - [x] 13.2 Move `http.c` and `http.h` to commons and update includes
    - Move `lib/core/http.c` → `lib/commons/http.c`
    - Move `api/core/http.h` → `api/commons/http.h`
    - Update all `#include "core/http.h"` → `#include "commons/http.h"`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 2.2, 2.3, 2.4, 2.8, 4.5_

  - [x] 13.3 Move `archive.c` and `archive.h` to commons and update includes
    - Move `lib/core/archive.c` → `lib/commons/archive.c`
    - Move `api/core/archive.h` → `api/commons/archive.h`
    - Update all `#include "core/archive.h"` → `#include "commons/archive.h"`
    - Verify: `.\cdo.exe build cdo` and `.\cdo.exe test cdo`
    - _Requirements: 2.2, 2.3, 2.4, 2.8, 4.5_

- [x] 14. Phase 2 Checkpoint
  - Ensure `.\cdo.exe build cdo` and `.\cdo.exe test cdo` both pass
  - Verify `lib/commons/` contains: json.c, toml_parse.c, toml_serialize.c, checksum.c, threadpool.c, semver.c, http.c, archive.c
  - Verify `api/commons/` mirrors with corresponding headers
  - Verify no `#include "core/*"` or `#include "commands/*"` directives in commons files
  - Ensure all tests pass, ask the user if questions arise.
  - _Requirements: 2.2, 2.4, 2.5, 3.1, 4.3, 4.4_

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each file split or move is individually verified with build + test before proceeding
- The build system auto-discovers `.c` files recursively, so no build config changes are needed
- Internal headers (`_internal.h`) go in `lib/` alongside sources, NOT in `api/`
- Checkpoints ensure incremental validation at phase boundaries
- The language is C — all code in this project is C17

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1", "2.1", "9.1"] },
    { "id": 1, "tasks": ["1.2", "3.1", "4.1", "5.1"] },
    { "id": 2, "tasks": ["1.3", "3.2", "6.1", "7.1"] },
    { "id": 3, "tasks": ["1.4", "3.3", "6.2", "7.2"] },
    { "id": 4, "tasks": ["1.5", "3.4"] },
    { "id": 5, "tasks": ["10.1"] },
    { "id": 6, "tasks": ["11.1", "12.1"] },
    { "id": 7, "tasks": ["12.2", "13.1"] },
    { "id": 8, "tasks": ["13.2", "13.3"] },
    { "id": 9, "tasks": ["15.1"] }
  ]
}
```
