/*
 * cdo_ut_capture.h — Per-test stdout/stderr capture for cdo_ut.
 *
 * Provides pipe-based redirection to isolate output from individual tests
 * during parallel execution. Each thread gets its own CdoUtCapture context.
 *
 * This is an internal header — not part of the public API.
 */

#ifndef CDO_UT_CAPTURE_H
#define CDO_UT_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   saved_stdout;
    int   saved_stderr;
    int   pipe_read;
    int   pipe_write;
    char *buffer;
    int   buffer_len;
} CdoUtCapture;

/*
 * Start capturing stdout/stderr.
 *
 * Saves the current stdout/stderr file descriptors, creates a pipe,
 * and redirects stdout/stderr to the write end of the pipe.
 *
 * On pipe creation failure, emits a protocol error and calls exit(1).
 *
 * Returns 0 on success.
 */
int cdo_ut_capture_start(CdoUtCapture *cap);

/*
 * End capture and store output in cap->buffer.
 *
 * Flushes stdout/stderr, restores original file descriptors,
 * reads all available data from the pipe read end into cap->buffer,
 * and closes pipe file descriptors.
 *
 * cap->buffer is null-terminated. cap->buffer_len is the length
 * excluding the null terminator.
 *
 * Returns 0 on success.
 */
int cdo_ut_capture_end(CdoUtCapture *cap);

/*
 * Free the captured buffer.
 *
 * Releases memory allocated by cdo_ut_capture_end().
 * Safe to call on a zeroed or already-freed CdoUtCapture.
 */
void cdo_ut_capture_free(CdoUtCapture *cap);

#ifdef __cplusplus
}
#endif

#endif /* CDO_UT_CAPTURE_H */
