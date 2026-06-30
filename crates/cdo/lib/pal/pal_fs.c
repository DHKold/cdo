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

int pal_file_mtime(const char* path, uint64_t* mtime_ns) {
    if (!path || !mtime_ns) return PAL_ERR_IO;

    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath) return PAL_ERR_IO;

    WIN32_FILE_ATTRIBUTE_DATA attr;
    BOOL ok = GetFileAttributesExW(wpath, GetFileExInfoStandard, &attr);
    free(wpath);

    if (!ok) return PAL_ERR_NOT_FOUND;

    // FILETIME is 100-nanosecond intervals since 1601-01-01.
    // Convert to nanoseconds since Unix epoch (1970-01-01).
    // Difference between 1601 and 1970 in 100ns intervals:
    static const uint64_t EPOCH_DIFF = 116444736000000000ULL;
    uint64_t ft = ((uint64_t)attr.ftLastWriteTime.dwHighDateTime << 32)
                | (uint64_t)attr.ftLastWriteTime.dwLowDateTime;

    if (ft < EPOCH_DIFF) {
        *mtime_ns = 0;
    } else {
        // Convert 100ns intervals to nanoseconds
        *mtime_ns = (ft - EPOCH_DIFF) * 100;
    }
    return PAL_OK;
}

int pal_file_info(const char* path, uint64_t* mtime_ns, int64_t* file_size) {
    if (!path || !mtime_ns || !file_size) return PAL_ERR_IO;

    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath) return PAL_ERR_IO;

    WIN32_FILE_ATTRIBUTE_DATA attr;
    BOOL ok = GetFileAttributesExW(wpath, GetFileExInfoStandard, &attr);
    free(wpath);

    if (!ok) return PAL_ERR_NOT_FOUND;

    // Convert FILETIME to nanoseconds since Unix epoch
    static const uint64_t EPOCH_DIFF = 116444736000000000ULL;
    uint64_t ft = ((uint64_t)attr.ftLastWriteTime.dwHighDateTime << 32)
                | (uint64_t)attr.ftLastWriteTime.dwLowDateTime;

    if (ft < EPOCH_DIFF) {
        *mtime_ns = 0;
    } else {
        *mtime_ns = (ft - EPOCH_DIFF) * 100;
    }

    *file_size = (int64_t)(((uint64_t)attr.nFileSizeHigh << 32) | (uint64_t)attr.nFileSizeLow);
    return PAL_OK;
}

int pal_dir_walk(const char* path,
                 void(*callback)(const char* entry_path, bool is_dir, void* ctx),
                 void* ctx) {
    if (!path || !callback) return PAL_ERR_IO;

    // Build search pattern: path\*
    size_t path_len = strlen(path);
    char* pattern = (char*)malloc(path_len + 3); // path + \* + null
    if (!pattern) return PAL_ERR_IO;
    memcpy(pattern, path, path_len);
    pattern[path_len] = '\\';
    pattern[path_len + 1] = '*';
    pattern[path_len + 2] = '\0';

    wchar_t* wpattern = utf8_to_wide(pattern);
    free(pattern);
    if (!wpattern) return PAL_ERR_IO;

    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(wpattern, &find_data);
    free(wpattern);

    if (hFind == INVALID_HANDLE_VALUE) {
        return PAL_ERR_NOT_FOUND;
    }

    int result = PAL_OK;
    do {
        // Skip . and ..
        if (wcscmp(find_data.cFileName, L".") == 0 ||
            wcscmp(find_data.cFileName, L"..") == 0) {
            continue;
        }

        char* name = wide_to_utf8(find_data.cFileName);
        if (!name) { result = PAL_ERR_IO; break; }

        // Build full path: path/name
        size_t name_len = strlen(name);
        size_t full_len = path_len + 1 + name_len;
        char* full_path = (char*)malloc(full_len + 1);
        if (!full_path) { free(name); result = PAL_ERR_IO; break; }

        memcpy(full_path, path, path_len);
        full_path[path_len] = '/';
        memcpy(full_path + path_len + 1, name, name_len);
        full_path[full_len] = '\0';
        free(name);

        bool is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        callback(full_path, is_dir, ctx);

        // Recurse into subdirectories
        if (is_dir) {
            int sub_result = pal_dir_walk(full_path, callback, ctx);
            if (sub_result != PAL_OK) {
                free(full_path);
                result = sub_result;
                break;
            }
        }

        free(full_path);
    } while (FindNextFileW(hFind, &find_data));

    FindClose(hFind);
    return result;
}

int pal_mkdir_p(const char* path) {
    if (!path || path[0] == '\0') return PAL_ERR_IO;

    // Work on a mutable copy
    size_t len = strlen(path);
    char* tmp = (char*)malloc(len + 1);
    if (!tmp) return PAL_ERR_IO;
    memcpy(tmp, path, len + 1);

    // Normalize separators to backslash for Windows
    for (size_t i = 0; i < len; i++) {
        if (tmp[i] == '/') tmp[i] = '\\';
    }

    // Create each directory component
    for (size_t i = 0; i < len; i++) {
        if (tmp[i] == '\\' && i > 0) {
            // Skip drive letter colon (e.g., C:\)
            if (i == 2 && tmp[1] == ':') continue;

            tmp[i] = '\0';
            wchar_t* wdir = utf8_to_wide(tmp);
            if (wdir) {
                CreateDirectoryW(wdir, NULL);
                // Ignore ERROR_ALREADY_EXISTS
                free(wdir);
            }
            tmp[i] = '\\';
        }
    }

    // Create the final directory
    wchar_t* wfinal = utf8_to_wide(tmp);
    free(tmp);
    if (!wfinal) return PAL_ERR_IO;

    BOOL ok = CreateDirectoryW(wfinal, NULL);
    free(wfinal);

    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS) return PAL_OK;
        return PAL_ERR_IO;
    }
    return PAL_OK;
}

int pal_rmdir_r(const char* path) {
    if (!path) return PAL_ERR_IO;

    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath) return PAL_ERR_IO;

    DWORD attrs = GetFileAttributesW(wpath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        free(wpath);
        return PAL_ERR_NOT_FOUND;
    }

    // If it's a file, just delete it
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        BOOL ok = DeleteFileW(wpath);
        free(wpath);
        return ok ? PAL_OK : PAL_ERR_IO;
    }
    free(wpath);

    // Build search pattern: path\*
    size_t path_len = strlen(path);
    char* pattern = (char*)malloc(path_len + 3);
    if (!pattern) return PAL_ERR_IO;
    memcpy(pattern, path, path_len);
    pattern[path_len] = '\\';
    pattern[path_len + 1] = '*';
    pattern[path_len + 2] = '\0';

    wchar_t* wpattern = utf8_to_wide(pattern);
    free(pattern);
    if (!wpattern) return PAL_ERR_IO;

    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(wpattern, &find_data);
    free(wpattern);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(find_data.cFileName, L".") == 0 ||
                wcscmp(find_data.cFileName, L"..") == 0) {
                continue;
            }

            char* name = wide_to_utf8(find_data.cFileName);
            if (!name) continue;

            size_t name_len = strlen(name);
            size_t full_len = path_len + 1 + name_len;
            char* full_path = (char*)malloc(full_len + 1);
            if (!full_path) { free(name); continue; }

            memcpy(full_path, path, path_len);
            full_path[path_len] = '\\';
            memcpy(full_path + path_len + 1, name, name_len);
            full_path[full_len] = '\0';
            free(name);

            // Recurse
            pal_rmdir_r(full_path);
            free(full_path);
        } while (FindNextFileW(hFind, &find_data));
        FindClose(hFind);
    }

    // Remove the now-empty directory
    wchar_t* wdir = utf8_to_wide(path);
    if (!wdir) return PAL_ERR_IO;
    BOOL ok = RemoveDirectoryW(wdir);
    free(wdir);
    return ok ? PAL_OK : PAL_ERR_IO;
}

int pal_path_exists(const char* path) {
    if (!path || path[0] == '\0') return PAL_ERR_NOT_FOUND;
    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath) return PAL_ERR_NOT_FOUND;
    DWORD attrs = GetFileAttributesW(wpath);
    free(wpath);
    return (attrs != INVALID_FILE_ATTRIBUTES) ? PAL_OK : PAL_ERR_NOT_FOUND;
}

int pal_file_read(const char* path, char** buf, size_t* len) {
    if (!path || !buf || !len) return PAL_ERR_IO;

    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath) return PAL_ERR_IO;

    HANDLE hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);

    if (hFile == INVALID_HANDLE_VALUE) return PAL_ERR_NOT_FOUND;

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(hFile, &file_size)) {
        CloseHandle(hFile);
        return PAL_ERR_IO;
    }

    size_t size = (size_t)file_size.QuadPart;
    char* data = (char*)malloc(size + 1);
    if (!data) {
        CloseHandle(hFile);
        return PAL_ERR_IO;
    }

    DWORD bytes_read = 0;
    size_t total_read = 0;
    while (total_read < size) {
        DWORD to_read = (DWORD)((size - total_read) > 0xFFFFFFFF
                                ? 0xFFFFFFFF : (size - total_read));
        if (!ReadFile(hFile, data + total_read, to_read, &bytes_read, NULL) || bytes_read == 0) {
            free(data);
            CloseHandle(hFile);
            return PAL_ERR_IO;
        }
        total_read += bytes_read;
    }

    CloseHandle(hFile);
    data[size] = '\0'; // Null-terminate for convenience
    *buf = data;
    *len = size;
    return PAL_OK;
}

int pal_file_write(const char* path, const char* buf, size_t len) {
    if (!path || (!buf && len > 0)) return PAL_ERR_IO;

    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath) return PAL_ERR_IO;

    HANDLE hFile = CreateFileW(wpath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);

    if (hFile == INVALID_HANDLE_VALUE) return PAL_ERR_IO;

    size_t total_written = 0;
    while (total_written < len) {
        DWORD to_write = (DWORD)((len - total_written) > 0xFFFFFFFF
                                 ? 0xFFFFFFFF : (len - total_written));
        DWORD bytes_written = 0;
        if (!WriteFile(hFile, buf + total_written, to_write, &bytes_written, NULL)) {
            CloseHandle(hFile);
            return PAL_ERR_IO;
        }
        total_written += bytes_written;
    }

    CloseHandle(hFile);
    return PAL_OK;
}

#else
// ============================================================================
// POSIX Implementation (Linux / macOS)
// ============================================================================

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int pal_file_mtime(const char* path, uint64_t* mtime_ns) {
    if (!path || !mtime_ns) return PAL_ERR_IO;

    struct stat st;
    if (stat(path, &st) != 0) {
        return (errno == ENOENT) ? PAL_ERR_NOT_FOUND : PAL_ERR_IO;
    }

#if defined(__APPLE__)
    // macOS uses st_mtimespec
    *mtime_ns = (uint64_t)st.st_mtimespec.tv_sec * 1000000000ULL
              + (uint64_t)st.st_mtimespec.tv_nsec;
#elif defined(__linux__)
    // Linux uses st_mtim
    *mtime_ns = (uint64_t)st.st_mtim.tv_sec * 1000000000ULL
              + (uint64_t)st.st_mtim.tv_nsec;
#else
    // Fallback: seconds only
    *mtime_ns = (uint64_t)st.st_mtime * 1000000000ULL;
#endif
    return PAL_OK;
}

int pal_file_info(const char* path, uint64_t* mtime_ns, int64_t* file_size) {
    if (!path || !mtime_ns || !file_size) return PAL_ERR_IO;

    struct stat st;
    if (stat(path, &st) != 0) {
        return (errno == ENOENT) ? PAL_ERR_NOT_FOUND : PAL_ERR_IO;
    }

#if defined(__APPLE__)
    *mtime_ns = (uint64_t)st.st_mtimespec.tv_sec * 1000000000ULL
              + (uint64_t)st.st_mtimespec.tv_nsec;
#elif defined(__linux__)
    *mtime_ns = (uint64_t)st.st_mtim.tv_sec * 1000000000ULL
              + (uint64_t)st.st_mtim.tv_nsec;
#else
    *mtime_ns = (uint64_t)st.st_mtime * 1000000000ULL;
#endif
    *file_size = (int64_t)st.st_size;
    return PAL_OK;
}

int pal_dir_walk(const char* path,
                 void(*callback)(const char* entry_path, bool is_dir, void* ctx),
                 void* ctx) {
    if (!path || !callback) return PAL_ERR_IO;

    DIR* dir = opendir(path);
    if (!dir) {
        return (errno == ENOENT) ? PAL_ERR_NOT_FOUND : PAL_ERR_IO;
    }

    size_t path_len = strlen(path);
    struct dirent* entry;
    int result = PAL_OK;

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        size_t name_len = strlen(entry->d_name);
        size_t full_len = path_len + 1 + name_len;
        char* full_path = (char*)malloc(full_len + 1);
        if (!full_path) { result = PAL_ERR_IO; break; }

        memcpy(full_path, path, path_len);
        full_path[path_len] = '/';
        memcpy(full_path + path_len + 1, entry->d_name, name_len);
        full_path[full_len] = '\0';

        struct stat st;
        if (stat(full_path, &st) != 0) {
            free(full_path);
            continue; // Skip entries we can't stat
        }

        bool is_dir = S_ISDIR(st.st_mode);
        callback(full_path, is_dir, ctx);

        // Recurse into subdirectories
        if (is_dir) {
            int sub_result = pal_dir_walk(full_path, callback, ctx);
            if (sub_result != PAL_OK) {
                free(full_path);
                result = sub_result;
                break;
            }
        }

        free(full_path);
    }

    closedir(dir);
    return result;
}

int pal_mkdir_p(const char* path) {
    if (!path || path[0] == '\0') return PAL_ERR_IO;

    size_t len = strlen(path);
    char* tmp = (char*)malloc(len + 1);
    if (!tmp) return PAL_ERR_IO;
    memcpy(tmp, path, len + 1);

    // Create each directory component
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                free(tmp);
                return PAL_ERR_IO;
            }
            tmp[i] = '/';
        }
    }

    // Create the final directory
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        free(tmp);
        return PAL_ERR_IO;
    }

    free(tmp);
    return PAL_OK;
}

int pal_rmdir_r(const char* path) {
    if (!path) return PAL_ERR_IO;

    struct stat st;
    if (stat(path, &st) != 0) {
        return (errno == ENOENT) ? PAL_ERR_NOT_FOUND : PAL_ERR_IO;
    }

    // If it's a file, just unlink
    if (!S_ISDIR(st.st_mode)) {
        return (unlink(path) == 0) ? PAL_OK : PAL_ERR_IO;
    }

    DIR* dir = opendir(path);
    if (!dir) return PAL_ERR_IO;

    size_t path_len = strlen(path);
    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        size_t name_len = strlen(entry->d_name);
        size_t full_len = path_len + 1 + name_len;
        char* full_path = (char*)malloc(full_len + 1);
        if (!full_path) continue;

        memcpy(full_path, path, path_len);
        full_path[path_len] = '/';
        memcpy(full_path + path_len + 1, entry->d_name, name_len);
        full_path[full_len] = '\0';

        pal_rmdir_r(full_path);
        free(full_path);
    }

    closedir(dir);
    return (rmdir(path) == 0) ? PAL_OK : PAL_ERR_IO;
}

int pal_path_exists(const char* path) {
    if (!path || path[0] == '\0') return PAL_ERR_NOT_FOUND;
    struct stat st;
    return (stat(path, &st) == 0) ? PAL_OK : PAL_ERR_NOT_FOUND;
}

int pal_file_read(const char* path, char** buf, size_t* len) {
    if (!path || !buf || !len) return PAL_ERR_IO;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return (errno == ENOENT) ? PAL_ERR_NOT_FOUND : PAL_ERR_IO;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return PAL_ERR_IO;
    }

    size_t size = (size_t)st.st_size;
    char* data = (char*)malloc(size + 1);
    if (!data) {
        close(fd);
        return PAL_ERR_IO;
    }

    size_t total_read = 0;
    while (total_read < size) {
        ssize_t n = read(fd, data + total_read, size - total_read);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            free(data);
            close(fd);
            return PAL_ERR_IO;
        }
        total_read += (size_t)n;
    }

    close(fd);
    data[size] = '\0'; // Null-terminate for convenience
    *buf = data;
    *len = size;
    return PAL_OK;
}

int pal_file_write(const char* path, const char* buf, size_t len) {
    if (!path || (!buf && len > 0)) return PAL_ERR_IO;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return PAL_ERR_IO;

    size_t total_written = 0;
    while (total_written < len) {
        ssize_t n = write(fd, buf + total_written, len - total_written);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            close(fd);
            return PAL_ERR_IO;
        }
        total_written += (size_t)n;
    }

    close(fd);
    return PAL_OK;
}

#endif // _WIN32
