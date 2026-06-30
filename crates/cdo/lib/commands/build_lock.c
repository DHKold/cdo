#include "commands/build_lock.h"
#include "core/log.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// BuildLock internal struct
// ---------------------------------------------------------------------------

struct BuildLock {
    PalFileLock* pal_lock;   // Underlying PAL lock handle
    char lock_path[260];     // Path to the lock file
};

// ---------------------------------------------------------------------------
// build_lock_acquire
// ---------------------------------------------------------------------------

int build_lock_acquire(const char* workspace_root, int timeout_sec, BuildLock** lock_out) {
    if (!workspace_root || !lock_out) return PAL_ERR_IO;

    *lock_out = NULL;

    // --- Re-entrancy check ---
    // If CDO_BUILD_LOCK_HELD is set to "1", a parent process already holds
    // the lock. Skip acquisition and return success with NULL lock handle.
    const char* held = getenv("CDO_BUILD_LOCK_HELD");
    if (held && strcmp(held, "1") == 0) {
        cdo_log_debug("build lock already held (re-entrant), skipping acquisition");
        return PAL_OK;
    }

    // --- Construct lock file path ---
    char cdo_dir[260];
    if (pal_path_join(cdo_dir, sizeof(cdo_dir), workspace_root, ".cdo") != 0) {
        return PAL_ERR_IO;
    }

    // Ensure .cdo directory exists
    int rc = pal_mkdir_p(cdo_dir);
    if (rc != 0) {
        cdo_log_error("failed to create .cdo directory: %s", cdo_dir);
        return PAL_ERR_IO;
    }

    char lock_path[260];
    if (pal_path_join(lock_path, sizeof(lock_path), cdo_dir, "build.lock") != 0) {
        return PAL_ERR_IO;
    }

    // --- Acquire exclusive file lock ---
    int timeout_ms = timeout_sec * 1000;
    PalFileLock* pal_lock = NULL;

    rc = pal_file_lock_exclusive(lock_path, timeout_ms, &pal_lock);
    if (rc != PAL_OK) {
        return rc;
    }

    // --- Write diagnostic JSON into lock file ---
    // Format: {"pid": <pid>, "started_at": "<ISO8601>", "command": "build"}
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
#else
    int pid = (int)getpid();
#endif

    // Get current time as ISO 8601
    time_t now = time(NULL);
    struct tm* utc = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", utc);

    char diag_json[512];
    snprintf(diag_json, sizeof(diag_json),
             "{\"pid\": %d, \"started_at\": \"%s\", \"command\": \"build\"}",
             (int)pid, timestamp);

    // Write diagnostic info to the lock file
    pal_file_write(lock_path, diag_json, strlen(diag_json));

    // --- Allocate and populate BuildLock struct ---
    BuildLock* lock = (BuildLock*)malloc(sizeof(BuildLock));
    if (!lock) {
        pal_file_lock_release(pal_lock);
        return PAL_ERR_IO;
    }

    lock->pal_lock = pal_lock;
    strncpy(lock->lock_path, lock_path, sizeof(lock->lock_path) - 1);
    lock->lock_path[sizeof(lock->lock_path) - 1] = '\0';

    *lock_out = lock;
    return PAL_OK;
}

// ---------------------------------------------------------------------------
// build_lock_release
// ---------------------------------------------------------------------------

void build_lock_release(BuildLock* lock) {
    if (!lock) return;

    pal_file_lock_release(lock->pal_lock);
    free(lock);
}
