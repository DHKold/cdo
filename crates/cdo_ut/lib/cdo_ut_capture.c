/**
 * cdo_ut_capture.c — Per-test stdout/stderr capture using pipe redirection.
 *
 * Uses POSIX-like file descriptor functions available in w64devkit/MinGW:
 *   _dup, _dup2, _pipe, _read, _close, _write
 *
 * Each test gets its own CdoUtCapture context so parallel tests don't
 * interleave their output.
 */

#include "cdo_ut_capture.h"
#include "cdo_ut_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

// Pipe buffer size (64 KB should handle most test output)
#define CAPTURE_PIPE_SIZE 65536

// Initial read buffer allocation
#define CAPTURE_INITIAL_BUF 4096

// =============================================================================
// cdo_ut_capture_start — Save FDs, create pipe, redirect stdout/stderr.
// =============================================================================

int cdo_ut_capture_start(CdoUtCapture *cap)
{
    // Zero out the struct
    memset(cap, 0, sizeof(*cap));
    cap->saved_stdout = -1;
    cap->saved_stderr = -1;
    cap->pipe_read    = -1;
    cap->pipe_write   = -1;

    // Save current stdout and stderr file descriptors
    cap->saved_stdout = _dup(_fileno(stdout));
    if (cap->saved_stdout == -1) {
        cdo_ut_emit_error("Failed to create capture pipe");
        exit(1);
    }

    cap->saved_stderr = _dup(_fileno(stderr));
    if (cap->saved_stderr == -1) {
        _close(cap->saved_stdout);
        cdo_ut_emit_error("Failed to create capture pipe");
        exit(1);
    }

    // Create a pipe in binary mode
    int pipe_fds[2];
    if (_pipe(pipe_fds, CAPTURE_PIPE_SIZE, _O_BINARY) == -1) {
        _close(cap->saved_stdout);
        _close(cap->saved_stderr);
        cdo_ut_emit_error("Failed to create capture pipe");
        exit(1);
    }

    cap->pipe_read  = pipe_fds[0];
    cap->pipe_write = pipe_fds[1];

    // Redirect stdout to the write end of the pipe
    if (_dup2(cap->pipe_write, _fileno(stdout)) == -1) {
        _close(cap->pipe_read);
        _close(cap->pipe_write);
        _dup2(cap->saved_stdout, _fileno(stdout));
        _close(cap->saved_stdout);
        _close(cap->saved_stderr);
        cdo_ut_emit_error("Failed to create capture pipe");
        exit(1);
    }

    // Redirect stderr to the write end of the pipe
    if (_dup2(cap->pipe_write, _fileno(stderr)) == -1) {
        _dup2(cap->saved_stdout, _fileno(stdout));
        _close(cap->pipe_read);
        _close(cap->pipe_write);
        _close(cap->saved_stdout);
        _close(cap->saved_stderr);
        cdo_ut_emit_error("Failed to create capture pipe");
        exit(1);
    }

    return 0;
}

// =============================================================================
// cdo_ut_capture_end — Restore FDs, read captured output from pipe.
// =============================================================================

int cdo_ut_capture_end(CdoUtCapture *cap)
{
    // Flush stdout and stderr so all output goes into the pipe
    fflush(stdout);
    fflush(stderr);

    // Restore original stdout
    _dup2(cap->saved_stdout, _fileno(stdout));
    _close(cap->saved_stdout);
    cap->saved_stdout = -1;

    // Restore original stderr
    _dup2(cap->saved_stderr, _fileno(stderr));
    _close(cap->saved_stderr);
    cap->saved_stderr = -1;

    // Close the write end so reads will see EOF
    _close(cap->pipe_write);
    cap->pipe_write = -1;

    // Read all available data from the pipe read end
    int   capacity = CAPTURE_INITIAL_BUF;
    char *buf      = (char *)malloc(capacity);
    int   total    = 0;

    if (buf == NULL) {
        _close(cap->pipe_read);
        cap->pipe_read = -1;
        cap->buffer     = NULL;
        cap->buffer_len = 0;
        return -1;
    }

    for (;;) {
        // Ensure we have room to read
        if (total >= capacity - 1) {
            capacity *= 2;
            char *newbuf = (char *)realloc(buf, capacity);
            if (newbuf == NULL) {
                break; // Keep what we have
            }
            buf = newbuf;
        }

        int bytes_read = _read(cap->pipe_read, buf + total, capacity - total - 1);
        if (bytes_read <= 0) {
            break; // EOF or error
        }
        total += bytes_read;
    }

    // Null-terminate the buffer
    buf[total] = '\0';

    // Close the read end
    _close(cap->pipe_read);
    cap->pipe_read = -1;

    // Store results
    cap->buffer     = buf;
    cap->buffer_len = total;

    return 0;
}

// =============================================================================
// cdo_ut_capture_free — Release the captured buffer memory.
// =============================================================================

void cdo_ut_capture_free(CdoUtCapture *cap)
{
    if (cap->buffer != NULL) {
        free(cap->buffer);
        cap->buffer     = NULL;
        cap->buffer_len = 0;
    }
}
