/**
 * sha256_cache.h - Optional SHA-256 content-addressable cache layer.
 *
 * Provides an optional cache layer that checks/stores build artifacts in a
 * content-addressable store keyed by SHA-256 hash. When enabled, the layer
 * attempts to restore cached artifacts before task execution and stores
 * newly built artifacts after successful execution.
 *
 * The cache is stored in `.cdo/cache/objects/` with a two-character prefix
 * directory structure (e.g., `.cdo/cache/objects/ab/abcdef...o`).
 *
 * When disabled, all operations short-circuit without filesystem access,
 * allowing the build pipeline to function using only condition-based
 * incremental builds.
 *
 * Part of the cdo::build pipeline refactor.
 */
#ifndef CDO_BUILD_SHA256_CACHE_H
#define CDO_BUILD_SHA256_CACHE_H

#include <string>

namespace cdo::build {

// Forward declaration to avoid including task.h in this header.
class Task;

/// Optional SHA-256 content-addressable cache layer for cross-branch
/// and cross-machine artifact sharing.
///
/// When enabled, the layer intercepts task execution to check if a cached
/// artifact already exists for the task's inputs. On a hit, the cached
/// artifact is copied to the output path without invoking execute(). On a
/// miss after successful execution, the built artifact is stored in the
/// cache for future reuse.
///
/// When disabled (enabled=false in Config), all methods short-circuit
/// immediately without touching the filesystem.
class SHA256CacheLayer {
public:
    /// Configuration for the SHA-256 cache layer.
    struct Config {
        std::string cache_root;     ///< Root directory for cached objects, e.g. ".cdo/cache/objects/"
        bool enabled = false;       ///< Whether the cache layer is active
    };

    /// Construct the cache layer with the given configuration.
    explicit SHA256CacheLayer(Config config);

    /// Attempt a cache lookup before execution using the task's primary output hash.
    /// Returns true if a cache hit occurred (primary output artifact copied from cache).
    /// Returns false on cache miss or if the layer is disabled.
    /// Secondary outputs are NOT cached — they are regenerated on execution.
    bool tryRestore(const Task& task);

    /// Store the task's primary output in the cache after successful execution.
    /// Does nothing if the layer is disabled or if the primary output does not exist.
    void store(const Task& task);

    /// Returns the number of cache hits since construction.
    int hits() const;

    /// Returns the number of cache misses since construction.
    int misses() const;

private:
    Config config_;
    int hits_ = 0;
    int misses_ = 0;
};

} // namespace cdo::build

#endif // CDO_BUILD_SHA256_CACHE_H
