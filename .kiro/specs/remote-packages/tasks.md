# Feature 2: Remote Package Fetching — Implementation Tasks

## Task 1: Extended Dependency Declaration Parsing

- [ ] Extend crate.toml parsing to recognize external dependency formats:
  - String value: `sdl3 = "^3.2.0"` (registry, version constraint shorthand)
  - Table with `version`: `zlib = { version = "^1.3.0" }` (registry)
  - Table with `git`: `lib = { git = "https://...", ref = "v1.0" }` (git source)
  - Table with `path`: `local = { path = "../vendor/lib" }` (local path)
  - Empty table: `internal = {}` (workspace-internal, existing behavior)
- [ ] Create `dep_manifest_parse.c` with `dep_parse_crate_deps(const TomlTable* deps_table, DepDeclSpec** ext_deps, int* ext_count, int* int_dep_count)`
- [ ] Distinguish internal vs external deps during parsing
- [ ] Validate: version constraints use valid semver format; git URLs are non-empty; paths exist
- [ ] Store parsed external deps in a new `DepDeclSpec` array on the Crate struct

## Task 2: Workspace Data Structure Extensions

- [ ] Add to `Crate` struct in `workspace.h`:
  ```c
  DepSpec*    ext_deps;           // External dependency specs (resolved from DepDeclSpec)
  int         ext_dep_count;
  ResolvedDep* resolved_ext_deps; // Populated after dep resolution
  ```
- [ ] Add to `Workspace` struct:
  ```c
  DepSpec*    all_ext_deps;       // Deduplicated workspace-wide ext deps
  int         all_ext_dep_count;
  bool        deps_resolved;
  ```
- [ ] Update `workspace_free()` to free new fields
- [ ] Update `workspace_load()` to call the extended dependency parser

## Task 3: Version Resolution Pipeline

- [ ] Create `crates/cdo/api/core/dep_resolve_ext.h` with:
  - `int dep_resolve_workspace(Workspace* ws, const Catalog* catalog)` — main entry point
  - `int dep_collect_all(const Workspace* ws, DepSpec** out, int* count)` — gather + deduplicate
  - `int dep_merge_constraints(...)` — find version satisfying all crate constraints
- [ ] Create `crates/cdo/lib/core/dep_resolve_ext.c`
- [ ] Implement collection: iterate crates, gather all ext_deps, deduplicate by name
- [ ] Implement constraint merging:
  1. For each unique package name, collect all constraints from different crates
  2. Query catalog for available versions of that package
  3. Find highest version satisfying ALL constraints (using `semver_satisfies` + `semver_compare`)
  4. If no version satisfies all: report conflict with details
- [ ] Populate resolved `DepSpec` with: name, exact version, source=REGISTRY, URL from catalog, checksum from catalog
- [ ] For git deps: populate with source=GIT, url, git_ref (no version resolution needed)
- [ ] For local deps: populate with source=LOCAL, url=path (no version resolution needed)

## Task 4: Lock File Integration

- [ ] Implement `dep_lock_check(const char* ws_root, const DepSpec* resolved, int count, bool* needs_update)`:
  1. Check if `<ws_root>/cdo.lock` exists
  2. If exists: read lock file, compare with resolved specs
  3. If all resolved specs match lock entries (same name+version): needs_update = false
  4. If any differ: needs_update = true
- [ ] Implement `dep_lock_update(const char* ws_root, const DepSpec* resolved, int count)`:
  - Write/overwrite `cdo.lock` using existing `dep_lock_write`
- [ ] In the resolution pipeline:
  - If lock exists and is consistent: use locked versions (skip catalog resolution)
  - If lock is stale or missing: resolve and write lock
- [ ] Local path deps are excluded from the lock file

## Task 5: Fetch Pipeline

- [ ] Implement `dep_fetch_all(const DepSpec* specs, int count, const char* cache_dir)`:
  - For each spec: call existing `dep_resolve(spec, cache_dir, &resolved_dep)` 
  - This already handles: cache check, registry download+extract, git clone
  - Log progress: "Downloading <name> <version>..."
  - On failure: report which dep failed and why
- [ ] Ensure checksum verification happens (existing `checksum_verify_file` in the download path)
- [ ] Handle network errors gracefully: clear partial downloads, report actionable error

## Task 6: Build Pipeline Integration

- [ ] After `workspace_resolve()` in `cmd_build.c`, add dependency resolution step:
  ```c
  if (has_external_deps(&ws)) {
      rc = dep_resolve_workspace(&ws, &catalog);
      if (rc != 0) { /* error and exit */ }
  }
  ```
- [ ] In module build functions (lib, exe, dyn, tst), extend include/lib/link arrays:
  - Append resolved include_dirs to compile job include_paths
  - Append resolved lib_dirs to link job lib_paths
  - Append resolved link_libs to link job link_libs
  - Append resolved defines to compile job defines
- [ ] After successful build of exe/dyn modules, copy runtime DLLs:
  - For each resolved dep with runtime_dlls: copy each DLL to build output directory
  - Only copy if DLL is newer than destination (incremental)
- [ ] Ensure transitive internal deps also get external dep paths (if crate A depends on internal B which has ext deps, A's link needs those libs too)

## Task 7: `cdo deps update` Subcommand

- [ ] Add "update" case to `cmd_deps.c` dispatcher
- [ ] Implement `deps_update(const CdoOptions* opts)`:
  1. Load workspace
  2. Load catalog (`catalog_load`)
  3. Collect all external deps from workspace
  4. If positional arg: filter to just that package name
  5. Re-resolve from catalog (ignoring current lock for targeted packages)
  6. Compare old lock vs new resolution, report changes
  7. Fetch newly resolved versions
  8. Write updated lock file
- [ ] Output format: "Updated sdl3: 3.2.0 -> 3.3.1"
- [ ] If nothing changed: "All dependencies are up to date."
- [ ] Update help text for `cdo deps` to include `update` subcommand

## Task 8: Diamond Dependency Detection

- [ ] When collecting deps across crates, detect when multiple crates declare the same package
- [ ] Implement constraint intersection:
  - Collect all `SemverConstraint` for the same package name
  - Iterate catalog versions from highest to lowest
  - Find first version satisfying ALL constraints
- [ ] On conflict: emit clear error:
  ```
  error: Version conflict for 'zlib':
    crate 'app' requires ^1.3.0
    crate 'network' requires ^1.2.0, <1.3.0
  No version satisfies both constraints.
  ```
- [ ] Test: compatible constraints (^1.2.0 and ^1.0.0) resolve to highest in range
- [ ] Test: incompatible constraints are detected and reported

## Task 9: Offline Mode Support

- [ ] In `dep_resolve_workspace`: if lock file exists, attempt resolution from lock+cache only
- [ ] Only access network (catalog resolution, http_download) when:
  - Lock file is missing or stale
  - Cache miss for a locked dependency
  - `cdo deps update` is explicitly called
- [ ] When network is needed but unavailable:
  - If lock exists + cache populated: succeed silently (offline mode)
  - If lock exists + cache miss: "Dependency 'X' not in cache. Run 'cdo deps update'."
  - If no lock: "Cannot resolve dependencies without network access."

## Task 10: Catalog Integration

- [ ] Ensure `catalog_resolve_package` is called during version resolution
- [ ] Detect current platform with `catalog_detect_platform`
- [ ] Select the platform-specific URL and checksum from the catalog entry
- [ ] Use catalog-provided `include_dirs`, `link_libs`, `defines` as authoritative metadata
  (override what's detected from the filesystem)
- [ ] If package is not in catalog: check if it's declared as git/path (those don't need catalog)
- [ ] If registry dep is not in catalog: "Package 'X' not found in any catalog."

## Task 11: Cache Clear Extension

- [ ] Extend `cmd_cache.c` to support `cdo cache clear --deps`:
  - Remove `~/.cdo/cache/deps/` directory
  - Remove `~/.cdo/cache/git/` directory
  - Report: "Cleared dependency cache (N entries removed)"
- [ ] Keep `cdo cache clear` (without --deps) clearing only the object cache (existing behavior)
- [ ] Add `--all` flag to clear both object cache and dependency cache

## Task 12: Unit Tests

- [ ] Test crate.toml parsing: all dependency format variants
- [ ] Test version resolution: single constraint, multiple compatible, incompatible
- [ ] Test lock file: generation, reading, staleness detection
- [ ] Test collection and deduplication across multiple crates
- [ ] Test metadata detection integration (mock filesystem layouts)
- [ ] Test offline mode: lock+cache present, lock+cache miss, no lock
- [ ] Test diamond dependency: compatible and incompatible scenarios
- [ ] Target >90% line coverage on dep_resolve_ext.c and dep_manifest_parse.c

## Task 13: Integration / E2E Tests

- [ ] Create `e2e/deps_registry/` workspace with a crate depending on a catalog package
  - Mock or use real catalog entry (sdl3 from packages.toml)
  - Verify build produces correct include paths and link flags
- [ ] Create `e2e/deps_local/` workspace with a local path dependency
  - Create a sibling directory with headers and a .a file
  - Verify build links successfully
- [ ] Test `cdo deps update` produces an updated lock file
- [ ] Test `cdo deps list` shows both internal and external deps
- [ ] Test `cdo deps add <pkg> <version>` updates crate.toml and lock file
- [ ] Test build with lock file but cleared cache fails with helpful message

## Task 14: Documentation

- [ ] Update README.md with dependency management section
- [ ] Document crate.toml external dependency formats
- [ ] Document `cdo.lock` purpose and version control recommendation
- [ ] Document `cdo deps update` command
- [ ] Document offline build workflow
- [ ] Add examples to help text for `cdo deps add`
