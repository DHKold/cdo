#include "pal.h"

#include <string.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <io.h>
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <pwd.h>
  #include <stdlib.h>
#endif

int pal_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int count = (int)si.dwNumberOfProcessors;
    return count > 0 ? count : 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

int pal_get_home_dir(char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        return -1;
    }

#ifdef _WIN32
    // Try USERPROFILE first
    const char* home = getenv("USERPROFILE");
    if (home && home[0] != '\0') {
        size_t len = strlen(home);
        if (len >= buf_size) {
            return -1;
        }
        memcpy(buf, home, len + 1);
        return 0;
    }

    // Fallback: combine HOMEDRIVE and HOMEPATH
    const char* drive = getenv("HOMEDRIVE");
    const char* path = getenv("HOMEPATH");
    if (drive && path) {
        size_t drive_len = strlen(drive);
        size_t path_len = strlen(path);
        if (drive_len + path_len >= buf_size) {
            return -1;
        }
        memcpy(buf, drive, drive_len);
        memcpy(buf + drive_len, path, path_len + 1);
        return 0;
    }

    return -1;
#else
    // Try HOME env var first
    const char* home = getenv("HOME");
    if (home && home[0] != '\0') {
        size_t len = strlen(home);
        if (len >= buf_size) {
            return -1;
        }
        memcpy(buf, home, len + 1);
        return 0;
    }

    // Fallback: getpwuid
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
        size_t len = strlen(pw->pw_dir);
        if (len >= buf_size) {
            return -1;
        }
        memcpy(buf, pw->pw_dir, len + 1);
        return 0;
    }

    return -1;
#endif
}

int pal_is_tty(int fd) {
#ifdef _WIN32
    return _isatty(fd) ? 1 : 0;
#else
    return isatty(fd) ? 1 : 0;
#endif
}
