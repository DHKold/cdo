#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// Convert a wide-char (UTF-16) path to UTF-8.
// Returns a heap-allocated string; caller must free().
static char* wide_to_utf8(const wchar_t* wide) {
    if (!wide) return NULL;
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (needed <= 0) return NULL;
    char* utf8 = (char*)malloc((size_t)needed);
    if (!utf8) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, needed, NULL, NULL);
    return utf8;
}

int pal_get_executable_path(char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) return PAL_ERR_IO;

    // Use a wide buffer to get the executable path
    wchar_t wbuf[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, wbuf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return PAL_ERR_IO;

    // Convert wide path to UTF-8
    char* utf8_path = wide_to_utf8(wbuf);
    if (!utf8_path) return PAL_ERR_IO;

    size_t path_len = strlen(utf8_path);
    if (path_len >= buf_size) {
        free(utf8_path);
        return PAL_ERR_IO;
    }

    memcpy(buf, utf8_path, path_len + 1);
    free(utf8_path);
    return PAL_OK;
}

int pal_file_copy(const char* src, const char* dst) {
    if (!src || !dst) return PAL_ERR_IO;

    wchar_t* wsrc = utf8_to_wide(src);
    if (!wsrc) return PAL_ERR_IO;

    wchar_t* wdst = utf8_to_wide(dst);
    if (!wdst) {
        free(wsrc);
        return PAL_ERR_IO;
    }

    // FALSE = overwrite existing file
    BOOL ok = CopyFileW(wsrc, wdst, FALSE);
    free(wsrc);
    free(wdst);

    return ok ? PAL_OK : PAL_ERR_IO;
}

#else
// ============================================================================
// POSIX Implementation (Linux / macOS)
// ============================================================================

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

int pal_get_executable_path(char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) return PAL_ERR_IO;

#ifdef __APPLE__
    // macOS: use _NSGetExecutablePath
    uint32_t size = (uint32_t)buf_size;
    if (_NSGetExecutablePath(buf, &size) != 0) {
        return PAL_ERR_IO;
    }
    // _NSGetExecutablePath may return a relative path; resolve it
    char resolved[PATH_MAX];
    if (realpath(buf, resolved) == NULL) {
        return PAL_ERR_IO;
    }
    size_t resolved_len = strlen(resolved);
    if (resolved_len >= buf_size) {
        return PAL_ERR_IO;
    }
    memcpy(buf, resolved, resolved_len + 1);
#else
    // Linux: read /proc/self/exe symlink
    ssize_t len = readlink("/proc/self/exe", buf, buf_size - 1);
    if (len < 0) return PAL_ERR_IO;
    buf[len] = '\0';
#endif

    return PAL_OK;
}

int pal_file_copy(const char* src, const char* dst) {
    if (!src || !dst) return PAL_ERR_IO;

    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0) return PAL_ERR_IO;

    // Get source file permissions
    struct stat st;
    if (fstat(fd_src, &st) != 0) {
        close(fd_src);
        return PAL_ERR_IO;
    }

    // Create/overwrite destination with same permissions
    int fd_dst = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if (fd_dst < 0) {
        close(fd_src);
        return PAL_ERR_IO;
    }

    // Copy data in 4096-byte chunks
    char chunk[4096];
    ssize_t n;
    while ((n = read(fd_src, chunk, sizeof(chunk))) > 0) {
        size_t written = 0;
        while (written < (size_t)n) {
            ssize_t w = write(fd_dst, chunk + written, (size_t)n - written);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                close(fd_src);
                close(fd_dst);
                return PAL_ERR_IO;
            }
            written += (size_t)w;
        }
    }

    if (n < 0) {
        close(fd_src);
        close(fd_dst);
        return PAL_ERR_IO;
    }

    // Preserve executable bit from source
    if (fchmod(fd_dst, st.st_mode) != 0) {
        close(fd_src);
        close(fd_dst);
        return PAL_ERR_IO;
    }

    close(fd_src);
    close(fd_dst);
    return PAL_OK;
}

#endif // _WIN32
