#include "cmd_build_internal.h"
#include "build/build_pipeline.h"
#include "core/log.h"

// ---------------------------------------------------------------------------
// cmd_build implementation
// ---------------------------------------------------------------------------

int cmd_build(const CliParseResult* result, void* ctx) {
    (void)ctx;  // ctx not needed by the C++ pipeline
    if (!result) {
        cdo_log_error("internal error: NULL parse result passed to build command");
        return 1;
    }

    // Delegate to the C++ build pipeline
    return cdo_build_run(result);
}
