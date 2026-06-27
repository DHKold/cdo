#ifndef CDO_COMMANDS_TEST_COVERAGE_H
#define CDO_COMMANDS_TEST_COVERAGE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Maximum number of files that can be reported in a single coverage run.
#define COVERAGE_MAX_FILES 512

/// Per-file coverage data extracted from gcov output.
typedef struct {
    char file[256];
    int  lines_total;
    int  lines_hit;
    double pct;
} FileCoverage;

/// Run gcov on all .gcda files found in build_dir (recursively).
/// Parses gcov output to extract per-file line coverage.
/// Stores results in the `out` array (up to max_files entries).
/// Returns the number of files processed, or -1 on error.
/// If gcov is not found in PATH, prints an error and returns -2.
int coverage_run_gcov(const char *build_dir, FileCoverage *out, int max_files);

/// Compute aggregate coverage percentage from an array of FileCoverage.
/// Returns sum(lines_hit) / sum(lines_total) * 100.0, or 0.0 if total is 0.
double coverage_aggregate(const FileCoverage *files, int count);

/// Display per-file coverage results and aggregate percentage.
/// Uses colored output when use_color is true.
void coverage_display(const FileCoverage *files, int count,
                      double aggregate_pct, bool use_color);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_TEST_COVERAGE_H
