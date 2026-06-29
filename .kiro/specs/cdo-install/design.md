# Feature 9: `cdo install` — Design

## Architecture

The install command orchestrates: build → stage → copy bundle → strip → write manifests → generate launcher.
It reuses the existing staging folder mechanism from `cdo run` to collect all runtime artifacts.

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ CLI Parse    │────▶│ Release Build │────▶│ Stage Runtime│────▶│ Copy Bundle  │
│ (cmd_install)│     │ (cmd_build)  │     │ (reuse run)  │     │ to apps dir  │
└──────────────┘     └──────────────┘     └──────────────┘     └──────┬───────┘
                                                                       │
                         ┌─────────────────────────────────────────────┘
                         ▼
┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ Strip Bins   │────▶│ Write App    │────▶│ Update Global│────▶│ Generate     │
│ (optional)   │     │ Manifest     │     │ Index        │     │ Launcher     │
└──────────────┘     └──────────────┘     └──────────────┘     └──────────────┘
```

## Directory Layout

```
~/.cdo/
├── apps/                          # App bundles root
│   ├── install.toml               # Global index (fast enumeration)
│   ├── my-app/                    # One directory per installed app
│   │   ├── manifest.toml          # Per-app metadata
│   │   ├── my-app.exe             # Executable
│   │   ├── SDL3.dll               # External dep DLLs
│   │   ├── my_engine.dll          # Workspace dyn/ outputs
│   │   ├── res/                   # Resources (from res/ module)
│   │   │   └── config.json
│   │   └── shd/                   # Compiled shaders (from shd/ module)
│   │       └── main.dxil
│   └── other-tool/
│       ├── manifest.toml
│       └── other-tool.exe
└── bin/                           # Launchers on PATH
    ├── my-app.cmd                 # Windows launcher
    └── other-tool.cmd
```

## New Source Files

| File | Purpose |
|------|---------|
| `crates/cdo/api/commands/cmd_install.h` | Public header for install/uninstall commands |
| `crates/cdo/lib/commands/cmd_install.c` | Install command implementation |
| `crates/cdo/lib/commands/cmd_install_manifest.c` | Per-app manifest and global index read/write |

## CLI Interface

```
cdo install [<crate>] [OPTIONS]

Options:
  --path <dir>     Base install directory (default: ~/.cdo/)
  --global         Install to system-wide location
  --debug          Preserve debug symbols (skip strip)
  --force          Overwrite without warning
  --list           List installed applications

cdo uninstall <name> [OPTIONS]

Options:
  --path <dir>     Base directory to uninstall from (default: ~/.cdo/)
  --global         Uninstall from system-wide location
```

## Command Enum Extension

Add to `CdoCommand` enum in `cli.h`:
```c
CDO_CMD_INSTALL,
CDO_CMD_UNINSTALL,
```

Add to `CdoOptions`:
```c
bool force;      // --force flag
bool global;     // --global flag
```

## Core Logic

### `cmd_install(const CdoOptions* opts)`

```
1. If --list: call install_list() and return
2. Load workspace
3. Determine target crate:
   - If positional arg: find crate by name
   - Else: find the single exe crate, error if 0 or >1
4. Validate: crate must have exe/ module
5. Build in release mode (construct CdoOptions with release=true, call cmd_build)
6. Stage runtime artifacts (reuse staging logic from cmd_run):
   - exe binary
   - dep DLLs (transitive)
   - workspace dyn/ outputs (transitive)
   - res/ directory contents
   - shd/ directory contents
7. Resolve install base dir:
   - --path: use provided
   - --global: platform system dir
   - Default: ~/.cdo/
8. Determine apps_dir = <base>/apps/<name>/
9. Determine bin_dir = <base>/bin/
10. Check overwrite protection (read existing manifest)
11. If app dir exists: remove it entirely (clean install)
12. Create apps_dir, copy staged contents
13. Strip binaries (unless --debug)
14. Write per-app manifest.toml
15. Update global install.toml
16. Generate launcher script in bin_dir
17. Log success: "Installed 'my-app' v0.1.0 to ~/.cdo/apps/my-app/"
```

### `cmd_uninstall(const CdoOptions* opts)`

```
1. Get app name from positional arg (required)
2. Resolve install base dir (same logic as install)
3. Check if app directory exists
4. If not: warn and return 0
5. Remove app directory (pal_rmdir_r)
6. Remove launcher from bin_dir
7. Remove entry from global install.toml
8. Log: "Uninstalled 'my-app'"
```

### `install_list(const CdoOptions* opts)`

```
1. Resolve install base dir
2. Read global install.toml
3. If missing/corrupt: scan apps/ subdirectories for manifest.toml files, rebuild index
4. Print table: Name | Version | Source | Installed
5. If empty: "No applications installed."
```

## Staging Folder Reuse

The `cdo run` command already has logic to:
1. Build the crate
2. Create a staging directory (`build/<profile>/<crate>/stage/`)
3. Copy the exe there
4. Propagate DLLs from dependencies (transitive)
5. Copy res/ and shd/ outputs

The install command reuses this staging by:
- Building in release mode
- Using the same propagation logic to populate a staging dir
- Then copying the entire staging dir contents to `<apps_dir>/<name>/`

If the staging logic lives in a shared function (e.g., `build_stage_runtime`), both
`cmd_run` and `cmd_install` can call it. If it's currently embedded in `cmd_run.c`,
it should be extracted to a shared utility.

## Per-App Manifest Format (`manifest.toml`)

```toml
[app]
name = "my-app"
version = "0.1.0"
crate = "my-app"
source_workspace = "C:/Workspace/my-project"
installed_at = "2026-06-29T10:00:00Z"
cdo_version = "0.5.0"
profile = "release"

[contents]
executable = "my-app.exe"
dlls = ["SDL3.dll", "my_engine.dll"]
has_resources = true
has_shaders = true
file_count = 15
total_size_bytes = 4521984
```

## Global Index Format (`install.toml`)

```toml
[[app]]
name = "my-app"
version = "0.1.0"
source_workspace = "C:/Workspace/my-project"
installed_at = "2026-06-29T10:00:00Z"
path = "my-app"

[[app]]
name = "other-tool"
version = "2.0.0"
source_workspace = "C:/Workspace/tools"
installed_at = "2026-06-28T15:30:00Z"
path = "other-tool"
```

The global index is a subset of per-app manifest data — just enough for `--list` output.
It's always rebuildable from the per-app manifests (self-healing).

## Launcher Scripts

### Windows (`<name>.cmd`)

```cmd
@"%~dp0..\apps\<name>\<name>.exe" %*
```

- `%~dp0` resolves to the directory containing the .cmd file (bin/)
- `%*` forwards all arguments
- Exit code is automatically propagated

### Unix (`<name>`, no extension)

```sh
#!/bin/sh
exec "$(dirname "$0")/../apps/<name>/<name>" "$@"
```

- `exec` replaces the shell process (proper exit code, signal handling)
- `$@` forwards all arguments
- Marked chmod +x after creation

## Stripping Strategy

| Compiler | Strip Command | Applies To |
|----------|---------------|------------|
| GCC | `strip --strip-all <file>` | exe + DLLs |
| Clang | `strip --strip-all <file>` (or `llvm-strip`) | exe + DLLs |
| MSVC | No-op (use `/DEBUG:NONE` at link time) | — |

Strip operates on files in the app bundle directory (after copy), never on build artifacts.
Failure to strip is non-fatal (debug message, continue).

## Platform-Specific Paths

| Platform | Default Base | Global Base |
|----------|-------------|-------------|
| Windows | `%USERPROFILE%\.cdo\` | `%LOCALAPPDATA%\Programs\cdo\` |
| Linux | `~/.cdo/` | `/usr/local/lib/cdo/` (apps) + `/usr/local/bin/` (launchers) |
| macOS | `~/.cdo/` | `/usr/local/lib/cdo/` (apps) + `/usr/local/bin/` (launchers) |

## Overwrite Protection Logic

```
existing_manifest = read manifest from <apps_dir>/<name>/manifest.toml
if exists:
    if existing.source_workspace != current_workspace AND !force:
        error: "App 'X' was installed from a different workspace (Y). Use --force to overwrite."
        return 1
    else:
        info: "Reinstalling 'X' (v_old -> v_new)"
        # proceed with overwrite (rmdir + fresh copy)
```

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Build fails | Propagate build error, don't install |
| No exe crate found | "No executable crate found in workspace" |
| Multiple exe crates | "Multiple executable crates found: X, Y, Z. Specify one." |
| Permission denied on apps dir | "Cannot write to install directory. Use --path or run with elevated privileges." |
| Staging incomplete (missing exe) | Internal error (shouldn't happen after successful build) |
| Strip tool not found | Debug log, continue without stripping |
| Global index corrupt | Rebuild from per-app manifests, warn |

## Integration Points

- `main.cpp`: Add `CDO_CMD_INSTALL` and `CDO_CMD_UNINSTALL` cases in dispatch switch
- `cli.h`: Add enum values, add `--force`, `--global` to CdoOptions
- `cdo_cli_parse`: Handle "install" and "uninstall" command tokens
- Help text: add install/uninstall descriptions
- `cmd_run.c`: Extract staging logic into shared utility (or call from install)
- `crate.toml` parser: support optional `version` field under `[crate]`
