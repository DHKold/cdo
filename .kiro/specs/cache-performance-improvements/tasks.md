# Implementation Plan: Cache Performance Improvements

## Overview

Implement performance optimizations to the CDo build cache pipeline: mtime-based fast-path validation, minimum filesize threshold, consolidated cache logs, artifact link caching, and parallel cache key computation. Follow TDD methodology: define interfaces first, write unit tests, then implement. Target >90% line coverage on all touched source files.

## Tasks

- [x] 1. Extend CacheConfig and CacheStats structures
  - [x] 1.1 Add `fast_path_enabled` and `min_file_size` fields to CacheConfig
    - Extend `model/cache_config.h` with `bool fast_path_enabled` (default: true) and `int64_t min_file_size` (default: 512)
    - _Requirements: 5.4_
  - [x] 1.2 Add `skipped` field to CacheStats
    - Extend `model/cache_config.h` CacheStats with `int skipped` field initialized to 0
    - _Requirements: 6.1_
  - [x] 1.3 Extend configuration parser for new cache fields
    - Parse `fast-path` (bool) and `min-file-size` (int64_t) from `[workspace.settings.cache]` in `cdo.toml`
    - Log warnings and use defaults for invalid values (non-boolean for fast-path, negative/non-integer/overflow for min-file-size)
    - _Requirements: 5.1, 5.2, 5.5, 5.6, 2.4, 2.6_
  - [x] 1.4 Write unit tests for configuration parsing
    - Test valid config, missing keys (defaults), invalid `fast-path` (non-boolean → warning + default true), invalid `min-file-size` (negative, non-integer, overflow → warning + default 512), threshold set to 0
    - Create `crates/cdo/tst/unit/test_cache_config.c`
    - _Requirements: 5.1, 5.2, 5.4, 5.5, 5.6, 2.4, 2.6_

- [x] 2. Implement MtimeIndex module
  - [x] 2.1 Define MtimeIndex interface header
    - Create `core/mtime_index.h` with the full interface as specified in design: `mtime_index_load`, `mtime_index_lookup`, `mtime_index_upsert`, `mtime_index_remove`, `mtime_index_save`, `mtime_index_free`, `mtime_index_delete`
    - Define `MtimeEntry` struct with path, mtime_ns, file_size, cache_key fields
    - _Requirements: 1.6, 4.3, 4.5_
  - [x] 2.2 Write unit tests for MtimeIndex CRUD and serialization
    - Test create, lookup (hit/miss), upsert (insert and update), remove (exists/no-op)
    - Test round-trip serialization: save then load produces equivalent entries
    - Test profile-scoped isolation: write to debug index, verify release index unaffected
    - Test corruption handling: zero-length file, bad magic bytes, truncated entries → treated as empty
    - Test atomic write: verify temp file created then renamed (not direct write)
    - Create `crates/cdo/tst/unit/test_mtime_index.c`
    - _Requirements: 1.6, 1.7, 1.10, 1.11, 4.3, 4.5, 4.6_
  - [x] 2.3 Implement MtimeIndex module
    - Create `core/mtime_index.c` implementing hash map keyed by absolute path
    - Binary format: magic "MTIX" + version 1 + entry count + packed entries
    - File location: `.cdo/cache/mtime_index_{profile}.bin`
    - Atomic write via write-to-tmp then rename
    - Handle corruption gracefully (discard, treat as empty)
    - _Requirements: 1.6, 1.7, 1.10, 1.11, 4.3, 4.5, 4.6_

- [x] 3. Checkpoint - MtimeIndex module
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Implement Fast-Path cache validation
  - [x] 4.1 Define fast-path interface
    - Create or extend the cache pipeline interface with a function that accepts a source file path, its dependency headers, the MtimeIndex, and the CacheConfig, returning a fast-path result (hit with cache key, or miss)
    - _Requirements: 1.1, 1.2, 1.3, 1.8, 1.9_
  - [x] 4.2 Write unit tests for fast-path logic
    - Test matching mtime+size → returns stored cache key, increments hits
    - Test mismatched mtime → falls through to hash
    - Test mismatched file size → falls through to hash
    - Test missing MtimeIndex entry → falls through to hash
    - Test stale cache key (object not in Build_Cache) → removes entry, falls through to hash
    - Test PAL mtime failure → treats as changed, falls through
    - Test deleted file tracked in index → treats as miss, removes stale entry
    - Test fast-path disabled (`fast_path_enabled = false`) → always falls through to hash
    - Create `crates/cdo/tst/unit/test_cache_fastpath.c`
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.8, 1.9, 4.4, 5.3_
  - [x] 4.3 Implement fast-path logic in cache pipeline
    - Integrate mtime checking into `compiler_compile_batch` flow
    - When fast-path hit: serve cached object, increment `CacheStats.hits`
    - When miss/stale/error: fall through to SHA-256 computation
    - Update MtimeIndex after successful full-hash cache hit or store
    - Do NOT update index on compilation failure
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.8, 1.9, 4.2, 4.4_

- [x] 5. Implement Filesize Threshold
  - [x] 5.1 Define threshold check interface
    - Add a function to evaluate whether a file should bypass cache based on its size vs the configured threshold
    - _Requirements: 2.1, 2.2, 2.5_
  - [x] 5.2 Write unit tests for filesize threshold
    - Test file at 511 bytes (below default 512) → skip cache, increment skipped
    - Test file at 512 bytes (at threshold) → proceed with cache pipeline
    - Test file at 513 bytes (above threshold) → proceed with cache pipeline
    - Test threshold = 0 → disabled, all files go through cache
    - Test file size cannot be determined → treat as above threshold
    - Test skipped files are NOT stored in cache
    - Create `crates/cdo/tst/unit/test_cache_threshold.c`
    - _Requirements: 2.2, 2.3, 2.5, 2.7, 6.2_
  - [x] 5.3 Implement threshold check in cache pipeline
    - Insert threshold check as first gate in `compiler_compile_batch` before mtime/hash
    - Skip cache lookup AND cache store for below-threshold files
    - Increment `CacheStats.skipped` for each skipped file
    - _Requirements: 2.2, 2.3, 6.2_

- [x] 6. Checkpoint - Fast-path and threshold
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Implement Consolidated Cache Logging
  - [x] 7.1 Define log consolidation interface
    - Add a function that takes a CacheStats and emits the summary log message
    - Define format: `"Cache: <hits> hit(s), <misses> miss(es), <skipped> skipped (below threshold)"`
    - _Requirements: 3.1, 3.2_
  - [x] 7.2 Write unit tests for cache log output
    - Test summary format with various hit/miss/skipped combinations
    - Test no log emitted when cache disabled (hits + misses + skipped = 0)
    - Test all files skipped (hits=0, misses=0, skipped>0) → still emit summary
    - Test per-batch INFO messages are removed
    - Test per-file TRACE messages are still emitted when trace enabled
    - Create `crates/cdo/tst/unit/test_cache_log.c`
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6_
  - [x] 7.3 Implement log consolidation
    - Remove existing per-batch `"All N file(s) served from cache"` INFO message
    - Emit single aggregate summary from build command after all batches complete
    - Keep per-file messages at TRACE level for `--trace`
    - Do not emit summary when cache is disabled or no interactions occurred
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6_

- [x] 8. Implement Artifact Link Caching
  - [x] 8.1 Define artifact freshness interface
    - Create `compiler_link_is_fresh` function signature in `core/compiler_link.c` (or appropriate header)
    - Accept output path and array of input paths (objects + dependency libs)
    - Return bool indicating whether link can be skipped
    - _Requirements: 7.1, 7.2, 7.3, 7.5_
  - [x] 8.2 Write unit tests for link freshness
    - Test all objects older than output → skip (return true)
    - Test one object newer than output → re-link (return false)
    - Test output artifact missing → always link (return false)
    - Test dependency library newer than output → re-link (return false)
    - Test shader source older than output → skip shader compilation
    - Test shader source newer than output → recompile shader
    - Test shader output missing → always compile
    - Create `crates/cdo/tst/unit/test_link_freshness.c` and `crates/cdo/tst/unit/test_shader_freshness.c`
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 7.7_
  - [x] 8.3 Implement artifact freshness check
    - Implement `compiler_link_is_fresh` comparing input mtimes against output mtime via PAL
    - Integrate check before `compiler_link` for MODULE_EXE, MODULE_DYN, MODULE_TST, MODULE_E2E, MODULE_LIB
    - Log DEBUG message when link is skipped ("artifact up-to-date")
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6_
  - [x] 8.4 Implement shader freshness check
    - Skip shader recompilation when source mtime < output mtime
    - Always compile when output (`.dxil`, `.spv`) is missing
    - _Requirements: 7.7_

- [x] 9. Checkpoint - Link and shader caching
  - Ensure all tests pass, ask the user if questions arise.

- [x] 10. Implement Parallel Cache Key Computation
  - [x] 10.1 Define parallel hash dispatch interface
    - Design the integration point in `compiler_compile_batch` for dispatching hash jobs to the existing ThreadPool
    - Define per-file result slots for thread-safe collection of computed keys
    - _Requirements: 8.1, 8.5_
  - [x] 10.2 Write unit tests for parallel hash computation
    - Test serial mode (jobs=1): hash computation on main thread, produces correct keys
    - Test parallel mode (jobs>1): same inputs produce identical keys to serial mode
    - Test fast-path resolved files are NOT submitted to hash pool
    - Create `crates/cdo/tst/unit/test_parallel_hash.c`
    - _Requirements: 8.1, 8.2, 8.3, 8.5, 8.6_
  - [x] 10.3 Implement parallel hash dispatch
    - In `compiler_compile_batch`: after threshold filter and mtime fast-path, submit remaining files to ThreadPool for SHA-256 computation
    - Collect results into pre-allocated per-file slots (no contention)
    - Fall back to serial computation if thread pool creation fails (log warning)
    - Partition results into cache-hit/miss sets before dispatching compilation
    - _Requirements: 8.1, 8.2, 8.4, 8.5, 8.6_

- [x] 11. Implement Cache Clear and Stats Integration
  - [x] 11.1 Extend `cdo cache clear` to delete MtimeIndex
    - When Build_Cache is cleared, also call `mtime_index_delete` for all profiles
    - _Requirements: 4.1_
  - [x] 11.2 Extend `cdo cache stats` for threshold display
    - Display configured Filesize_Threshold value
    - Show "0 (disabled)" when threshold is 0
    - _Requirements: 6.3, 6.4_
  - [x] 11.3 Write unit tests for cache clear and stats
    - Test `cdo cache clear` removes mtime index files
    - Test `cdo cache stats` displays threshold value
    - Test threshold = 0 displays "0 (disabled)"
    - Create `crates/cdo/tst/unit/test_cache_stats.c`
    - _Requirements: 4.1, 6.3, 6.4_

- [x] 12. Final checkpoint - Full integration
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Tasks marked with `?` may be partialy done but were canceled or failed and should be carefully analysed and completed
- Each task references specific requirements for traceability
- TDD approach: interfaces (step N.1) → tests (step N.2) → implementation (step N.3)
- No property-based testing per project conventions; use extensive unit testing for >90% line coverage
- All MtimeIndex operations use PAL conventions: 0 = success, non-zero = error
- Parallel hash falls back to serial on failure — never blocks the build

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1", "1.2"] },
    { "id": 1, "tasks": ["1.3", "2.1"] },
    { "id": 2, "tasks": ["1.4", "2.2"] },
    { "id": 3, "tasks": ["2.3"] },
    { "id": 4, "tasks": ["4.1", "5.1", "7.1", "8.1"] },
    { "id": 5, "tasks": ["4.2", "5.2", "7.2", "8.2"] },
    { "id": 6, "tasks": ["4.3", "5.3", "7.3", "8.3", "8.4"] },
    { "id": 7, "tasks": ["10.1", "11.1", "11.2"] },
    { "id": 8, "tasks": ["10.2", "11.3"] },
    { "id": 9, "tasks": ["10.3"] }
  ]
}
```
