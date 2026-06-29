/**
 * out_internal.h - Shared internal definition of the opaque CliOutCtx struct.
 *
 * Included by styled.c, fmt.c, and icon.c to access context fields directly.
 * This header is NOT part of the public API.
 */
#ifndef CDO_CLI_OUT_INTERNAL_H
#define CDO_CLI_OUT_INTERNAL_H

#include "../../api/out/cli_out.h"
#include "../../api/term/cli_term.h"

struct CliOutCtx {
    CliColorLevel color_level;
    bool          unicode;
    bool          stdout_tty;
    int           columns;
};

#endif /* CDO_CLI_OUT_INTERNAL_H */
