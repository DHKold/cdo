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
│   │   ├── config.json            # Resources (flattened from res/)
│   │   ├── textures/              # Resource subdirectories preserved
│   │   │   └── hero.png
│   │   ├── main.dxil              # Compiled shaders (flattened from shd/)
│   │   └── effects/               # Shader subdirectories preserved
│   │       └── bloom.dxil
│   └── other-tool/
│       ├── manifest.toml
│       └── other-tool.exe
└── bin/                           # Launchers on PATH
    ├── my-app.cmd                 # Windows launcher
    └── other-tool.cmd
```

Resources and shaders are placed directly relative to the app directory (default base: `.`).
This means:
- A resource file at `res/config.json` becomes `apps/my-app/config.json`
- A resource at `res/textures/hero.png` becomes `apps/my-app/textures/hero.png`
- A shader at `shd/main.hlsl` compiles to `apps/my-app/main.dxil`
- A shader at `shd/effects/bloom.hlsl` compiles to `apps/my-app/effects/bloom.dxil`

The base folder (relative to the app directory) is configurable via `crate.toml`:

```toml
[install]
resource-base = "."     # default: resources placed at app root
shader-base = "."       # default: shaders placed at app root
```

Setting `resource-base = "data"` would place resources under `apps/my-app/data/` instead.
Setting `shader-base = "shaders"` would place compiled shaders under `apps/my-app/shaders/`.

## New Source Files

| File | Purpose |
|------|---------|
| `crates/cdo/api/commands/bundle.h` | Public header for shared bundling utility |
| `crates/cdo/lib/commands/bundle.c` | Shared bundle logic (extracted from cmd_run) |
| `crates/cdo/api/commands/cmd_install.h` | Public header for install/uninstall commands |
| `crates/cdo/lib/commands/cmd_install.c` | Install/uninstall command implementation |
| `crates/cdo/lib/commands/cmd_install_internal.h` | Internal types (InstallManifest, InstallIndexEntry) |
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
   - res/ directory contents → placed at <resource-base>/ (default: app root)
   - shd/ directory contents → placed at <shader-base>/ (default: app root)
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

This logic has been extracted into a shared bundle utility (`bundle.h` / `bundle.c`) that both
`cmd_run` and `cmd_install` call via `bundle_prepare()`. The old `cmd_run.c` staging functions
(`run_prepare_staging`, `run_copy_dir_recursive`, `run_select_crate`) now delegate to the
shared implementations (`bundle_prepare`, `bundle_copy_dir_recursive`, `bundle_select_exe_crate`).

### Resource and Shader Placement

Resources and shaders are **not** placed in `res/` or `shd/` subdirectories in the app bundle.
Instead, their contents are copied directly relative to the app directory (or a configurable
base folder).

For `cdo run`, the staging behavior is the same — the exe runs from the staging root, so
resources placed at the staging root are accessible via relative paths like `"config.json"`
rather than `"res/config.json"`.

The base folder for each asset type is read from `crate.toml`:

```toml
[install]
resource-base = "."     # default: place res/ contents at app root
shader-base = "."       # default: place compiled shaders at app root
```

The bundler resolves the effective destination as:
- `<staging_dir>/<resource-base>/<relative_path_within_res>`
- `<staging_dir>/<shader-base>/<relative_path_within_shd>`

When `resource-base` is `"."` (default), `res/textures/hero.png` becomes `<staging_dir>/textures/hero.png`.
When `resource-base` is `"data"`, it becomes `<staging_dir>/data/textures/hero.png`.

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
resource_base = "."
shader_base = "."
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
- `crate.toml` parser: support optional `[install]` section with `resource-base` and `shader-base` fields

## Crate Configuration (`crate.toml`)

The `[install]` section is optional. When absent, defaults apply.

```toml
[crate]
name = "my-app"
version = "1.2.0"       # Optional, defaults to "0.0.0"

[install]
resource-base = "."     # Where res/ contents are placed relative to app dir (default: ".")
shader-base = "."       # Where compiled shaders are placed relative to app dir (default: ".")
```

Both `resource-base` and `shader-base` are relative paths from the app directory root.
A value of `"."` means the contents of `res/` and `shd/` are placed directly at the app root,
flattening one level of nesting. A value like `"assets"` would create an `assets/` subdirectory
in the app bundle.

This configuration also applies to `cdo run` staging, ensuring the app sees the same
relative paths in development and after installation.
