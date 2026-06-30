/**
 * depfile_parser.h - Compiler-generated dependency file parser.
 *
 * Defines the DepFileParser class which extracts header dependencies from
 * compiler-generated .d files. Supports GCC/Clang Makefile-style format
 * and MSVC /showIncludes output, with an Auto mode that attempts both.
 *
 * Used by BuildCSource/BuildCppSource tasks to determine the full set of
 * input artifacts (including transitively included headers) for freshness
 * condition evaluation.
 *
 * Part of the cdo::build pipeline refactor.
 */
#ifndef CDO_BUILD_DEPFILE_PARSER_H
#define CDO_BUILD_DEPFILE_PARSER_H

#include <string>
#include <vector>

namespace cdo::build {

/// Parses compiler-generated dependency files (.d) to extract header dependencies.
/// Supports GCC/Clang Makefile-style format and MSVC /showIncludes output.
class DepFileParser {
public:
    /// Compiler format hint for parsing strategy selection.
    enum class Format {
        GccClang,   // Makefile-style: "target.o: dep1.h dep2.h \\\n dep3.h"
        Msvc,       // /showIncludes output: "Note: including file: <path>"
        Auto,       // Attempt GCC/Clang first, fall back to MSVC
    };

    /// Construct a DepFileParser with the given format hint.
    /// Defaults to Auto which tries GCC/Clang first, then MSVC on failure.
    explicit DepFileParser(Format format = Format::Auto);

    /// Parse a dependency file at the given path.
    /// Returns true on success (dependencies extracted), false on failure
    /// (file not found, unreadable, or unparseable).
    /// On success, results are accessible via dependencies() and target().
    /// On failure, the error is accessible via lastError().
    bool parse(const std::string& dep_file_path);

    /// Get the list of dependency paths extracted from the last successful parse.
    /// Returns empty vector if parse() was not called or failed.
    const std::vector<std::string>& dependencies() const;

    /// Get the target path from the dependency file (the left-hand side of the rule).
    /// Empty string if not available (e.g., MSVC format has no explicit target).
    const std::string& target() const;

    /// Get a human-readable error message if parse() returned false.
    const std::string& lastError() const;

private:
    bool parseGccClang(const std::string& content);
    bool parseMsvc(const std::string& content);
    std::string normalizePathSeparators(const std::string& path) const;
    std::string unescapePath(const std::string& raw) const;

    Format format_;
    std::vector<std::string> dependencies_;
    std::string target_;
    std::string error_;
};

} // namespace cdo::build

#endif // CDO_BUILD_DEPFILE_PARSER_H
