/**
 * cli_arguments.cpp - Implementation of cli::Arguments conversion from CliParseResult.
 *
 * Extracts and validates build-specific options from the C CliParseResult struct
 * produced by the cdo_cli framework.
 */
#include "build/cli_arguments.h"
#include "core/cli_arg_access.h"
#include "pal/pal.h"

#include <cstring>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

namespace cdo::build::cli {

// =============================================================================
// Helpers
// =============================================================================

/// Walk up from start_dir looking for cdo.toml (or cdo.yaml, cdo.json).
/// Returns the directory containing the manifest, or empty string on failure.
static std::string discover_workspace_root(const char* start_dir) {
    if (!start_dir || start_dir[0] == '\0') return {};

    char current[520];
    std::strncpy(current, start_dir, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';
    pal_path_normalize(current);

    // Walk up until we find cdo.toml or run out of parents
    for (;;) {
        char manifest[520];
        if (pal_path_join(manifest, sizeof(manifest), current, "cdo.toml") == 0) {
            if (pal_path_exists(manifest) == PAL_OK) {
                return std::string(current);
            }
        }

        // Try parent: find last '/' and truncate
        char* last_sep = std::strrchr(current, '/');
        if (!last_sep || last_sep == current) {
            // Check root "/" on Unix or we're at root
            if (last_sep == current) {
                // Check "/" itself
                char root_manifest[520];
                if (pal_path_join(root_manifest, sizeof(root_manifest), "/", "cdo.toml") == 0) {
                    if (pal_path_exists(root_manifest) == PAL_OK) {
                        return std::string("/");
                    }
                }
            }
            break;
        }
        *last_sep = '\0';
    }

    return {};
}

// =============================================================================
// Arguments Construction
// =============================================================================

Arguments::Arguments(const CliParseResult* result) {
    if (!result) {
        error_ = "null CliParseResult";
        valid_ = false;
        return;
    }

    // --- Extract named args ---
    bool release_flag = cli_arg_get_bool(result, "release");
    const char* profile_val = cli_arg_get_str(result, "profile");
    bool no_cache = cli_arg_get_bool(result, "no-cache");
    bool force_flag = cli_arg_get_bool(result, "force");
    bool clean_flag = cli_arg_get_bool(result, "clean");
    bool verbose_flag = cli_arg_get_bool(result, "verbose");
    int jobs_val = cli_arg_get_int(result, "jobs", 0);

    // --- Resolve profile ---
    if (profile_val && profile_val[0] != '\0') {
        profile_ = profile_val;
    } else if (release_flag) {
        profile_ = "release";
    } else {
        profile_ = "debug";
    }

    // --- Resolve other flags ---
    jobs_ = jobs_val;
    force_ = force_flag;
    clean_ = clean_flag;
    cache_enabled_ = !no_cache;
    verbosity_ = verbose_flag ? 3 : 2;

    // --- Extract positional crate names ---
    for (int i = 0; i < result->positional_count; i++) {
        if (result->positional_values[i] && result->positional_values[i][0] != '\0') {
            crate_filter_.emplace_back(result->positional_values[i]);
        }
    }

    // --- Discover workspace root ---
    char cwd[520];
#ifdef _WIN32
    if (_getcwd(cwd, sizeof(cwd)) == NULL) {
#else
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
#endif
        error_ = "failed to determine current working directory";
        valid_ = false;
        return;
    }

    workspace_root_ = discover_workspace_root(cwd);
    if (workspace_root_.empty()) {
        error_ = "no workspace found (no cdo.toml in current directory or any parent)";
        valid_ = false;
        return;
    }

    valid_ = true;
}

// =============================================================================
// Accessors
// =============================================================================

bool Arguments::isValid() const { return valid_; }
const std::string& Arguments::lastError() const { return error_; }
const std::string& Arguments::workspaceRoot() const { return workspace_root_; }
const std::vector<std::string>& Arguments::crateFilter() const { return crate_filter_; }
const std::string& Arguments::profile() const { return profile_; }
int Arguments::jobs() const { return jobs_; }
bool Arguments::force() const { return force_; }
bool Arguments::clean() const { return clean_; }
bool Arguments::cacheEnabled() const { return cache_enabled_; }
int Arguments::verbosity() const { return verbosity_; }

} // namespace cdo::build::cli
