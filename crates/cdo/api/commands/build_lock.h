#ifndef CDO_COMMANDS_BUILD_LOCK_H
#define CDO_COMMANDS_BUILD_LOCK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque handle for an acquired build lock.
typedef struct BuildLock BuildLock;

/// Acquire the build lock for the workspace rooted at `workspace_root`.
/// Creates/opens `.cdo/build.lock` and attempts exclusive lock.
/// Writes PID and timestamp into the lock file.
///
/// @param workspace_root  Path to workspace root directory
/// @param timeout_sec     Timeout in seconds (0 = fail immediately, default 30)
/// @param lock_out        On success, receives the lock handle
/// @return 0 on success, PAL_ERR_TIMEOUT on timeout, PAL_ERR_IO on error
int build_lock_acquire(const char* workspace_root, int timeout_sec, BuildLock** lock_out);

/// Release a previously acquired build lock.
/// Closes the file handle (OS releases the lock automatically).
/// The lock file is left on disk for diagnostics but the lock is released.
void build_lock_release(BuildLock* lock);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_BUILD_LOCK_H
