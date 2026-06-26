# Requirements Document

## Introduction

CDo currently models each crate as a single artifact type (executable, static library, shared library, or test). This creates awkward coupling when test crates need to depend on the internal logic of an executable crate — forcing recompilation of all sources with test defines. The multi-module crate architecture replaces this model by allowing a single crate directory to contain multiple *modules*, each producing a distinct artifact. A crate's `lib/` module holds the core code, while thin `exe/`, `dyn/`, and `tst/` modules depend on it implicitly, enabling clean separation of concerns and proper inter-crate dependency on library code.

## Glossary

- **Workspace**: The top-level project structure defined by `cdo.toml`, containing one or more Crates.
- **Crate**: A named unit within the Workspace, identified by a directory under `crates/` and a `crate.toml` manifest.
- **Module**: A sub-unit within a Crate that produces a single build artifact. Each module corresponds to a well-known subdirectory (`lib/`, `exe/`, `dyn/`, `tst/`, `api/`).
- **Library_Module**: The `lib/` module within a Crate, producing a static library artifact (`.a` / `.lib`). Contains the core logic of the Crate.
- **Executable_Module**: The `exe/` module within a Crate, producing an executable artifact. Contains only the entry point and thin wiring code.
- **Shared_Library_Module**: The `dyn/` module within a Crate, producing a shared library artifact (`.dll` / `.so`). Acts as a dynamic wrapper around the Library_Module.
- **Test_Module**: The `tst/` module within a Crate, producing a test executable artifact. Contains unit tests exercising the Library_Module.
- **API_Module**: The `api/` directory within a Crate, holding public headers that other Crates may include. Replaces the previous `include/` convention.
- **Implicit_Dependency**: An automatic link-time and include-path dependency that a module has on its own Crate's Library_Module without requiring explicit declaration in `crate.toml`.
- **Scanner**: The CDo subsystem responsible for discovering module directories and source files within a Crate.
- **Build_Order**: The topologically-sorted sequence in which Crates and their Modules are compiled and linked.
- **CDO_TESTING**: A preprocessor define passed to the Test_Module during compilation, enabling test-specific code paths in shared headers.

## Requirements

### Requirement 1: Module Directory Discovery

**User Story:** As a CDo user, I want CDo to automatically detect which modules a crate contains based on the presence of well-known subdirectories, so that I do not need to explicitly declare each module type in the manifest.

#### Acceptance Criteria

1. WHEN a Crate directory contains a `lib/` subdirectory, THE Scanner SHALL recognize a Library_Module for that Crate, regardless of whether the subdirectory contains source files.
2. WHEN a Crate directory contains an `exe/` subdirectory, THE Scanner SHALL recognize an Executable_Module for that Crate.
3. WHEN a Crate directory contains a `dyn/` subdirectory, THE Scanner SHALL recognize a Shared_Library_Module for that Crate.
4. WHEN a Crate directory contains a `tst/` subdirectory, THE Scanner SHALL recognize a Test_Module for that Crate.
5. WHEN a Crate directory contains an `api/` subdirectory, THE Scanner SHALL recognize an API_Module for that Crate.
6. THE Scanner SHALL recognize each module subdirectory independently, allowing any combination of `lib/`, `exe/`, `dyn/`, `tst/`, and `api/` within a single Crate.
7. IF a Crate directory contains none of the well-known module subdirectories (`lib/`, `exe/`, `dyn/`, `tst/`, `api/`), THEN THE Scanner SHALL report an error indicating no compilable module layout was found for that Crate.
8. THE Scanner SHALL ignore any subdirectories that do not match the well-known module directory names (`lib/`, `exe/`, `dyn/`, `tst/`, `api/`) during module discovery, including `src/`.

### Requirement 2: Library Module Compilation

**User Story:** As a CDo user, I want the library module to be compiled into a static library artifact so that other modules and crates can link against the core code.

#### Acceptance Criteria

1. WHEN a Library_Module is present, THE Build_System SHALL compile all `.c` and `.cpp` source files found recursively in `lib/` into object files and archive them into a static library artifact.
2. THE Build_System SHALL compile the Library_Module before any other module in the same Crate.
3. THE Build_System SHALL add the `lib/` directory to the include path when compiling the Library_Module.
4. WHEN an `api/` directory is present in the same Crate, THE Build_System SHALL add the `api/` directory to the include path when compiling the Library_Module.
5. IF a Library_Module's `lib/` directory contains no compilable source files (`.c` or `.cpp`), THEN THE Build_System SHALL report an error and halt the build for that Crate.

### Requirement 3: Executable Module Compilation

**User Story:** As a CDo user, I want the executable module to link against the crate's own library module so that the entry point can access core logic without duplicating source files.

#### Acceptance Criteria

1. WHEN an Executable_Module is present and a Library_Module exists in the same Crate, THE Build_System SHALL establish an Implicit_Dependency from the Executable_Module to the Library_Module.
2. WHEN an Executable_Module has an Implicit_Dependency on a Library_Module, THE Build_System SHALL add the Library_Module's `lib/` directory, the `exe/` directory, and the Crate's `api/` directory to the include path when compiling the Executable_Module.
3. WHEN an Executable_Module has an Implicit_Dependency on a Library_Module, THE Build_System SHALL link the Executable_Module against the Library_Module's static library artifact to produce an executable.
4. IF an Executable_Module is present but no Library_Module exists in the same Crate, THEN THE Build_System SHALL compile the Executable_Module as a standalone executable using only the files in `exe/`, with the `exe/` directory as the sole include path.

### Requirement 4: Shared Library Module Compilation

**User Story:** As a CDo user, I want an optional shared library module that wraps the core library, so that I can produce dynamic linking artifacts when needed.

#### Acceptance Criteria

1. WHEN a Shared_Library_Module is present and a Library_Module exists in the same Crate, THE Build_System SHALL establish an Implicit_Dependency from the Shared_Library_Module to the Library_Module.
2. THE Build_System SHALL compile source files in `dyn/` with position-independent code flags and link them against the Library_Module's static library artifact to produce a shared library (`.dll` / `.so`).
3. THE Build_System SHALL add the Library_Module's `lib/` directory and the Crate's `api/` directory to the include path when compiling the Shared_Library_Module.
4. IF a Shared_Library_Module is present but no Library_Module exists in the same Crate, THEN THE Build_System SHALL report an error and halt the build for that Crate.

### Requirement 5: Test Module Compilation

**User Story:** As a CDo user, I want the test module to link against the library module with test defines enabled, so that tests can exercise core logic cleanly without recompiling the entire crate.

#### Acceptance Criteria

1. WHEN a Test_Module is present and a Library_Module exists in the same Crate, THE Build_System SHALL establish an Implicit_Dependency from the Test_Module to the Library_Module.
2. THE Build_System SHALL compile source files in `tst/` with the `CDO_TESTING` preprocessor define.
3. THE Build_System SHALL link the Test_Module against the Library_Module's static library artifact to produce a test executable.
4. THE Build_System SHALL add the Library_Module's `lib/` directory and the Crate's `api/` directory to the include path when compiling the Test_Module.
5. IF a Test_Module is present but no Library_Module exists in the same Crate, THEN THE Build_System SHALL report an error and halt the build for that Crate.

### Requirement 6: API Module and Public Headers

**User Story:** As a CDo user, I want a dedicated `api/` directory for public headers so that other crates have a clear contract boundary to include from.

#### Acceptance Criteria

1. WHEN another Crate declares a dependency on a Crate that has an `api/` directory, THE Build_System SHALL add the `api/` directory to the dependent Crate's include path.
2. THE Build_System SHALL use the `api/` directory as the public include path and SHALL NOT expose internal `lib/` headers to dependent Crates.
3. WHEN a Crate has no `api/` directory but has a legacy `include/` directory, THE Build_System SHALL fall back to using `include/` as the public include path for backward compatibility.

### Requirement 7: Inter-Crate Dependency Resolution

**User Story:** As a CDo user, I want inter-crate dependencies to resolve against the library module of the target crate, so that executables are never accidentally treated as linkable dependencies.

#### Acceptance Criteria

1. WHEN Crate A declares a dependency on Crate B, THE Build_System SHALL resolve the dependency against Crate B's Library_Module.
2. WHEN Crate A declares a dependency on Crate B, THE Build_System SHALL link Crate A's Library_Module, Executable_Module, Shared_Library_Module, and Test_Module against Crate B's static library artifact produced by the Library_Module.
3. IF Crate A declares a dependency on Crate B and Crate B has no Library_Module, THEN THE Build_System SHALL report an error indicating that only Crates with a Library_Module can be depended upon.
4. THE Build_System SHALL NOT allow any Crate to depend on another Crate's Executable_Module.
5. WHEN Crate A depends on Crate B and Crate B depends on Crate C, THE Build_System SHALL transitively link Crate A's modules against Crate C's static library artifact.
6. IF the dependency graph among Crates contains a cycle, THEN THE Build_System SHALL report an error identifying the Crates involved in the cycle and halt the build.

### Requirement 8: Build Order Within a Crate

**User Story:** As a CDo user, I want modules within a crate to build in the correct dependency order, so that the library artifact is available before modules that need it.

#### Acceptance Criteria

1. THE Build_System SHALL compile modules within a single Crate in the following order: Library_Module first, then Executable_Module, Shared_Library_Module, and Test_Module in any order after.
2. WHEN the Library_Module compilation fails, THE Build_System SHALL skip compilation of all dependent modules in the same Crate and report the failure.
3. THE Build_System SHALL compile independent Crates in the workspace-level topologically-sorted Build_Order, processing all modules of a Crate before moving to the next Crate.

### Requirement 9: Crate Manifest Simplification

**User Story:** As a CDo user, I want `crate.toml` to derive artifact types from the module directories present, so that I do not need to declare a `type` field.

#### Acceptance Criteria

1. THE Build_System SHALL derive artifact types from the detected module subdirectories (`lib/`, `exe/`, `dyn/`, `tst/`) and SHALL NOT require a `type` field in `crate.toml`.
2. IF a `crate.toml` contains a `type` field, THE Build_System SHALL ignore it and derive types from module directories.
3. THE Build_System SHALL read `c-standard`, `cpp-standard`, `[dependencies]`, `[build]`, and other existing manifest fields and apply them to all modules within the Crate.

### Requirement 10: Source Scanning for Module Directories

**User Story:** As a CDo user, I want the scanner to discover source files in module-specific directories, so that the new layout is properly compiled.

#### Acceptance Criteria

1. THE Scanner SHALL scan each module directory (`lib/`, `exe/`, `dyn/`, `tst/`) independently for compilable source files.
2. THE Scanner SHALL NOT scan the `api/` directory for compilable source files; it SHALL treat `api/` as containing only header files.
3. THE Scanner SHALL support recursive subdirectory scanning within each module directory.
4. THE Scanner SHALL apply exclude patterns relative to the module directory root.

### Requirement 11: Output Artifact Naming and Placement

**User Story:** As a CDo user, I want each module's artifact to be placed in a predictable location under the build directory, so that I can find and use the outputs easily.

#### Acceptance Criteria

1. THE Build_System SHALL place all object files and artifacts for a module in `build/<profile>/<crate_name>/` grouped by module type.
2. THE Build_System SHALL name the Library_Module artifact as `<crate_name>.lib` on Windows and `lib<crate_name>.a` on Unix.
3. THE Build_System SHALL name the Executable_Module artifact as `<crate_name>.exe` on Windows and `<crate_name>` on Unix.
4. THE Build_System SHALL name the Shared_Library_Module artifact as `<crate_name>.dll` on Windows and `lib<crate_name>.so` on Unix.
5. THE Build_System SHALL name the Test_Module artifact as `<crate_name>_test.exe` on Windows and `<crate_name>_test` on Unix.
