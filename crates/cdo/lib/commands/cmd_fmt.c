/**
 * cmd_fmt â€” Format source files.
 *
 * Implements source file discovery and the fmt command entry point.
 * The handler reads --check (bool), --verbose, --quiet, and positional crate
 * names from CliParseResult.
 */

#include "commands/cmd_fmt.h"
#include "core/handler_ctx.h"
#include "core/log.h"
#include "model/workspace.h"
#include "model/fmt_settings.h"
#include "pal/pal.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Argument extraction helpers (match main_new.cpp pattern)
// ---------------------------------------------------------------------------

/// Find a named argument in the parse result. Returns NULL if not found.
static const CliArgValue* fmt_find_arg(const CliParseResult* result, const char* name) {
    for (int i = 0; i < result->arg_value_count; i++) {
        if (result->arg_values[i].name && strcmp(result->arg_values[i].name, name) == 0) {
            return &result->arg_values[i];
        }
    }
    return NULL;
}

/// Get a bool argument value. Returns false if not present.
static bool fmt_get_arg_bool(const CliParseResult* result, const char* name) {
    const CliArgValue* v = fmt_find_arg(result, name);
    return (v && v->present && v->type == CLI_ARG_BOOL) ? v->value.bool_val : false;
}

// ---------------------------------------------------------------------------
// Glob pattern matching (supports *, **, and ? wildcards)
// ---------------------------------------------------------------------------

/**
 * Match a path against a glob pattern.
 * Supports:
 *   *  - matches any sequence of characters except '/'
 *   ** - matches any sequence of characters including '/'
 *   ?  - matches exactly one character (not '/')
 *
 * Both pattern and path should use '/' as separator.
 */
static bool fmt_glob_match(const char* pattern, const char* path) {
    const char* p = pattern;
    const char* s = path;
    const char* star_p = NULL;
    const char* star_s = NULL;

    while (*s) {
        if (*p == '*') {
            if (*(p + 1) == '*') {
                // ** matches any characters including '/'
                p += 2;
                if (*p == '/') p++;
                if (*p == '\0') return true;
                for (const char* try_s = s; *try_s; try_s++) {
                    if (fmt_glob_match(p, try_s)) return true;
                }
                return fmt_glob_match(p, s + strlen(s));
            } else {
                // * matches any chars except '/'
                star_p = p;
                star_s = s;
                p++;
            }
        } else if (*p == '?' && *s != '/') {
            p++;
            s++;
        } else if (*p == *s) {
            p++;
            s++;
        } else if (star_p) {
            star_s++;
            if (*(star_s - 1) == '/' || *(star_s - 1) == '\0') return false;
            s = star_s;
            p = star_p + 1;
        } else {
            return false;
        }
    }

    while (*p == '*') p++;
    return *p == '\0';
}

// ---------------------------------------------------------------------------
// Extension matching
// ---------------------------------------------------------------------------

/// Returns true if the extension matches a formattable C/C++ source file.
static bool is_fmt_source_ext(const char* ext) {
    return (strcmp(ext, ".c") == 0 ||
            strcmp(ext, ".cpp") == 0 ||
            strcmp(ext, ".cxx") == 0 ||
            strcmp(ext, ".cc") == 0 ||
            strcmp(ext, ".h") == 0 ||
            strcmp(ext, ".hpp") == 0 ||
            strcmp(ext, ".hxx") == 0);
}

// ---------------------------------------------------------------------------
// Directory exclusion helpers
// ---------------------------------------------------------------------------

/// Check if a path segment (directory name) is always excluded.
/// Returns true if the segment is "build" or ".cdo".
static bool is_always_excluded_dir(const char* segment, size_t len) {
    if (len == 5 && memcmp(segment, "build", 5) == 0) return true;
    if (len == 4 && memcmp(segment, ".cdo", 4) == 0) return true;
    return false;
}

/// Check if the given absolute path contains an always-excluded directory component.
/// base_len is the length of the crate root prefix (everything before the relative part).
static bool path_contains_excluded_dir(const char* path, size_t base_len) {
    // Start scanning after the base path (the crate root directory)
    const char* rel = path + base_len;
    if (*rel == '/') rel++;

    const char* seg_start = rel;
    for (const char* c = rel; ; c++) {
        if (*c == '/' || *c == '\0') {
            size_t seg_len = (size_t)(c - seg_start);
            if (seg_len > 0 && is_always_excluded_dir(seg_start, seg_len)) {
                return true;
            }
            if (*c == '\0') break;
            seg_start = c + 1;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Discovery walk context
// ---------------------------------------------------------------------------

typedef struct {
    const Workspace* ws;
    const FmtSettings* settings;
    char (*out_files)[260];
    int max_files;
    int count;
    const char* crate_root;     // normalized absolute crate root path
    size_t crate_root_len;
    size_t ws_root_len;         // length of workspace root for relative path computation
    bool verbose;
} FmtDiscoverCtx;

/// pal_dir_walk callback: collects formattable source files, skipping excluded paths.
static void fmt_walk_callback(const char* entry_path, bool is_dir, void* ctx) {
    FmtDiscoverCtx* dc = (FmtDiscoverCtx*)ctx;
    if (dc->count >= dc->max_files) return;

    // Normalize path for consistent matching
    size_t entry_len = strlen(entry_path);
    char normalized[260];
    if (entry_len >= sizeof(normalized)) return;
    memcpy(normalized, entry_path, entry_len + 1);
    pal_path_normalize(normalized);

    // For directories, we just skip (pal_dir_walk recurses automatically).
    // We filter out excluded directories when processing files below.
    if (is_dir) return;

    // Check if path passes through an always-excluded directory
    if (path_contains_excluded_dir(normalized, dc->crate_root_len)) return;

    // Check file extension
    const char* ext = pal_path_ext(normalized);
    if (!is_fmt_source_ext(ext)) return;

    // Compute path relative to workspace root for exclude pattern matching
    const char* rel_path = normalized;
    if (dc->ws_root_len > 0 && strlen(normalized) > dc->ws_root_len && normalized[dc->ws_root_len] == '/') {
        rel_path = normalized + dc->ws_root_len + 1;
    }

    // Check against configured exclude patterns
    for (int i = 0; i < dc->settings->exclude_count; i++) {
        if (fmt_glob_match(dc->settings->exclude_patterns[i], rel_path)) {
            if (dc->verbose) {
                cdo_log_debug("Excluding: %s (pattern: %s)", rel_path, dc->settings->exclude_patterns[i]);
            }
            return;
        }
    }

    // Add to output buffer
    if (dc->count < dc->max_files) {
        size_t norm_len = strlen(normalized);
        if (norm_len < 260) {
            memcpy(dc->out_files[dc->count], normalized, norm_len + 1);
            dc->count++;
        }
    }
}

// ---------------------------------------------------------------------------
// Public discovery function
// ---------------------------------------------------------------------------

/// Discover all formattable source files in the given crates.
/// Writes paths into out_files (caller provides pre-allocated buffer).
/// Returns the number of files discovered.
int fmt_discover_sources(const Workspace* ws, const Crate** crates, int crate_count, const FmtSettings* settings, bool verbose, char (*out_files)[260], int max_files) {
    if (!ws || !crates || !settings || !out_files || crate_count <= 0 || max_files <= 0) return 0;

    // Normalize workspace root for prefix computation
    char ws_root[260];
    size_t root_len = strlen(ws->root_path);
    if (root_len >= sizeof(ws_root)) return 0;
    memcpy(ws_root, ws->root_path, root_len + 1);
    pal_path_normalize(ws_root);
    // Strip trailing slash
    size_t ws_root_len = strlen(ws_root);
    while (ws_root_len > 0 && ws_root[ws_root_len - 1] == '/') {
        ws_root[--ws_root_len] = '\0';
    }

    FmtDiscoverCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ws = ws;
    ctx.settings = settings;
    ctx.out_files = out_files;
    ctx.max_files = max_files;
    ctx.count = 0;
    ctx.ws_root_len = ws_root_len;
    ctx.verbose = verbose;

    for (int i = 0; i < crate_count; i++) {
        if (ctx.count >= max_files) break;

        const Crate* crate = crates[i];
        if (!crate) continue;

        // Compute absolute path: ws_root + "/" + crate->path
        char crate_path[260];
        if (pal_path_join(crate_path, sizeof(crate_path), ws_root, crate->path) != 0) {
            cdo_log_error("fmt: crate path too long: %s/%s", ws_root, crate->path);
            continue;
        }
        pal_path_normalize(crate_path);

        // Strip trailing slash from crate path
        size_t crate_path_len = strlen(crate_path);
        while (crate_path_len > 0 && crate_path[crate_path_len - 1] == '/') {
            crate_path[--crate_path_len] = '\0';
        }

        ctx.crate_root = crate_path;
        ctx.crate_root_len = crate_path_len;

        // Walk the crate directory recursively
        int rc = pal_dir_walk(crate_path, fmt_walk_callback, &ctx);
        if (rc != PAL_OK && rc != PAL_ERR_NOT_FOUND) {
            cdo_log_debug("fmt: failed to walk crate directory: %s (rc=%d)", crate_path, rc);
        }
    }

    return ctx.count;
}

// ---------------------------------------------------------------------------
// Formatter discovery
// ---------------------------------------------------------------------------

int fmt_find_formatter(const char* ws_root, const FmtSettings* settings, char* out_path, int path_size) {
    if (!ws_root || !settings || !out_path || path_size <= 0) return 1;

    // 1. Check settings.tool_path (explicit override)
    if (settings->tool_path[0] != '\0') {
        if (pal_path_exists(settings->tool_path) == 0) {
            size_t len = strlen(settings->tool_path);
            if ((int)len < path_size) {
                memcpy(out_path, settings->tool_path, len + 1);
                return 0;
            }
        }
        cdo_log_error("configured formatter not found: %s", settings->tool_path);
        return 1;
    }

    // 2. Check .cdo/tools/clang-format/clang-format.exe (workspace-local install)
    char local_path[260];
    if (pal_path_join(local_path, sizeof(local_path), ws_root, ".cdo/tools/clang-format/clang-format.exe") == 0) {
        if (pal_path_exists(local_path) == 0) {
            size_t len = strlen(local_path);
            if ((int)len < path_size) {
                memcpy(out_path, local_path, len + 1);
                return 0;
            }
        }
    }

    // 3. Check system PATH by running clang-format --version
    {
        const char* args[] = { "--version" };
        PalSpawnOpts spawn_opts;
        memset(&spawn_opts, 0, sizeof(spawn_opts));
        spawn_opts.program = "clang-format";
        spawn_opts.args = args;
        spawn_opts.arg_count = 1;
        spawn_opts.capture_output = true;
        spawn_opts.timeout_ms = 5000;

        PalSpawnResult result;
        memset(&result, 0, sizeof(result));

        int rc = pal_spawn(&spawn_opts, &result);
        if (rc == 0 && result.exit_code == 0) {
            pal_spawn_result_free(&result);
            const char* name = "clang-format";
            size_t len = strlen(name);
            if ((int)len < path_size) {
                memcpy(out_path, name, len + 1);
                return 0;
            }
        }
        pal_spawn_result_free(&result);
    }

    // 4. Not found anywhere
    cdo_log_error("clang-format not found. Install with: cdo tool install clang-format");
    return 1;
}

// ---------------------------------------------------------------------------
// Formatter invocation with batching
// ---------------------------------------------------------------------------

/// Maximum total command-line length per batch (stay under Windows 8191 limit).
#define FMT_MAX_CMDLINE_LEN 7000

/// Maximum number of files per batch (safety cap).
#define FMT_MAX_BATCH_SIZE 50

/// Parse clang-format stderr to extract file paths that would be reformatted.
/// clang-format --dry-run --Werror outputs lines like:
///   path/to/file.c:10:5: warning: code should be clang-formatted [-Wclang-format-violations]
/// We count unique file paths that appear in stderr.
static int fmt_count_nonconformant_from_stderr(const char* stderr_buf, char (*files)[260], int file_count) {
    if (!stderr_buf || !stderr_buf[0]) return 0;

    // Track which files from the batch had issues reported
    int count = 0;
    bool seen[FMT_MAX_BATCH_SIZE];
    memset(seen, 0, sizeof(seen));

    for (int i = 0; i < file_count && i < FMT_MAX_BATCH_SIZE; i++) {
        if (seen[i]) continue;
        // Check if this file path appears in stderr
        if (strstr(stderr_buf, files[i]) != NULL) {
            seen[i] = true;
            count++;
        }
    }

    // If stderr has content but we didn't match any specific file paths,
    // assume all files in the batch are nonconformant (conservative approach for check mode).
    if (count == 0 && stderr_buf[0] != '\0') {
        count = file_count;
    }

    return count;
}

/// Report per-file errors from formatter stderr output.
static void fmt_report_errors(const char* stderr_buf, char (*files)[260], int file_count) {
    if (!stderr_buf || !stderr_buf[0]) return;

    // Report each file that appears in stderr
    for (int i = 0; i < file_count; i++) {
        if (strstr(stderr_buf, files[i]) != NULL) {
            cdo_log_error("  %s: formatter error", files[i]);
        }
    }

    // Also log the raw stderr for debugging
    cdo_log_debug("Formatter stderr: %s", stderr_buf);
}

int fmt_invoke(const char* formatter_path, const FmtSettings* settings, char (*files)[260], int file_count, bool check_mode, FmtInvokeResult* result) {
    if (!formatter_path || !settings || !files || file_count <= 0 || !result) return -1;

    int overall_rc = 0;

    // Determine base args (before file paths)
    // Max base args: formatter_path, -i OR --dry-run --Werror, --style=X = 4 args max
    const char* base_args[4];
    int base_arg_count = 0;

    if (check_mode) {
        base_args[base_arg_count++] = "--dry-run";
        base_args[base_arg_count++] = "--Werror";
    } else {
        base_args[base_arg_count++] = "-i";
    }

    // Add --style argument if configured and not "file" (which is clang-format's default)
    char style_arg[80];
    style_arg[0] = '\0';
    if (settings->style[0] != '\0' && strcmp(settings->style, "file") != 0) {
        snprintf(style_arg, sizeof(style_arg), "--style=%s", settings->style);
        base_args[base_arg_count++] = style_arg;
    }

    // Compute base command length (formatter path + base args)
    size_t base_len = strlen(formatter_path) + 1; // +1 for space
    for (int i = 0; i < base_arg_count; i++) {
        base_len += strlen(base_args[i]) + 1; // +1 for space
    }

    // Process files in batches
    int file_idx = 0;
    while (file_idx < file_count) {
        // Build this batch: accumulate files until we hit the length or count limit
        int batch_start = file_idx;
        size_t batch_len = base_len;
        int batch_count = 0;

        while (file_idx < file_count && batch_count < FMT_MAX_BATCH_SIZE) {
            size_t file_path_len = strlen(files[file_idx]);
            size_t needed = file_path_len + 3; // +3 for quotes and space on Windows
            if (batch_count > 0 && (batch_len + needed) > FMT_MAX_CMDLINE_LEN) break;
            batch_len += needed;
            batch_count++;
            file_idx++;
        }

        // Ensure at least one file per batch (even if over limit)
        if (batch_count == 0 && file_idx < file_count) {
            batch_count = 1;
            file_idx++;
        }

        // Build the full args array: base_args + file paths
        // Max args = base_arg_count + batch_count
        int total_args = base_arg_count + batch_count;
        const char** args = (const char**)malloc((size_t)total_args * sizeof(const char*));
        if (!args) {
            cdo_log_error("fmt: out of memory building argument list");
            result->errored += batch_count;
            overall_rc = 1;
            continue;
        }

        int arg_idx = 0;
        for (int i = 0; i < base_arg_count; i++) {
            args[arg_idx++] = base_args[i];
        }
        for (int i = 0; i < batch_count; i++) {
            args[arg_idx++] = files[batch_start + i];
        }

        // Spawn the formatter
        PalSpawnOpts opts;
        memset(&opts, 0, sizeof(opts));
        opts.program = formatter_path;
        opts.args = args;
        opts.arg_count = total_args;
        opts.cwd = NULL;
        opts.capture_output = true;
        opts.timeout_ms = 0; // use default

        PalSpawnResult spawn_result;
        memset(&spawn_result, 0, sizeof(spawn_result));

        cdo_log_debug("fmt: invoking %s on %d files (batch at index %d)", formatter_path, batch_count, batch_start);

        int rc = pal_spawn(&opts, &spawn_result);

        if (rc != PAL_OK) {
            cdo_log_error("fmt: failed to spawn formatter: %s (error %d)", formatter_path, rc);
            result->errored += batch_count;
            overall_rc = 1;
            free(args);
            pal_spawn_result_free(&spawn_result);
            continue;
        }

        if (spawn_result.exit_code == 0) {
            // All files in this batch are OK
            if (check_mode) {
                result->conformant += batch_count;
            } else {
                // In normal mode with -i, clang-format doesn't tell us which files
                // were actually modified vs already conformant. We count them all as
                // formatted (the caller/summary can refine this if needed).
                result->formatted += batch_count;
            }
        } else {
            // Non-zero exit code
            if (check_mode) {
                // In check mode, non-zero means some files would change
                int nonconformant_count = fmt_count_nonconformant_from_stderr(spawn_result.stderr_buf, &files[batch_start], batch_count);
                int conformant_count = batch_count - nonconformant_count;

                result->nonconformant += nonconformant_count;
                result->conformant += conformant_count;

                // Report the nonconformant files
                if (spawn_result.stderr_buf && spawn_result.stderr_buf[0]) {
                    // Log individual nonconformant files from stderr
                    for (int i = 0; i < batch_count; i++) {
                        if (strstr(spawn_result.stderr_buf, files[batch_start + i]) != NULL) {
                            cdo_log_info("  %s", files[batch_start + i]);
                        }
                    }
                } else {
                    // No stderr detail â€” report all files in batch
                    for (int i = 0; i < batch_count; i++) {
                        cdo_log_info("  %s", files[batch_start + i]);
                    }
                }
                overall_rc = 1;
            } else {
                // In normal mode, non-zero means formatter error
                cdo_log_error("fmt: formatter returned error (exit code %d)", spawn_result.exit_code);
                fmt_report_errors(spawn_result.stderr_buf, &files[batch_start], batch_count);
                result->errored += batch_count;
                overall_rc = 1;
            }
        }

        pal_spawn_result_free(&spawn_result);
        free(args);
    }

    return overall_rc;
}

// ---------------------------------------------------------------------------
// cmd_fmt entry point (new CLI framework handler)
// ---------------------------------------------------------------------------

int cmd_fmt(const CliParseResult* result, void* ctx) {
    (void)ctx; // CdoHandlerCtx* â€” not used directly yet (output goes through global)

    if (!result) {
        cdo_log_error("internal error: NULL parse result passed to fmt command");
        return 1;
    }

    // Extract args from CliParseResult
    bool check_mode = fmt_get_arg_bool(result, "check");
    bool verbose = fmt_get_arg_bool(result, "verbose");
    bool quiet = fmt_get_arg_bool(result, "quiet");

    // --- Step 1: Load workspace ---
    Workspace ws = {0};
    int rc = workspace_load(".", &ws);
    if (rc != 0) {
        cdo_log_error("failed to load workspace");
        return 1;
    }

    // --- Step 2: Resolve target crates ---
    const Crate* target_crates[64];
    int target_count = 0;

    if (result->positional_count > 0) {
        // Format specific crates
        for (int i = 0; i < result->positional_count; i++) {
            bool found = false;
            for (int j = 0; j < ws.crate_count; j++) {
                if (strcmp(ws.crates[j].name, result->positional_values[i]) == 0) {
                    if (target_count < 64) {
                        target_crates[target_count++] = &ws.crates[j];
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                cdo_log_error("unknown crate: '%s'", result->positional_values[i]);
                workspace_free(&ws);
                return 1;
            }
        }
    } else {
        // Format all workspace crates
        for (int i = 0; i < ws.crate_count && target_count < 64; i++) {
            target_crates[target_count++] = &ws.crates[i];
        }
    }

    // --- Step 3: Read format settings ---
    const FmtSettings* settings = &ws.format_settings;

    // --- Step 4: Discover source files ---
    static char files[4096][260];
    int file_count = fmt_discover_sources(&ws, target_crates, target_count, settings, verbose, files, 4096);

    if (file_count == 0) {
        if (!quiet) {
            cdo_log_info("No source files found.");
        }
        workspace_free(&ws);
        return 0;
    }

    // --- Step 5: Find formatter ---
    char formatter_path[260];
    rc = fmt_find_formatter(ws.root_path, settings, formatter_path, sizeof(formatter_path));
    if (rc != 0) {
        workspace_free(&ws);
        return 1;
    }

    // Verbose: log formatter and file count
    if (verbose) {
        cdo_log_debug("Formatter: %s", formatter_path);
        cdo_log_debug("Files to process: %d", file_count);
    }

    // --- Step 6: Invoke formatter ---
    FmtInvokeResult fmt_result = {0};
    rc = fmt_invoke(formatter_path, settings, files, file_count, check_mode, &fmt_result);

    // --- Step 7: Print summary and determine exit code ---
    if (!quiet) {
        if (check_mode) {
            if (fmt_result.nonconformant == 0) {
                cdo_log_info("All %d files formatted correctly", fmt_result.conformant);
            } else {
                cdo_log_info("%d files would be reformatted", fmt_result.nonconformant);
            }
        } else {
            if (fmt_result.formatted == 0 && fmt_result.errored == 0) {
                cdo_log_info("All %d files already formatted correctly", file_count);
            } else {
                cdo_log_info("Formatted %d files (%d already conformant)", fmt_result.formatted, fmt_result.conformant);
            }
        }
    }

    workspace_free(&ws);

    // Exit codes:
    // - Any errors -> exit 1
    // - Check mode with nonconformant files -> exit 1
    // - Otherwise -> exit 0
    if (fmt_result.errored > 0) return 1;
    if (check_mode && fmt_result.nonconformant > 0) return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// End of cmd_fmt.c
