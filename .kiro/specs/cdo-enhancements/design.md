# Design Document: CDo Enhancements

## Overview

This design covers three CDo enhancements: (1) a comprehensive README with feature documentation and quick-start guide, (2) a virtual environment (`--venv`) feature for `cdo init`, and (3) a file-locking mechanism for `cdo build` and `cdo test`. The design integrates with existing PAL, command, and workspace infrastructure.

## Components and Interfaces

### Components

| Component | File(s) | Responsibility |
|-----------|---------|----------------|
| README_Generator | `README.md` | Hand-authored feature documentation and quick-start guide |
| Venv_Initializer | `crates/cdo/lib/commands/cmd_new.c` | Creates `.cdo` venv structure, copies binary, generates activation scripts |
| Activation_Script | Generated: `activate.bat`, `activate.ps1`, `activate.sh` | Shell environment modification for local CDo isolation |
| Build_Lock_Manager | `crates/cdo/lib/commands/build_lock.c` | High-level lock acquire/release around build operations |
| PAL File Lock | `crates/cdo/lib/pal/pal_file_lock.c` | Cross-platform file locking primitives (`LockFileEx` / `flock`) |

### Interfaces

```c
// Venv_Initializer
int venv_init(const char* workspace_root);

// Build_Lock_Manager
int build_lock_acquire(const char* workspace_root, int timeout_sec, BuildLock** lock_out);
void build_lock_release(BuildLock* lock);

// PAL File Lock
int pal_file_lock_exclusive(const char* path, int timeout_ms, PalFileLock** lock_out);
void pal_file_lock_release(PalFileLock* lock);

// PAL Utilities (new)
int pal_get_executable_path(char* buf, size_t buf_size);
int pal_file_copy(const char* src, const char* dst);
```

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                      CDo CLI (main.cpp)                  │
│   cdo_cli_parse → dispatch → cmd_init / cmd_build / ... │
└────────────┬────────────────────────┬───────────────────┘
             │                        │
   ┌─────────▼─────────┐   ┌─────────▼──────────┐
   │  Venv_Initializer  │   │  Build_Lock_Manager │
   │  (cmd_new.c ext)   │   │  (build_lock.c)     │
   └─────────┬──────────┘   └─────────┬───────────┘
             │                         │
   ┌─────────▼─────────────────────────▼──────────┐
   │              PAL Layer (pal.h)                 │
   │  pal_fs.c  pal_path.c  pal_process.c         │
   │  + NEW: pal_file_lock.c                       │
   └───────────────────────────────────────────────┘
```

### Component Boundaries

| Component | File(s) | Responsibility |
|-----------|---------|----------------|
| README_Generator | Manual documentation in `README.md` | Static documentation (hand-authored, not code-generated) |
| Venv_Initializer | `crates/cdo/lib/commands/cmd_new.c` (extension) | Creates `.cdo` venv structure, copies binary, generates scripts |
| Activation_Script | Generated files: `activate.bat`, `activate.ps1`, `activate.sh` | Modifies shell environment for local CDo usage |
| Build_Lock_Manager | `crates/cdo/lib/commands/build_lock.c` (new) | Acquires/releases exclusive file locks around builds |
| PAL File Lock | `crates/cdo/lib/pal/pal_file_lock.c` (new) | Cross-platform file locking primitives |

## Feature 1: README Documentation

The README is a hand-authored markdown file, not a generated artifact. It will be structured with the following sections:

```
README.md
├── Overview / Introduction
├── Installation & Prerequisites
├── Quick Start (init → build → run)
├── Commands Reference
│   ├── build, run, test, clean
│   ├── new, init (including --venv)
│   ├── deps, catalog, tool, doctor, shader
├── Configuration
│   ├── cdo.toml (workspace)
│   ├── crate.toml (per-crate)
│   └── Build Profiles
├── Virtual Environment (--venv)
├── Build Locking
└── Contributing / License
```

No code changes are required beyond writing the README content itself.

## Feature 2: Virtual Environment (`cdo init --venv`)

### Integration with `cmd_init`

The `--venv` flag extends the existing `cmd_init` function in `cmd_new.c`. After the template instantiation step (or standalone if no template is specified), the venv initialization runs.

```c
// Modified cmd_init flow:
int cmd_init(const CdoOptions* opts) {
    // ... existing template logic ...

    // NEW: If --venv flag is present, initialize virtual environment
    if (has_flag(opts, "--venv")) {
        int rc = venv_init(workspace_root);
        if (rc != 0) return rc;
    }

    return 0;
}
```

### Venv_Initializer Interface

```c
// crates/cdo/api/commands/cmd_new.h (additions)

/// Initialize a virtual environment in the given workspace root.
/// Creates .cdo/ directory structure, copies current executable,
/// and generates activation scripts.
/// If .cdo/ already exists, preserves tools/ and cache/ but
/// regenerates scripts and binary.
/// Returns 0 on success, non-zero on failure.
int venv_init(const char* workspace_root);
```

### Directory Structure Created

```
<workspace>/
└── .cdo/
    ├── cdo.exe          (or 'cdo' on Unix) — copied from running executable
    ├── activate.bat     — Windows CMD activation
    ├── activate.ps1     — Windows PowerShell activation
    ├── activate.sh      — POSIX shell activation
    ├── tools/           — preserved if pre-existing
    └── cache/           — preserved if pre-existing
```

### Venv Initialization Logic

```c
int venv_init(const char* workspace_root) {
    char cdo_dir[MAX_PATH_LEN];
    pal_path_join(cdo_dir, sizeof(cdo_dir), workspace_root, ".cdo");

    // 1. Create .cdo directory (pal_mkdir_p preserves existing)
    pal_mkdir_p(cdo_dir);

    // 2. Copy current executable into .cdo/
    char self_path[MAX_PATH_LEN];
    pal_get_executable_path(self_path, sizeof(self_path)); // NEW PAL function
    char dest_exe[MAX_PATH_LEN];
#ifdef _WIN32
    pal_path_join(dest_exe, sizeof(dest_exe), cdo_dir, "cdo.exe");
#else
    pal_path_join(dest_exe, sizeof(dest_exe), cdo_dir, "cdo");
#endif
    pal_file_copy(self_path, dest_exe); // NEW PAL function

    // 3. Generate activation scripts
    venv_generate_activate_bat(cdo_dir);
    venv_generate_activate_ps1(cdo_dir);
    venv_generate_activate_sh(cdo_dir);

    return 0;
}
```

### Activation Script Generation

Each script generator writes a platform-appropriate script that:
1. Stores the original `PATH` (and prompt) in a backup variable
2. Prepends `.cdo` to `PATH`
3. Sets `CDO_HOME` and `CDO_VENV` to the absolute `.cdo` path
4. Modifies the shell prompt with a `(cdo)` prefix
5. Defines a `deactivate` command/function that reverses all changes

#### activate.bat Template

```bat
@echo off
REM CDo Virtual Environment Activation (Windows CMD)
set "CDO_VENV_OLD_PATH=%PATH%"
set "CDO_VENV_OLD_PROMPT=%PROMPT%"
set "CDO_HOME=%~dp0"
set "CDO_VENV=%~dp0"
set "PATH=%~dp0;%PATH%"
set "PROMPT=(cdo) %PROMPT%"
echo CDo virtual environment activated.
echo Use 'deactivate' to restore the original environment.
doskey deactivate=set "PATH=%CDO_VENV_OLD_PATH%" $T set "PROMPT=%CDO_VENV_OLD_PROMPT%" $T set "CDO_HOME=" $T set "CDO_VENV=" $T set "CDO_VENV_OLD_PATH=" $T set "CDO_VENV_OLD_PROMPT="
```

#### activate.ps1 Template

```powershell
# CDo Virtual Environment Activation (PowerShell)
$script:CDO_VENV_OLD_PATH = $env:PATH
$script:CDO_VENV_OLD_PROMPT = $function:prompt

$env:CDO_HOME = $PSScriptRoot
$env:CDO_VENV = $PSScriptRoot
$env:PATH = "$PSScriptRoot;$env:PATH"

function global:prompt {
    "(cdo) " + (& $script:CDO_VENV_OLD_PROMPT)
}

function global:deactivate {
    $env:PATH = $script:CDO_VENV_OLD_PATH
    Remove-Item Env:CDO_HOME -ErrorAction SilentlyContinue
    Remove-Item Env:CDO_VENV -ErrorAction SilentlyContinue
    $function:global:prompt = $script:CDO_VENV_OLD_PROMPT
    Remove-Item Function:deactivate
}

Write-Host "CDo virtual environment activated."
Write-Host "Use 'deactivate' to restore the original environment."
```

#### activate.sh Template

```bash
#!/bin/sh
# CDo Virtual Environment Activation (POSIX)
_CDO_VENV_OLD_PATH="$PATH"
_CDO_VENV_OLD_PS1="${PS1:-}"

CDO_DIR="$(cd "$(dirname "$0")" && pwd)"
export CDO_HOME="$CDO_DIR"
export CDO_VENV="$CDO_DIR"
export PATH="$CDO_DIR:$PATH"
PS1="(cdo) ${PS1:-}"
export PS1

deactivate() {
    export PATH="$_CDO_VENV_OLD_PATH"
    PS1="$_CDO_VENV_OLD_PS1"
    export PS1
    unset CDO_HOME
    unset CDO_VENV
    unset _CDO_VENV_OLD_PATH
    unset _CDO_VENV_OLD_PS1
    unset -f deactivate
}

echo "CDo virtual environment activated."
echo "Use 'deactivate' to restore the original environment."
```

### PAL Extensions for Venv

Two new PAL utility functions are needed:

```c
// Additions to pal.h

/// Get the absolute path of the currently running executable.
/// Returns 0 on success, PAL_ERR_IO on failure.
int pal_get_executable_path(char* buf, size_t buf_size);

/// Copy a file from src to dst. Creates dst if it doesn't exist,
/// overwrites if it does. Preserves executable permissions on Unix.
/// Returns 0 on success, non-zero on failure.
int pal_file_copy(const char* src, const char* dst);
```

Platform implementations:
- **Windows**: `GetModuleFileNameW(NULL, ...)` for executable path; `CopyFileW` for file copy.
- **Unix**: `/proc/self/exe` (Linux) or `_NSGetExecutablePath` (macOS) for executable path; `open`/`read`/`write` loop with `fchmod` for file copy.

## Feature 3: Build Locking

### Build_Lock_Manager

A new module `build_lock.c` provides the high-level locking logic used by `cmd_build` and `cmd_test`.

```c
// crates/cdo/api/commands/build_lock.h (new file)

#ifndef CDO_BUILD_LOCK_H
#define CDO_BUILD_LOCK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque handle for an acquired build lock.
typedef struct BuildLock BuildLock;

/// Acquire the build lock for the workspace rooted at `workspace_root`.
/// Creates/opens `.cdo/build.lock` and attempts exclusive lock.
/// Writes PID and timestamp into the lock file.
///
/// @param workspace_root  Path to workspace root directory
/// @param timeout_sec     Timeout in seconds (0 = fail immediately, default 30)
/// @param lock_out        On success, receives the lock handle
/// @return 0 on success, PAL_ERR_TIMEOUT on timeout, PAL_ERR_IO on error
int build_lock_acquire(const char* workspace_root, int timeout_sec, BuildLock** lock_out);

/// Release a previously acquired build lock.
/// Closes the file handle (OS releases the lock automatically).
/// The lock file is left on disk for diagnostics but the lock is released.
void build_lock_release(BuildLock* lock);

#ifdef __cplusplus
}
#endif

#endif // CDO_BUILD_LOCK_H
```

### Integration with cmd_build

The lock is acquired at the top of `cmd_build` (after workspace load, before compilation) and released at the end (regardless of success/failure).

```c
int cmd_build(const CdoOptions* opts) {
    // ... existing preamble ...

    // NEW: Acquire build lock
    int lock_timeout = opts->lock_timeout > 0 ? opts->lock_timeout : 30;
    BuildLock* lock = NULL;
    int lock_rc = build_lock_acquire(ws.root_path, lock_timeout, &lock);
    if (lock_rc != 0) {
        if (lock_rc == PAL_ERR_TIMEOUT) {
            cdo_error("could not acquire build lock within %d seconds "
                      "(another cdo process may be building)", lock_timeout);
        } else {
            cdo_error("failed to acquire build lock");
        }
        workspace_free(&ws);
        return 1;
    }

    // ... existing build logic ...

    // NEW: Release build lock (always, on all exit paths)
    build_lock_release(lock);

    workspace_free(&ws);
    return failed ? 1 : 0;
}
```

### Integration with cmd_test

Same pattern as `cmd_build`. Since `cmd_test` internally calls `cmd_build` for compilation, the lock must be acquired in `cmd_test` before it calls `cmd_build`, and `cmd_build` must detect that it's already running under a lock (re-entrant lock or skip lock when already held).

**Approach**: Use a flag or environment variable (`CDO_BUILD_LOCK_HELD=1`) to signal that a parent process already holds the lock, so nested `cmd_build` calls skip lock acquisition.

```c
int cmd_test(const CdoOptions* opts) {
    // ... existing preamble ...

    // NEW: Acquire build lock for the test session
    int lock_timeout = opts->lock_timeout > 0 ? opts->lock_timeout : 30;
    BuildLock* lock = NULL;
    int lock_rc = build_lock_acquire(ws.root_path, lock_timeout, &lock);
    if (lock_rc != 0) {
        if (lock_rc == PAL_ERR_TIMEOUT) {
            cdo_error("could not acquire build lock within %d seconds", lock_timeout);
        }
        workspace_free(&ws);
        return 1;
    }

    // Set re-entrancy flag so nested cmd_build calls skip locking
    _putenv_s("CDO_BUILD_LOCK_HELD", "1");

    // ... existing test logic (calls cmd_build internally) ...

    // Release lock
    _putenv_s("CDO_BUILD_LOCK_HELD", "");
    build_lock_release(lock);

    workspace_free(&ws);
    return result;
}
```

### Lock File Format

The lock file `.cdo/build.lock` contains diagnostic JSON when held:

```json
{"pid": 12345, "started_at": "2024-01-15T10:30:45Z", "command": "build"}
```

This allows developers to identify which process holds the lock if they encounter a timeout.

### CLI Extension: `--lock-timeout`

The `CdoOptions` struct gains a new field:

```c
// Addition to CdoOptions in cli.h
typedef struct {
    // ... existing fields ...
    int lock_timeout;   // --lock-timeout <seconds> (0 = immediate fail, -1 = unset/use default)
} CdoOptions;
```

Parsing in `cdo_cli_parse`:
- `--lock-timeout <N>` → `opts->lock_timeout = N`
- If unset, `opts->lock_timeout = -1` (sentinel; Build_Lock_Manager uses 30s default)

### PAL File Lock API

```c
// Additions to pal.h

/// Opaque file lock handle.
typedef struct PalFileLock PalFileLock;

/// Acquire an exclusive lock on the file at `path`.
/// Creates the file if it doesn't exist.
/// Blocks up to `timeout_ms` milliseconds (0 = non-blocking try).
///
/// @param path        File path to lock
/// @param timeout_ms  Maximum wait time in milliseconds (0 = try once)
/// @param lock_out    On success, receives the lock handle
/// @return PAL_OK on success, PAL_ERR_TIMEOUT on timeout, PAL_ERR_IO on error
int pal_file_lock_exclusive(const char* path, int timeout_ms, PalFileLock** lock_out);

/// Release a previously acquired file lock.
/// Closes the underlying file handle, which releases the OS-level lock.
/// After this call, the lock handle is invalid.
void pal_file_lock_release(PalFileLock* lock);
```

### PAL File Lock Implementation

#### Windows (`pal_file_lock.c` — Windows section)

```c
#ifdef _WIN32
#include <windows.h>

struct PalFileLock {
    HANDLE hFile;
};

int pal_file_lock_exclusive(const char* path, int timeout_ms, PalFileLock** lock_out) {
    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath) return PAL_ERR_IO;

    // Open or create the lock file
    HANDLE hFile = CreateFileW(wpath, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (hFile == INVALID_HANDLE_VALUE) return PAL_ERR_IO;

    // Attempt exclusive lock with retry loop
    OVERLAPPED ovl = {0};
    DWORD flags = LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY;
    uint64_t start = pal_time_ms();

    while (1) {
        if (LockFileEx(hFile, flags, 0, MAXDWORD, MAXDWORD, &ovl)) {
            // Lock acquired
            PalFileLock* lock = (PalFileLock*)malloc(sizeof(PalFileLock));
            lock->hFile = hFile;
            *lock_out = lock;
            return PAL_OK;
        }

        // Check timeout
        uint64_t elapsed = pal_time_ms() - start;
        if (timeout_ms == 0 || (int)elapsed >= timeout_ms) {
            CloseHandle(hFile);
            return PAL_ERR_TIMEOUT;
        }

        // Sleep briefly before retry
        Sleep(50);
    }
}

void pal_file_lock_release(PalFileLock* lock) {
    if (!lock) return;
    // Closing the handle releases the lock
    CloseHandle(lock->hFile);
    free(lock);
}
#endif
```

#### Unix (`pal_file_lock.c` — POSIX section)

```c
#ifndef _WIN32
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

struct PalFileLock {
    int fd;
};

int pal_file_lock_exclusive(const char* path, int timeout_ms, PalFileLock** lock_out) {
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return PAL_ERR_IO;

    uint64_t start = pal_time_ms();

    while (1) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            // Lock acquired
            PalFileLock* lock = (PalFileLock*)malloc(sizeof(PalFileLock));
            lock->fd = fd;
            *lock_out = lock;
            return PAL_OK;
        }

        // Check timeout
        uint64_t elapsed = pal_time_ms() - start;
        if (timeout_ms == 0 || (int)elapsed >= timeout_ms) {
            close(fd);
            return PAL_ERR_TIMEOUT;
        }

        // Sleep 50ms before retry
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 50000000 };
        nanosleep(&ts, NULL);
    }
}

void pal_file_lock_release(PalFileLock* lock) {
    if (!lock) return;
    // Closing the fd releases the flock
    close(lock->fd);
    free(lock);
}
#endif
```

### Crash Safety

Both Windows and POSIX guarantee that file locks are released when the process terminates (even abnormally):
- **Windows**: `LockFileEx` locks are tied to the file handle; handle is closed on process exit.
- **Unix**: `flock()` advisory locks are released when the file descriptor is closed (process exit closes all fds).

No additional cleanup mechanism is required. The lock file itself (`.cdo/build.lock`) persists on disk for diagnostic purposes, but the OS-level lock is always released.

## Data Models

### BuildLock (internal struct)

```c
typedef struct BuildLock {
    PalFileLock* pal_lock;   // Underlying PAL lock handle
    char lock_path[260];     // Path to the lock file
} BuildLock;
```

### Lock File Content

```c
typedef struct {
    int pid;                 // Process ID of lock holder
    uint64_t started_at;     // Timestamp (Unix epoch ms) when lock was acquired
    const char* command;     // "build" or "test"
} LockFileInfo;
```

## Error Handling

| Scenario | Behavior |
|----------|----------|
| `--venv` with no write permissions | Error message + non-zero exit |
| `--venv` self-copy fails | Error message, skip scripts, non-zero exit |
| Lock timeout reached | `cdo_error` with diagnostic info from lock file, exit 1 |
| Lock file unreadable | Treat as stale, attempt acquisition |
| Lock file I/O error | `cdo_error`, exit 1 |
| `pal_get_executable_path` fails | Error message, suggest manual copy |

## File Changes Summary

| Action | Path |
|--------|------|
| New | `crates/cdo/lib/pal/pal_file_lock.c` |
| New | `crates/cdo/lib/commands/build_lock.c` |
| New | `crates/cdo/api/commands/build_lock.h` |
| Modify | `crates/cdo/api/pal/pal.h` (add lock + copy + exe_path APIs) |
| Modify | `crates/cdo/api/core/cli.h` (add `lock_timeout` field) |
| Modify | `crates/cdo/lib/commands/cmd_new.c` (add `venv_init` + script generators) |
| Modify | `crates/cdo/lib/commands/cmd_build.c` (add lock acquire/release) |
| Modify | `crates/cdo/lib/commands/cmd_test.c` (add lock acquire/release) |
| Modify | `README.md` (full rewrite with documentation) |

## Testing Strategy

**Unit Tests (example-based):**
- README content contains all required sections and command names (Req 1.x, 2.x)
- Default lock timeout is 30 seconds (Req 7.4, 10.3)
- PAL lock/unlock API compiles and links (Req 9.1, 9.2)

**Property Tests (property-based, 100+ iterations):**
- Venv structure creation, binary copy fidelity, content preservation
- Activation/deactivation round-trip
- Script syntax correctness per platform
- Lock timeout behavior with varying durations
- Lock release guarantee on all exit paths
- Lock file diagnostic metadata validity

**Integration Tests:**
- End-to-end `cdo init --venv` → source activate → verify environment → deactivate
- Concurrent `cdo build` processes with lock contention
- `cdo test` calling `cmd_build` internally without deadlock (re-entrancy)

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system — essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Venv initialization creates required structure

*For any* valid workspace root path, invoking `venv_init` SHALL result in the `.cdo` directory existing and containing at minimum: the CDo binary, `activate.bat`, `activate.ps1`, and `activate.sh`.

**Validates: Requirements 3.1, 3.3, 3.4**

### Property 2: Venv binary copy is faithful

*For any* CDo executable (regardless of file size), after `venv_init` copies it into `.cdo/`, the copied file SHALL be byte-for-byte identical to the source executable.

**Validates: Requirements 3.2**

### Property 3: Venv preserves existing content

*For any* pre-existing `.cdo` directory containing arbitrary files in `tools/` and `cache/` subdirectories, running `venv_init` SHALL not delete or modify those pre-existing files while still regenerating the activation scripts and binary.

**Validates: Requirements 3.5**

### Property 4: Activation/deactivation round-trip restores environment

*For any* initial `PATH` value and shell prompt string, sourcing the activation script followed by invoking the deactivation function SHALL restore both `PATH` and the prompt to their original values, and `CDO_HOME` and `CDO_VENV` SHALL be unset.

**Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5, 5.1, 5.2, 5.3, 5.4**

### Property 5: Generated scripts use platform-appropriate syntax

*For any* workspace path, the generated `activate.bat` SHALL exclusively use `set` and `%VAR%` syntax, the generated `activate.ps1` SHALL exclusively use `$env:VAR` syntax, and the generated `activate.sh` SHALL exclusively use `export` syntax for environment manipulation.

**Validates: Requirements 6.1, 6.2, 6.3, 6.4, 6.5, 6.6**

### Property 6: Build lock is acquired before compilation

*For any* `cdo build` or `cdo test` invocation, the lock file `.cdo/build.lock` SHALL be exclusively held before any compiler process is spawned.

**Validates: Requirements 7.1, 7.2**

### Property 7: Lock timeout causes deterministic failure

*For any* lock that is already held by another process and *for any* timeout value T, if the lock is not released within T seconds, then `build_lock_acquire` SHALL return `PAL_ERR_TIMEOUT` and the command SHALL exit with a non-zero code. When T is 0, the function SHALL fail immediately without waiting.

**Validates: Requirements 7.3, 7.5, 9.5, 10.1, 10.2**

### Property 8: Lock is always released after command completion

*For any* `cdo build` or `cdo test` invocation, regardless of whether it succeeds or fails, the build lock SHALL be released (file handle closed) after the command function returns.

**Validates: Requirements 8.1, 8.2**

### Property 9: Lock file contains valid diagnostic metadata

*For any* acquired build lock, the lock file content SHALL contain the current process ID (matching `getpid()`) and a timestamp that is within a reasonable range of the current system time.

**Validates: Requirements 8.3**
