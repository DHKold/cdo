// sha256_cache.cpp — Optional SHA-256 content-addressable cache layer.
//
// Implements tryRestore() and store() for the SHA256CacheLayer. The cache
// key is computed by hashing the concatenation of all input file paths using
// the existing checksum_compute() utility (CHECKSUM_SHA256). The cache path
// format is: <cache_root>/<first_2_chars_of_hash>/<full_hash>.o
//
// When disabled (enabled=false), all methods short-circuit immediately
// without any filesystem access.

#include "build/sha256_cache.h"
#include "build/task.h"
#include "build/artifact.h"
#include "commons/checksum.h"
#include "pal/pal.h"

#include <string>
#include <cstring>

namespace cdo::build {

SHA256CacheLayer::SHA256CacheLayer(Config config)
    : config_(std::move(config)) {}

/// Compute the SHA-256 hash of all input paths concatenated.
/// Returns the 64-character hex digest as a string.
static std::string compute_cache_key(const Task& task) {
    std::string combined;
    for (const Artifact* input : task.inputs()) {
        combined += input->path();
    }

    char hex_digest[129] = {};
    checksum_compute(combined.data(), combined.size(), CHECKSUM_SHA256, hex_digest);
    return std::string(hex_digest);
}

/// Build the full cache file path: <cache_root>/<prefix>/<hash>.o
static std::string cache_path_for_key(const std::string& cache_root, const std::string& key) {
    // Format: cache_root/ab/abcdef...64chars.o
    std::string prefix = key.substr(0, 2);
    return cache_root + "/" + prefix + "/" + key + ".o";
}

bool SHA256CacheLayer::tryRestore(const Task& task) {
    if (!config_.enabled) {
        return false;
    }

    std::string key = compute_cache_key(task);
    std::string cached_path = cache_path_for_key(config_.cache_root, key);

    if (pal_path_exists(cached_path.c_str()) == 0) {
        // Cache hit: copy the cached artifact to the primary output path
        const std::string& output_path = task.primaryOutput().path();
        int rc = pal_file_copy(cached_path.c_str(), output_path.c_str());
        if (rc == 0) {
            hits_++;
            return true;
        }
        // Copy failed — fall through to miss
    }

    misses_++;
    return false;
}

void SHA256CacheLayer::store(const Task& task) {
    if (!config_.enabled) {
        return;
    }

    const std::string& output_path = task.primaryOutput().path();
    if (pal_path_exists(output_path.c_str()) != 0) {
        // Primary output doesn't exist — nothing to store
        return;
    }

    std::string key = compute_cache_key(task);
    std::string cached_path = cache_path_for_key(config_.cache_root, key);

    // Ensure the prefix directory exists
    std::string prefix_dir = config_.cache_root + "/" + key.substr(0, 2);
    pal_mkdir_p(prefix_dir.c_str());

    pal_file_copy(output_path.c_str(), cached_path.c_str());
}

int SHA256CacheLayer::hits() const {
    return hits_;
}

int SHA256CacheLayer::misses() const {
    return misses_;
}

} // namespace cdo::build
