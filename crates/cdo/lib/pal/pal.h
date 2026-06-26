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
int pal_is_tty(int fd);

/// Returns current monotonic time in milliseconds.
uint64_t pal_time_ms(void);

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
