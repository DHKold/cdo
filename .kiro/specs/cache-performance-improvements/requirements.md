# Requirements Document

## Introduction

This document specifies requirements for performance improvements to the CDo build cache system. The current cache pipeline computes a full SHA-256 hash of every source file and its dependency headers before checking the cache, which can take as much time as recompiling small files. Additionally, the per-batch "served from cache" log message produces noisy output when many modules are individually cached. This feature introduces three optimizations: an mtime-based fast-path to skip hashing when files have not changed, a minimum filesize threshold below which files are recompiled directly instead of hashed, and consolidated cache log output.

## Glossary

- **CDo**: The C/C++ build tool that manages workspaces containing crates with modules.
- **Build_Cache**: The content-addressable object store in `.cdo/cache/objects/` that maps SHA-256 cache keys to compiled object files.
- **Cache_Key**: A 64-character hex string produced by hashing the canonical representation of a source file, its headers, compiler info, and build flags.
- **Mtime_Index**: A persistent data file storing the last-known modification timestamp, file size, and cache key for each source and header file that participated in a successful cache hit or store operation.
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

1. WHEN a source file and all its dependency headers have mtime and file size values exactly matching the Mtime_Index entries, THE Cache_Pipeline SHALL skip SHA-256 hash computation and serve the cached object file identified by the cache key stored in the corresponding Mtime_Index entry.
2. WHEN a source file or any of its dependency headers has an mtime or file size that differs from the Mtime_Index entry, THE Cache_Pipeline SHALL fall through to the standard SHA-256 cache key computation.
3. WHEN no Mtime_Index entry exists for a source file, THE Cache_Pipeline SHALL fall through to the standard SHA-256 cache key computation.
4. WHEN a cache hit occurs via the Fast_Path, THE Cache_Pipeline SHALL record the operation as a cache hit in the CacheStats structure.
5. WHEN a successful cache store or cache hit via full hash computation occurs, THE Cache_Pipeline SHALL update the Mtime_Index with the current mtime, file size, and computed cache key of the source file and all its dependency headers.
6. THE Mtime_Index SHALL persist across builds by writing to a file in the `.cdo/cache/` directory.
7. WHEN the Mtime_Index file does not exist or fails to parse, THE Cache_Pipeline SHALL treat all entries as missing and fall through to full hash computation without producing an error.
8. IF the PAL mtime retrieval fails for a file, THEN THE Cache_Pipeline SHALL treat that file as changed and fall through to full hash computation.
9. IF the Fast_Path resolves a cache key from the Mtime_Index but the corresponding object file does not exist in the Build_Cache, THEN THE Cache_Pipeline SHALL remove the stale Mtime_Index entry and fall through to full SHA-256 cache key computation.
10. THE Mtime_Index SHALL compare mtime values using the exact integer timestamp returned by the PAL layer without applying any tolerance or rounding.
11. THE Mtime_Index SHALL be scoped per build profile, so that switching between profiles (e.g., debug and release) maintains separate mtime entries and cache keys for each profile without invalidating the other profile's index.

### Requirement 2: Minimum Filesize Threshold

**User Story:** As a developer, I want files smaller than a configurable threshold to bypass cache lookup entirely, so that the overhead of hashing is not spent on files that are cheaper to recompile than to hash.

#### Acceptance Criteria

1. THE CacheConfig SHALL include a `min_file_size` field representing the minimum source file size in bytes below which cache lookup is skipped.
2. WHEN a source file has a size strictly less than the Filesize_Threshold, THE Cache_Pipeline SHALL skip both mtime checking and SHA-256 hash computation for that file and proceed directly to compilation.
3. WHEN a source file has a size strictly less than the Filesize_Threshold, THE Cache_Pipeline SHALL NOT store the resulting object file in the Build_Cache.
4. IF no `min-file-size` value is specified in `cdo.toml`, THEN THE CacheConfig SHALL default the Filesize_Threshold to 512 bytes.
5. WHEN the Filesize_Threshold is set to 0, THE Cache_Pipeline SHALL disable the threshold and perform cache operations on all files regardless of size.
6. THE `cdo.toml` SHALL support a `min-file-size` key under `[workspace.settings.cache]` accepting a non-negative integer value in bytes with a maximum accepted value of 2,147,483,647.
7. IF the file size cannot be determined for a source file during threshold evaluation, THEN THE Cache_Pipeline SHALL treat that file as above the threshold and proceed with the normal cache lookup pipeline.

### Requirement 3: Consolidated Cache Log Output

**User Story:** As a developer, I want a single summary log message for cache results instead of per-batch messages, so that build output is concise and informative.

#### Acceptance Criteria

1. WHEN a build completes and the Build_Cache was active and at least one file underwent cache lookup (hits + misses > 0) or was skipped due to the Filesize_Threshold, THE CDo build command SHALL emit one aggregate INFO-level log message summarizing total cache hits, total cache misses, and total files skipped across all batches.
2. THE aggregate cache log message SHALL follow the format: `"Cache: <hits> hit(s), <misses> miss(es), <skipped> skipped (below threshold)"` where `<hits>` is the total number of files served from cache, `<misses>` is the total number of files that required compilation after a cache miss, and `<skipped>` is the total number of files that bypassed cache lookup due to the Filesize_Threshold.
3. WHILE the Build_Cache is active, THE Cache_Pipeline SHALL NOT emit per-batch INFO-level log messages about cache results, including the "All N file(s) served from cache" message.
4. WHILE TRACE-level logging is enabled via the `--trace` flag or equivalent log level configuration, THE Cache_Pipeline SHALL emit per-file cache hit and miss messages at TRACE level.
5. WHEN a build completes with zero cache interactions (cache disabled via configuration, or the `--no-cache` flag is set), THE CDo build command SHALL NOT emit the aggregate cache log message.
6. IF the Filesize_Threshold causes all files to be skipped and no files undergo cache lookup (hits = 0 and misses = 0 but skipped > 0), THEN THE CDo build command SHALL still emit the aggregate cache log message.

### Requirement 4: Mtime Index Integrity

**User Story:** As a developer, I want the mtime index to remain consistent even after interrupted builds or manual cache clears, so that the fast-path does not serve stale cached objects.

#### Acceptance Criteria

1. WHEN the Build_Cache is cleared via `cdo cache clear`, THE CDo cache command SHALL also delete the Mtime_Index file.
2. WHEN a compilation fails for a source file, THE Cache_Pipeline SHALL NOT update the Mtime_Index entry for that source file or any of its dependency headers involved in that compilation unit.
3. THE Mtime_Index SHALL store entries keyed by absolute path of each source and header file.
4. WHEN the Cache_Pipeline performs a lookup for a source file and a file tracked in the Mtime_Index no longer exists on the filesystem, THE Cache_Pipeline SHALL treat the entry as a miss and remove the stale entry from the index.
5. THE Cache_Pipeline SHALL write Mtime_Index updates to a temporary file and then replace the existing Mtime_Index file via a single rename operation, so that an interrupted build leaves either the previous complete index or the new complete index on disk.
6. IF the Mtime_Index file contains a partial or zero-length write (indicating a previously interrupted rename failed), THEN THE Cache_Pipeline SHALL treat the index as corrupted, discard it, and fall through to full hash computation for all files.

### Requirement 5: Configuration Integration

**User Story:** As a developer, I want to configure the new cache optimizations through the existing `cdo.toml` configuration, so that the behavior is consistent with other CDo settings.

#### Acceptance Criteria

1. THE `cdo.toml` SHALL support a `fast-path` boolean key under `[workspace.settings.cache]` to enable or disable the mtime-based Fast_Path (default: true).
2. THE `cdo.toml` SHALL support a `min-file-size` integer key under `[workspace.settings.cache]` to configure the Filesize_Threshold in bytes, accepting values from 0 to 2^63 (signed 64bits integer) inclusive (default: 512).
3. WHEN `fast-path` is set to false, THE Cache_Pipeline SHALL skip mtime checking and always perform full SHA-256 hash computation for cache lookups, while still applying the Filesize_Threshold check before any hash computation.
4. THE CacheConfig structure SHALL be extended with fields for `fast_path_enabled` (bool, default true) and `min_file_size` (int64_t, default 512) parsed from `cdo.toml`.
5. IF an invalid value is provided for `min-file-size` (negative number, non-integer, or value exceeded), THEN THE configuration parser SHALL log a warning indicating the invalid value and use the default value of 512.
6. IF an invalid value is provided for `fast-path` (non-boolean), THEN THE configuration parser SHALL log a warning indicating the invalid value and use the default value of true.

### Requirement 6: CacheStats Extension for Threshold Skips

**User Story:** As a developer, I want visibility into how many files were skipped due to the filesize threshold, so that I can tune the threshold effectively.

#### Acceptance Criteria

1. THE CacheStats structure SHALL include a `skipped` field of type `int` that counts files bypassed due to the Filesize_Threshold.
2. WHEN a source file is below the Filesize_Threshold and bypasses cache lookup, THE Cache_Pipeline SHALL increment the `skipped` counter in CacheStats by 1.
3. WHEN the `cdo cache stats` command is executed, THE CDo cache command SHALL display the configured Filesize_Threshold value in bytes as an INFO-level log message.
4. IF the Filesize_Threshold is set to 0, THEN THE CDo cache command SHALL display the threshold as "0 (disabled)" to indicate that size-based skipping is inactive.

### Requirement 7: Artifact Link Caching

**User Story:** As a developer, I want the linker step to be skipped when all input object files are unchanged, so that re-linking unchanged executables, libraries, and shared libraries does not waste time.

#### Acceptance Criteria

1. WHEN all object files that participate in a link step have mtime values that are older than the existing output artifact (exe, lib, dll, or shader output), THE build system SHALL skip the link step for that artifact.
2. WHEN any object file that participates in a link step has an mtime newer than the existing output artifact, THE build system SHALL re-execute the link step.
3. WHEN the output artifact does not exist on disk, THE build system SHALL always execute the link step regardless of object file timestamps.
4. WHEN a link step is skipped due to all inputs being up-to-date, THE build system SHALL log a DEBUG-level message indicating the artifact was up-to-date.
5. THE artifact freshness check SHALL also consider the mtime of linked dependency library artifacts (e.g., a crate's `.lib` dependency), so that re-linking occurs when a dependency was rebuilt.
6. THE artifact caching behavior SHALL apply to all linked module kinds: MODULE_EXE, MODULE_DYN, MODULE_TST, MODULE_E2E, and MODULE_LIB (the archive/static library step).
7. WHEN shader compilation produces output files (`.dxil`, `.spv`), THE build system SHALL skip recompilation when the shader source file mtime is older than the output file mtime.

### Requirement 8: Parallel Cache Key Computation

**User Story:** As a developer, I want cache key computation (hashing) to happen in parallel across worker threads, so that multi-core machines are utilized during cache lookups and the main thread is not blocked by serial hash computation.

#### Acceptance Criteria

1. WHEN the `--jobs` option specifies N > 1 worker threads, THE Cache_Pipeline SHALL distribute cache key computation (SHA-256 hashing of source files and headers) across up to N threads in parallel.
2. WHEN `--jobs` is 1 or not specified (serial mode), THE Cache_Pipeline SHALL perform cache key computation on the main compilation thread as it does today.
3. THE parallel cache key computation SHALL produce identical cache keys to the serial implementation for the same inputs.
4. WHEN parallel cache computation completes for a batch, THE Cache_Pipeline SHALL partition files into cache-hit (serve from cache) and cache-miss (need compilation) sets before dispatching compilation jobs.
5. THE parallel hash computation SHALL be thread-safe: concurrent reads of source files and Mtime_Index lookups SHALL NOT produce data races or corrupted state.
6. WHEN the Fast_Path (mtime check) resolves a file without hashing, THE Cache_Pipeline SHALL NOT submit that file to the parallel hash worker pool, reducing the work dispatched to threads.
