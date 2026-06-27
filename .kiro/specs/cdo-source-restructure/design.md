# Design Document: CDo Source Restructure

## Overview

Restructure the CDo build tool's source code in two phases: split large files into focused compilation units, then reorganize generic modules into a new `commons/` layer.

## Architecture

The restructuring operates on the `crates/cdo/` package, reorganizing its `lib/` (source) and `api/` (header) directories. The build system auto-discovers all `.c` files recursively under `lib/`, so new files are compiled automatically without build-config changes. The effort is two phases executed sequentially:

1. **File Splitting** — decompose large files (>500 lines) into focused compilation units
2. **Folder Reorganization** — move generic modules from `lib/core/` to a new `lib/commons/`

### Directory Layout (After)

```
crates/cdo/
├── api/
│   ├── commands/        # command headers (unchanged)
│   ├── commons/         # NEW: generic module headers
│   │   ├── archive.h
│   │   ├── checksum.h
│   │   ├── http.h
│   │   ├── json.h
│   │   ├── semver.h
│   │   ├── threadpool.h
│   │   ├── toml.h
│   │   └── toml_parse.h  (if split exposes sub-API)
│   ├── core/            # domain-specific headers only
│   │   ├── catalog.h
│   │   ├── cli.h
│   │   ├── compiler.h
│   │   ├── deps.h
│   │   ├── errors.h
│   │   ├── module.h
│   │   ├── output.h
│   │   ├── scanner.h
│   │   ├── shader.h
│   │   ├── template.h
│   │   └── workspace.h
│   └── pal/             # unchanged
├── lib/
│   ├── commands/        # command implementations (split files here)
│   │   ├── cmd_build_lib.c
│   │   ├── cmd_build_exe.c
│   │   ├── cmd_build_test.c
│   │   ├── cmd_build_util.c
│   │   ├── cmd_build.c          # orchestrator (cmd_build entry point)
│   │   ├── cmd_deps_add.c
│   │   ├── cmd_deps_remove.c
│   │   ├── cmd_deps_list.c
│   │   ├── cmd_deps.c           # dispatcher
│   │   └── ... (other cmd_*.c unchanged)
│   ├── commons/         # NEW: generic modules
│   │   ├── archive.c
│   │   ├── checksum.c
│   │   ├── http.c
│   │   ├── json.c
│   │   ├── semver.c
│   │   ├── threadpool.c
│   │   ├── toml_parse.c
│   │   └── toml_serialize.c
│   ├── core/            # domain-specific modules only
│   │   ├── catalog.c
│   │   ├── catalog_load.c
│   │   ├── catalog_resolve.c
│   │   ├── catalog_serialize.c
│   │   ├── cli_parse.c
│   │   ├── cli_suggest.c
│   │   ├── compiler_detect.c
│   │   ├── compiler_dirty.c
│   │   ├── compiler_compile.c
│   │   ├── compiler_link.c
│   │   ├── deps_resolve.c
│   │   ├── deps_lock.c
│   │   ├── workspace_load.c
│   │   ├── workspace_resolve.c
│   │   └── ... (errors.c, module.c, output.c, scanner.c, shader.c, template.c)
│   └── pal/             # unchanged
└── tst/
```

## Components and Interfaces

### 1. File Split Strategy

Each large file is decomposed by identifying logical boundaries (related function groups). The original `.c` file is either replaced by multiple files or retained as a thin dispatcher that delegates to split files.

**Internal headers**: Split files that share internal state or helper functions use a file-local `_internal.h` header placed alongside the `.c` files in `lib/` (not in `api/`). These are `#include`d with a relative path from the source directory.

#### 1.1 cmd_build.c (3015 lines → 5 files)

| New File | Responsibility | Key Functions |
|----------|---------------|---------------|
| `cmd_build_util.c` | Shared utilities: profile loading, path helpers, job resolution | `build_profile_load`, `build_dir_for_crate`, `output_path_for_crate`, `resolve_jobs`, `object_path_from_source`, `deploy_catalog_files` |
| `cmd_build_lib.c` | Library module build | `build_library_module` |
| `cmd_build_exe.c` | Executable module build | `build_executable_module` |
| `cmd_build_test.c` | Test module build | `build_test_module` |
| `cmd_build.c` | Orchestrator + shared-library build + `cmd_build()` entry | `build_shared_library_module`, `build_crate_modules`, `cmd_build` |

**Internal header**: `lib/commands/cmd_build_internal.h` — declares shared types (`BuildProfile`) and utility functions used across the split files.

#### 1.2 toml.c (1530 lines → 2 files)

| New File | Responsibility | Key Functions |
|----------|---------------|---------------|
| `toml_parse.c` | TOML parser + value construction + free | `toml_parse`, `toml_value_free`, `toml_free`, all `parse_*` and `scanner_*` helpers |
| `toml_serialize.c` | TOML serialization | `toml_serialize`, all `ser_*` helpers |

Both files include `"commons/toml.h"` (after reorganization) for the public types. The scanner and string-buffer helpers are static within `toml_parse.c`.

#### 1.3 compiler.c (1462 lines → 4 files)

| New File | Responsibility | Key Functions |
|----------|---------------|---------------|
| `compiler_detect.c` | Compiler probing and detection | `compiler_detect`, `try_compiler`, `try_msvc`, `try_vendored_tools`, `extract_version` |
| `compiler_dirty.c` | Dirty-set computation and depfile parsing | `compiler_compute_dirty_set`, `compiler_compute_dirty`, `parse_depfile` |
| `compiler_compile.c` | Compilation dispatch (arg building + batch) | `compiler_compile_batch`, `build_gcc_clang_args`, `build_msvc_args`, `compile_task` |
| `compiler_link.c` | Linking (static + dynamic, GCC/MSVC) | `compiler_link`, `link_static_gcc`, `link_static_msvc`, `link_gcc_clang`, `link_msvc` |

**Internal header**: `lib/core/compiler_internal.h` — shared between split files for the `CompilerInfo` usage and helper signatures like `is_compilable_source`, `derive_stem`.

#### 1.4 catalog.c (1261 lines → 3 files, keeping existing catalog_serialize.c)

| New File | Responsibility | Key Functions |
|----------|---------------|---------------|
| `catalog.c` → `catalog_load.c` | Loading, parsing, deduplication | `catalog_load`, `catalog_parse_file`, `catalog_parse_*`, `catalog_deduplicate*`, `catalog_free` |
| `catalog_resolve.c` | Resolution and search | `catalog_resolve_tool`, `catalog_resolve_package`, `catalog_search`, `catalog_resolve_result_free` |
| `catalog_serialize.c` | Already exists — unchanged | `catalog_serialize` |

#### 1.5 workspace.c (1038 lines → 2 files)

| New File | Responsibility | Key Functions |
|----------|---------------|---------------|
| `workspace_load.c` | Workspace loading + crate manifest parsing | `workspace_load`, `parse_crate_manifest`, `read_config_file`, `expand_member_pattern` |
| `workspace_resolve.c` | Dependency resolution + topological sort + free | `workspace_resolve`, `workspace_resolve_module_deps`, `workspace_free`, `resolve_dep_indices`, `dfs_find_cycle`, `compute_transitive_closure` |

#### 1.6 cmd_deps.c (984 lines → 3 files)

| New File | Responsibility | Key Functions |
|----------|---------------|---------------|
| `cmd_deps_add.c` | The `add` subcommand logic | `deps_add`, `deps_add_entry`, `deps_add_inline_entry`, `deps_add_catalog_entry`, `parse_name_version`, `deps_persist_build_metadata` |
| `cmd_deps_remove.c` | The `remove` + `list` subcommand logic | `deps_remove`, `deps_list`, `deps_remove_entry` |
| `cmd_deps.c` | Dispatcher + shared helpers | `cmd_deps`, `manifest_load`, `manifest_save`, `deps_has`, `get_cache_dir`, `collect_dep_specs`, `regenerate_lock` |

#### 1.7 cli.c (701 lines → 2 files)

| New File | Responsibility | Key Functions |
|----------|---------------|---------------|
| `cli_parse.c` | Argument parsing + help | `cdo_cli_parse`, `cdo_cli_print_help`, `match_command`, `is_option`, `parse_log_level`, `parse_color_mode` |
| `cli_suggest.c` | Command suggestion (Levenshtein) | `cdo_cli_suggest`, `levenshtein`, `cdo_min3` |

#### 1.8 deps.c (693 lines → 2 files)

| New File | Responsibility | Key Functions |
|----------|---------------|---------------|
| `deps_resolve.c` | Dependency fetching and resolution | `dep_resolve`, `dep_fetch_registry`, `dep_fetch_git`, `dep_populate_resolved`, `dep_resolved_free`, `dep_detect_metadata` |
| `deps_lock.c` | Lock file read/write + source string helpers | `dep_lock_write`, `dep_lock_read`, `dep_build_source_string`, `dep_parse_source_string` |

### 2. Folder Reorganization Strategy

After all splits are complete and tests pass, the following modules move from `lib/core/` → `lib/commons/` (and `api/core/` → `api/commons/`):

| Module | Rationale |
|--------|-----------|
| `json` | Generic JSON parser, no CDo concepts |
| `toml` (parse + serialize) | Generic TOML parser/serializer |
| `checksum` | Generic SHA-256/hash utilities |
| `threadpool` | Generic work-stealing thread pool |
| `semver` | Semantic version parsing/comparison |
| `http` | Generic HTTP client wrapper |
| `archive` | Generic tar/zip extraction |

**Criteria for commons**: The module has zero `#include` references to `"core/*.h"` or `"commands/*.h"`. If any such reference exists, it must be refactored out (e.g., by extracting the CDo-specific part into a callback parameter) before the move.

### 3. Include Path Changes

The build system adds `api/` as an include root. After reorganization:

| Before | After |
|--------|-------|
| `#include "core/toml.h"` | `#include "commons/toml.h"` |
| `#include "core/json.h"` | `#include "commons/json.h"` |
| `#include "core/checksum.h"` | `#include "commons/checksum.h"` |
| `#include "core/threadpool.h"` | `#include "commons/threadpool.h"` |
| `#include "core/semver.h"` | `#include "commons/semver.h"` |
| `#include "core/http.h"` | `#include "commons/http.h"` |
| `#include "core/archive.h"` | `#include "commons/archive.h"` |

All files that `#include` these headers must be updated. Internal headers (in `lib/`) use relative paths from their own location (e.g., `#include "cmd_build_internal.h"` for sibling files).

### 4. Internal Header Convention

Split files sharing internal state follow this pattern:

```c
// lib/core/compiler_internal.h  (NOT in api/)
#ifndef CDO_CORE_COMPILER_INTERNAL_H
#define CDO_CORE_COMPILER_INTERNAL_H

#include "core/compiler.h"  // public types

// Internal helpers shared between compiler_*.c files
static inline bool is_compilable_source(const char* path);
int derive_stem(const char* source_path, const char* crate_src_prefix,
                char* stem_out, size_t stem_size);

#endif
```

Source files include the internal header:
```c
// lib/core/compiler_compile.c
#include "compiler_internal.h"  // relative path (same directory)
#include "core/compiler.h"
#include "core/output.h"
// ...
```

## Data Models

No new data structures are introduced. All existing types (`Workspace`, `Crate`, `CompilerInfo`, `TomlTable`, `Catalog`, etc.) remain unchanged. The restructuring only affects file boundaries, not interfaces.

## Error Handling

Error handling patterns remain unchanged:
- Functions return `int` (0 = success, non-zero = failure)
- Errors are reported via `cdo_error()` before returning
- Resource cleanup follows the existing goto-based or early-return patterns in each file

The split preserves all error paths. Each split file handles errors for its own domain and propagates failure codes to callers.

## Interfaces

No public API changes. All existing header files (`api/core/*.h`, `api/commands/*.h`) retain their current function signatures. The only changes to headers are:
1. Moving files between `api/core/` and `api/commons/` (path change, not content change)
2. Potential addition of sub-headers (e.g., `api/commons/toml_parse.h`) only if a split introduces new public symbols not already in the parent header

## Testing Strategy

The restructuring is validated through two complementary approaches:

- **Integration tests**: After each file split or move, run `.\cdo.exe build cdo` and `.\cdo.exe test cdo` to verify identical external behavior. This catches linker errors, missing symbols, and behavioral regressions.
- **Structural property checks**: Automated scripts verify invariants (file sizes, dependency direction, header mirroring) across the entire source tree. These run as part of the property-based test suite (`cdo_pbt`).

Unit tests are not needed for the restructuring itself since no logic changes — the existing test suite provides behavioral coverage.

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system — essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: File Size Invariant

*For any* `.c` source file in `crates/cdo/lib/` after the splitting phase is complete, its line count SHALL be at most 500 lines.

**Validates: Requirements 1.11**

### Property 2: Commons Dependency Isolation

*For any* source file (`.c`) or header file (`.h`) residing under `lib/commons/` or `api/commons/`, the file SHALL contain zero `#include` directives referencing paths matching `"core/*"` or `"commands/*"`.

**Validates: Requirements 2.4**

### Property 3: API Directory Mirroring

*For any* subdirectory that exists under `crates/cdo/lib/` (e.g., `lib/core/`, `lib/commons/`, `lib/commands/`), a corresponding subdirectory with the same name SHALL exist under `crates/cdo/api/`.

**Validates: Requirements 3.1**

### Property 4: Header-Symbol Correspondence

*For any* `.c` source file in `crates/cdo/lib/`, if the file defines at least one non-static (externally-visible) function, then a corresponding `.h` header SHALL exist in the matching `api/` subdirectory. Conversely, if no corresponding header exists in `api/`, then all function definitions in that `.c` file SHALL be declared `static`.

**Validates: Requirements 3.2, 3.3**
