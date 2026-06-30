// crates/cdo/lib/core/cache_threshold.c
// Implementation of filesize threshold check for cache bypass.
// Implements: Requirements 2.2, 2.5, 2.7

#include "core/cache_threshold.h"

bool cache_threshold_skip(int64_t file_size, const CacheConfig* config) {
    if (!config) {
        return false;
    }

    // Requirement 2.5: threshold of 0 disables size-based skipping
    if (config->min_file_size == 0) {
        return false;
    }

    // Requirement 2.7: if file size is unknown (negative), treat as above threshold
    if (file_size < 0) {
        return false;
    }

    // Requirement 2.2: file strictly below threshold → skip cache
    if (file_size < config->min_file_size) {
        return true;
    }

    return false;
}
