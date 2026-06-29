# Feature 2: Remote/Third-Party Package Fetching & Build Integration

## Overview

Complete the external dependency lifecycle: fetch from registry/git/local, build from
source if needed, produce include/lib paths, lock versions for reproducibility, and
integrate resolved dependencies into the compile and link pipelines.

## Motivation

This is the single biggest gap compared to Cargo, Conan, vcpkg, and xmake. Without it,
CDo can only build code that lives within the workspace. Real-world projects need zlib,
SDL2, OpenSSL, libcurl, and dozens of other third-party libraries. The infrastructure
for fetching and resolving exists (http.c, archive.c, checksum.c, semver.c, deps.h,
catalog.h), but the end-to-end workflow from `crate.toml` declaration through build
integration is incomplete.

## Requirements

### Requirement 1: Lock File Generation and Usage

**ID:** REQ-PKG-1
**Description:** On first build (or when dependencies change), generate a `cdo.lock` file that pins exact versions and checksums for all resolved dependencies. Subsequent builds use the lock file for reproducibility.
**Acceptance Criteria:**
- When `cdo.lock` does not exist and the workspace has external dependencies, resolve dependencies and generate `cdo.lock`
- When `cdo.lock` exists and is consistent with `crate.toml` manifests, use locked versions without re-resolving
- When `crate.toml` adds/removes/changes a dependency that conflicts with the lock, re-resolve affected packages and update `cdo.lock`
- Lock file format is TOML (using existing `dep_lock_write`/`dep_lock_read`)
- Lock file contains: name, version, source URL, checksum, metadata kind
- `cdo.lock` should be committed to version control (document this)

### Requirement 2: `cdo deps update` Command

**ID:** REQ-PKG-2
**Description:** Provide a `cdo deps update [<package>]` command that re-resolves dependencies within their version constraints and updates the lock file.
**Acceptance Criteria:**
- `cdo deps update` re-resolves ALL external dependencies to their latest compatible versions
- `cdo deps update <name>` re-resolves only the named package (and its dependents if needed)
- Updates `cdo.lock` with new versions, URLs, checksums
- Reports which packages were updated and from which version to which version
- Does not modify `crate.toml` (constraints stay the same)
- If resolution fails (no compatible version found), error clearly and leave lock file unchanged

### Requirement 3: Registry Fetching

**ID:** REQ-PKG-3
**Description:** Download packages from the catalog registry (archive URL), verify checksum, and extract to the local cache.
**Acceptance Criteria:**
- Downloads the archive from the URL specified in the catalog (per-platform)
- Verifies SHA256 checksum against the catalog entry (using existing `checksum_verify_file`)
- On checksum mismatch: deletes the downloaded file, emits error, fails resolution
- Extracts to `.cdo/cache/deps/<name>-<version>/` (using existing `archive_extract_zip` / `archive_extract_targz`)
- If already cached (directory exists and is valid), skips download
- Supports both .zip and .tar.gz archives (detected from URL extension)
- Reports download progress at info level ("Downloading sdl3 3.2.0 ...")
- Retries failed downloads up to 3 times with backoff (existing `http_download` behavior)

### Requirement 4: Git Fetching

**ID:** REQ-PKG-4
**Description:** Clone git repositories as dependencies, checking out the specified tag, branch, or commit.
**Acceptance Criteria:**
- `cdo deps add <name> --git <url> [--ref <tag|branch|commit>]` adds a git dependency
- Clones to `.cdo/cache/git/<name>-<ref_hash>/`
- Uses shallow clone for tags (`--depth 1 --branch <tag>`)
- Uses full clone for branch tracking (to enable updates)
- If already cached, verifies the ref is correct; re-fetches if stale
- Git must be available on PATH; if not found, emit clear error
- Supports HTTPS and SSH URLs

### Requirement 5: Local Path Dependencies

**ID:** REQ-PKG-5
**Description:** Support dependencies specified as local filesystem paths for development/testing scenarios.
**Acceptance Criteria:**
- In `crate.toml`: `sdl3 = { path = "../third_party/sdl3" }`
- Resolves the path relative to the crate directory
- No caching, downloading, or version checking — uses the directory directly
- Validates the directory exists; errors clearly if not found
- Local deps are excluded from lock file (they're always "live")

### Requirement 6: Version Constraint Resolution

**ID:** REQ-PKG-6
**Description:** Resolve version constraints (semver) from `crate.toml` against available versions in the catalog.
**Acceptance Criteria:**
- Supports constraint formats: `"^1.2.0"`, `"~1.2.0"`, `">=1.2.0"`, `"<2.0.0"`, `"*"`, `"1.2.3"` (exact)
- Resolves to the highest version that satisfies the constraint from available catalog entries
- If no version satisfies the constraint, emit error listing available versions
- Uses existing `semver_satisfies` and `semver_compare` for resolution logic

### Requirement 7: Dependency Metadata Detection

**ID:** REQ-PKG-7
**Description:** After fetching a dependency, detect how it provides build metadata (include paths, link libraries) using the existing `dep_detect_metadata` function.
**Acceptance Criteria:**
- CDo-native (`cdo-package.toml`): parse the manifest for include_dirs, link_libs, defines
- CMake (`*Config.cmake`): extract include/lib paths from CMake package config (best-effort parsing)
- pkg-config (`.pc` files): parse .pc files for Cflags and Libs
- Directory convention fallback: assume `include/` for headers, `lib/` for libraries
- For catalog packages: use the catalog-specified include_dirs, link_libs, defines directly
- The detection hierarchy is: catalog metadata > cdo-package.toml > CMake > pkg-config > directory convention

### Requirement 8: Build Pipeline Integration

**ID:** REQ-PKG-8
**Description:** Integrate resolved dependency include paths, library paths, and link libraries into the compile and link jobs for crates that declare external dependencies.
**Acceptance Criteria:**
- Resolved include directories are added to compile job `-I` flags
- Resolved library directories are added to link job `-L` flags
- Resolved link library names are added to link job `-l` flags
- Resolved defines are added to compile job `-D` flags
- Runtime DLLs are copied to the crate's build output directory (for exe/ and dyn/ modules)
- Dependencies are resolved before compilation begins (during workspace load or build setup)
- Transitive dependencies are handled: if A depends on B which depends on C, A gets C's headers/libs too

### Requirement 9: Dependency Manifest Format

**ID:** REQ-PKG-9
**Description:** Define the format for declaring external dependencies in `crate.toml` alongside workspace-internal dependencies.
**Acceptance Criteria:**
- External dependencies are distinguished from internal ones by having version/url/git/path specifiers:
  ```toml
  [dependencies]
  internal_crate = {}                              # workspace-internal (no version)
  sdl3 = "^3.2.0"                                 # registry, version constraint
  my_lib = { version = "^1.0", features = [] }    # registry with options
  custom = { git = "https://...", ref = "v1.0" }   # git source
  local = { path = "../vendor/local_lib" }         # local path
  ```
- A bare `{}` or missing version means it's a workspace-internal dependency (existing behavior)
- A string value (e.g., `"^3.2.0"`) is shorthand for `{ version = "^3.2.0" }` from registry
- The parser distinguishes internal vs external and routes accordingly

### Requirement 10: Diamond Dependency Handling

**ID:** REQ-PKG-10
**Description:** Handle cases where multiple crates depend on the same external package, potentially at different version constraints.
**Acceptance Criteria:**
- If all constraints are compatible (can be satisfied by a single version), resolve to one shared version
- If constraints are incompatible, emit a clear error listing the conflicting constraints and which crates declared them
- The lock file stores the single resolved version per package (workspace-wide)
- Do NOT support multiple versions of the same package simultaneously (simplification; match Cargo's approach for v1 resolver)

### Requirement 11: Offline Mode

**ID:** REQ-PKG-11
**Description:** Support building without network access when all dependencies are cached.
**Acceptance Criteria:**
- If `cdo.lock` exists and all locked dependencies are in the local cache, build succeeds without network
- If a required dependency is missing from cache, emit a clear error: "Dependency 'X' not in cache. Run 'cdo deps update' with network access."
- No implicit network requests during `cdo build` if lock file is present and cache is populated
- `cdo deps update` is the explicit command that may require network access

### Requirement 12: Cache Management

**ID:** REQ-PKG-12
**Description:** Provide basic cache management for downloaded dependencies.
**Acceptance Criteria:**
- Dependencies are cached in `~/.cdo/cache/deps/` (registry) and `~/.cdo/cache/git/` (git)
- `cdo cache clear --deps` removes all cached dependencies
- Cache entries are shared across workspaces (if two workspaces use sdl3-3.2.0, only one copy in cache)
- No automatic eviction for dependency cache (unlike object cache) — dependencies are typically small

## Non-Requirements (Out of Scope)

- Build-from-source for non-CDo dependencies (e.g., running CMake on fetched sources) — Phase D deferred
- Private registry authentication
- Dependency auditing or vulnerability scanning
- Multiple versions of the same package in one workspace
- Source patches or overlays on fetched dependencies
- Publishing packages (Feature 12)

## Dependencies

- `http.c` — HTTP download with retries
- `archive.c` — ZIP and tar.gz extraction
- `checksum.c` — SHA256 verification
- `semver.c` — version parsing and constraint matching
- `deps.h` / `deps_resolve.c` — DepSpec, ResolvedDep, dep_resolve (already partially implemented)
- `deps_lock.c` — dep_lock_write, dep_lock_read (already implemented)
- `catalog.h` — CatalogPackageEntry, catalog_resolve_package
- `toml_parse.c` — TOML parsing for crate.toml extended format
- `cmd_deps.c` — existing deps subcommands (add, remove, list)

## Technical Notes

- Much of the infrastructure already exists. The main work is:
  1. Extended `crate.toml` parsing to distinguish internal vs external deps
  2. Resolution pipeline: collect all external deps across workspace → resolve versions → check lock → fetch if needed
  3. Integration into build pipeline: inject resolved paths into CompileJob/LinkJob
- The `dep_resolve` function already handles registry+git+local fetching. It needs to be wired into the build flow.
- The `catalog_resolve_package` function already resolves name+constraint to URL+checksum+metadata.
- The lock file implementation (`dep_lock_write`/`dep_lock_read`) is already complete.
- Estimated net new code: ~800-1200 lines (resolution pipeline + crate.toml parser extension + build integration)
