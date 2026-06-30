#ifndef CDO_CORE_CACHE_LOG_H
#define CDO_CORE_CACHE_LOG_H

#include "model/cache_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Emit the consolidated cache summary log message at INFO level.
///
/// Format: "Cache: <hits> hit(s), <misses> miss(es), <skipped> skipped (below threshold)"
///
/// This should be called once after all compilation batches complete.
/// It does NOT emit if cache had no interactions (hits + misses + skipped == 0).
///
/// @param stats  The accumulated cache statistics across all batches
void cache_log_summary(const CacheStats* stats);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_CACHE_LOG_H
