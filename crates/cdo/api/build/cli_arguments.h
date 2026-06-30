/**
 * cli_arguments.h - C++ representation of build command arguments.
 *
 * Converts from the C CliParseResult (produced by cdo_cli) into a typed,
 * validated C++ object at the C/C++ boundary. Designed to eventually replace
 * the C struct when the CLI migrates to C++.
 */
#ifndef CDO_BUILD_CLI_ARGUMENTS_H
#define CDO_BUILD_CLI_ARGUMENTS_H

#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif
#include "cmd/cli_cmd.h"
#ifdef __cplusplus
}
#endif

namespace cdo::build::cli {

/// C++ representation of build command arguments.
/// Converts from the C CliParseResult at the boundary, extracting and validating
/// all build-specific options into typed accessors.
///
/// Usage:
///   Arguments args(parse_result);
///   if (!args.isValid()) { /* handle error via args.lastError() */ }
///   auto profile = args.profile();  // "debug", "release", "relwithdebinfo"
///
class Arguments {
public:
    /// Construct from a CliParseResult. Extracts and validates all build-specific
    /// options from the parse result's arg_values[] and positional_values[].
    /// Check isValid() after construction; if false, lastError() describes the problem.
    explicit Arguments(const CliParseResult* result);

    /// Returns true if the parse result was successfully converted.
    bool isValid() const;

    /// Human-readable error description if isValid() returns false.
    const std::string& lastError() const;

    /// Absolute path to the workspace root (directory containing cdo.toml).
    const std::string& workspaceRoot() const;

    /// List of crate names to build. Empty means "all crates in workspace".
    const std::vector<std::string>& crateFilter() const;

    /// Build profile: "debug", "release", or "relwithdebinfo".
    const std::string& profile() const;

    /// Number of parallel jobs. 0 means auto-detect (resolve to cpu_count at dispatch time).
    int jobs() const;

    /// Whether --force was specified (rebuild all targets regardless of freshness).
    bool force() const;

    /// Whether --clean was specified (delete build directory before building).
    bool clean() const;

    /// Whether the SHA-256 object cache is enabled (true unless --no-cache was given).
    bool cacheEnabled() const;

    /// Verbosity level: 0=error, 1=warn, 2=info (default), 3=debug, 4=trace.
    int verbosity() const;

private:
    std::string workspace_root_;
    std::vector<std::string> crate_filter_;
    std::string profile_;
    int jobs_ = 0;
    bool force_ = false;
    bool clean_ = false;
    bool cache_enabled_ = true;
    int verbosity_ = 2;
    bool valid_ = false;
    std::string error_;
};

} // namespace cdo::build::cli

#endif // CDO_BUILD_CLI_ARGUMENTS_H
