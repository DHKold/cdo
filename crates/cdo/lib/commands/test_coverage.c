#include "commands/test_coverage.h"
#include "core/log.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal: Walk context for collecting .gcda files
// ---------------------------------------------------------------------------

#define MAX_GCDA_FILES 1024

typedef struct {
    char paths[MAX_GCDA_FILES][260];
    int  count;
} GcdaFileList;

static void gcda_walk_callback(const char *path, bool is_dir, void *ctx) {
    if (is_dir) return;

    GcdaFileList *list = (GcdaFileList *)ctx;
    if (list->count >= MAX_GCDA_FILES) return;

    const char *ext = pal_path_ext(path);
    if (ext && strcmp(ext, ".gcda") == 0) {
        strncpy(list->paths[list->count], path, 259);
        list->paths[list->count][259] = '\0';
        list->count++;
    }
}

// ---------------------------------------------------------------------------
// Internal: Check if gcov is available
// ---------------------------------------------------------------------------

static int check_gcov_available(void) {
    PalSpawnOpts opts = {0};
    opts.program = "gcov";
    const char *args[] = {"--version"};
    opts.args = args;
    opts.arg_count = 1;
    opts.capture_output = true;
    opts.timeout_ms = 10000;

    PalSpawnResult result = {0};
    int rc = pal_spawn(&opts, &result);

    if (rc != 0) {
        pal_spawn_result_free(&result);
        return -1;
    }

    int exit_code = result.exit_code;
    pal_spawn_result_free(&result);

    if (exit_code != 0) {
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Internal: Parse gcov output for a single invocation
// ---------------------------------------------------------------------------

/// Parse gcov stdout output and extract file coverage information.
/// Looks for pairs of lines:
///   File 'path/to/file.c'
///   Lines executed:XX.XX% of N
/// Returns the number of file entries parsed from this output.
static int parse_gcov_output(const char *output, FileCoverage *out, int max_files,
                             int current_count) {
    if (!output || !out) return 0;

    int parsed = 0;
    const char *p = output;

    while (*p != '\0' && (current_count + parsed) < max_files) {
        // Look for "File '"
        const char *file_marker = strstr(p, "File '");
        if (!file_marker) break;

        // Extract file path between quotes
        const char *path_start = file_marker + 6; // skip "File '"
        const char *path_end = strchr(path_start, '\'');
        if (!path_end) break;

        size_t path_len = (size_t)(path_end - path_start);
        if (path_len >= 256) path_len = 255;

        // Move past the file line to look for "Lines executed:"
        const char *after_file = path_end + 1;
        const char *lines_marker = strstr(after_file, "Lines executed:");
        if (!lines_marker) {
            p = after_file;
            continue;
        }

        // Make sure the Lines marker is before the next File marker (or end)
        const char *next_file = strstr(after_file, "File '");
        if (next_file && lines_marker > next_file) {
            p = next_file;
            continue;
        }

        // Parse "Lines executed:XX.XX% of N"
        const char *pct_start = lines_marker + 15; // skip "Lines executed:"
        double pct = 0.0;
        int total = 0;

        // Parse percentage
        char *endptr = NULL;
        pct = strtod(pct_start, &endptr);
        if (endptr == pct_start) {
            p = lines_marker + 15;
            continue;
        }

        // Skip "% of "
        const char *of_marker = strstr(endptr, "% of ");
        if (!of_marker) {
            p = endptr;
            continue;
        }

        const char *total_start = of_marker + 5; // skip "% of "
        total = atoi(total_start);

        // Skip system/internal files (those starting with '<' or ending in no extension)
        // We only want real source files
        if (path_start[0] == '<') {
            p = total_start;
            continue;
        }

        // Store the result
        FileCoverage *fc = &out[current_count + parsed];
        memset(fc, 0, sizeof(FileCoverage));
        memcpy(fc->file, path_start, path_len);
        fc->file[path_len] = '\0';
        fc->lines_total = total;
        fc->pct = pct;
        fc->lines_hit = (total > 0) ? (int)(pct / 100.0 * total + 0.5) : 0;

        parsed++;

        // Move past this entry
        p = total_start;
    }

    return parsed;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int coverage_run_gcov(const char *build_dir, FileCoverage *out, int max_files) {
    if (!build_dir || !out || max_files <= 0) return -1;

    // Check if gcov is available
    if (check_gcov_available() != 0) {
        cdo_log_error("gcov not found in PATH. gcov is required for --coverage.");
        return -2;
    }

    // Walk the build directory to find all .gcda files
    GcdaFileList *gcda_files = (GcdaFileList *)calloc(1, sizeof(GcdaFileList));
    if (!gcda_files) {
        cdo_log_error("out of memory allocating gcda file list");
        return -1;
    }

    int rc = pal_dir_walk(build_dir, gcda_walk_callback, gcda_files);
    if (rc != 0) {
        cdo_log_debug("pal_dir_walk returned %d for '%s'", rc, build_dir);
        free(gcda_files);
        return -1;
    }

    if (gcda_files->count == 0) {
        cdo_log_debug("no .gcda files found in '%s'", build_dir);
        free(gcda_files);
        return 0;
    }

    cdo_log_debug("found %d .gcda files in '%s'", gcda_files->count, build_dir);

    int total_files = 0;

    // Run gcov on each .gcda file
    for (int i = 0; i < gcda_files->count && total_files < max_files; i++) {
        PalSpawnOpts spawn_opts = {0};
        spawn_opts.program = "gcov";

        // The walk produces paths relative to build_dir (e.g. "./build/debug/cdo/lib/foo.gcda").
        // Since we set cwd to build_dir, we must make the path relative to it.
        const char *gcda_path = gcda_files->paths[i];
        size_t bd_len = strlen(build_dir);
        if (strncmp(gcda_path, build_dir, bd_len) == 0 &&
            (gcda_path[bd_len] == '/' || gcda_path[bd_len] == '\\')) {
            gcda_path = gcda_path + bd_len + 1;
        }

        const char *args[1];
        args[0] = gcda_path;
        spawn_opts.args = args;
        spawn_opts.arg_count = 1;
        spawn_opts.capture_output = true;
        spawn_opts.timeout_ms = 30000;

        // Run gcov from the build directory so it finds the .gcno files
        spawn_opts.cwd = build_dir;

        PalSpawnResult spawn_result = {0};
        rc = pal_spawn(&spawn_opts, &spawn_result);

        if (rc != 0 || spawn_result.exit_code != 0) {
            cdo_log_debug("gcov failed for '%s' (rc=%d, exit=%d)",
                      gcda_files->paths[i], rc, spawn_result.exit_code);
            pal_spawn_result_free(&spawn_result);
            continue;
        }

        // Parse the gcov output
        if (spawn_result.stdout_buf) {
            int parsed = parse_gcov_output(spawn_result.stdout_buf, out, max_files,
                                           total_files);
            total_files += parsed;
        }

        pal_spawn_result_free(&spawn_result);
    }

    free(gcda_files);
    return total_files;
}

double coverage_aggregate(const FileCoverage *files, int count) {
    if (!files || count <= 0) return 0.0;

    int sum_total = 0;
    int sum_hit = 0;

    for (int i = 0; i < count; i++) {
        sum_total += files[i].lines_total;
        sum_hit += files[i].lines_hit;
    }

    if (sum_total == 0) return 0.0;

    return (double)sum_hit / (double)sum_total * 100.0;
}

// ---------------------------------------------------------------------------
// Filtered coverage: only workspace sources under <ws_root>/crates/
// ---------------------------------------------------------------------------

/// Normalize a path buffer in-place: backslash â†’ forward slash.
static void normalize_slashes(char *path) {
    for (char *p = path; *p != '\0'; p++) {
        if (*p == '\\') *p = '/';
    }
}

/// Check if a path is absolute (drive letter on Windows, or starts with /).
static bool is_absolute_path(const char *path) {
    if (!path || path[0] == '\0') return false;
#ifdef _WIN32
    // Drive letter: C:/ or C:\.
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
        return true;
    // UNC path
    if (path[0] == '\\' && path[1] == '\\') return true;
    if (path[0] == '/' && path[1] == '/') return true;
#endif
    if (path[0] == '/') return true;
    return false;
}

int coverage_run_gcov_filtered(const char *build_dir, const char *ws_root, FileCoverage *out, int max_files) {
    if (!build_dir || !ws_root || !out || max_files <= 0) return -1;

    // Get raw coverage results
    int raw_count = coverage_run_gcov(build_dir, out, max_files);
    if (raw_count <= 0) return raw_count;

    // Resolve ws_root to an absolute path so it matches gcov's absolute file paths.
    // gcov reports absolute source paths (e.g., C:/Workspace/.../crates/cdo/lib/foo.c)
    // but ws_root may be relative (e.g., ".").
    char abs_root[512];
#ifdef _WIN32
    if (_fullpath(abs_root, ws_root, sizeof(abs_root)) == NULL) {
        strncpy(abs_root, ws_root, sizeof(abs_root) - 1);
        abs_root[sizeof(abs_root) - 1] = '\0';
    }
#else
    char *rp = realpath(ws_root, NULL);
    if (rp) {
        strncpy(abs_root, rp, sizeof(abs_root) - 1);
        abs_root[sizeof(abs_root) - 1] = '\0';
        free(rp);
    } else {
        strncpy(abs_root, ws_root, sizeof(abs_root) - 1);
        abs_root[sizeof(abs_root) - 1] = '\0';
    }
#endif

    // Build the inclusion prefix: <abs_root>/crates/
    char prefix[512];
    strncpy(prefix, abs_root, sizeof(prefix) - 1);
    prefix[sizeof(prefix) - 1] = '\0';
    normalize_slashes(prefix);

    // Ensure prefix ends with /crates/
    size_t ws_len = strlen(prefix);
    // Remove trailing slash from ws_root if present
    if (ws_len > 0 && prefix[ws_len - 1] == '/') {
        prefix[ws_len - 1] = '\0';
        ws_len--;
    }

    // Append /crates/
    if (ws_len + 8 >= sizeof(prefix)) return -1; // overflow guard
    strcat(prefix, "/crates/");
    size_t prefix_len = strlen(prefix);
    // Filter in-place
    int kept = 0;
    for (int i = 0; i < raw_count; i++) {
        // Resolve to absolute path if relative
        char resolved[512];
        if (is_absolute_path(out[i].file)) {
            strncpy(resolved, out[i].file, sizeof(resolved) - 1);
            resolved[sizeof(resolved) - 1] = '\0';
        } else {
            if (pal_path_join(resolved, sizeof(resolved), ws_root, out[i].file) != 0) {
                continue; // skip on overflow
            }
        }

        // Normalize path separators to forward slash
        normalize_slashes(resolved);

        // Check if the resolved path starts with the inclusion prefix
        bool match;
#ifdef _WIN32
        match = (_strnicmp(resolved, prefix, prefix_len) == 0);
#else
        match = (strncmp(resolved, prefix, prefix_len) == 0);
#endif

        if (match) {
            if (kept != i) {
                out[kept] = out[i];
            }
            kept++;
        }
    }

    return kept;
}

void coverage_display(const FileCoverage *files, int count,
                      double aggregate_pct, bool use_color) {
    if (!files || count <= 0) {
        cdo_log_info("Coverage: no source files instrumented");
        return;
    }

    // ANSI escape codes
    const char *green = use_color ? "\033[32m" : "";
    const char *yellow = use_color ? "\033[33m" : "";
    const char *red = use_color ? "\033[31m" : "";
    const char *bold = use_color ? "\033[1m" : "";
    const char *reset = use_color ? "\033[0m" : "";

    cdo_log_info("");
    cdo_log_info("%sCoverage Report:%s", bold, reset);
    cdo_log_info("%-60s %s", "File", "Lines");

    for (int i = 0; i < count; i++) {
        const char *color;
        if (files[i].pct >= 80.0) {
            color = green;
        } else if (files[i].pct >= 50.0) {
            color = yellow;
        } else {
            color = red;
        }

        cdo_log_info("  %-58s %s%5.1f%%%s (%d/%d)",
                 files[i].file, color, files[i].pct, reset,
                 files[i].lines_hit, files[i].lines_total);
    }

    // Aggregate line
    const char *agg_color;
    if (aggregate_pct >= 80.0) {
        agg_color = green;
    } else if (aggregate_pct >= 50.0) {
        agg_color = yellow;
    } else {
        agg_color = red;
    }

    cdo_log_info("");
    cdo_log_info("%sAggregate coverage: %s%.1f%%%s", bold, agg_color, aggregate_pct, reset);
}
