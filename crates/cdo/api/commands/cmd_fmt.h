#ifndef CDO_COMMANDS_CMD_FMT_H
#define CDO_COMMANDS_CMD_FMT_H

#include "cmd/cli_cmd.h"
#include "model/workspace.h"
#include "model/fmt_settings.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Results from a formatter invocation batch.
typedef struct {
    int formatted;      // Files that were modified (normal mode)
    int conformant;     // Files already conformant (normal mode) or passing (check mode)
    int errored;        // Files that caused formatter errors
    int nonconformant;  // Files that would change (check mode)
} FmtInvokeResult;

/// Execute the fmt command (new CLI framework handler).
/// Extracts --check (bool) and positional crate names from CliParseResult.
/// Discovers source files and invokes the formatter.
/// --check mode: exits non-zero if any file would change.
/// Returns 0 on success, non-zero on failure.
int cmd_fmt(const CliParseResult* result, void* ctx);

/// Discover all formattable source files in the given crates.
/// Writes normalized absolute paths into out_files (caller provides pre-allocated buffer).
/// Files matched: .c, .cpp, .cxx, .cc, .h, .hpp, .hxx
/// Always excludes: build/ and .cdo/ directories.
/// Applies configured exclude glob patterns from settings.
/// When verbose is true, logs excluded files at debug level.
/// Returns the number of files discovered.
int fmt_discover_sources(const Workspace* ws, const Crate** crates, int crate_count, const FmtSettings* settings, bool verbose, char (*out_files)[260], int max_files);

/// Locate the formatter binary. Checks:
///   1. settings->tool_path (if set)
///   2. .cdo/tools/clang-format/ (workspace-local install)
///   3. System PATH
/// Returns 0 on success (path written to out_path), non-zero if not found.
int fmt_find_formatter(const char* ws_root, const FmtSettings* settings, char* out_path, int path_size);

/// Invoke the formatter on a batch of files.
/// In normal mode: uses clang-format -i to format in-place.
/// In check mode: uses --dry-run --Werror (clang-format 10+) to verify conformance.
/// Batches files into groups to avoid command-line length limits on Windows.
/// If settings->style is non-empty and not "file", adds --style=<value>.
/// Results are accumulated in *result (caller should zero-initialize before first call).
/// Returns 0 if all files OK, non-zero if any file would change or errored.
int fmt_invoke(const char* formatter_path, const FmtSettings* settings, char (*files)[260], int file_count, bool check_mode, FmtInvokeResult* result);

#ifdef __cplusplus
}
#endif

#endif // CDO_COMMANDS_CMD_FMT_H
