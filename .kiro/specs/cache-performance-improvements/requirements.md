# Requirements Document

## Introduction

This document specifies requirements for performance improvements to the CDo build cache system. The current cache pipeline computes a full SHA-256 hash of every source file and its dependency headers before checking the cache, which can take as much time as recompiling small files. Additionally, the per-batch "served from cache" log message produces noisy output when many modules are individually cached. This feature introduces three optimizations: an mtime-based fast-path to skip hashing when files have not changed, a minimum filesize threshold below which files are recompiled directly instead of hashed, and consolidated cache log output.

## Glossary

- **CDo**: The C/C++ build tool that manages workspaces containing crates with modules.
- **Build_Cache**: The content-addressable object store in `.cdo/cache/objects/` that maps SHA-256 cache keys to compiled object files.
- **Cache_Key**: A 64-character hex string produced by hashing the canonical representation of a source file, its headers, compiler info, and build flags.
- **Mtime_Index**: A persistent data file storing the last-known modification timestamp and file size for each source and header file that participated in a successful cache hit or store operation.
- **Fast_Path**: The optimized code path that skips SHA-256 hash computation when the Mtime_Index indicates no file has changed since the last successful build.
- **Filesize_Threshold**: A configurable byte size below which source files bypass cache lookup entirely and are always recompiled directly.
- **Cache_Pipeline**: The sequence of operations in `compiler_compile_batch` and `cache_compute_key` that determines whether a source file can be served from the Build_Cache.
- **PAL**: The Platform Abstraction Layer providing filesystem, process, and OS-level operations for CDo.
- **CacheConfig**: The configuration structure parsed from `[workspace.settings.cache]` in `cdo.toml`.
- **Batch**: A set of source files submitted together to `compiler_compile_batch` for compilation within a single module.

## Requirements

### Requirement 1: Mtime-Based Fast-Path Cache Validation

**User Story:** As a developer, I want the build cache to skip expensive SHA-256 hashing when source files have not been modified, so that incremental builds with unchanged files complete faster.

#### Acceptance Criteria

1. WHEN a source file and all its dependency headers have mtime and file size values matching the Mtime_Index, THE Cache_Pipeline SHALL skip SHA-256 hash computation and serve the cached object file directly.
2. WHEN a source file or any of its dependency headers has an mtime or file size that differs from the Mtime_Index entry, THE Cache_Pipeline SHALL fall through to the standard SHA-256 cache key computation.
3. WHEN no Mtime_Index entry exists for a source file, THE Cache_Pipeline SHALL fall through to the standard SHA-256 cache key computation.
4. WHEN a cache hit occurs via the Fast_Path, THE Cache_Pipeline SHALL record the operation as a cache hit in the CacheStats structure.
5. WHEN a successful cache store or cache hit via full hash computation occurs, THE Cache_Pipeline SHALL update the Mtime_Index with the current mtime and file size of the source file and all its dependency headers.
6. THE Mtime_Index SHALL persist across builds by writing to a file in the `.cdo/cache/` directory.
7. WHEN the Mtime_Index file does not exist or is corrupted, THE Cache_Pipeline SHALL treat all entries as missing and fall through to full hash computation without producing an error.
8. IF the PAL mtime retrieval fails for a file, THEN THE Cache_Pipeline SHALL treat that file as changed and fall through to full hash computation.

### Requirement 2: Minimum Filesize Threshold

**User Story:** As a developer, I want files smaller than a configurable threshold to bypass cache lookup entirely, so that the overhead of hashing is not spent on files that are cheaper to recompile than to hash.

#### Acceptance Criteria

1. THE CacheConfig SHALL include a `min_file_size` field representing the minimum source file size in bytes below which cache lookup is skipped.
2. WHEN a source file has a size strictly less than the Filesize_Threshold, THE Cache_Pipeline SHALL skip both mtime checking and SHA-256 hash computation for that file and proceed directly to compilation.
3. WHEN a source file has a size strictly less than the Filesize_Threshold, THE Cache_Pipeline SHALL NOT store the resulting object file in the Build_Cache.
4. THE CacheConfig SHALL default the Filesize_Threshold to 4096 bytes when no value is specified in `cdo.toml`.
5. WHEN the Filesize_Threshold is set to 0, THE Cache_Pipeline SHALL disable the threshold and perform cache operations on all files regardless of size.
6. THE `cdo.toml` SHALL support a `min-file-size` key under `[workspace.settings.cache]` accepting an integer value in bytes.

### Requirement 3: Consolidated Cache Log Output

**User Story:** As a developer, I want a single summary log message for cache results instead of per-batch messages, so that build output is concise and informative.

#### Acceptance Criteria

1. WHEN a build completes and the Build_Cache was active, THE CDo build command SHALL emit one aggregate INFO-level log message summarizing total cache hits and total files compiled across all batches.
2. THE aggregate cache log message SHALL follow the format: `"Cache: <hits> hit(s), <misses> miss(es), <skipped> skipped (below threshold)"` where `<skipped>` represents files that bypassed the cache due to the Filesize_Threshold.
3. WHEN all files in a single batch are served from cache, THE Cache_Pipeline SHALL NOT emit the per-batch "All N file(s) served from cache" message at INFO level.
4. WHILE verbose logging is enabled, THE Cache_Pipeline SHALL continue to emit per-file cache hit and miss messages at TRACE level.
5. WHEN a build completes with zero cache interactions (cache disabled or no-cache flag), THE CDo build command SHALL NOT emit the aggregate cache log message.

### Requirement 4: Mtime Index Integrity

**User Story:** As a developer, I want the mtime index to remain consistent even after interrupted builds or manual cache clears, so that the fast-path does not serve stale cached objects.

#### Acceptance Criteria

1. WHEN the Build_Cache is cleared via `cdo cache clear`, THE CDo cache command SHALL also delete the Mtime_Index file.
2. WHEN a compilation fails for a source file, THE Cache_Pipeline SHALL NOT update the Mtime_Index entry for that file.
3. THE Mtime_Index SHALL store entries keyed by absolute path of each source and header file.
4. WHEN a file tracked in the Mtime_Index is deleted from the filesystem, THE Cache_Pipeline SHALL treat the entry as a miss and remove the stale entry from the index.
5. THE Mtime_Index file format SHALL support atomic updates to prevent corruption from interrupted builds.

### Requirement 5: Configuration Integration

**User Story:** As a developer, I want to configure the new cache optimizations through the existing `cdo.toml` configuration, so that the behavior is consistent with other CDo settings.

#### Acceptance Criteria

1. THE `cdo.toml` SHALL support a `fast-path` boolean key under `[workspace.settings.cache]` to enable or disable the mtime-based Fast_Path (default: true).
2. THE `cdo.toml` SHALL support a `min-file-size` integer key under `[workspace.settings.cache]` to configure the Filesize_Threshold in bytes (default: 4096).
3. WHEN `fast-path` is set to false, THE Cache_Pipeline SHALL skip mtime checking and always perform full SHA-256 hash computation for cache lookups.
4. THE CacheConfig structure SHALL be extended with fields for `fast_path_enabled` and `min_file_size` parsed from `cdo.toml`.
5. WHEN an invalid value is provided for `min-file-size` (negative number or non-integer), THE configuration parser SHALL log a warning and use the default value of 4096.

### Requirement 6: CacheStats Extension for Threshold Skips

**User Story:** As a developer, I want visibility into how many files were skipped due to the filesize threshold, so that I can tune the threshold effectively.

#### Acceptance Criteria

1. THE CacheStats structure SHALL include a `skipped` field that counts files bypassed due to the Filesize_Threshold.
2. WHEN a source file is below the Filesize_Threshold and bypasses cache lookup, THE Cache_Pipeline SHALL increment the `skipped` counter in CacheStats.
3. WHEN the `cdo cache stats` command is executed, THE CDo cache command SHALL display the configured Filesize_Threshold value.
