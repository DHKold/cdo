/**
 * spinner.c - Spinner dynamic zone.
 *
 * TTY mode: uses cursor movement to animate frames.
 * Non-TTY mode: emits a single line with the label.
 */
#include "out_internal.h"
#include "../../api/cli_errors.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================= */
/* Internal: Spinner state structure.                                         */
/* ========================================================================= */

struct CliSpinner {
    CliOutCtx*  ctx;
    char*       label;
    int         interval_ms;
    bool        is_tty;
};

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

CliSpinner* cli_out_spinner_start(CliOutCtx* ctx, const char* label, int interval_ms) {
    if (!ctx) return NULL;

    CliSpinner* spinner = (CliSpinner*)malloc(sizeof(CliSpinner));
    if (!spinner) return NULL;

    spinner->ctx = ctx;
    spinner->label = label ? strdup(label) : NULL;
    spinner->interval_ms = interval_ms > 0 ? interval_ms : 100;
    spinner->is_tty = ctx->stdout_tty;

    /* Non-TTY: emit label as a single line immediately */
    if (!spinner->is_tty && label) {
        fprintf(stdout, "%s\n", label);
    }

    return spinner;
}

void cli_out_spinner_set_label(CliSpinner* spinner, const char* label) {
    if (!spinner) return;

    free(spinner->label);
    spinner->label = label ? strdup(label) : NULL;
}

void cli_out_spinner_stop(CliSpinner* spinner, const char* final_msg) {
    if (!spinner) return;

    if (final_msg) {
        fprintf(stdout, "%s\n", final_msg);
    }

    free(spinner->label);
    free(spinner);
}
