#include "commands/cmd_tool.h"
#include "core/cli_arg_access.h"
#include "core/handler_ctx.h"
#include "core/log.h"
#include "commons/http.h"
#include "commons/archive.h"
#include "commons/toml.h"
#include "core/catalog.h"
#include "commons/checksum.h"
#include "out/cli_out.h"
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

/* -------------------------------------------------------------------------- */
/* Internal helpers                                                            */
/* -------------------------------------------------------------------------- */

/// Extract the filename from a URL (everything after the last '/').
static const char* url_filename(const char* url) {
    const char* last_slash = strrchr(url, '/');
    if (last_slash && last_slash[1] != '\0') {
        return last_slash + 1;
    }
    return url;
}

/// Download progress callback for http_download.
static void download_progress(size_t downloaded, size_t total, void* ctx) {
    CliProgressBar* bar = (CliProgressBar*)ctx;
    if (bar && total > 0) {
        int pct = (int)((downloaded * 100) / total);
        cli_out_progress_update(bar, pct);
    }
}

/// Build an HTTPS fallback URL from a given URL.
/// If the URL starts with "http://", replace with "https://".
/// Returns a heap-allocated string (caller frees) or NULL if already HTTPS or no fallback needed.
static char* make_https_url(const char* url) {
    if (strncmp(url, "http://", 7) == 0) {
        size_t len = strlen(url);
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
        cdo_log_error("Path too long for tool manifest");
        return 1;
    }

    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    TomlTable table = { .head = NULL, .tail = NULL, .count = 0 };
    TomlTable tool_table = { .head = NULL, .tail = NULL, .count = 0 };

    TomlValue name_val = { .type = TOML_STRING, .as.string = (char*)name };
    TomlValue url_val = { .type = TOML_STRING, .as.string = (char*)url };
    TomlValue ts_val = { .type = TOML_STRING, .as.string = timestamp };

    TomlEntry ts_entry = { .key = "installed_at", .value = &ts_val, .next = NULL };
    TomlEntry url_entry = { .key = "url", .value = &url_val, .next = &ts_entry };
    TomlEntry name_entry = { .key = "name", .value = &name_val, .next = &url_entry };

    tool_table.head = &name_entry;
    tool_table.tail = &ts_entry;
    tool_table.count = 3;

    TomlValue tool_val = { .type = TOML_TABLE, .as.table = &tool_table };
    TomlEntry root_entry = { .key = "tool", .value = &tool_val, .next = NULL };

    table.head = &root_entry;
    table.tail = &root_entry;
    table.count = 1;

    char* buf = NULL;
    size_t buf_len = 0;
    int rc = toml_serialize(&table, &buf, &buf_len);
    if (rc != 0) {
        cdo_log_error("Failed to serialize tool manifest");
        return 1;
    }

    rc = pal_file_write(manifest_path, buf, buf_len);
    free(buf);

    if (rc != 0) {
        cdo_log_error("Failed to write tool manifest: %s", manifest_path);
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

    if (strcmp(ext, ".tgz") == 0) {
        return archive_extract_targz(archive_path, dest_dir);
    }

    if (strcmp(ext, ".gz") == 0) {
        size_t len = strlen(archive_path);
        if (len > 7 && strcmp(archive_path + len - 7, ".tar.gz") == 0) {
            return archive_extract_targz(archive_path, dest_dir);
        }
    }

    cdo_log_error("Unsupported archive format: %s", ext);
    return 1;
}

/* -------------------------------------------------------------------------- */
/* tool install                                                                */
/* -------------------------------------------------------------------------- */

int cmd_tool_install(const CliParseResult* result, void* ctx) {
    CdoHandlerCtx* hctx = (CdoHandlerCtx*)ctx;
    CliOutCtx* out_ctx = hctx ? hctx->out : NULL;

    /* Extract positional tool name */
    if (result->positional_count < 1) {
        cdo_log_error("Missing tool name. Usage: cdo tool install <name> [--url <url>]");
        return 1;
    }

    const char* name = result->positional_values[0];
    const char* url = cli_arg_get_str(result, "url");
    const char* version_constraint = cli_arg_get_str(result, "version");
    bool refresh = cli_arg_get_bool(result, "refresh");
    const char* checksum_str = NULL;

    /* Check if tool is already installed (skip unless --refresh) */
    char tool_dir_check[MAX_PATH_LEN];
    if (pal_path_join(tool_dir_check, sizeof(tool_dir_check), TOOLS_DIR, name) == 0) {
        if (!refresh && pal_path_exists(tool_dir_check) == 0) {
            cdo_log_info("Tool '%s' is already installed (use --refresh to re-install)", name);
            return 0;
        }
    }

    /* Catalog resolution: if no --url provided, resolve from catalog */
    Catalog catalog = {0};
    CatalogResolveResult resolve_result = {0};
    bool used_catalog = false;

    if (!url || url[0] == '\0') {
        CatalogPlatform platform = {0};
        int rc = catalog_detect_platform(&platform);
        if (rc != 0) {
            cdo_log_error("Failed to detect platform");
            return 1;
        }

        rc = catalog_load(&catalog, ".");
        if (rc != 0) {
            cdo_log_error("Failed to load catalogs");
            return 1;
        }

        rc = catalog_resolve_tool(&catalog, name, version_constraint, &platform, &resolve_result);
        if (rc != 0) {
            catalog_free(&catalog);
            return 1;
        }

        url = resolve_result.url;
        if (resolve_result.checksum[0] != '\0') {
            checksum_str = resolve_result.checksum;
        }
        used_catalog = true;

        if (resolve_result.version[0] != '\0') {
            cdo_log_info("Resolved '%s' version %s for platform %s", name, resolve_result.version, platform.triple);
        }
    }

    const char* filename = url_filename(url);

    /* Build cache path: .cdo/cache/<filename> */
    char cache_path[MAX_PATH_LEN];
    if (pal_path_join(cache_path, sizeof(cache_path), CACHE_DIR, filename) != 0) {
        cdo_log_error("Cache path too long");
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    /* Build tool directory: .cdo/tools/<name>/ */
    char tool_dir[MAX_PATH_LEN];
    if (pal_path_join(tool_dir, sizeof(tool_dir), TOOLS_DIR, name) != 0) {
        cdo_log_error("Tool path too long");
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    /* Step 1: Ensure cache directory exists */
    if (pal_mkdir_p(CACHE_DIR) != PAL_OK) {
        cdo_log_error("Failed to create cache directory: %s", CACHE_DIR);
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    /* Step 2: Check cache â€” skip download if archive exists and --refresh not set */
    bool need_download = true;
    if (!refresh && pal_path_exists(cache_path) == 0) {
        cdo_log_info("Using cached archive: %s", filename);
        need_download = false;
    }

    /* Step 3: Download archive */
    if (need_download) {
        cdo_log_info("Downloading %s...", filename);

        CliProgressBar* bar = cli_out_progress_create(out_ctx, "Downloading", 100);
        int rc = http_download(url, cache_path, MAX_RETRIES, download_progress, bar);
        cli_out_progress_finish(bar);

        if (rc != 0) {
            char* https_url = make_https_url(url);
            if (https_url) {
                cdo_log_warn("HTTP download failed, retrying with HTTPS...");
                bar = cli_out_progress_create(out_ctx, "Downloading (HTTPS)", 100);
                rc = http_download(https_url, cache_path, MAX_RETRIES, download_progress, bar);
                cli_out_progress_finish(bar);
                free(https_url);
            }

            if (rc != 0) {
                cdo_log_error("Failed to download tool archive from: %s", url);
                if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
                return 1;
            }
        }
    }

    /* Step 4: Checksum verification (before extraction) */
    if (checksum_str && checksum_str[0] != '\0') {
        ChecksumSpec spec = {0};
        int rc = checksum_parse(checksum_str, &spec);
        if (rc != 0) {
            cdo_log_error("Malformed checksum: %s", checksum_str);
            if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
            return 1;
        }

        cdo_log_info("Verifying checksum (%s)...", checksum_str);
        rc = checksum_verify_file(cache_path, &spec);
        if (rc != 0) {
            cdo_log_error("Checksum verification failed for '%s'", name);
            if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
            return 1;
        }
        cdo_log_info("Checksum verified");
    } else {
        cdo_log_warn("Checksum not provided for '%s' â€” archive integrity not verified", name);
    }

    /* Step 5: Create tool directory */
    if (pal_mkdir_p(tool_dir) != PAL_OK) {
        cdo_log_error("Failed to create tool directory: %s", tool_dir);
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    /* Step 6: Extract archive into tool directory */
    cdo_log_info("Extracting %s...", filename);
    int rc2 = extract_archive(cache_path, tool_dir);
    if (rc2 != 0) {
        cdo_log_error("Failed to extract archive: %s", cache_path);
        cdo_log_info("  hint: if on Windows, your antivirus may be blocking the file.");
        cdo_log_info("  hint: try adding '.cdo/cache' to your antivirus exclusions.");
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    /* Step 7: Write cdo-tool.toml manifest */
    rc2 = write_tool_manifest(tool_dir, name, url);
    if (rc2 != 0) {
        if (used_catalog) { catalog_resolve_result_free(&resolve_result); catalog_free(&catalog); }
        return 1;
    }

    /* Step 8: Report success */
    cdo_log_info("Tool '%s' installed successfully", name);

    if (used_catalog) {
        catalog_resolve_result_free(&resolve_result);
        catalog_free(&catalog);
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/* tool list                                                                   */
/* -------------------------------------------------------------------------- */

int cmd_tool_list(const CliParseResult* result, void* ctx) {
    (void)result;
    (void)ctx;

    if (pal_path_exists(TOOLS_DIR) != 0) {
        cdo_log_info("No tools installed");
        return 0;
    }

    cdo_log_info("Installed tools:");

    typedef struct {
        int count;
    } ListCtx;

    ListCtx list_ctx = { .count = 0 };
    pal_dir_walk(TOOLS_DIR, NULL, &list_ctx);

    cdo_log_info("  (use 'cdo tool install' to manage tools)");
    return 0;
}

/* -------------------------------------------------------------------------- */
/* tool remove                                                                 */
/* -------------------------------------------------------------------------- */

int cmd_tool_remove(const CliParseResult* result, void* ctx) {
    (void)ctx;

    if (result->positional_count < 1) {
        cdo_log_error("Missing tool name. Usage: cdo tool remove <name>");
        return 1;
    }

    const char* name = result->positional_values[0];

    char tool_dir[MAX_PATH_LEN];
    if (pal_path_join(tool_dir, sizeof(tool_dir), TOOLS_DIR, name) != 0) {
        cdo_log_error("Tool path too long");
        return 1;
    }

    if (pal_path_exists(tool_dir) != 0) {
        cdo_log_error("Tool '%s' is not installed", name);
        return 1;
    }

    int rc = pal_rmdir_r(tool_dir);
    if (rc != PAL_OK) {
        cdo_log_error("Failed to remove tool '%s'", name);
        return 1;
    }

    cdo_log_info("Tool '%s' removed", name);
    return 0;
}

/* -------------------------------------------------------------------------- */
// End of cmd_tool.c
