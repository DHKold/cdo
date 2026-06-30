# Feature 9: `cdo install` — System-Wide Binary Installation

## Overview

Build a release binary with all its runtime dependencies (DLLs, resources, shaders)
and install it as a self-contained app bundle to a well-known location, with a
lightweight launcher on PATH. Enables CDo-built tools to be used system-wide.

## Motivation

Cargo's `cargo install` is one of its most-used features — it turns any Rust crate
into a system-available tool with one command. CDo should offer the same for C/C++
projects, but must also handle the reality of native binaries: dynamic libraries,
resource files, and shader assets that must ship alongside the executable.

## Installation Layout

```
~/.cdo/
├── apps/
│   ├── install.toml           # Global installation index (fast lookup)
│   ├── my-app/
│   │   ├── manifest.toml      # Per-app manifest (self-describing)
│   │   ├── my-app.exe         # The actual binary
│   │   ├── SDL3.dll           # Runtime DLLs (from dependencies)
│   │   ├── my_engine.dll      # Workspace dyn/ outputs
│   │   ├── config.json        # Resources (placed at app root by default)
│   │   ├── textures/          # Resource subdirectories preserved
│   │   │   └── hero.png
│   │   ├── main.dxil          # Compiled shaders (placed at app root by default)
│   │   └── effects/           # Shader subdirectories preserved
│   │       └── bloom.dxil
│   └── other-tool/
│       ├── manifest.toml
│       └── other-tool.exe
└── bin/
    ├── my-app.cmd             # Launcher: @"%~dp0..\apps\my-app\my-app.exe" %*
    └── other-tool.cmd         # Launcher: @"%~dp0..\apps\other-tool\other-tool.exe" %*
```

Resources and shaders are placed directly relative to the app directory (not inside
`res/` or `shd/` subfolders). The base folder is configurable via `crate.toml`:

```toml
[install]
resource-base = "."     # default: res/ contents go to app root
shader-base = "."       # default: compiled shaders go to app root
```

On Unix, launchers are shell scripts instead of `.cmd` files:
```sh
#!/bin/sh
exec "$(dirname "$0")/../apps/my-app/my-app" "$@"
```

## Requirements

### Requirement 1: Basic Install Command

**ID:** REQ-INSTALL-1
**Description:** Provide a `cdo install [<crate>]` command that builds the specified crate in release mode, stages all runtime artifacts, and installs the complete app bundle.
**Acceptance Criteria:**
- Running `cdo install my-app` builds `my-app` in release mode
- The staging folder (exe + DLLs + res/ + shd/) is copied to `~/.cdo/apps/my-app/`
- A launcher script is generated in `~/.cdo/bin/my-app.cmd` (Windows) or `~/.cdo/bin/my-app` (Unix)
- If no crate name is given and only one executable crate exists, installs that one
- If no crate name is given and multiple executable crates exist, errors with a helpful message listing options
- Only crates with an exe/ module (producing an executable) can be installed
- Returns 0 on success, non-zero on failure

### Requirement 2: Complete App Bundle with Runtime Dependencies

**ID:** REQ-INSTALL-2
**Description:** The installed app bundle must include all runtime dependencies so the binary works standalone, outside the workspace.
**Acceptance Criteria:**
- The exe binary is included
- All runtime DLLs from external dependencies (resolved via `ResolvedDep.runtime_dlls`) are included
- All workspace dyn/ module outputs that the crate depends on (transitively) are included
- The res/ module output (if present) is included with its contents placed relative to the app directory (not inside a `res/` subfolder). E.g. `res/config.json` → `apps/my-app/config.json`, `res/textures/hero.png` → `apps/my-app/textures/hero.png`
- The shd/ module output (compiled shaders, if present) follows the same pattern: `shd/effects/bloom.hlsl` → `apps/my-app/effects/bloom.dxil`
- The base folder for resource and shader placement is configurable via `[install].resource-base` and `[install].shader-base` in `crate.toml` (default: `"."` = app root)
- The bundle is self-contained: moving the app directory elsewhere still works (DLLs sit next to exe)
- Reuses the existing staging folder mechanism from `cdo run`

### Requirement 3: Custom Install Path

**ID:** REQ-INSTALL-3
**Description:** Allow users to specify an alternative base installation directory with `--path <dir>`.
**Acceptance Criteria:**
- `cdo install --path /opt/mytools` installs app bundle to `/opt/mytools/apps/my-app/` and launcher to `/opt/mytools/bin/my-app`
- The target directories are created (recursively) if they don't exist
- Relative paths are resolved against the current working directory
- If the directory cannot be created or written to, emit a clear error
- The global manifest and launcher are placed relative to the provided path

### Requirement 4: Global Install

**ID:** REQ-INSTALL-4
**Description:** Provide a `--global` flag that installs to the system-wide location.
**Acceptance Criteria:**
- On Windows: installs to `%LOCALAPPDATA%\Programs\cdo\apps\` with launchers in `%LOCALAPPDATA%\Programs\cdo\bin\`
- On Linux/macOS: installs to `/usr/local/lib/cdo/apps/` with launchers in `/usr/local/bin/`
- If the directory doesn't exist, attempts to create it
- If writing fails (permissions), emit a clear error suggesting `--path` or elevated privileges

### Requirement 5: Debug Symbol Stripping

**ID:** REQ-INSTALL-5
**Description:** By default, strip debug symbols from the installed binary to reduce size. Provide `--debug` to preserve them.
**Acceptance Criteria:**
- By default, runs `strip` (GCC/Clang) on the installed exe and DLLs
- `--debug` skips the stripping step
- If `strip` is not available, installs without stripping and emits a debug-level message
- The original build artifacts in `build/release/` are never modified (strip operates on copies)

### Requirement 6: Per-App Manifest

**ID:** REQ-INSTALL-6
**Description:** Each installed app bundle contains a `manifest.toml` that describes the installation, making the bundle self-describing.
**Acceptance Criteria:**
- Written to `<apps_dir>/<name>/manifest.toml` on install
- Contains:
  ```toml
  [app]
  name = "my-app"
  version = "0.1.0"
  crate = "my-app"
  source_workspace = "C:/projects/my-workspace"
  installed_at = "2026-06-29T10:00:00Z"
  cdo_version = "0.5.0"

  [contents]
  executable = "my-app.exe"
  dlls = ["SDL3.dll", "my_engine.dll"]
  has_resources = true
  has_shaders = true
  resource_base = "."
  shader_base = "."
  ```
- Used by `cdo uninstall` to know what was installed
- Used to detect reinstall vs new install

### Requirement 7: Global Installation Index

**ID:** REQ-INSTALL-7
**Description:** Maintain a global `install.toml` at the apps root for fast enumeration without scanning subdirectories.
**Acceptance Criteria:**
- Written to `<apps_dir>/install.toml`
- Contains an entry per installed app:
  ```toml
  [[app]]
  name = "my-app"
  version = "0.1.0"
  source_workspace = "C:/projects/my-workspace"
  installed_at = "2026-06-29T10:00:00Z"
  path = "my-app"

  [[app]]
  name = "other-tool"
  version = "2.0.0"
  source_workspace = "C:/projects/tools"
  installed_at = "2026-06-28T15:30:00Z"
  path = "other-tool"
  ```
- Updated atomically on install/uninstall (write to temp file, rename)
- If corrupt or missing: rebuilt by scanning subdirectories for `manifest.toml` files
- `cdo install --list` reads this for fast output

### Requirement 8: Uninstall Command

**ID:** REQ-INSTALL-8
**Description:** Provide `cdo uninstall <name>` to remove a previously installed app bundle and its launcher.
**Acceptance Criteria:**
- Removes the app directory: `<apps_dir>/<name>/`
- Removes the launcher: `<bin_dir>/<name>.cmd` (Windows) or `<bin_dir>/<name>` (Unix)
- Removes the entry from the global `install.toml`
- If the app doesn't exist, emits a warning and returns 0
- Works for any previously installed app regardless of which workspace installed it
- Logs the removed path at info level

### Requirement 9: List Installed Apps

**ID:** REQ-INSTALL-9
**Description:** Provide `cdo install --list` to show all installed apps with metadata.
**Acceptance Criteria:**
- Reads from the global `install.toml` for speed
- Displays a table: name, version, source workspace, install date
- If the global index is missing, falls back to scanning app directories for manifests
- If no apps are installed, prints "No applications installed."

### Requirement 10: Overwrite Protection

**ID:** REQ-INSTALL-10
**Description:** Warn before overwriting an existing installation from a different workspace or with a different version.
**Acceptance Criteria:**
- If the target app already exists and was installed from a different workspace, warn and require `--force`
- If re-installing the same app from the same workspace (updating), proceed silently
- `--force` flag bypasses the overwrite check
- On overwrite: the old app directory is fully replaced (not merged)

### Requirement 11: Launcher Script Generation

**ID:** REQ-INSTALL-11
**Description:** Generate platform-appropriate launcher scripts that forward execution to the app bundle.
**Acceptance Criteria:**
- Windows: generates `<name>.cmd` with content: `@"%~dp0..\apps\<name>\<name>.exe" %*`
- Unix: generates `<name>` (no extension) with content:
  ```sh
  #!/bin/sh
  exec "$(dirname "$0")/../apps/<name>/<name>" "$@"
  ```
- Unix launcher is marked executable (chmod +x)
- The launcher correctly forwards all arguments and preserves exit codes
- Relative path from bin/ to apps/ works regardless of symlinks

### Requirement 12: Version Source

**ID:** REQ-INSTALL-12
**Description:** Determine the installed app's version from the crate configuration.
**Acceptance Criteria:**
- If `crate.toml` has a `version` field under `[crate]`, use it
- If no version field exists, default to "0.0.0"
- The version is recorded in both the per-app manifest and global index
- Future: `cdo install` could reject installing if version hasn't been bumped (opt-in via config)

## Non-Requirements (Out of Scope)

- `cdo install --git <url>` (depends on Feature 2: Remote Package Fetching)
- Automatic PATH configuration (handled by venv activation scripts; user must add `~/.cdo/bin/` to PATH manually for non-venv usage)
- Cross-platform binary installation (only installs for the current host)
- Package signing or verification of installed binaries
- Self-update mechanism (`cdo update` is a separate feature)
- Dependency resolution at install time (uses whatever the workspace resolves)

## Dependencies

- Existing `cmd_build` infrastructure (release build)
- Existing staging folder mechanism from `cdo run` (collects exe + DLLs + res + shd)
- PAL: `pal_file_copy`, `pal_get_home_dir`, `pal_mkdir_p`, `pal_path_exists`, `pal_rmdir_r`
- CLI parsing infrastructure for the new command and flags
- TOML serialization for manifest files

## Technical Notes

- The staging folder from `cdo run` already solves the "collect all runtime deps" problem. The install command should reuse or share that logic rather than reimplementing it.
- On Windows, DLL search order checks the exe's directory first, so DLLs next to the exe are found automatically — no PATH manipulation needed for the app bundle.
- The `.cmd` wrapper approach is used by npm, Scoop, Chocolatey, and rustup on Windows. It's well-understood and reliable.
- Version info requires adding an optional `version = "x.y.z"` field to `crate.toml`'s `[crate]` section. The parser should treat it as optional with a "0.0.0" default.
- Resources and shaders are placed directly relative to the app directory (not in `res/` or `shd/` subfolders). This matches how most applications expect to find their assets — relative to the executable. The base folder is configurable via `[install].resource-base` and `[install].shader-base` in `crate.toml` (both default to `"."`).
- The same base-folder logic applies to `cdo run` staging, so the app sees identical relative paths during development and after installation.
