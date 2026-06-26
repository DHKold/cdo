# Requirements Document

## Introduction

CDo currently requires users to provide explicit URLs when installing tools (`cdo tool install w64devkit --url <url>`) and when adding dependencies (`cdo add` constructs a hardcoded registry URL). This catalog system introduces a TOML-based registry that allows CDo to resolve tool and package names to download URLs, platform-specific metadata, include paths, and link libraries — enabling `cdo tool install w64devkit` and `cdo add sdl3` to work without manual URL specification.

## Glossary

- **CDo**: The C17/C++20 native build tool that this catalog system extends
- **Catalog**: A TOML file containing entries that map package or tool names to download URLs, versions, and build metadata
- **Catalog_Loader**: The CDo subsystem responsible for discovering, reading, parsing, and merging catalog files
- **Catalog_Resolver**: The CDo subsystem responsible for selecting the correct catalog entry for a given name, version constraint, and platform
- **Tool_Entry**: A catalog entry describing a downloadable development tool (compiler, utility) with platform-specific archive URLs
- **Package_Entry**: A catalog entry describing a C/C++ library dependency with include paths, link libraries, and compile flags
- **Built_In_Catalog**: A catalog file shipped alongside the CDo binary in a `catalogs/` directory
- **User_Catalog**: A catalog file provided by the user, stored in the workspace `.cdo/catalogs/` directory or a user-global `~/.cdo/catalogs/` directory
- **Platform_Triple**: A string identifying the target platform in the format `os-arch` (e.g., `windows-x86_64`, `linux-x86_64`, `macos-arm64`)
- **Version_Constraint**: A string specifying acceptable version ranges using semver-compatible syntax (e.g., `>=3.0.0`, `^1.2`, `*`)
- **Dependency_Scope**: A classification of when a dependency is needed: `normal` (always linked), `dev` (test and development builds only)
- **Dev_Dependency**: A package dependency required only during testing and development, not linked into release builds

## Requirements

### Requirement 1: Catalog File Format

**User Story:** As a CDo user, I want catalogs defined in TOML files with a well-defined schema, so that I can read, author, and maintain catalog entries without specialized tools.

#### Acceptance Criteria

1. THE Catalog_Loader SHALL parse catalog files conforming to the TOML v1.0 specification
2. WHEN a catalog file contains a `[[tool]]` array-of-tables entry, THE Catalog_Loader SHALL interpret each entry as a Tool_Entry with fields: `name` (string, required, 1 to 128 characters, containing only lowercase alphanumeric characters, hyphens, and underscores), `version` (string, required, conforming to semantic versioning `major.minor.patch` format), `description` (string, optional, maximum 512 characters), and a `[tool.platforms]` sub-table
3. WHEN a catalog file contains a `[[package]]` array-of-tables entry, THE Catalog_Loader SHALL interpret each entry as a Package_Entry with fields: `name` (string, required, 1 to 128 characters, containing only lowercase alphanumeric characters, hyphens, and underscores), `version` (string, required, conforming to semantic versioning `major.minor.patch` format), `description` (string, optional, maximum 512 characters), `include_dirs` (array of strings, optional, maximum 64 entries), `link_libs` (array of strings, optional, maximum 64 entries), `defines` (array of strings, optional, maximum 64 entries), and a `[package.platforms]` sub-table
4. WHEN a `[tool.platforms]` or `[package.platforms]` sub-table contains a key matching the current Platform_Triple, THE Catalog_Resolver SHALL use the `url` (string, required) and `checksum` (string, optional, in `algorithm:hex_digest` format) values from that platform entry
5. IF a catalog file fails TOML parsing, THEN THE Catalog_Loader SHALL report the file path, line number, and error description, and skip that file without aborting the entire catalog load
6. IF a `[[tool]]` or `[[package]]` entry is missing any required field, THEN THE Catalog_Loader SHALL report a warning identifying the catalog file path and entry index, skip that entry, and continue loading remaining entries in the file

### Requirement 2: Catalog Discovery and Loading

**User Story:** As a CDo user, I want CDo to automatically discover catalogs from multiple locations with a clear precedence order, so that I get sensible defaults while retaining the ability to override entries.

#### Acceptance Criteria

1. THE Catalog_Loader SHALL search for catalog files in the following locations in precedence order (highest first): workspace `.cdo/catalogs/` directory, user-global `~/.cdo/catalogs/` directory, Built_In_Catalog directory alongside the CDo binary, silently skipping any location whose directory does not exist
2. WHEN multiple catalog files exist in a single directory, THE Catalog_Loader SHALL load all files with the `.toml` extension from that directory in lexicographic filename order
3. WHEN the same package or tool name and version appear in catalogs from different precedence levels, THE Catalog_Resolver SHALL use the entry from the highest-precedence catalog; WHEN the same name and version appear in multiple files within the same precedence level, THE Catalog_Resolver SHALL use the entry from the file that is lexicographically last by filename
4. WHEN no catalog files are found in any search location, THE Catalog_Loader SHALL print a warning message to the console indicating no catalogs were found and fall back to requiring explicit URLs for tool installation and package addition
5. IF a catalog file cannot be read due to filesystem errors, THEN THE Catalog_Loader SHALL report the file path and error description to the console and skip that file without aborting the catalog load

### Requirement 3: Tool Installation by Name

**User Story:** As a CDo user, I want to install tools by name alone (e.g., `cdo tool install w64devkit`), so that I do not need to find and specify download URLs manually.

#### Acceptance Criteria

1. WHEN `cdo tool install <name>` is invoked without a `--url` argument, THE Catalog_Resolver SHALL search loaded catalogs for a Tool_Entry matching the specified name using case-insensitive comparison
2. WHEN a matching Tool_Entry is found, THE Catalog_Resolver SHALL select the platform-specific URL for the current Platform_Triple
3. WHEN a matching Tool_Entry is found and no `--version` argument is provided, THE Catalog_Resolver SHALL select the entry with the highest version number according to semantic versioning precedence
4. WHEN `--version <constraint>` is provided, THE Catalog_Resolver SHALL select the highest version that satisfies the Version_Constraint
5. IF no Tool_Entry matches the specified name in any loaded catalog, THEN THE Catalog_Resolver SHALL report an error listing the tool name and suggest using `--url` for manual installation
6. IF a matching Tool_Entry exists but has no platform entry for the current Platform_Triple, THEN THE Catalog_Resolver SHALL report an error stating the tool is not available for the current platform and listing the available platforms for that tool
7. WHEN `--url` is provided alongside a catalog-resolvable name, THE Catalog_Resolver SHALL use the explicit URL and skip catalog lookup
8. WHEN a tool is successfully resolved from the catalog, THE Catalog_Resolver SHALL pass the resolved URL to the existing download and extraction pipeline and write the tool manifest upon successful installation

### Requirement 4: Package Addition by Name

**User Story:** As a CDo user, I want to add C/C++ library dependencies by name (e.g., `cdo deps add sdl3`), so that CDo can resolve download URLs and build metadata automatically.

#### Acceptance Criteria

1. WHEN `cdo deps add <name>` is invoked, THE Catalog_Resolver SHALL search loaded catalogs for a Package_Entry matching the specified name using case-insensitive comparison
2. WHEN a matching Package_Entry is found and no `@<version>` is specified, THE Catalog_Resolver SHALL select the entry with the highest version number and populate the DepSpec `url` field with the platform-specific download URL for the current Platform_Triple
3. WHEN a matching Package_Entry provides `include_dirs`, `link_libs`, or `defines`, THE Catalog_Resolver SHALL persist this metadata in the crate manifest so the build system can apply the specified compiler and linker flags
4. WHEN `cdo deps add <name>@<version>` syntax is used, THE Catalog_Resolver SHALL select the highest version satisfying the specified Version_Constraint
5. IF no Package_Entry matches the specified name, THEN THE Catalog_Resolver SHALL report an error listing up to 5 available packages whose name contains the specified name as a substring (case-insensitive)
6. IF a matching Package_Entry exists but has no platform entry for the current Platform_Triple, THEN THE Catalog_Resolver SHALL report an error stating the package is not available for the current platform
7. WHEN `cdo deps remove <name>` is invoked, THE CDo CLI SHALL remove the named dependency from the crate manifest and regenerate the lock file
8. IF `cdo deps remove <name>` is invoked and the specified name does not exist in the crate manifest, THEN THE CDo CLI SHALL report an error indicating the dependency was not found

### Requirement 5: Platform Detection

**User Story:** As a CDo user, I want CDo to automatically detect my platform, so that it downloads the correct platform-specific archives without manual configuration.

#### Acceptance Criteria

1. THE Catalog_Resolver SHALL detect the current operating system as one of: `windows`, `linux`, `macos`
2. THE Catalog_Resolver SHALL detect the current CPU architecture as one of: `x86_64`, `arm64`
3. THE Catalog_Resolver SHALL construct the Platform_Triple by combining the detected operating system and architecture with a hyphen separator in the format `{os}-{arch}` (e.g., `windows-x86_64`, `linux-arm64`)
4. IF the detected operating system is not one of the supported values (`windows`, `linux`, `macos`), THEN THE Catalog_Resolver SHALL report an error indicating the unsupported operating system name and abort the catalog resolution
5. IF the detected CPU architecture is not one of the supported values (`x86_64`, `arm64`), THEN THE Catalog_Resolver SHALL report an error indicating the unsupported architecture name and abort the catalog resolution

### Requirement 6: Version Resolution

**User Story:** As a CDo user, I want to specify version constraints when installing tools or adding packages, so that I can control which versions are acceptable for my project.

#### Acceptance Criteria

1. THE Catalog_Resolver SHALL support the following Version_Constraint formats: exact (`1.2.3` — matches only that version), caret (`^1.2.3` — matches versions `>=1.2.3` and `<2.0.0`), tilde (`~1.2.3` — matches versions `>=1.2.3` and `<1.3.0`), greater-or-equal (`>=1.2.3`), less-than (`<2.0.0`), wildcard (`*` — matches any version)
2. WHEN multiple catalog entries for the same name satisfy a Version_Constraint, THE Catalog_Resolver SHALL select the entry with the highest version according to semantic versioning precedence
3. THE Catalog_Resolver SHALL compare versions using semantic versioning precedence rules where major version is compared first, then minor, then patch as numeric values, and pre-release versions (e.g., `1.0.0-alpha`) SHALL have lower precedence than the corresponding release version
4. IF no catalog entry satisfies the specified Version_Constraint, THEN THE Catalog_Resolver SHALL report an error listing the requested constraint and all available versions for that name in the loaded catalogs
5. IF a Version_Constraint string does not conform to any of the supported formats defined in criterion 1, THEN THE Catalog_Resolver SHALL report an error indicating the constraint is malformed and listing the supported formats

### Requirement 7: Built-In Catalog Content

**User Story:** As a CDo user, I want CDo to ship with catalogs for common tools and packages, so that I can get started without additional configuration.

#### Acceptance Criteria

1. THE Built_In_Catalog SHALL include a Tool_Entry for `w64devkit` with a valid semantic version string and a platform entry for `windows-x86_64` containing a download URL
2. THE Built_In_Catalog SHALL include a Package_Entry for `sdl3` with a valid semantic version string, platform entries for at minimum `windows-x86_64`, and build metadata fields (`include_dirs` and `link_libs`) sufficient for the build system to compile and link against the library
3. WHEN CDo is built, THE build process SHALL place the Built_In_Catalog TOML files in a `catalogs/` directory relative to the CDo binary output path
4. THE Built_In_Catalog entries SHALL conform to the catalog schema defined in Requirement 1, including all required fields for their entry type (Tool_Entry or Package_Entry)

### Requirement 8: User-Defined Catalogs

**User Story:** As a CDo user, I want to author custom catalog files for internal or third-party tools and packages, so that my team can share tool and package definitions without modifying the CDo source.

#### Acceptance Criteria

1. WHEN a `.toml` file is placed in the workspace `.cdo/catalogs/` directory, THE Catalog_Loader SHALL load the file as a User_Catalog and ignore any non-`.toml` files in that directory
2. WHEN a `.toml` file is placed in the user-global `~/.cdo/catalogs/` directory, THE Catalog_Loader SHALL load the file as a User_Catalog and ignore any non-`.toml` files in that directory
3. THE Catalog_Loader SHALL validate User_Catalog files against the same schema rules as the Built_In_Catalog, requiring each Tool_Entry to have `name` and `version` fields and each Package_Entry to have `name` and `version` fields
4. IF a User_Catalog file contains entries with missing required fields, THEN THE Catalog_Loader SHALL report a warning that includes the file path, the entry index or name (if parseable), and the list of missing fields, and skip that entry without aborting the catalog load
5. IF a User_Catalog file contains multiple entries with the same `name` and `version` combination, THEN THE Catalog_Loader SHALL use the last occurrence in file order and report a warning identifying the duplicate entry by file path and entry name

### Requirement 9: Catalog Listing and Search

**User Story:** As a CDo user, I want to list and search available catalog entries, so that I can discover what tools and packages are available without reading TOML files manually.

#### Acceptance Criteria

1. WHEN `cdo catalog list` is invoked, THE CDo CLI SHALL display each available Tool_Entry and Package_Entry on a separate line showing the entry name, version, and description (if present)
2. WHEN `cdo catalog search <query>` is invoked, THE CDo CLI SHALL display entries whose name or description contains the query string using case-insensitive substring matching, showing the same per-entry information as `cdo catalog list`
3. WHEN `cdo catalog list --tools` is invoked, THE CDo CLI SHALL display only Tool_Entry entries
4. WHEN `cdo catalog list --packages` is invoked, THE CDo CLI SHALL display only Package_Entry entries
5. IF `cdo catalog search <query>` matches zero entries, THEN THE CDo CLI SHALL display a message indicating no entries matched the query
6. IF `cdo catalog list` is invoked and no catalog entries are loaded, THEN THE CDo CLI SHALL display a message indicating the catalog is empty
7. IF `cdo catalog search` is invoked without a query argument, THEN THE CDo CLI SHALL display a usage error indicating that a query argument is required

### Requirement 10: Checksum Verification

**User Story:** As a CDo user, I want downloaded archives to be verified against checksums in the catalog, so that I can trust that the downloaded content has not been corrupted or tampered with.

#### Acceptance Criteria

1. WHEN a catalog entry provides a `checksum` field, THE Catalog_Resolver SHALL compute the hash of the downloaded archive using the specified algorithm and compare it to the expected hex digest before extraction
2. THE Catalog_Resolver SHALL support checksums in the format `algorithm:hex_digest` where `algorithm` is one of `sha256`, `sha384`, or `sha512`, and `hex_digest` is a lowercase hexadecimal string whose length matches the algorithm's output size (64, 96, or 128 characters respectively)
3. IF the checksum verification fails, THEN THE Catalog_Resolver SHALL delete the downloaded archive, report an error indicating the expected and actual hex digests, and abort the installation of that catalog entry
4. WHEN a catalog entry does not provide a `checksum` field, THE Catalog_Resolver SHALL proceed with installation and emit a warning that the archive was not verified
5. IF the `checksum` field value does not conform to the `algorithm:hex_digest` format or specifies an unsupported algorithm, THEN THE Catalog_Resolver SHALL report an error identifying the malformed checksum value and abort the installation of that catalog entry without downloading

### Requirement 11: Catalog TOML Serialization Round-Trip

**User Story:** As a CDo developer, I want the catalog TOML parser and serializer to preserve catalog content through a parse-serialize-parse cycle, so that programmatic catalog edits do not corrupt data.

#### Acceptance Criteria

1. WHEN a valid catalog TOML file is parsed and then serialized, THE Catalog_Loader SHALL produce output that, when parsed again, yields a structurally equivalent in-memory catalog (same keys, values, types, and nesting)
2. THE Catalog_Loader SHALL preserve the ordering of array-of-tables entries (`[[tool]]`, `[[package]]`) and the order of key-value pairs within each entry during serialization
3. IF serialization fails due to internal errors, THEN THE Catalog_Loader SHALL report an error indicating the failure reason without writing a partial or corrupt file
4. THE Catalog_Loader serialization output SHALL itself be valid TOML v1.0 that can be parsed without errors

### Requirement 12: Dev/Test-Only Dependencies

**User Story:** As a CDo user, I want to declare dependencies that are only needed for testing, so that my release builds do not link unnecessary libraries.

#### Acceptance Criteria

1. WHEN `cdo deps add <name> --dev` is invoked, THE CDo CLI SHALL add the dependency to a `[dev-dependencies]` section in the crate manifest instead of `[dependencies]`
2. WHILE the build profile is `release`, THE build system SHALL exclude Dev_Dependency entries by not passing their include paths, link libraries, or defines to the compiler or linker
3. WHILE the build profile is `debug`, THE build system SHALL include Dev_Dependency entries in compilation and linking by passing their include paths, link libraries, and defines to the compiler and linker
4. WHEN running tests via `cdo test`, THE build system SHALL include Dev_Dependency entries in compilation and linking by passing their include paths, link libraries, and defines to the compiler and linker
5. WHEN `cdo deps remove <name> --dev` is invoked, THE CDo CLI SHALL remove the named dependency from the `[dev-dependencies]` section
6. IF `cdo deps remove <name> --dev` is invoked and the named dependency does not exist in `[dev-dependencies]`, THEN THE CDo CLI SHALL report an error message indicating the dependency was not found in dev-dependencies
7. WHEN `cdo deps list` is invoked, THE CDo CLI SHALL display both normal and dev dependencies, with each entry labeled as either `[normal]` or `[dev]` scope
8. IF `cdo deps add <name> --dev` is invoked and the named dependency already exists in `[dependencies]`, THEN THE CDo CLI SHALL report an error message indicating the dependency already exists as a normal dependency and take no action

### Requirement 13: CLI Command Coherence

**User Story:** As a CDo user, I want a consistent CLI structure where tools and dependencies are managed through parallel command hierarchies, so that the interface is predictable and discoverable.

#### Acceptance Criteria

1. THE CDo CLI SHALL expose tool management under the `cdo tool` command group with subcommands: `install`, `remove`, `list`
2. THE CDo CLI SHALL expose dependency management under the `cdo deps` command group with subcommands: `add`, `remove`, `list`
3. THE CDo CLI SHALL expose catalog browsing under the `cdo catalog` command group with subcommands: `list`, `search`
4. WHEN an unrecognized subcommand is used within a valid command group, THE CDo CLI SHALL display the available subcommands for that command group and exit with a non-zero exit code
5. WHEN an unrecognized top-level command is used (e.g., `cdo foo`), THE CDo CLI SHALL display the available command groups (`tool`, `deps`, `catalog`) and exit with a non-zero exit code
6. WHEN a command group is invoked with no subcommand or with `--help`, THE CDo CLI SHALL display usage information listing the available subcommands and their one-line descriptions
