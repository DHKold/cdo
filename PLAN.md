# CDo Feature Roadmap

## Overview

This document details 12 proposed features for CDo, analyzed against the current project state
and comparable tools (Cargo, CMake, Meson, xmake, Bazel). Each feature includes scope,
difficulty, estimated effort, dependencies on existing infrastructure, and priority ranking.

### Effort Scale

| Label | Meaning |
|-------|---------|
| XS | < 1 day |
| S | 1–3 days |
| M | 3–7 days |
| L | 1–3 weeks |
| XL | 3–6 weeks |

### Difficulty Scale

| Label | Meaning |
|-------|---------|
| Low | Straightforward implementation, well-understood patterns |
| Medium | Requires design decisions, some platform-specific work |
| High | Significant architecture, cross-cutting concerns, or protocol design |

---

## Feature 1: Parallel Compilation with Dependency Graph Awareness

### Summary

Allow files from downstream crates to begin compiling as soon as the upstream crate's
`api/` headers are available — before the upstream crate finishes linking. Only the
link step of a downstream crate truly depends on the upstream `.a`/`.dll` being ready.

### Motivation

Currently `build_crate_modules` processes crates sequentially in topological order.
On a workspace with 5+ crates and 8+ CPU cores, the CPU is idle during link phases.
Ninja and Bazel both exploit this fine-grained parallelism.

### Scope

- Refactor `cmd_build.c` to emit a DAG of compile + link tasks rather than per-crate batches
- Each compile job depends only on api/ headers of its upstream crates (already available pre-link)
- Each link job depends on all its own `.o` files + upstream `.a`/`.dll` artifacts
- Feed the DAG into the existing threadpool with dependency tracking

### Existing Infrastructure

- `threadpool.c` — already exists, handles parallel job dispatch
- `compiler_compile_batch` — compiles jobs in parallel within a batch
- `workspace_resolve` — produces topological build order
- `compiler_compute_dirty_set` — identifies which files need rebuilding

### Risks & Complexity

- Requires a task-graph scheduler (new component, ~300–500 lines)
- Error reporting becomes trickier (which crate failed?)
- Progress bar must handle interleaved crate progress
- Must preserve correctness: a downstream compile that `#include`s a dep's api/ header
  that itself depends on generated content (from gen/ module, future) could race

### Effort: L | Difficulty: High | Priority: Low

### Impact

20–40% build time reduction on multi-crate workspaces with available cores.
Minimal benefit for single-crate projects or fully-dirty builds on 2-core machines.

---

## Feature 2: Remote/Third-Party Package Fetching & Build Integration

### Summary

Complete the external dependency lifecycle: fetch from registry/git/local, build from
source if needed, produce include/lib paths, and lock versions for reproducibility.

### Motivation

This is the single biggest gap compared to Cargo, Conan, vcpkg, and xmake. Without it,
CDo can only build code that lives within the workspace. Real-world projects need zlib,
SDL2, OpenSSL, etc.

### Scope

**Phase A — Lock file & resolution (M)**
- Implement `dep_lock_write` and `dep_lock_read` (currently stubs)
- Generate `cdo.lock` on first resolve, re-use on subsequent builds
- Semver constraint matching (already have `semver.c`)
- `cdo deps update` to refresh within constraints

**Phase B — Registry fetching (M)**
- Download archive from catalog URL (have `http.c` + `archive.c`)
- Verify checksum (have `checksum.c`)
- Extract to `.cdo/cache/deps/<name>-<version>/`
- Detect metadata kind (have `dep_detect_metadata`)

**Phase C — Git fetching (M)**
- Clone/fetch to `.cdo/cache/git/<name>-<hash>/`
- Checkout specified tag/branch/commit
- Shallow clone for tags, full clone for branches

**Phase D — Build-from-source (L)**
- If dep has `cdo.toml` → invoke `cdo build --release` recursively
- If dep has `CMakeLists.txt` → invoke cmake + build
- If dep has `Makefile` → invoke make
- Fallback: use directory layout convention (include/ + lib/)

**Phase E — Integration into build pipeline (M)**
- Add resolved include paths to compile jobs
- Add resolved lib paths + link libs to link jobs
- Copy runtime DLLs into build output (similar to existing dyn propagation)

### Existing Infrastructure

- `deps.h` — DepSpec, ResolvedDep, dep_resolve already defined
- `catalog.h` — CatalogResolveResult with include_dirs, link_libs, defines
- `http.c` — HTTP GET with WinHTTP
- `archive.c` — tar/zip extraction
- `checksum.c` — SHA256 verification
- `semver.c` — version parsing and comparison

### Risks & Complexity

- Git integration requires either shelling out to `git` or bundling libgit2
- CMake build integration is fragile (different projects have different patterns)
- Diamond dependency problem: A depends on B and C, both depend on D at different versions
- Network failures, partial downloads, cache corruption

### Effort: XL | Difficulty: High | Priority: High

### Impact

Transforms CDo from a workspace-only tool to a full package manager.
Without this, adoption beyond hobby/single-repo projects is unlikely.

---

## Feature 3: Build Caching (Content-Addressed Object Cache)

### Summary

Cache compiled object files by content hash (source + flags + deps) so that switching
branches, running `cdo clean`, or re-building in CI avoids redundant recompilation.

### Motivation

mtime-based dirty checking only helps within a single linear workflow. Branch switches,
clean builds, and CI runs always start from scratch. Tools like ccache and sccache solve
this for C/C++ but require separate installation and configuration. A built-in cache
makes this zero-config.

### Scope

**Phase A — Cache key computation (S)**
- For each compile job, compute a hash of:
  - Source file content (SHA256)
  - Resolved include file contents (transitive, from dep file if available)
  - Compiler path + version
  - All flags (defines, standards, optimization level)
- Store as `<hash>.o` in `.cdo/cache/objects/`

**Phase B — Cache lookup & population (M)**
- Before compiling: check if `<hash>.o` exists in cache → copy to build dir
- After compiling: store the new `.o` in cache
- Track cache size, implement LRU eviction at a configurable max (default 2GB)

**Phase C — Shared/remote cache (optional, L)**
- Support `[workspace.settings.cache]` config pointing to a network path or S3 bucket
- Pull from remote on miss, push on local compile
- Useful for CI and team sharing

**Phase D — Integration with ccache/sccache (S)**
- If user has ccache/sccache on PATH, optionally prefix compile commands with it
- Config: `[workspace.settings.cache] backend = "ccache"` or `"builtin"`

### Existing Infrastructure

- `checksum.c` — SHA256 computation
- `pal_file_copy` — copying cached objects
- `pal_file_read` — reading source content for hashing
- `compiler_compile_batch` — insertion point for cache check
- `.cdo/cache/` directory already exists in venv layout

### Risks & Complexity

- Include-dependency tracking for cache key: must parse `.d` dep files or scan includes
- Cache invalidation correctness (if system headers change, compiler updates, etc.)
- Disk usage management (LRU eviction logic)
- Thread safety: multiple parallel compile jobs writing to cache simultaneously

### Effort: L | Difficulty: Medium | Priority: High

### Impact

Dramatic improvement for branch-switching workflows and CI builds.
Typical cache hit rates of 60–90% on iterative development.

---

## Feature 4: `cdo fmt` — Code Formatting Command

### Summary

Integrate source code formatting into the CDo workflow, providing a single command to
format all workspace sources using clang-format (or a configurable alternative).

### Motivation

Every serious C/C++ team uses a formatter. Currently developers must configure and
invoke it manually. An integrated command reduces friction and enables CI enforcement
(`cdo fmt --check` in pipelines).

### Scope

- New command: `cdo fmt [<crate>] [--check] [--verbose]`
- Discovers all `.c`, `.cpp`, `.h`, `.hpp` files in target crates
- Invokes `clang-format -i` (or `--dry-run --Werror` for `--check`)
- Locates clang-format: `.cdo/tools/clang-format/`, then PATH
- If not found: suggests `cdo tool install clang-format`
- Configuration in `cdo.toml`:
  ```toml
  [workspace.settings.format]
  tool = "clang-format"           # default
  style-file = ".clang-format"    # default (looks at workspace root)
  exclude = ["crates/vendor/**"]  # glob patterns to skip
  ```
- If no `.clang-format` exists, generates a sensible default on first run

### Existing Infrastructure

- `pal_spawn` — can invoke external tools
- `pal_dir_walk` — recursive file discovery
- `cdo tool install` — can auto-install clang-format from catalog
- CLI parsing infrastructure — adding a new command is well-understood

### Risks & Complexity

- Low risk. clang-format is a mature, stable tool
- Minor complexity: discovering which files to format (respect .gitignore? exclude patterns?)
- Edge case: formatting files that are being compiled (shouldn't conflict with build lock)

### Effort: S | Difficulty: Low | Priority: Medium

### Impact

Table-stakes for team adoption. Quick win that demonstrates polish.
Enables CI enforcement of consistent style.

---

## Feature 5: `cdo lint` — Static Analysis Command

### Summary

Provide an integrated static analysis command using cppcheck (bundled in w64devkit)
or user-configured analyzers, with structured output and optional auto-fix.

### Motivation

Static analysis catches bugs that the compiler misses (null derefs, buffer overflows,
resource leaks, UB). w64devkit already ships cppcheck configs — CDo should make them
trivially accessible rather than requiring manual invocation.

### Scope

- New command: `cdo lint [<crate>] [--fix] [--severity <error|warning|style>] [--json]`
- Default tool: cppcheck (shipped with w64devkit, or from PATH)
- Passes correct include paths (same as compile jobs) + defines to the analyzer
- Filters results to workspace sources only (like coverage filtering)
- `--json` outputs structured results for IDE/CI integration
- `--fix` applies auto-corrections where cppcheck supports them
- Configuration in `cdo.toml`:
  ```toml
  [workspace.settings.lint]
  tool = "cppcheck"
  severity = "warning"        # minimum severity to report
  suppressions = ["unusedFunction", "missingInclude"]
  extra-args = ["--std=c17"]
  ```
- Compiler-warning lint mode: `cdo lint --compiler` rebuilds with `-Wall -Wextra -Wpedantic`
  and reports warnings as lint results

### Existing Infrastructure

- cppcheck cfgs already in `.cdo/tools/w64devkit/share/cppcheck/cfg/`
- `module_include_paths` — provides correct include paths per module
- `pal_spawn` with `capture_output` — captures analyzer stdout/stderr
- `json.c` — can serialize structured results

### Risks & Complexity

- cppcheck can be slow on large codebases (mitigated by per-crate targeting)
- Different analyzers have different output formats (need a parse/normalize layer)
- `--fix` support varies by tool and may produce incorrect patches

### Effort: M | Difficulty: Medium | Priority: Medium

### Impact

Catches real bugs without extra developer effort. Particularly valuable
when combined with CI (`cdo lint` as a gate). Leverages already-bundled tooling.

---

## Feature 6: Cross-Compilation & Target Triples

### Summary

Support compiling for a target platform different from the host, using configurable
toolchain definitions and target-specific build output directories.

### Motivation

Embedded development, multi-platform game engines, and server/desktop targeting from
one machine all require cross-compilation. CMake, Meson, and xmake all support this.
CDo's single-host-only model limits it to local development scenarios.

### Scope

**Phase A — Target triple specification (S)**
- `cdo build --target <triple>` CLI option
- Triple format: `<arch>-<os>[-<env>]` (e.g., `x86_64-linux-gnu`, `arm64-windows-msvc`)
- Output directory becomes `build/<profile>-<target>/` instead of `build/<profile>/`

**Phase B — Toolchain definitions (M)**
- Configuration in `cdo.toml`:
  ```toml
  [workspace.targets.arm64-linux]
  cc = "aarch64-linux-gnu-gcc"
  cxx = "aarch64-linux-gnu-g++"
  ar = "aarch64-linux-gnu-ar"
  linker = "aarch64-linux-gnu-ld"
  sysroot = "/opt/sysroots/arm64"
  defines = ["__ARM_NEON"]
  ```
- Override `CompilerInfo` with target toolchain paths during build
- Inject `--sysroot` and target-specific flags

**Phase C — Platform-conditional dependencies (M)**
- Per-platform link libs in `crate.toml`:
  ```toml
  [build.windows]
  link-libs = ["ws2_32", "winhttp"]
  
  [build.linux]
  link-libs = ["pthread", "dl"]
  ```
- Conditional source inclusion: files named `*_win.c` / `*_linux.c` only compiled for matching target

**Phase D — Tool catalog per-target (S)**
- Extend `cdo tool install` to fetch cross-toolchains from catalog
- `cdo tool install arm-gcc --target arm64-linux`

### Existing Infrastructure

- `compiler_detect` — can be extended to load target-specific compiler info
- `CompilerInfo` struct — already has path/family/version fields
- `CompileJob`/`LinkJob` — already parameterized (flags, paths)
- `catalog.h` — has `CatalogPlatform` with os/arch/triple

### Risks & Complexity

- Sysroot management is complex (finding the right headers/libs for the target)
- Platform-conditional code adds complexity to the scanner and build pipeline
- Testing cross-compilation in CI is hard without actual cross-toolchains
- MSVC cross-compilation has different semantics than GCC/Clang

### Effort: XL | Difficulty: High | Priority: High

### Impact

Opens CDo to embedded, mobile, and multi-platform development.
Critical for game development (targeting consoles) and IoT.

---

## Feature 7: `cdo bench` — Benchmarking Support

### Summary

Add a benchmark module kind (`ben/`) and a `cdo bench` command that builds, runs,
and reports performance measurements with optional baseline comparison.

### Motivation

Performance-sensitive C/C++ code (game engines, parsers, data structures) needs
repeatable benchmarks. Currently developers write ad-hoc timing code. A built-in
framework standardizes measurement and enables regression detection in CI.

### Scope

**Phase A — ben/ module and command (M)**
- New module kind: `MODULE_BEN` in the enum
- Scanner detects `ben/` directory, collects `.c`/`.cpp` files
- `cdo bench [<crate>] [--filter <pattern>] [--json] [--iterations <n>]`
- Builds ben/ module as an executable (like tst/)
- Links against crate's lib/ module

**Phase B — Minimal benchmark harness (M)**
- Ship a header-only `cdo_bench.h` in a built-in template/include:
  ```c
  CDO_BENCH(name) { /* benchmark body */ }
  CDO_BENCH_MAIN()  // discovers and runs all CDO_BENCH functions
  ```
- Automatic iteration counting, warm-up, and statistical measurement
- Reports: min, max, median, mean, stddev per benchmark
- Output formats: pretty-printed table (default), JSON (`--json`)

**Phase C — Baseline comparison (S)**
- `cdo bench --save baseline.json` saves results
- `cdo bench --compare baseline.json` reports deltas and flags regressions (>5% slower)
- Exit non-zero if regression detected (CI-friendly)

### Existing Infrastructure

- `pal_time_ms` — monotonic timing
- Module system — adding MODULE_BEN follows the same pattern as MODULE_TST
- `test_protocol.c` / `test_renderer.c` — similar pattern for test discovery/reporting
- `json.c` — serialization for results

### Risks & Complexity

- Accurate benchmarking is hard (warm-up, outliers, OS scheduling noise)
- Need to disable optimizations that remove benchmark code (compiler barriers)
- Iteration count auto-tuning adds complexity
- Cross-platform timing accuracy varies

### Effort: L | Difficulty: Medium | Priority: Low

### Impact

Valuable for performance-sensitive projects. Enables regression detection in CI.
Nice differentiator vs. CMake/Meson which have no built-in bench support.

---

## Feature 8: `cdo watch` — File Watcher with Auto-Rebuild

### Summary

Monitor source files for changes and automatically trigger incremental rebuild,
test re-run, or executable restart.

### Motivation

The edit-compile-test loop is the inner loop of C development. Shaving seconds off
each iteration compounds into hours saved per week. Rust's `cargo watch`, JavaScript's
webpack/vite, and Go's `air` all provide this. CDo should too.

### Scope

- New command: `cdo watch [build|run|test] [<crate>] [--debounce <ms>]`
- Default mode: `cdo watch build` (rebuild on change)
- `cdo watch run` — rebuild + re-launch executable (kill previous if still running)
- `cdo watch test` — rebuild + re-run tests
- Watches all files in `crates/` (source, headers, toml configs)
- Debounce: waits 150ms (configurable) after last change before triggering
- On rebuild, shows only *new* errors (diff from previous build output)
- `Ctrl+C` exits cleanly

### Platform Implementation

- **Windows**: `ReadDirectoryChangesW` with `FILE_NOTIFY_CHANGE_LAST_WRITE`
- **Linux**: `inotify_init` + `inotify_add_watch` on all source directories
- **macOS**: `kqueue` + `EVFILT_VNODE`

### Existing Infrastructure

- `pal_spawn` / `pal_spawn_async` — can launch builds and managed processes
- `pal_dir_walk` — discover directories to watch
- Incremental build already works — watch just triggers it repeatedly

### Risks & Complexity

- Platform-specific file watching is the main complexity (new PAL functions)
- Handling rapid file saves without triggering multiple builds
- Killing a running process cleanly (for `watch run` mode)
- Large directory trees on Linux may hit inotify watch limits

### Effort: M | Difficulty: Medium | Priority: Medium

### Impact

Significant ergonomics improvement for daily development.
Low risk, well-understood problem domain.

---

## Feature 9: `cdo install` — System-Wide Binary Installation

### Summary

Build a release binary and install it to a well-known location on the user's PATH,
enabling CDo-built tools to be used system-wide.

### Motivation

Cargo's `cargo install` is one of its most-used features — it turns any Rust crate
into a system-available tool with one command. CDo should offer the same for C/C++
projects.

### Scope

- New command: `cdo install [<crate>] [--global] [--path <dir>]`
- Default install location: `~/.cdo/bin/` (added to PATH by venv activation)
- `--global` installs to system path (`/usr/local/bin` or `%LOCALAPPDATA%\Programs\cdo\bin`)
- Builds in release mode automatically
- Strips debug symbols on install (unless `--debug`)
- `cdo uninstall <name>` removes the binary
- `cdo install --list` shows all installed binaries with version/path
- `cdo install --git <url>` clones and installs from a remote repo (depends on Feature 2)

### Existing Infrastructure

- `cmd_build` — already handles release builds
- `pal_file_copy` — copying binary to destination
- `pal_get_home_dir` — locating user directory
- Venv activation scripts — already modify PATH

### Risks & Complexity

- Low complexity for local install
- `--git` variant depends on Feature 2 (package fetching)
- Platform differences in "system bin" location
- Potential permission issues installing to system directories

### Effort: S | Difficulty: Low | Priority: Low

### Impact

Polishes the end-to-end workflow. Enables CDo projects to produce
distributable tools without manual copy steps.

---

## Feature 10: Build Lifecycle Hooks (Scripts)

### Summary

Allow crates to define pre/post-build scripts that run automatically as part of
the build pipeline, enabling code generation, asset processing, and custom steps.

### Motivation

Real C/C++ projects often need pre-build steps: generating version headers from git,
running protobuf/flatbuffers compilers, processing assets, generating bindings.
Currently the only option is external scripts run manually. Lifecycle hooks make
these steps reproducible and automatic.

### Scope

- Configuration in `crate.toml`:
  ```toml
  [hooks]
  pre-build = "python scripts/gen_version.py"
  post-build = "strip build/release/my-app/my-app"
  pre-test = "scripts/setup_test_fixtures.sh"
  post-test = "scripts/cleanup_test_fixtures.sh"
  ```
- Workspace-level hooks in `cdo.toml`:
  ```toml
  [workspace.hooks]
  pre-build = "echo Building workspace..."
  post-build = "echo Done!"
  ```
- Hook execution order: workspace pre → crate pre → build → crate post → workspace post
- Environment variables injected:
  - `CDO_WS_ROOT` — workspace root path
  - `CDO_CRATE_NAME` — current crate name
  - `CDO_CRATE_PATH` — current crate directory
  - `CDO_PROFILE` — current build profile
  - `CDO_BUILD_DIR` — crate build output directory
  - `CDO_TARGET` — target triple (when cross-compiling)
- Hooks run inside the build lock (no concurrent hook execution)
- Hook failure (non-zero exit) aborts the build with error

### Existing Infrastructure

- `pal_spawn` — execute arbitrary commands with env/cwd
- Build lock — already acquired before build starts
- `crate.toml` parsing — `toml_parse.c` can be extended for `[hooks]` section

### Risks & Complexity

- Security: hooks run arbitrary commands (but so does Make, CMake, etc.)
- Platform portability: shell commands differ across OS
  - Mitigation: recommend using portable tools (python, cdo scripts)
- Incremental: when should hooks re-run? (Always? Only when inputs change?)
  - Simple approach: hooks always run (fast hooks are cheap, slow hooks should be guarded by the script itself)

### Effort: M | Difficulty: Low | Priority: Medium

### Impact

Unblocks a large class of real-world build requirements.
Flexible escape hatch that avoids over-engineering CDo's core.

---

## Feature 11: Generated Header / Build-Time Code Generation (`gen/` module)

### Summary

A new module kind (`gen/`) that contains build scripts producing generated headers
and source files, compiled/run before the main lib/ module.

### Motivation

Common C/C++ patterns that require code generation:
- Embedding file contents as byte arrays (shaders, configs, images)
- Version headers from git describe
- Protocol buffer / FlatBuffers code generation
- Generating dispatch tables, enum-to-string mappings
- FFI binding generation

Cargo's `build.rs` is the gold standard here. CDo needs an equivalent.

### Scope

**Phase A — gen/ module detection and execution (M)**
- New module kind: `MODULE_GEN`
- Scanner detects `gen/` directory
- If `gen/` contains a `.c` file: compile it, run it as a build step (output to `build/<profile>/<crate>/gen/`)
- If `gen/` contains a `.py`/`.sh` file: run it directly with interpreter
- Generated output directory added to include paths for all other modules in the crate
- Incremental: re-run generator only if gen/ sources or inputs changed

**Phase B — Built-in generation helpers (M)**
- `cdo_embed.h` — compile-time file embedding:
  ```c
  // gen/embed.c
  #include <cdo_gen.h>
  int main() {
      cdo_gen_embed_file("res/config.json", "config_json", "generated/config.h");
      cdo_gen_version_header("generated/version.h");
      return 0;
  }
  ```
- `cdo_gen_embed_file(input, symbol_name, output)` — writes `const unsigned char symbol_name[]`
- `cdo_gen_version_header(output)` — writes `#define CDO_VERSION "..."` from git

**Phase C — Input tracking for incremental (S)**
- Generator declares its inputs: `cdo_gen_depends("res/config.json")`
- CDo tracks input mtimes; only re-runs generator if inputs changed
- If no inputs declared, always re-runs (safe default)

### Existing Infrastructure

- Module system — adding MODULE_GEN follows established patterns
- `pal_spawn` — running compiled generators or scripts
- `pal_file_write` — generators produce output files
- `compiler_compile_batch` — can compile the generator binary

### Risks & Complexity

- Chicken-and-egg: gen/ module must be compiled before lib/ (build ordering)
- Cross-compilation: generator must be compiled for HOST, not TARGET
  (this is the same problem Cargo faces with `build.rs`)
- Input tracking complexity; without it, generators always re-run
- Error reporting from generators needs to be surfaced clearly

### Effort: L | Difficulty: High | Priority: Medium

### Impact

Enables a wide class of patterns that are currently impossible without external scripts.
Combined with lifecycle hooks (Feature 10), covers virtually all code generation needs.

---

## Feature 12: `cdo publish` — Package Publishing & Registry

### Summary

Enable crates to be packaged and published to a registry (private or public),
completing the package ecosystem lifecycle.

### Motivation

Without publish, sharing code between teams/projects requires git submodules,
manual copying, or monorepo structures. A publish command enables:
- Internal teams to share compiled libraries
- Open-source C/C++ libraries distributed via CDo
- A self-hosted registry for organizations

### Scope

**Phase A — Package creation (M)**
- `cdo publish --dry-run` packages the crate into `.cdo/dist/<name>-<version>.tar.gz`
- Includes: api/ headers, built lib artifacts, crate.toml, README, LICENSE
- Generates `cdo-package.toml` metadata (include dirs, link libs, defines)
- Validates: version not already published, all files present, no uncommitted changes

**Phase B — Registry protocol (L)**
- Simple HTTPS REST API:
  - `PUT /api/v1/packages/<name>/<version>` — upload package
  - `GET /api/v1/packages/<name>/<version>` — download package
  - `GET /api/v1/packages/<name>/versions` — list versions
  - `GET /api/v1/packages?search=<query>` — search
- Authentication: Bearer token from environment or credential file
- Compatible with static file hosting (S3, GitHub Releases) for read-only registries

**Phase C — Credential management (S)**
- `cdo login <registry-url>` — prompts for token, stores in `~/.cdo/credentials.toml`
- `cdo logout <registry-url>` — removes stored token
- Environment variable override: `CDO_REGISTRY_TOKEN`

**Phase D — Workspace configuration (XS)**
- In `cdo.toml`:
  ```toml
  [workspace.registry]
  url = "https://registry.example.com"
  token-env = "CDO_REGISTRY_TOKEN"
  ```
- Per-crate publish config in `crate.toml`:
  ```toml
  [publish]
  include = ["api/**", "README.md", "LICENSE"]
  exclude = ["tst/**", "ben/**"]
  ```

### Existing Infrastructure

- `http.c` — HTTP client (GET works; would need PUT/POST for publishing)
- `archive.c` — tar/zip creation (currently only extraction — needs creation)
- `catalog.h` — already has the concept of packages with metadata
- `checksum.c` — SHA256 for integrity verification
- `cdo-package.toml` concept already exists in `dep_detect_metadata`

### Risks & Complexity

- Registry server is out of CDo's scope (but protocol must be defined)
- Archive creation (tar.gz) is not yet implemented (only extraction exists)
- Versioning conflicts and yanking/deprecation semantics
- Security: package tampering, checksum verification, signature verification
- Dependency on Feature 2 being complete (consumers need to fetch published packages)

### Effort: XL | Difficulty: High | Priority: Low

### Impact

Completes the ecosystem vision. Only valuable once there are multiple
teams/projects using CDo. Depends on Feature 2 being functional first.

---

## Summary Table

| # | Feature | Effort | Difficulty | Priority | Dependencies |
|---|---------|--------|------------|----------|--------------|
| 1 | Parallel DAG compilation | L | High | Low | None |
| 2 | Remote package fetching | XL | High | **High** | None |
| 3 | Build caching | L | Medium | **High** | None |
| 4 | `cdo fmt` | S | Low | Medium | None |
| 5 | `cdo lint` | M | Medium | Medium | None |
| 6 | Cross-compilation | XL | High | **High** | None |
| 7 | `cdo bench` | L | Medium | Low | None |
| 8 | `cdo watch` | M | Medium | Medium | None |
| 9 | `cdo install` | S | Low | Low | Feature 2 (for --git) |
| 10 | Lifecycle hooks | M | Low | Medium | None |
| 11 | Code generation (`gen/`) | L | High | Medium | None |
| 12 | `cdo publish` | XL | High | Low | Feature 2 |

---

## Recommended Implementation Order

### Wave 1 — Foundation & Quick Wins (weeks 1–4)

1. **Feature 4: `cdo fmt`** (S) — Quick win, builds confidence, useful immediately
2. **Feature 10: Lifecycle hooks** (M) — Unblocks custom workflows without core changes
3. **Feature 3: Build caching** (L) — High-impact DX improvement

### Wave 2 — Core Package Management (weeks 5–12)

4. **Feature 2: Remote packages** (XL) — The critical missing piece
5. **Feature 5: `cdo lint`** (M) — Leverages bundled cppcheck, complements fmt

### Wave 3 — Platform Expansion (weeks 13–20)

6. **Feature 6: Cross-compilation** (XL) — Opens new use cases
7. **Feature 8: `cdo watch`** (M) — Ergonomics for daily development
8. **Feature 11: Code generation** (L) — Enables advanced patterns

### Wave 4 — Ecosystem & Polish (weeks 21+)

9. **Feature 9: `cdo install`** (S) — Small polish item
10. **Feature 7: `cdo bench`** (L) — Nice-to-have for perf-sensitive projects
11. **Feature 1: Parallel DAG** (L) — Optimization for large workspaces
12. **Feature 12: `cdo publish`** (XL) — Ecosystem completion

---

## Additional Small Features (not fully scoped)

These are smaller items that could be implemented opportunistically:

| Feature | Effort | Notes |
|---------|--------|-------|
| `compile_commands.json` export | XS | Essential for clangd/LSP integration |
| `cdo build --timings` | S | Per-file timing report for identifying slow compilations |
| Parallel test execution | S | Run test binaries from multiple crates concurrently |
| Sanitizer profiles | XS | `--sanitize=address\|thread\|undefined` flag on build |
| `cdo doc` (Doxygen) | M | Generate documentation from source comments |
| `.gitignore` generation | XS | Auto-generate on `cdo init` |
| `cdo update` (self-update) | S | Download latest CDo binary and replace self |
| Build notifications | XS | OS toast/sound on build complete (for watch mode) |
| Colored diagnostics pass-through | XS | Preserve compiler color output in terminal |
| Dependency graph visualization | S | `cdo deps graph` outputs DOT/Mermaid format |

---

## Relationship to Existing Specs

The completed **crate-modules** spec established:
- Module system architecture (scanner, build pipeline, inter-crate propagation)
- Run command with staging folder
- PAL conventions and error handling patterns
- Testing methodology (extensive unit tests, >90% coverage)

All 12 proposed features build on this foundation. Features 2, 7, and 11 extend
the module system. Features 4, 5, 8, and 9 add new commands following established
CLI patterns. Features 3, 6, and 10 enhance the build pipeline internals.

None of the proposals require architectural rewrites — they're all additive
extensions to the existing structure.
