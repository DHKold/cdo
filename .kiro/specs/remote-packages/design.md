# Feature 2: Remote Package Fetching — Design

## Architecture

The dependency resolution pipeline runs during workspace load (or early in the build
command), before compilation begins. It collects external deps from all crates, resolves
versions, checks the lock file, fetches if needed, and populates a `ResolvedDep` table
that the build pipeline reads for include/lib paths.

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│ workspace_load   │────▶│ dep_collect_all   │────▶│ dep_resolve_all  │
│ (parse crate.toml)     │ (gather ext deps) │     │ (version resolve)│
└──────────────────┘     └──────────────────┘     └────────┬─────────┘
                                                           │
                              ┌─────────────────────────────┘
                              ▼
                    ┌──────────────────┐     ┌──────────────────┐
                    │ dep_lock_check   │────▶│ dep_fetch_all    │
                    │ (compare w/ lock)│     │ (download/clone) │
                    └──────────────────┘     └────────┬─────────┘
                                                      │
                              ┌────────────────────────┘
                              ▼
                    ┌──────────────────┐     ┌──────────────────┐
                    │ dep_populate_all │────▶│ Build Pipeline   │
                    │ (scan inc/lib)   │     │ (inject paths)   │
                    └──────────────────┘     └──────────────────┘
```

## New Source Files

| File | Purpose |
|------|---------|
| `crates/cdo/api/core/dep_resolve_ext.h` | Extended resolution: collect, resolve, populate for external deps |
| `crates/cdo/lib/core/dep_resolve_ext.c` | Resolution pipeline implementation |
| `crates/cdo/lib/core/dep_manifest_parse.c` | Parse external dep declarations from crate.toml |

## Modified Files

| File | Change |
|------|--------|
| `crates/cdo/lib/core/workspace_load.c` | Parse external deps from crate.toml into new Crate fields |
| `crates/cdo/lib/commands/cmd_build.c` | Invoke resolution pipeline before compilation; inject paths into jobs |
| `crates/cdo/api/core/workspace.h` | Add external dep fields to Crate struct |
| `crates/cdo/lib/commands/cmd_deps.c` | Add `update` subcommand |
| `crates/cdo/api/core/cli.h` | No change needed (deps update is handled as positional arg) |

## Data Structures

### Extended Crate Fields (workspace.h)

```c
typedef struct Crate {
    // ... existing fields ...

    // External dependencies (third-party packages)
    DepSpec*    ext_deps;           // Array of external dependency specs
    int         ext_dep_count;
    ResolvedDep* resolved_ext_deps; // Populated after resolution
} Crate;
```

### Workspace Extension

```c
typedef struct Workspace {
    // ... existing fields ...

    // External dependency resolution state
    DepSpec*    all_ext_deps;       // Deduplicated list of all ext deps in workspace
    int         all_ext_dep_count;
    ResolvedDep* resolved_deps;    // Parallel array to all_ext_deps
    bool        deps_resolved;     // True if resolution has been performed
} Workspace;
```

### DepSpec Extension (optional field additions)

The existing `DepSpec` struct already has the needed fields. We add a helper for parsing
the extended TOML format:

```c
typedef struct {
    char name[64];
    char version_constraint[32];   // "^1.2.0", "~1.0", "*", etc.
    DepSourceKind source;          // REGISTRY, GIT, LOCAL
    char url[512];                 // git URL or local path (for non-registry)
    char git_ref[128];             // tag/branch/commit
} DepDeclSpec;  // What's declared in crate.toml (pre-resolution)
```

## crate.toml Parsing Extension

### Format

```toml
[dependencies]
# Internal workspace dependency (no version, no URL)
my_utils = {}

# External registry dependency (string shorthand)
sdl3 = "^3.2.0"

# External registry dependency (table form)
zlib = { version = "^1.3.0" }

# Git dependency
custom_lib = { git = "https://github.com/user/lib.git", ref = "v2.0.0" }

# Local path dependency
dev_helper = { path = "../vendor/dev_helper" }
```

### Detection Logic

A dependency is **external** if it has any of:
- A string value (version constraint shorthand)
- A `version` key in its table
- A `git` key in its table
- A `path` key in its table

A dependency is **internal** (workspace crate) if:
- Its value is `{}` (empty table)
- It matches a workspace crate by name

### Parse Algorithm

```
for each entry in [dependencies]:
    if value is string:
        -> external registry dep, string is version constraint
    else if value is table:
        if table has "git":
            -> external git dep
        else if table has "path":
            -> external local dep
        else if table has "version":
            -> external registry dep
        else:
            -> internal workspace dep (matches current behavior)
```

## Resolution Pipeline

### Phase 1: Collection (`dep_collect_all`)

Iterate all crates in workspace, gather all `DepDeclSpec` entries into a workspace-wide list.
Deduplicate by name (same package referenced by multiple crates).

### Phase 2: Constraint Merging

For each unique package name, collect all version constraints from different crates.
Attempt to find a single version that satisfies all constraints:

```c
int dep_merge_constraints(const SemverConstraint* constraints, int count,
                          const Catalog* catalog, const char* pkg_name,
                          const CatalogPlatform* platform,
                          DepSpec* out_resolved);
```

If no version satisfies all constraints, report the conflict.

### Phase 3: Lock File Check (`dep_lock_check`)

```
if cdo.lock exists:
    read locked specs
    for each collected dep:
        if dep is in lock file and constraint still satisfied:
            use locked version (skip resolution)
        else:
            mark for re-resolution
    if any deps need re-resolution:
        resolve only those
        write updated lock file
else:
    resolve all deps
    write new lock file
```

### Phase 4: Fetch (`dep_fetch_all`)

For each resolved dep that is not in the local cache:
- Registry: `http_download` + `checksum_verify_file` + `archive_extract_*`
- Git: `pal_spawn("git", ["clone", ...])` 
- Local: no-op (path used directly)

Uses existing `dep_resolve` function which already handles all three cases.

### Phase 5: Populate (`dep_populate_all`)

For each fetched dep, scan its directory for include/lib paths:
- Use existing `dep_populate_resolved` (already implemented in `deps_resolve.c`)
- For catalog packages: override with catalog-specified metadata (include_dirs, link_libs, defines)

### Phase 6: Inject into Build

In `cmd_build.c`, after resolution, when setting up CompileJob and LinkJob for each crate:
- Add resolved include_dirs to CompileJob.include_paths
- Add resolved lib_dirs to LinkJob.lib_paths
- Add resolved link_libs to LinkJob.link_libs
- Copy runtime DLLs to build output (same as existing dyn propagation)

## `cdo deps update` Command

```c
int deps_update(const CdoOptions* opts) {
    // 1. Load workspace
    // 2. Load catalog
    // 3. Collect all external deps
    // 4. If positional arg given: filter to just that package
    // 5. Re-resolve within constraints (ignore lock file for targeted packages)
    // 6. Report changes: "Updated sdl3: 3.2.0 -> 3.3.1"
    // 7. Write updated lock file
    // 8. Fetch newly resolved versions (populate cache)
}
```

Added as a subcommand in `cmd_deps.c`:
```c
} else if (strcmp(subcmd, "update") == 0) {
    return deps_update(opts);
}
```

## Build Integration Points

### In `cmd_build.c`

After workspace load, before the build loop:

```c
// --- Resolve external dependencies ---
if (ws.all_ext_dep_count > 0) {
    Catalog cat = {0};
    catalog_load(&cat, ws.root_path);
    
    rc = dep_resolve_workspace(&ws, &cat);
    if (rc != 0) {
        cdo_error("Failed to resolve external dependencies");
        catalog_free(&cat);
        workspace_free(&ws);
        return 1;
    }
    catalog_free(&cat);
}
```

### In module build functions (cmd_build_lib.c, cmd_build_exe.c, etc.)

When constructing CompileJob arrays, append external dep include paths:
```c
// Add external dependency include paths
for (int e = 0; e < crate->ext_dep_count; e++) {
    if (crate->resolved_ext_deps[e].include_dir[0]) {
        include_paths[include_count++] = crate->resolved_ext_deps[e].include_dir;
    }
}
```

When constructing LinkJob, append external dep lib paths and link libs:
```c
// Add external dependency link paths and libs
for (int e = 0; e < crate->ext_dep_count; e++) {
    ResolvedDep* rd = &crate->resolved_ext_deps[e];
    if (rd->lib_dir[0]) {
        lib_paths[lib_count++] = rd->lib_dir;
    }
    for (int l = 0; l < rd->link_lib_count; l++) {
        link_libs[link_count++] = rd->link_libs[l];
    }
}
```

## Lock File Location

`cdo.lock` is written at the workspace root (same directory as `cdo.toml`).

## Cache Layout

```
~/.cdo/cache/
├── deps/                    # Registry packages
│   ├── sdl3-3.2.0/         # Extracted archive
│   │   ├── include/
│   │   ├── lib/
│   │   └── bin/
│   └── zlib-1.3.1/
├── git/                     # Git clones
│   ├── custom_lib-a1b2c3d/ # <name>-<short_hash>
│   └── ...
└── objects/                 # Build cache (existing, unrelated)
```

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Network unavailable + lock exists + cache populated | Build succeeds (offline mode) |
| Network unavailable + lock missing | Error: "Cannot resolve dependencies without network. No lock file found." |
| Network unavailable + lock exists + cache miss | Error: "Dependency 'X' not in cache. Run 'cdo deps update' with network." |
| Checksum mismatch | Delete download, error: "Checksum mismatch for 'X'. Expected: ... Got: ..." |
| Git not installed | Error: "Git is required for dependency 'X'. Install git and ensure it's on PATH." |
| Incompatible constraints | Error: "Version conflict for 'X': crate A requires ^1.0, crate B requires ^2.0" |
| Package not in catalog | Error: "Package 'X' not found in catalog. Run 'cdo catalog list' to see available packages." |

## Transitive External Dependency Handling

For v1: External dependencies are NOT resolved transitively. If package A depends on
package B, the user must explicitly declare both in their crate.toml. This matches
the current catalog design (flat package list, no dependency graph between catalog entries).

Future: Add a `[dependencies]` section to `cdo-package.toml` for CDo-native packages,
enabling transitive resolution.
