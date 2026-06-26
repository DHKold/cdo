# Implementation Plan: Catalog System

## Overview

This plan implements a TOML-based catalog registry for CDo that resolves tool and package names to platform-specific download URLs, version metadata, and build flags. The implementation proceeds bottom-up: core modules first (semver, catalog data structures, TOML parsing integration), then CLI integration, then built-in catalog content, and finally testing.

## Tasks

- [x] 1. Implement semantic version parsing and comparison
  - [x] 1.1 Create `src/cdo/core/semver.h` and `src/cdo/core/semver.c`
    - Define `Semver`, `SemverConstraintKind`, and `SemverConstraint` types as specified in the design
    - Implement `semver_parse()` to parse `major.minor.patch[-prerelease]` strings
    - Implement `semver_compare()` with total ordering (major > minor > patch, pre-release < release)
    - Implement `semver_constraint_parse()` supporting exact, caret, tilde, gte, lt, and wildcard formats
    - Implement `semver_satisfies()` applying range logic for each constraint kind
    - _Requirements: 6.1, 6.2, 6.3_

  - [x]* 1.2 Write property tests for semver module
    - **Property 8: Semantic Version Comparison Total Order**
    - **Property 9: Version Constraint Parse Round-Trip**
    - **Property 7: Version Constraint Satisfaction**
    - Add `prop_semver_total_order` (1000 trials), `prop_semver_constraint_roundtrip` (500 trials), and `prop_semver_constraint_satisfaction` (500 trials) to `tests/test_semver.c`
    - **Validates: Requirements 6.1, 6.2, 6.3**

  - [x]* 1.3 Write unit tests for semver edge cases
    - Test parsing malformed strings (missing patch, leading zeros, empty string)
    - Test pre-release ordering (alpha < beta < rc < release)
    - Test constraint boundary conditions (caret/tilde wraparound)
    - _Requirements: 6.4, 6.5_

- [x] 2. Implement catalog data structures and TOML parsing
  - [x] 2.1 Create `src/cdo/core/catalog.h` with type definitions
    - Define `CatalogPlatform`, `CatalogPlatformEntry`, `CatalogToolEntry`, `CatalogPackageEntry`, `Catalog`, and `CatalogResolveResult` structs as specified in the design
    - Declare the public API functions: `catalog_detect_platform`, `catalog_load`, `catalog_resolve_tool`, `catalog_resolve_package`, `catalog_search`, `catalog_free`, `catalog_resolve_result_free`
    - _Requirements: 1.2, 1.3, 1.4, 5.1, 5.2, 5.3_

  - [x] 2.2 Implement `catalog_detect_platform()` in `src/cdo/core/catalog.c`
    - Use platform-specific preprocessor macros and/or `pal.h` abstractions to detect OS and architecture
    - Construct the triple string as `{os}-{arch}`
    - Return non-zero error for unsupported OS or architecture values
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_

  - [x] 2.3 Implement TOML catalog file parsing in `catalog.c`
    - Integrate with existing `toml.c`/`toml.h` to parse TOML v1.0 files
    - Parse `[[tool]]` array-of-tables into `CatalogToolEntry` structs, validating required fields (name, version) and field constraints (name length 1–128, alphanumeric/hyphen/underscore only)
    - Parse `[[package]]` array-of-tables into `CatalogPackageEntry` structs, validating required fields and populating `include_dirs`, `link_libs`, `defines` arrays
    - Parse `[tool.platforms.<triple>]` and `[package.platforms.<triple>]` sub-tables for URL and optional checksum
    - On TOML parse failure: report file path, line number, error description; skip file
    - On missing required fields: report warning with file path and entry index; skip entry
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6_

  - [x]* 2.4 Write property tests for catalog entry parsing
    - **Property 1: Catalog Entry Parsing Preserves Fields**
    - **Property 3: Invalid Entry Skipping Preserves Valid Entries**
    - Add `prop_catalog_entry_parsing` (500 trials) and `prop_catalog_invalid_entry_skip` (300 trials) to `tests/test_catalog.c`
    - **Validates: Requirements 1.2, 1.3, 1.6, 8.4**

- [x] 3. Implement catalog discovery and loading with precedence
  - [x] 3.1 Implement `catalog_load()` with multi-location discovery
    - Search workspace `.cdo/catalogs/`, user-global `~/.cdo/catalogs/`, and built-in `<binary_dir>/catalogs/` in precedence order
    - Silently skip non-existent directories
    - Load only `.toml` files from each directory in lexicographic filename order
    - If no catalog files found anywhere, emit warning and return success (empty catalog)
    - On filesystem I/O errors, report file path and error, skip that file
    - _Requirements: 2.1, 2.2, 2.4, 2.5, 8.1, 8.2_

  - [x] 3.2 Implement precedence and deduplication logic
    - When same name+version appears at different precedence levels, keep highest-precedence entry
    - When same name+version appears in multiple files within the same level, keep the lexicographically-last file's entry
    - When same name+version appears multiple times within a single file, keep last occurrence and emit warning
    - _Requirements: 2.3, 8.5_

  - [x]* 3.3 Write property tests for precedence and duplicate resolution
    - **Property 4: Precedence Resolution**
    - **Property 10: Intra-File Duplicate Resolution**
    - Add `prop_catalog_precedence` (200 trials) and `prop_catalog_duplicate_last_wins` (200 trials) to `tests/test_catalog.c`
    - **Validates: Requirements 2.3, 8.5**

- [x] 4. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Implement catalog resolution (tool and package)
  - [x] 5.1 Implement `catalog_resolve_tool()`
    - Case-insensitive name matching against loaded tool entries
    - If no version constraint: select highest version via `semver_compare`
    - If version constraint provided: filter entries by `semver_satisfies`, then select highest
    - Select platform-specific URL from the matching entry's platforms array
    - Error if no name match, no version satisfies constraint, or platform triple not present
    - On platform mismatch: list available platforms for the tool
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6_

  - [x] 5.2 Implement `catalog_resolve_package()`
    - Same resolution logic as tools (case-insensitive name, version selection, platform lookup)
    - Additionally populate `CatalogResolveResult` with `include_dirs`, `link_libs`, and `defines`
    - On no match: suggest up to 5 package names containing the query as substring
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6_

  - [x]* 5.3 Write property tests for catalog resolution
    - **Property 2: Platform Selection Correctness**
    - **Property 5: Case-Insensitive Name Lookup**
    - **Property 6: Highest Version Selection**
    - **Property 14: Package Suggestion Substring Match**
    - Add `prop_catalog_platform_selection` (200 trials), `prop_catalog_case_insensitive` (200 trials), `prop_catalog_highest_version` (300 trials), and `prop_catalog_suggestion_substring` (200 trials) to `tests/test_catalog.c`
    - **Validates: Requirements 3.1, 3.2, 3.3, 4.1, 4.2, 4.5**

- [x] 6. Implement catalog search
  - [x] 6.1 Implement `catalog_search()`
    - Case-insensitive substring matching on entry name and description
    - Support `tools_only` and `packages_only` filter flags
    - Write matching indices into output arrays and return counts
    - _Requirements: 9.2, 9.3, 9.4_

  - [x]* 6.2 Write property tests for catalog search
    - **Property 11: Catalog Search Completeness and Soundness**
    - Add `prop_catalog_search_correctness` (300 trials) to `tests/test_catalog.c`
    - **Validates: Requirements 9.2**

- [x] 7. Implement checksum verification
  - [x] 7.1 Implement checksum parsing and validation logic
    - Parse `algorithm:hex_digest` format, validating algorithm is sha256/sha384/sha512
    - Validate hex_digest length matches algorithm (64/96/128 characters)
    - Reject malformed checksum format before download
    - After download: compute hash of archive, compare to expected digest
    - On mismatch: delete downloaded archive, report expected vs actual, abort
    - On missing checksum: proceed with warning
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5_

  - [x]* 7.2 Write property tests for checksum validation
    - **Property 13: Checksum Validation Correctness**
    - Add `prop_catalog_checksum_validation` (300 trials) to `tests/test_catalog.c`
    - **Validates: Requirements 10.1, 10.2**

- [x] 8. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 9. Implement CLI integration for `cmd_tool`
  - [x] 9.1 Modify `cmd_tool.c` to use catalog resolution
    - When `cdo tool install <name>` is invoked without `--url`: call `catalog_load()` + `catalog_resolve_tool()`
    - Parse optional `--version` argument as a version constraint
    - When `--url` is provided, skip catalog lookup entirely
    - Pass resolved URL (and checksum if present) to existing download/extract pipeline
    - Invoke checksum verification before extraction when checksum is available
    - Write tool manifest on successful installation
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 10.1, 10.3, 10.4_

  - [x]* 9.2 Write unit tests for cmd_tool catalog integration
    - Test install by name resolves correct URL
    - Test `--url` bypass skips catalog
    - Test version constraint selects correct entry
    - Test error messages for missing tool and platform mismatch
    - _Requirements: 3.1, 3.5, 3.6, 3.7_

- [x] 10. Implement CLI integration for `cmd_deps`
  - [x] 10.1 Modify `cmd_deps.c` to use catalog resolution for `add`
    - When `cdo deps add <name>` is invoked: call `catalog_load()` + `catalog_resolve_package()`
    - Parse optional `@<version>` suffix from name argument as version constraint
    - Populate `DepSpec` with resolved URL and persist build metadata (include_dirs, link_libs, defines) to crate manifest
    - Support `--dev` flag to route dependency to `[dev-dependencies]` section
    - Error if `--dev` used and dependency already exists in `[dependencies]`
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 12.1, 12.8_

  - [x] 10.2 Implement `deps remove` and `deps list` commands
    - `cdo deps remove <name>`: remove from `[dependencies]`, regenerate lock file
    - `cdo deps remove <name> --dev`: remove from `[dev-dependencies]`
    - Error if named dependency not found in the targeted section
    - `cdo deps list`: display all dependencies with `[normal]` or `[dev]` scope labels
    - _Requirements: 4.7, 4.8, 12.5, 12.6, 12.7_

  - [x]* 10.3 Write unit tests for cmd_deps catalog integration
    - Test add by name populates manifest correctly
    - Test `@version` constraint parsing
    - Test `--dev` routing and conflict detection
    - Test remove from both sections with error cases
    - Test list output with scope labels
    - _Requirements: 4.1, 4.7, 12.1, 12.5, 12.7_

- [x] 11. Implement dev-dependency build integration
  - [x] 11.1 Modify build system to handle dependency scopes
    - In release profile: exclude `[dev-dependencies]` from compiler/linker flags
    - In debug profile: include `[dev-dependencies]` include paths, link libs, and defines
    - When running `cdo test`: include `[dev-dependencies]` regardless of build profile
    - _Requirements: 12.2, 12.3, 12.4_

- [x] 12. Implement `cmd_catalog` command
  - [x] 12.1 Create `src/cdo/commands/cmd_catalog.h` and `cmd_catalog.c`
    - Implement `cdo catalog list`: display all entries (name, version, description)
    - Implement `cdo catalog list --tools`: filter to tool entries only
    - Implement `cdo catalog list --packages`: filter to package entries only
    - Implement `cdo catalog search <query>`: case-insensitive substring search on name/description
    - Display "no entries matched" when search has zero results
    - Display "catalog is empty" when list has no entries
    - Display usage error when search is invoked without a query argument
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5, 9.6, 9.7_

  - [x]* 12.2 Write unit tests for cmd_catalog
    - Test list output format
    - Test search with matching and non-matching queries
    - Test filter flags
    - Test empty catalog and missing query argument messages
    - _Requirements: 9.1, 9.2, 9.5, 9.6, 9.7_

- [x] 13. Implement CLI command coherence and routing
  - [x] 13.1 Update `cli.h` and CLI router for new commands
    - Add `CDO_CMD_CATALOG` and `CDO_CMD_DEPS` to `CdoCommand` enum
    - Add `--dev` bool, `--version` string, and `--tools`/`--packages` flags to `CdoOptions`
    - Route `cdo tool` subcommands (install, remove, list)
    - Route `cdo deps` subcommands (add, remove, list)
    - Route `cdo catalog` subcommands (list, search)
    - On unrecognized subcommand: display available subcommands, exit non-zero
    - On command group with no subcommand or `--help`: display usage info
    - _Requirements: 13.1, 13.2, 13.3, 13.4, 13.5, 13.6_

- [x] 14. Implement TOML serialization round-trip
  - [x] 14.1 Implement catalog serialization to TOML
    - Serialize `Catalog` back to valid TOML v1.0 text
    - Preserve array-of-tables ordering and key-value pair ordering
    - On serialization failure: report error without writing partial file
    - _Requirements: 11.1, 11.2, 11.3, 11.4_

  - [x]* 14.2 Write property tests for TOML round-trip
    - **Property 12: Catalog TOML Serialization Round-Trip**
    - Add `prop_catalog_toml_roundtrip` (500 trials) to `tests/test_catalog.c`
    - **Validates: Requirements 11.1, 11.2, 11.4**

- [x] 15. Create built-in catalog content
  - [x] 15.1 Create `catalogs/tools.toml` with w64devkit entry
    - Add `[[tool]]` entry for w64devkit with valid semver version
    - Add `[tool.platforms.windows-x86_64]` with download URL and checksum
    - _Requirements: 7.1, 7.3, 7.4_

  - [x] 15.2 Create `catalogs/packages.toml` with sdl3 entry
    - Add `[[package]]` entry for sdl3 with valid semver version
    - Add platform entry for at minimum `windows-x86_64` with download URL
    - Include `include_dirs` and `link_libs` build metadata
    - _Requirements: 7.2, 7.3, 7.4_

  - [x] 15.3 Ensure build process deploys catalog files
    - Configure build to copy `catalogs/*.toml` to output directory alongside CDo binary
    - _Requirements: 7.3_

- [x] 16. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from the design document
- Unit tests validate specific examples and edge cases
- The implementation language is C, matching the existing CDo codebase
- All property tests use the `theft` library already integrated in the project
- Test files are organized in `tests/test_semver.c` and `tests/test_catalog.c`

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1", "2.1"] },
    { "id": 1, "tasks": ["1.2", "1.3", "2.2", "2.3"] },
    { "id": 2, "tasks": ["2.4", "3.1"] },
    { "id": 3, "tasks": ["3.2"] },
    { "id": 4, "tasks": ["3.3", "5.1", "5.2", "6.1", "7.1"] },
    { "id": 5, "tasks": ["5.3", "6.2", "7.2", "14.1"] },
    { "id": 6, "tasks": ["9.1", "10.1", "10.2", "11.1", "12.1", "13.1", "14.2"] },
    { "id": 7, "tasks": ["9.2", "10.3", "12.2", "15.1", "15.2"] },
    { "id": 8, "tasks": ["15.3"] }
  ]
}
```
