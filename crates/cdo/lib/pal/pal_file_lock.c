#include "pal/pal.h"

#include <stdlib.h>

#ifdef _WIN32
// ============================================================================
// Windows Implementation
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Convert a UTF-8 path to a wide-char (UTF-16) path for Windows APIs.
// Returns a heap-allocated wide string; caller must free().
static wchar_t* utf8_to_wide(const char* utf8) {
    if (!utf8) return NULL;
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (needed <= 0) return NULL;
    wchar_t* wide = (wchar_t*)malloc((size_t)needed * sizeof(wchar_t));
    if (!wide) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, needed);
    return wide;
}

struct PalFileLock {
    HANDLE hFile;
};

int pal_file_lock_exclusive(const char* path, int timeout_ms, PalFileLock** lock_out) {
    if (!path || !lock_out) return PAL_ERR_IO;

    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath) return PAL_ERR_IO;

    // Open or create the lock file
    HANDLE hFile = CreateFileW(
        wpath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    free(wpath);
    if (hFile == INVALID_HANDLE_VALUE) return PAL_ERR_IO;

    // Attempt exclusive lock with retry loop
    OVERLAPPED ovl = {0};
    DWORD flags = LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY;
    uint64_t start = pal_time_ms();

    while (1) {
        if (LockFileEx(hFile, flags, 0, MAXDWORD, MAXDWORD, &ovl)) {
            // Lock acquired
            PalFileLock* lock = (PalFileLock*)malloc(sizeof(PalFileLock));
            if (!lock) {
                CloseHandle(hFile);
                return PAL_ERR_IO;
            }
            lock->hFile = hFile;
            *lock_out = lock;
            return PAL_OK;
        }

        // Check timeout
        uint64_t elapsed = pal_time_ms() - start;
        if (timeout_ms == 0 || (int)elapsed >= timeout_ms) {
            CloseHandle(hFile);
            return PAL_ERR_TIMEOUT;
        }

        // Sleep 50ms before retry
        Sleep(50);
    }
}

void pal_file_lock_release(PalFileLock* lock) {
    if (!lock) return;
    // Closing the handle releases the lock
    CloseHandle(lock->hFile);
    free(lock);
}

#else
// ============================================================================
// Unix Implementation
// ============================================================================

#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

struct PalFileLock {
    int fd;
};

int pal_file_lock_exclusive(const char* path, int timeout_ms, PalFileLock** lock_out) {
    if (!path || !lock_out) return PAL_ERR_IO;

    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return PAL_ERR_IO;

    uint64_t start = pal_time_ms();

    while (1) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            // Lock acquired
            PalFileLock* lock = (PalFileLock*)malloc(sizeof(PalFileLock));
            if (!lock) {
                close(fd);
                return PAL_ERR_IO;
            }
            lock->fd = fd;
            *lock_out = lock;
            return PAL_OK;
        }

        // If error is not EWOULDBLOCK, it's a real I/O error
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            close(fd);
            return PAL_ERR_IO;
        }

        // Check timeout
        uint64_t elapsed = pal_time_ms() - start;
        if (timeout_ms == 0 || (int)elapsed >= timeout_ms) {
            close(fd);
            return PAL_ERR_TIMEOUT;
        }

        // Sleep 50ms before retry
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 50000000 };
        nanosleep(&ts, NULL);
    }
}

void pal_file_lock_release(PalFileLock* lock) {
    if (!lock) return;
    // Closing the fd releases the flock
    close(lock->fd);
    free(lock);
}

#endif
