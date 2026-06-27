# CDo

A portable C/C++ build system and project manager, written in C. CDo uses a workspace model with automatic compiler detection, dependency management between crates, and cross-platform support for Windows, Linux, and macOS.

## Installation & Prerequisites

### Prerequisites

- A C/C++ compiler: GCC, Clang, or MSVC
- A POSIX-like shell (Linux/macOS) or Windows CMD/PowerShell

CDo auto-detects available compilers on your system. No manual toolchain configuration is required.

### Installation

1. Download the latest `cdo` binary for your platform from the releases page.
2. Place it somewhere on your `PATH`.
3. Verify the installation:

```sh
cdo doctor
```

This checks that CDo can locate a working compiler and reports system information.

## Quick Start

Get from zero to a running project in three commands:

```sh
# 1. Create a new project from a template
cdo init my-app --template exe

# 2. Build the project
cd my-app
cdo build

# 3. Run the executable
cdo run
```

That's it. CDo creates the workspace structure, detects your compiler, compiles all source files, resolves dependencies, and links the final binary.

### With a Virtual Environment

To create a project with a local, isolated CDo installation:

```sh
cdo init my-app --template exe --venv
cd my-app

# Activate the virtual environment
# Windows CMD:
.cdo\activate.bat
# Windows PowerShell:
. .cdo\activate.ps1
# Unix:
source .cdo/activate.sh

# Now 'cdo' resolves to the local copy in .cdo/
cdo build
cdo run
```

## Commands Reference

### build

Compile the workspace or a specific crate.

```sh
cdo build              # Build all crates (default: debug profile)
cdo build my-crate     # Build a specific crate
cdo build --release    # Build with the release profile
cdo build -r           # Short form for --release
cdo build --verbose    # Show detailed compiler invocations
```

### run

Build and execute the default executable crate.

```sh
cdo run                # Build (if needed) and run
cdo run -- --flag      # Pass arguments to the executable
```

### test

Build and run tests for a crate.

```sh
cdo test               # Build and run all tests
cdo test my-crate      # Test a specific crate
```

### clean

Remove build artifacts.

```sh
cdo clean              # Remove all build output
```

### new

Create a new crate within the current workspace.

```sh
cdo new my-lib         # Create a new library crate
cdo new my-exe --template exe   # Create an executable crate from a template
```

### init

Initialize a new workspace in the current directory or a named directory.

```sh
cdo init               # Initialize in the current directory
cdo init my-project    # Create and initialize a new directory
cdo init my-project --template exe   # Use a template
cdo init my-project --venv           # Create with a virtual environment
```

The `--venv` flag creates a `.cdo` directory with a local CDo binary and shell activation scripts. See the [Virtual Environment](#virtual-environment---venv) section for details.

### deps

Manage crate dependencies.

```sh
cdo deps               # List dependencies for the workspace
```

### catalog

Manage the CDo package catalog.

```sh
cdo catalog            # List available packages
```

### tool

Manage tools installed in the workspace.

```sh
cdo tool               # List installed tools
```

### doctor

Run diagnostics to verify your environment is set up correctly.

```sh
cdo doctor             # Check compiler, paths, and workspace health
```

### shader

Compile shader files (GLSL/HLSL) as part of the build.

```sh
cdo shader             # Process shader files in the workspace
```

## Configuration

### Workspace Configuration: `cdo.toml`

The `cdo.toml` file at the workspace root defines the workspace members and shared settings.

```toml
[workspace]
members = ["crates/cdo", "crates/my-lib"]

[workspace.settings]
c-standard = 17
cpp-standard = 20

[workspace.profiles.debug]
optimize = false
debug = true
defines = ["DEBUG"]

[workspace.profiles.release]
optimize = true
debug = false
defines = ["NDEBUG"]

[workspace.profiles.relwithdebinfo]
optimize = true
debug = true
defines = ["NDEBUG"]
```

### Per-Crate Configuration: `crate.toml`

Each crate has its own `crate.toml` specifying crate-specific settings.

```toml
[crate]
name = "my-crate"
c-standard = 17
cpp-standard = 20

[build]
link-libs = ["pthread"]

[dependencies]
other-crate = {}
```

Key fields:

| Section | Field | Description |
|---------|-------|-------------|
| `[crate]` | `name` | The crate name (must match directory name) |
| `[crate]` | `c-standard` | C language standard (11, 17, etc.) |
| `[crate]` | `cpp-standard` | C++ language standard (17, 20, etc.) |
| `[build]` | `link-libs` | System libraries to link against |
| `[dependencies]` | `<name> = {}` | Other workspace crates this crate depends on |

### Crate Directory Structure

Each crate can contain the following module directories:

```
crates/my-crate/
├── api/           # Public headers (exposed to dependents)
├── lib/           # Library source files
├── exe/           # Executable entry point (main)
├── dyn/           # Dynamic/shared library source
├── tst/           # Test source files
└── crate.toml     # Crate configuration
```

### Build Profiles

CDo supports three build profiles that control optimization and debug settings:

| Profile | Optimization | Debug Symbols | Typical Use |
|---------|:---:|:---:|---|
| `debug` | Off | Yes | Development and debugging |
| `release` | On | No | Production builds |
| `relwithdebinfo` | On | Yes | Profiling with optimizations |

The default profile is `debug`. Use `--release` or `-r` to select the release profile:

```sh
cdo build --release
```

Profiles are configured in `cdo.toml` under `[workspace.profiles.<name>]`. The `defines` array lets you set preprocessor macros per profile.

## Virtual Environment (`--venv`)

The `--venv` flag for `cdo init` creates a self-contained `.cdo` directory with:

- A local copy of the `cdo` binary
- Activation scripts for CMD, PowerShell, and POSIX shells
- A `tools/` directory for workspace-local tool installations
- A `cache/` directory for build cache

### Directory Structure

```
my-project/
└── .cdo/
    ├── cdo.exe          # (or 'cdo' on Unix) local binary
    ├── activate.bat     # Windows CMD activation
    ├── activate.ps1     # Windows PowerShell activation
    ├── activate.sh      # POSIX shell activation (bash, zsh, sh)
    ├── tools/           # Local tool installations
    └── cache/           # Build cache
```

### Activation

Source the appropriate script for your shell:

```sh
# Windows CMD
.cdo\activate.bat

# Windows PowerShell
. .cdo\activate.ps1

# Unix (bash, zsh, sh)
source .cdo/activate.sh
```

When activated:

- The `.cdo` directory is prepended to `PATH`
- `CDO_HOME` is set to the `.cdo` path
- `CDO_VENV` is set to indicate the active environment
- Your shell prompt shows a `(cdo)` prefix

### Deactivation

Run `deactivate` in your shell to restore the original environment:

```sh
deactivate
```

This removes the `(cdo)` prefix, restores `PATH`, and unsets `CDO_HOME` and `CDO_VENV`.

### Re-initialization

Running `cdo init --venv` on an existing workspace preserves the contents of `tools/` and `cache/` while regenerating the activation scripts and updating the local binary.

## Build Locking

CDo uses file-based locking to prevent concurrent builds from corrupting artifacts. When `cdo build` or `cdo test` starts, it acquires an exclusive lock on `.cdo/build.lock` before any compilation begins.

### Behavior

- If the lock is available, CDo acquires it and proceeds with the build.
- If another CDo process holds the lock, CDo waits up to the timeout (default: 30 seconds).
- If the timeout expires, CDo exits with an error indicating another build is in progress.
- The lock is always released when the command finishes, whether it succeeds or fails.
- If CDo crashes, the operating system automatically releases the lock.

### Configuring the Timeout

Use `--lock-timeout` to control how long CDo waits for the lock:

```sh
cdo build --lock-timeout 60    # Wait up to 60 seconds
cdo build --lock-timeout 0     # Fail immediately if locked
cdo build                      # Default: 30 seconds
```

### Diagnostics

The lock file (`.cdo/build.lock`) contains the PID and timestamp of the process holding the lock. If a build times out, CDo reports which process holds the lock to help you diagnose the issue.

## Contributing

Contributions are welcome. To build CDo from source:

```sh
git clone <repository-url>
cd cdo
cdo build --release
```

If building for the first time without an existing `cdo` binary, use the bootstrap script:

```powershell
# Windows (PowerShell)
powershell -File build.ps1
```

### Running Tests

```sh
cdo test cdo_ut
```

## License

See the LICENSE file for details.
