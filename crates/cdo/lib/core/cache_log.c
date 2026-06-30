// crates/cdo/lib/core/cache_log.c
// Stub implementation for consolidated cache log output.
// TODO: Full implementation in task 7.3

#include "core/cache_log.h"
#include "core/log.h"

void cache_log_summary(const CacheStats* stats) {
    if (!stats) return;

    // Do not emit if no cache interactions occurred
    int total = stats->hits + stats->misses + stats->skipped;
    if (total == 0) return;

    cdo_log_info("Cache: %d hit(s), %d miss(es), %d skipped (below threshold)", stats->hits, stats->misses, stats->skipped);
}
