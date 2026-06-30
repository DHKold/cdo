/**
 * handler_ctx.h - Context passed to all command handlers.
 */
#ifndef CDO_CORE_HANDLER_CTX_H
#define CDO_CORE_HANDLER_CTX_H

#include "out/cli_out.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Context passed to all command handlers.
typedef struct {
    CliOutCtx* out;  // Styled output context (process-lifetime)
} CdoHandlerCtx;

#ifdef __cplusplus
}
#endif

#endif /* CDO_CORE_HANDLER_CTX_H */
