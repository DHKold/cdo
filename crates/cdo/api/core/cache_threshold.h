#ifndef CDO_CORE_CACHE_THRESHOLD_H
#define CDO_CORE_CACHE_THRESHOLD_H

#include "model/cache_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Check if a source file should bypass the cache due to its size being below the threshold.
///
/// @param file_size    Size of the source file in bytes (-1 if unknown)
/// @param config       Cache configuration containing min_file_size threshold
/// @return true if the file should BYPASS cache (size < threshold and threshold > 0),
///         false if the file should proceed through normal cache pipeline
///
/// Special cases:
///   - If config->min_file_size == 0, threshold is disabled: always returns false
///   - If file_size < 0 (unknown), treats as above threshold: returns false
///   - If file_size < config->min_file_size, file is below threshold: returns true
bool cache_threshold_skip(int64_t file_size, const CacheConfig* config);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_CACHE_THRESHOLD_H
