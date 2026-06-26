#include "commands/cmd_clean.h"
#include "core/output.h"
#include "pal/pal.h"
#include <string.h>

#define BUILD_DIR "build"

/// Attempt to remove a directory. Returns 0 on success, 1 on failure.
/// Prints "Nothing to clean" if the path does not exist.
static int clean_path(const char* path) {
    int exists = pal_path_exists(path);
    if (exists != 1) {
        cdo_info("Nothing to clean");
        return 0;
    }

    int rc = pal_rmdir_r(path);
    if (rc != PAL_OK) {
        cdo_error("Failed to clean '%s'", path);
        return 1;
    }

    cdo_info("Cleaned '%s'", path);
    return 0;
}

int cmd_clean(const CdoOptions* opts) {
    if (opts->positional_count > 0) {
        // Clean specific crate(s)
        for (int i = 0; i < opts->positional_count; i++) {
            char path[1024];
            int join_rc = pal_path_join(path, sizeof(path), BUILD_DIR, opts->positional_args[i]);
            if (join_rc != 0) {
                cdo_error("Path too long for crate '%s'", opts->positional_args[i]);
                return 1;
            }
            int rc = clean_path(path);
            if (rc != 0) {
                return rc;
            }
        }
        return 0;
    }

    // Clean entire build directory
    return clean_path(BUILD_DIR);
}
