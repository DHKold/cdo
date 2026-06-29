/**
 * progress.c - Progress bar dynamic zone.
 *
 * TTY mode: renders bar with \r for in-place updates.
 * Non-TTY mode: emits milestone lines at 25%, 50%, 75%, 100%.
 */
#include "out_internal.h"
#include "../../api/cli_errors.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================= */
/* Internal: Progress bar state structure.                                    */
/* ========================================================================= */

struct CliProgressBar {
    CliOutCtx*  ctx;
    char*       label;
    int         total;
    int         completed;
    bool        is_tty;
    int         last_milestone; /* Last emitted milestone percentage (non-TTY) */
};

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

CliProgressBar* cli_out_progress_create(CliOutCtx* ctx, const char* label, int total) {
    if (!ctx) return NULL;

    CliProgressBar* bar = (CliProgressBar*)malloc(sizeof(CliProgressBar));
    if (!bar) return NULL;

    bar->ctx = ctx;
    bar->label = label ? strdup(label) : NULL;
    bar->total = total > 0 ? total : 1;
    bar->completed = 0;
    bar->is_tty = ctx->stdout_tty;
    bar->last_milestone = 0;

    return bar;
}

void cli_out_progress_update(CliProgressBar* bar, int completed) {
    if (!bar) return;

    bar->completed = completed;

    int percent = (bar->completed * 100) / bar->total;
    if (percent > 100) percent = 100;

    if (bar->is_tty) {
        /* TTY: in-place update with carriage return */
        fprintf(stdout, "\r%s [%3d%%]", bar->label ? bar->label : "", percent);
        fflush(stdout);
    } else {
        /* Non-TTY: emit at 25% milestones */
        int milestone = (percent / 25) * 25;
        if (milestone > bar->last_milestone) {
            fprintf(stdout, "%s: %d%%\n", bar->label ? bar->label : "Progress", milestone);
            bar->last_milestone = milestone;
        }
    }
}

void cli_out_progress_finish(CliProgressBar* bar) {
    if (!bar) return;

    if (bar->is_tty) {
        fprintf(stdout, "\r%s [100%%]\n", bar->label ? bar->label : "");
    } else {
        if (bar->last_milestone < 100) {
            fprintf(stdout, "%s: 100%%\n", bar->label ? bar->label : "Progress");
        }
    }

    free(bar->label);
    free(bar);
}
