#ifndef CDO_PAL_H
#define CDO_PAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Error Codes ---
// PAL functions return 0 on success, non-zero on failure.
#define PAL_OK            0
#define PAL_ERR_IO        1
#define PAL_ERR_TIMEOUT   2
#define PAL_ERR_NOT_FOUND 9

// --- Process Spawning ---
typedef struct {
    const char*     program;
    const char**    args;
    int             arg_count;
    const char**    env;        // NULL = inherit
    const char*     cwd;        // NULL = inherit
    bool            capture_output;
    int             timeout_ms; // 0 = use default (120000ms), -1 = no timeout
    const char*     raw_cmdline; // If non-NULL, used directly as lpCommandLine on Windows
                                 // (bypasses build_command_line quoting). Ignored on POSIX.
} PalSpawnOpts;

typedef struct {
    int     exit_code;
    char*   stdout_buf;     // if captured
    char*   stderr_buf;     // if captured
} PalSpawnResult;

int pal_spawn(const PalSpawnOpts* opts, PalSpawnResult* result);
int pal_spawn_async(const PalSpawnOpts* opts, int* pid_out);
int pal_wait(int pid, int* exit_code);
void pal_spawn_result_free(PalSpawnResult* result);

// --- Filesystem ---
int pal_file_mtime(const char* path, uint64_t* mtime_ns);
int pal_dir_walk(const char* path, void(*callback)(const char* path, bool is_dir, void* ctx), void* ctx);
int pal_mkdir_p(const char* path);
int pal_rmdir_r(const char* path);
int pal_path_exists(const char* path);
int pal_file_read(const char* path, char** buf, size_t* len);
int pal_file_write(const char* path, const char* buf, size_t len);

// --- System Info ---
int pal_cpu_count(void);
int pal_get_home_dir(char* buf, size_t buf_size);

/// Returns current monotonic time in milliseconds.
uint64_t pal_time_ms(void);

// --- File Locking ---

/// Opaque file lock handle.
typedef struct PalFileLock PalFileLock;

/// Acquire an exclusive lock on the file at `path`.
/// Creates the file if it doesn't exist.
/// Blocks up to `timeout_ms` milliseconds (0 = non-blocking try).
///
/// @param path        File path to lock
/// @param timeout_ms  Maximum wait time in milliseconds (0 = try once)
/// @param lock_out    On success, receives the lock handle
/// @return PAL_OK on success, PAL_ERR_TIMEOUT on timeout, PAL_ERR_IO on error
int pal_file_lock_exclusive(const char* path, int timeout_ms, PalFileLock** lock_out);

/// Release a previously acquired file lock.
/// Closes the underlying file handle, which releases the OS-level lock.
/// After this call, the lock handle is invalid.
void pal_file_lock_release(PalFileLock* lock);

// --- File Utilities ---

/// Get the absolute path of the currently running executable.
/// Returns 0 on success, PAL_ERR_IO on failure.
int pal_get_executable_path(char* buf, size_t buf_size);

/// Copy a file from src to dst. Creates dst if it doesn't exist,
/// overwrites if it does. Preserves executable permissions on Unix.
/// Returns 0 on success, non-zero on failure.
int pal_file_copy(const char* src, const char* dst);

// --- Path Utilities ---

/// Normalize a path in-place: convert backslashes to forward slashes,
/// collapse multiple consecutive slashes into one.
void pal_path_normalize(char* path);

/// Join two path components into dest with a '/' separator.
/// Handles trailing slashes on base and leading slashes on rel.
/// Returns 0 on success, non-zero if the result would exceed dest_size.
int pal_path_join(char* dest, size_t dest_size, const char* base, const char* rel);

/// Return a pointer to the file extension (including the dot) within path.
/// Returns a pointer to an empty string if no extension is found.
const char* pal_path_ext(const char* path);

#ifdef __cplusplus
}
#endif

#endif // CDO_PAL_H
