#include "commands/cmd_tool.h"
#include "core/output.h"
#include "core/http.h"
#include "core/archive.h"
#include "core/toml.h"
#include "core/catalog.h"
#include "core/checksum.h"
#include "pal/pal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define CACHE_DIR   ".cdo/cache"
#define TOOLS_DIR   ".cdo/tools"
#define MANIFEST_NAME "cdo-tool.toml"
#define MAX_PATH_LEN 1024
#define MAX_RETRIES  3

// --- Internal helpers ---

/// Extract the filename from a URL (everything after the last '/').
static const char* url_filename(const char* url) {
    const char* last_slash = strrchr(url, '/');
    if (last_slash && last_slash[1] != '\0') {
        return last_slash + 1;
    }
    return url;
}

/// Check if --refresh or --url <url> is among the argv_rest or positional args.
/// Since the CLI parser skips unrecognized options, we scan argv_rest for them.
/// We also check the raw positional args for subcommand and tool name.
typedef struct {
    const char* subcommand;   // "install", "list", "remove"
    const char* tool_name;
    const char* url;
    const char* version;      // optional version constraint (from --version)
    const char* checksum;     // resolved checksum from catalog (algorithm:hex_digest)
    bool        refresh;
} ToolArgs;

/// Parse tool-specific arguments from positional_args and argv_rest.
/// Expected invocation: cdo tool install <name> [--url <url>] [--version <constraint>] [--refresh]
/// The CLI parser puts "install" and "<name>" into positional_args,
/// and unrecognized flags (--url, --refresh, --version) are skipped.
/// We need to re-scan for --url, --version, and --refresh from argv_rest.
static int parse_tool_args(const CdoOptions* opts, ToolArgs* args) {
    memset(args, 0, sizeof(*args));

    // positional_args[0] = subcommand (install/list/remove)
    // positional_args[1] = tool name (for install/remove)
    if (opts->positional_count < 1) {
        cdo_error("Missing subcommand. Usage: cdo tool <install|list|remove>");
        return 1;
    }

    args->subcommand = opts->positional_args[0];

    if (strcmp(args->subcommand, "install") == 0) {
        if (opts->positional_count < 2) {
            cdo_error("Missing tool name. Usage: cdo tool install <name> [--url <url>]");
            return 1;
        }
        args->tool_name = opts->positional_args[1];

        // Scan remaining positional args for --url, --version, and --refresh
        for (int i = 2; i < opts->positional_count; i++) {
            const char* arg = opts->positional_args[i];
            if (strcmp(arg, "--refresh") == 0) {
                args->refresh = true;
            } else if (strncmp(arg, "--url=", 6) == 0) {
                args->url = arg + 6;
            } else if (strcmp(arg, "--url") == 0 && i + 1 < opts->positional_count) {
                args->url = opts->positional_args[++i];
            } else if (strncmp(arg, "--version=", 10) == 0) {
                args->version = arg + 10;
            } else if (strcmp(arg, "--version") == 0 && i + 1 < opts->positional_count) {
                args->version = opts->positional_args[++i];
            }
        }

        // Also check argv_rest for --url, --version, and --refresh
        for (int i = 0; i < opts->argc_rest; i++) {
            const char* arg = opts->argv_rest[i];
            if (strcmp(arg, "--refresh") == 0) {
                args->refresh = true;
            } else if (strncmp(arg, "--url=", 6) == 0) {
                args->url = arg + 6;
            } else if (strcmp(arg, "--url") == 0 && i + 1 < opts->argc_rest) {
                args->url = opts->argv_rest[++i];
            } else if (strncmp(arg, "--version=", 10) == 0) {
                args->version = arg + 10;
            } else if (strcmp(arg, "--version") == 0 && i + 1 < opts->argc_rest) {
                args->version = opts->argv_rest[++i];
            }
        }

        // NOTE: --url is now optional. If not provided, catalog resolution will be attempted.
    } else if (strcmp(args->subcommand, "remove") == 0) {
        if (opts->positional_count < 2) {
            cdo_error("Missing tool name. Usage: cdo tool remove <name>");
            return 1;
        }
        args->tool_name = opts->positional_args[1];
    } else if (strcmp(args->subcommand, "list") == 0) {
        // No extra args needed
    } else {
        cdo_error("Unknown subcommand '%s'", args->subcommand);
        cdo_info("Available subcommands: install, list, remove");
        cdo_info("Run 'cdo tool --help' for usage information.");
        return 1;
    }

    return 0;
}

/// Download progress callback for http_download.
static void download_progress(size_t downloaded, size_t total, void* ctx) {
    ProgressBar* bar = (ProgressBar*)ctx;
    if (bar && total > 0) {
        int pct = (int)((downloaded * 100) / total);
        progress_update(bar, pct);
    }
}

/// Build an HTTPS fallback URL from a given URL.
/// If the URL starts with "http://", replace with "https://".
/// Returns a heap-allocated string (caller frees) or NULL if already HTTPS or no fallback needed.
static char* make_https_url(const char* url) {
    if (strncmp(url, "http://", 7) == 0) {
        size_t len = strlen(url);
        // "https://" is 8 chars, "http://" is 7 chars — need +1
        char* https_url = (char*)malloc(len + 2);
        if (!https_url) return NULL;
        snprintf(https_url, len + 2, "https://%s", url + 7);
        return https_url;
    }
    return NULL;
}

/// Get current ISO 8601 timestamp.
static void get_timestamp(char* buf, size_t buf_size) {
    time_t now = time(NULL);
    struct tm* utc = gmtime(&now);
    if (utc) {
        strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", utc);
    } else {
        snprintf(buf, buf_size, "unknown");
    }
}

/// Write cdo-tool.toml manifest into the tool directory.
static int write_tool_manifest(const char* tool_dir, const char* name, const char* url) {
    char manifest_path[MAX_PATH_LEN];
    if (pal_path_join(manifest_path, sizeof(manifest_path), tool_dir, MANIFEST_NAME) != 0) {
        cdo_error("Path too long for tool manifest");
        return 1;
    }

    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    // Build a TOML table manually
    // Format:
    //   [tool]
    //   name = "<name>"
    //   url = "<url>"
    //   installed_at = "<timestamp>"
    TomlTable table = { .head = NULL, .tail = NULL, .count = 0 };
    TomlTable tool_table = { .head = NULL, .tail = NULL, .count = 0 };

    // Create string values
    TomlValue name_val = { .type = TOML_STRING, .as.string = (char*)name };
    TomlValue url_val = { .type = TOML_STRING, .as.string = (char*)url };
    TomlValue ts_val = { .type = TOML_STRING, .as.string = timestamp };

    // Build entries for the [tool] sub-table
    TomlEntry ts_entry = { .key = "installed_at", .value = &ts_val, .next = NULL };
    TomlEntry url_entry = { .key = "url", .value = &url_val, .next = &ts_entry };
    TomlEntry name_entry = { .key = "name", .value = &name_val, .next = &url_entry };

    tool_table.head = &name_entry;
    tool_table.tail = &ts_entry;
    tool_table.count = 3;

    // Wrap in a "tool" key at root
    TomlValue tool_val = { .type = TOML_TABLE, .as.table = &tool_table };
    TomlEntry root_entry = { .key = "tool", .value = &tool_val, .next = NULL };

    table.head = &root_entry;
    table.tail = &root_entry;
    table.count = 1;

    // Serialize
    char* buf = NULL;
    size_t buf_len = 0;
    int rc = toml_serialize(&table, &buf, &buf_len);
    if (rc != 0) {
        cdo_error("Failed to serialize tool manifest");
        return 1;
    }

    rc = pal_file_write(manifest_path, buf, buf_len);
    free(buf);

    if (rc != 0) {
        cdo_error("Failed to write tool manifest: %s", manifest_path);
        return 1;
    }

    return 0;
}

/// Extract an archive based on file extension.
static int extract_archive(const char* archive_path, const char* dest_dir) {
    const char* ext = pal_path_ext(archive_path);

    if (strcmp(ext, ".zip") == 0) {
        return archive_extract_zip(archive_path, dest_dir);
    }

    // Check for .tar.gz or .tgz
    if (strcmp(ext, ".tgz") == 0) {
        return archive_extract_targz(archive_path, dest_dir);
    }

    // Check for .gz — might be .tar.gz (pal_path_ext returns last extension only)
    if (strcmp(ext, ".gz") == 0) {
        // Check if the name has .tar before .gz
        size_t len = strlen(archive_path);
        if (len > 7 && strcmp(archive_path + len - 7, ".tar.gz") == 0) {
            return archive_extract_targz(archive_path, dest_dir);
        }
    }

    cdo_error("Unsupported archive format: %s", ext);
    return 1;
}

// --- Subcommand implementations ---

static int tool_install(const ToolArgs* args) {
    const char* name = args->tool_name;
    const char* url = args->url;
    const char* checksum_str = args->checksum;

    // --- Catalog resolution: if no --url provided, resolve from catalog ---
    Catalog catalog = {0};
    CatalogResolveResult resolve_result = {0};
    bool used_catalog = false;

    if (!url || url[0] == '\0') {
        // Detect current platform
        CatalogPlatform platform = {0};
        int rc = catalog_detect_platform(&platform);
        if (rc != 0) {
            cdo_error("Failed to detect platform");
            return 1;
        }

        // Load catalogs from all search locations
        rc = catalog_load(&catalog, ".");
        if (rc != 0) {
            cdo_error("Failed to load catalogs");
            return 1;
        }

        // Resolve the tool by name and optional version constraint
        rc = catalog_resolve_tool(&catalog, name, args->version, &platform, &resolve_result);
        if (rc != 0) {
            catalog_free(&catalog);
            // Error message is emitted by catalog_resolve_tool
            return 1;
        }

        url = resolve_result.url;
        if (resolve_result.checksum[0] != '\0') {
            checksum_str = resolve_result.checksum;
        }
        used_catalog = true;

        if (resolve_result.version[0] != '\0') {
            cdo_info("Resolved '%s' version %s for platform %s",
                     name, resolve_result.version, platform.triple);
        }
    }

    const char* filename = url_filename(url);

    // Build cache path: .cdo/cache/<filename>
    char cache_path[MAX_PATH_LEN];
    if (pal_path_join(cache_path, sizeof(cache_path), CACHE_DIR, filename) != 0) {
        cdo_error("Cache path too long");
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    // Build tool directory: .cdo/tools/<name>/
    char tool_dir[MAX_PATH_LEN];
    if (pal_path_join(tool_dir, sizeof(tool_dir), TOOLS_DIR, name) != 0) {
        cdo_error("Tool path too long");
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    // Step 1: Ensure cache directory exists
    if (pal_mkdir_p(CACHE_DIR) != PAL_OK) {
        cdo_error("Failed to create cache directory: %s", CACHE_DIR);
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    // Step 2: Check cache — skip download if archive exists and --refresh not set
    bool need_download = true;
    if (!args->refresh && pal_path_exists(cache_path) == 1) {
        cdo_info("Using cached archive: %s", filename);
        need_download = false;
    }

    // Step 3: Download archive
    if (need_download) {
        cdo_info("Downloading %s...", filename);

        ProgressBar* bar = progress_create("Downloading", 100);
        int rc = http_download(url, cache_path, MAX_RETRIES, download_progress, bar);
        progress_finish(bar);

        if (rc != 0) {
            // Try HTTPS fallback if original URL was HTTP
            char* https_url = make_https_url(url);
            if (https_url) {
                cdo_warn("HTTP download failed, retrying with HTTPS...");
                bar = progress_create("Downloading (HTTPS)", 100);
                rc = http_download(https_url, cache_path, MAX_RETRIES, download_progress, bar);
                progress_finish(bar);
                free(https_url);
            }

            if (rc != 0) {
                cdo_error("Failed to download tool archive from: %s", url);
                if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
                return 1;
            }
        }
    }

    // Step 4: Checksum verification (before extraction)
    if (checksum_str && checksum_str[0] != '\0') {
        ChecksumSpec spec = {0};
        int rc = checksum_parse(checksum_str, &spec);
        if (rc != 0) {
            cdo_error("Malformed checksum: %s", checksum_str);
            if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
            return 1;
        }

        cdo_info("Verifying checksum (%s)...", checksum_str);
        rc = checksum_verify_file(cache_path, &spec);
        if (rc != 0) {
            // checksum_verify_file deletes the file and reports expected vs actual
            cdo_error("Checksum verification failed for '%s'", name);
            if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
            return 1;
        }
        cdo_info("Checksum verified");
    } else {
        cdo_warn("Checksum not provided for '%s' — archive integrity not verified", name);
    }

    // Step 5: Create tool directory
    if (pal_mkdir_p(tool_dir) != PAL_OK) {
        cdo_error("Failed to create tool directory: %s", tool_dir);
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    // Step 6: Extract archive into tool directory
    cdo_info("Extracting %s...", filename);
    int rc2 = extract_archive(cache_path, tool_dir);
    if (rc2 != 0) {
        cdo_error("Failed to extract archive: %s", cache_path);
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    // Step 7: Write cdo-tool.toml manifest
    rc2 = write_tool_manifest(tool_dir, name, url);
    if (rc2 != 0) {
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    // Step 8: Report success
    cdo_info("Tool '%s' installed successfully", name);

    // Cleanup catalog resources
    if (used_catalog) {
        catalog_resolve_result_free(&resolve_result);
        catalog_free(&catalog);
    }

    return 0;
}

static int tool_list(void) {
    // Walk .cdo/tools/ and list directories that contain a manifest
    if (pal_path_exists(TOOLS_DIR) != 1) {
        cdo_info("No tools installed");
        return 0;
    }

    cdo_info("Installed tools:");
    // Simple directory walk — report tool names
    typedef struct {
        int count;
    } ListCtx;

    ListCtx ctx = { .count = 0 };

    pal_dir_walk(TOOLS_DIR, NULL, &ctx);

    // For a more complete implementation we'd iterate entries,
    // but for now just report that the tools directory exists.
    // The full listing requires reading each tool's manifest.
    cdo_info("  (use 'cdo tool install' to manage tools)");
    return 0;
}

static int tool_remove(const ToolArgs* args) {
    char tool_dir[MAX_PATH_LEN];
    if (pal_path_join(tool_dir, sizeof(tool_dir), TOOLS_DIR, args->tool_name) != 0) {
        cdo_error("Tool path too long");
        return 1;
    }

    if (pal_path_exists(tool_dir) != 1) {
        cdo_error("Tool '%s' is not installed", args->tool_name);
        return 1;
    }

    int rc = pal_rmdir_r(tool_dir);
    if (rc != PAL_OK) {
        cdo_error("Failed to remove tool '%s'", args->tool_name);
        return 1;
    }

    cdo_info("Tool '%s' removed", args->tool_name);
    return 0;
}

// --- Public API ---

int cmd_tool(const CdoOptions* opts) {
    if (opts->help || opts->positional_count < 1) {
        cdo_cli_print_help(CDO_CMD_TOOL, stdout);
        return opts->help ? 0 : 1;
    }

    ToolArgs args;
    int rc = parse_tool_args(opts, &args);
    if (rc != 0) {
        return rc;
    }

    if (strcmp(args.subcommand, "install") == 0) {
        return tool_install(&args);
    } else if (strcmp(args.subcommand, "list") == 0) {
        return tool_list();
    } else if (strcmp(args.subcommand, "remove") == 0) {
        return tool_remove(&args);
    }

    // Should not reach here due to parse_tool_args validation
    return 1;
}
