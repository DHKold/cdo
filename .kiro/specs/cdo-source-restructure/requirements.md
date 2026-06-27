# Requirements Document

## Introduction

Restructure the CDo build tool's source code to improve maintainability. The effort has two phases: (1) split large source files (>500 lines) into smaller, focused compilation units, and (2) reorganize generic/reusable modules into a new `lib/commons/` directory while keeping domain-specific code in `lib/core/` and `lib/commands/`. Headers in `api/` mirror the source layout.

## Glossary

- **CDo**: The self-building C17 build tool that is the subject of this restructuring
- **Crate**: A CDo package unit located under `crates/cdo/`
- **Source_File**: A `.c` implementation file located under `crates/cdo/lib/`
- **Header_File**: A `.h` public API file located under `crates/cdo/api/`
- **Split**: Decomposing one source file into multiple smaller files, each with a focused responsibility
- **Commons**: The new `lib/commons/` directory for generic, reusable modules with no domain-specific CDo logic
- **Core**: The existing `lib/core/` directory for domain-specific CDo functionality
- **Build_System**: The CDo build tool invoked via `.\cdo.exe build cdo`
- **Test_Suite**: The CDo test suite invoked via `.\cdo.exe test cdo`
- **Line_Threshold**: The 500-line limit above which a source file is a candidate for splitting

## Requirements

### Requirement 1: File Splitting Phase

**User Story:** As a developer, I want large source files split into smaller focused units, so that each file has a single responsibility and is easier to navigate and maintain.

#### Acceptance Criteria

1. WHEN a Source_File exceeds the Line_Threshold, THE Build_System SHALL support compiling that file's functionality from multiple smaller Source_Files that each contain a single logical responsibility.
2. THE Split SHALL preserve the original public API of each module by keeping the existing Header_File intact or by having split Header_Files collectively export the same symbols.
3. WHEN cmd_build.c is split, THE Build_System SHALL compile separate Source_Files for library-build logic, executable-build logic, test-build logic, and shared build utilities.
4. WHEN toml.c is split, THE Build_System SHALL compile separate Source_Files for TOML parsing and TOML serialization.
5. WHEN compiler.c is split, THE Build_System SHALL compile separate Source_Files for compiler detection, dirty-checking, compilation dispatch, and linking.
6. WHEN catalog.c is split, THE Build_System SHALL compile separate Source_Files for catalog loading, searching, dependency resolution, and serialization.
7. WHEN workspace.c is split, THE Build_System SHALL compile separate Source_Files for workspace loading and dependency resolution.
8. WHEN cmd_deps.c is split, THE Build_System SHALL compile separate Source_Files for the add, remove, sync, and download subcommands.
9. WHEN cli.c is split, THE Build_System SHALL compile separate Source_Files for CLI argument parsing and command suggestion.
10. WHEN deps.c is split, THE Build_System SHALL compile separate Source_Files for dependency resolution and manifest handling.
11. WHEN a Source_File is split, THE resulting files SHALL each remain below the Line_Threshold.
12. WHEN a Source_File is split, THE Build_System SHALL produce a binary with identical external behavior to the pre-split binary.

### Requirement 2: Folder Reorganization Phase

**User Story:** As a developer, I want generic reusable modules separated from domain-specific code, so that the codebase has clear architectural boundaries and modules can be reused independently.

#### Acceptance Criteria

1. THE Build_System SHALL compile Source_Files from a new `lib/commons/` directory for generic, reusable modules.
2. WHEN the reorganization is complete, THE `lib/commons/` directory SHALL contain the json, toml, checksum, threadpool, semver, http, and archive modules.
3. WHEN a module is moved to `lib/commons/`, THE corresponding Header_File SHALL be moved to a matching `api/commons/` directory.
4. WHILE a module resides in `lib/commons/`, THE module SHALL contain no `#include` references to headers in `api/core/` or `api/commands/`.
5. THE `lib/core/` directory SHALL retain only domain-specific modules that depend on CDo concepts such as workspace, catalog, compiler, deps, module, scanner, output, errors, shader, and template.
6. THE `lib/commands/` directory SHALL retain command implementations and test-related modules.
7. THE `lib/pal/` directory SHALL remain unchanged during the reorganization phase.
8. WHEN a module is moved between directories, THE Build_System SHALL produce a binary with identical external behavior to the pre-move binary.

### Requirement 3: Header Mirroring Consistency

**User Story:** As a developer, I want headers to mirror the source layout, so that include paths are predictable and consistent.

#### Acceptance Criteria

1. THE directory structure under `api/` SHALL mirror the directory structure under `lib/` for every module.
2. WHEN a new Source_File is created from a split, THE Build_System SHALL have a corresponding Header_File in the matching `api/` subdirectory if the new file exposes public symbols.
3. WHEN a Source_File only contains internal implementation details, THE Source_File SHALL use a file-local static scope and require no new Header_File.

### Requirement 4: Build and Test Integrity

**User Story:** As a developer, I want each phase to produce a passing build and test suite, so that the restructuring does not introduce regressions.

#### Acceptance Criteria

1. WHEN the file-splitting phase is complete, THE Build_System SHALL compile the crate successfully via `.\cdo.exe build cdo`.
2. WHEN the file-splitting phase is complete, THE Test_Suite SHALL pass all existing tests via `.\cdo.exe test cdo`.
3. WHEN the folder-reorganization phase is complete, THE Build_System SHALL compile the crate successfully via `.\cdo.exe build cdo`.
4. WHEN the folder-reorganization phase is complete, THE Test_Suite SHALL pass all existing tests via `.\cdo.exe test cdo`.
5. WHEN any individual file is split or moved, THE Build_System SHALL compile successfully before proceeding to the next file operation.

### Requirement 5: Two-Phase Ordering

**User Story:** As a developer, I want splitting completed before reorganization, so that each phase is independently verifiable and risk is minimized.

#### Acceptance Criteria

1. THE restructuring SHALL execute the file-splitting phase to completion before beginning the folder-reorganization phase.
2. WHEN the file-splitting phase is complete, THE Build_System SHALL pass build and test verification before the folder-reorganization phase begins.
3. IF a build or test failure occurs during either phase, THEN THE developer SHALL resolve the failure before proceeding to the next file operation.
