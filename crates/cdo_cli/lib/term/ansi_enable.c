/**
 * ansi_enable.c - ANSI virtual terminal processing enablement.
 *
 * Windows: calls SetConsoleMode with ENABLE_VIRTUAL_TERMINAL_PROCESSING.
 * POSIX: no-op (ANSI is natively supported).
 */
#include "../../api/term/cli_term.h"
#include "../../api/cli_errors.h"

#ifdef _WIN32
#include <windows.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

int cli_term_enable_ansi(void) {
    /* Enable VT processing on stdout */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return CLI_ERR_PLATFORM;

    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) {
        /* GetConsoleMode fails when stdout is redirected (not a real console).
         * In that case ANSI processing isn't needed, so treat as success. */
        return CLI_OK;
    }

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, mode)) return CLI_ERR_PLATFORM;

    /* Also enable on stderr (best-effort) */
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD err_mode = 0;
        if (GetConsoleMode(hErr, &err_mode)) {
            err_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hErr, err_mode);
        }
    }

    return CLI_OK;
}

#else /* POSIX */

int cli_term_enable_ansi(void) {
    /* ANSI escape sequences are natively supported on POSIX terminals. */
    return CLI_OK;
}

#endif
