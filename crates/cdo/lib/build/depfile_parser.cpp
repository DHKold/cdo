/**
 * depfile_parser.cpp - Implementation of DepFileParser.
 *
 * Parses GCC/Clang Makefile-style .d files and MSVC /showIncludes output
 * to extract header dependency lists for incremental build decisions.
 */
#include "build/depfile_parser.h"
#include "pal/pal.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace cdo::build {

// =============================================================================
// Construction
// =============================================================================

DepFileParser::DepFileParser(Format format)
    : format_(format) {}

// =============================================================================
// Public API
// =============================================================================

bool DepFileParser::parse(const std::string& dep_file_path) {
    // Clear previous results
    dependencies_.clear();
    target_.clear();
    error_.clear();

    // Read the file via PAL
    char* buf = nullptr;
    size_t len = 0;
    int rc = pal_file_read(dep_file_path.c_str(), &buf, &len);
    if (rc != 0) {
        error_ = "failed to read file: " + dep_file_path;
        return false;
    }

    std::string content(buf, len);
    std::free(buf);

    // Dispatch based on format
    switch (format_) {
        case Format::GccClang:
            return parseGccClang(content);
        case Format::Msvc:
            return parseMsvc(content);
        case Format::Auto: {
            // Heuristic: if the content contains "including file:" markers,
            // it is MSVC /showIncludes output — try MSVC first.
            if (content.find("including file:") != std::string::npos) {
                if (parseMsvc(content)) {
                    return true;
                }
                // Clear and try GCC as fallback
                dependencies_.clear();
                target_.clear();
                error_.clear();
            }
            // Try GCC/Clang — if it finds a valid target, success
            if (parseGccClang(content)) {
                return true;
            }
            // Clear partial results and try MSVC as final fallback
            dependencies_.clear();
            target_.clear();
            error_.clear();
            return parseMsvc(content);
        }
    }

    error_ = "unknown format";
    return false;
}

const std::vector<std::string>& DepFileParser::dependencies() const {
    return dependencies_;
}

const std::string& DepFileParser::target() const {
    return target_;
}

const std::string& DepFileParser::lastError() const {
    return error_;
}

// =============================================================================
// GCC/Clang Format Parsing
// =============================================================================

bool DepFileParser::parseGccClang(const std::string& content) {
    // Handle empty content — success with empty dependency list
    if (content.empty()) {
        return true;
    }

    // Step 1: Handle backslash-newline continuation — join lines
    std::string joined;
    joined.reserve(content.size());
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\\' && i + 1 < content.size()) {
            char next = content[i + 1];
            // Backslash-newline (LF) or backslash-CRLF continuation
            if (next == '\n') {
                // Skip the backslash and the newline, replace with a space
                ++i; // skip '\n'
                joined += ' ';
                continue;
            }
            if (next == '\r' && i + 2 < content.size() && content[i + 2] == '\n') {
                // Skip backslash + \r\n
                i += 2; // skip '\r\n'
                joined += ' ';
                continue;
            }
        }
        joined += content[i];
    }

    // Step 2: Find the colon separating target from dependencies.
    // The colon can appear inside a Windows drive letter (e.g., C:\path),
    // so we look for ": " (colon followed by space/tab) or ":\t" after the first char,
    // skipping drive-letter patterns (single alpha followed by colon followed by backslash or slash).
    size_t colon_pos = std::string::npos;
    for (size_t i = 0; i < joined.size(); ++i) {
        if (joined[i] == ':') {
            // Skip drive letters: single alphabetic char before colon, followed by path separator
            if (i == 1 && std::isalpha(static_cast<unsigned char>(joined[0])) &&
                i + 1 < joined.size() && (joined[i + 1] == '/' || joined[i + 1] == '\\')) {
                continue;
            }
            // Found the rule separator
            colon_pos = i;
            break;
        }
    }

    if (colon_pos == std::string::npos) {
        // No colon found — not a valid GCC/Clang dep file
        error_ = "no target separator ':' found in dep file";
        return false;
    }

    // Extract the target (left of colon)
    std::string raw_target = joined.substr(0, colon_pos);
    // Trim whitespace from target
    while (!raw_target.empty() && (raw_target.back() == ' ' || raw_target.back() == '\t')) {
        raw_target.pop_back();
    }
    while (!raw_target.empty() && (raw_target.front() == ' ' || raw_target.front() == '\t')) {
        raw_target.erase(raw_target.begin());
    }
    target_ = normalizePathSeparators(unescapePath(raw_target));

    // Step 3: Extract dependencies (right of colon)
    std::string deps_str = joined.substr(colon_pos + 1);

    // Split by whitespace, handling escaped spaces
    dependencies_.clear();
    std::string current_path;
    for (size_t i = 0; i < deps_str.size(); ++i) {
        char c = deps_str[i];

        if (c == '\\' && i + 1 < deps_str.size()) {
            char next = deps_str[i + 1];
            // Escaped space: `\ ` represents a literal space in the path
            if (next == ' ') {
                current_path += '\\';
                current_path += ' ';
                ++i;
                continue;
            }
            // Other escape sequences preserved for later unescaping
            current_path += c;
            continue;
        }

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            // Whitespace delimiter — finish current path
            if (!current_path.empty()) {
                std::string unescaped = unescapePath(current_path);
                std::string normalized = normalizePathSeparators(unescaped);
                if (!normalized.empty()) {
                    dependencies_.push_back(normalized);
                }
                current_path.clear();
            }
            continue;
        }

        current_path += c;
    }

    // Don't forget the last path (no trailing whitespace)
    if (!current_path.empty()) {
        std::string unescaped = unescapePath(current_path);
        std::string normalized = normalizePathSeparators(unescaped);
        if (!normalized.empty()) {
            dependencies_.push_back(normalized);
        }
    }

    return true;
}

// =============================================================================
// MSVC Format Parsing
// =============================================================================

bool DepFileParser::parseMsvc(const std::string& content) {
    // Handle empty content — success with empty dependency list
    if (content.empty()) {
        return true;
    }

    // MSVC /showIncludes produces lines like:
    //   Note: including file:   C:\Program Files\include\stdio.h
    // The prefix may be localized (e.g., "Remarque : fichier inclus :" in French).
    // We look for "including file:" as the key marker (locale-invariant portion).
    const std::string marker = "including file:";

    dependencies_.clear();
    target_.clear(); // MSVC format has no explicit target

    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        // Handle CRLF: if line ends with \r, strip it
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Look for the marker
        size_t pos = line.find(marker);
        if (pos == std::string::npos) {
            continue;
        }

        // Extract path after the marker
        std::string path = line.substr(pos + marker.size());

        // Trim leading whitespace
        size_t start = 0;
        while (start < path.size() && (path[start] == ' ' || path[start] == '\t')) {
            ++start;
        }
        path = path.substr(start);

        // Trim trailing whitespace
        while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) {
            path.pop_back();
        }

        if (path.empty()) {
            continue;
        }

        // Normalize path separators
        std::string normalized = normalizePathSeparators(path);

        // Deduplicate
        if (std::find(dependencies_.begin(), dependencies_.end(), normalized) == dependencies_.end()) {
            dependencies_.push_back(normalized);
        }
    }

    return true;
}

// =============================================================================
// Path Utilities
// =============================================================================

std::string DepFileParser::normalizePathSeparators(const std::string& path) const {
    std::string result = path;
    for (char& c : result) {
        if (c == '\\') {
            c = '/';
        }
    }
    return result;
}

std::string DepFileParser::unescapePath(const std::string& raw) const {
    std::string result;
    result.reserve(raw.size());

    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            char next = raw[i + 1];
            switch (next) {
                case ' ':
                    result += ' ';
                    ++i;
                    continue;
                case '#':
                    result += '#';
                    ++i;
                    continue;
                case '$':
                    result += '$';
                    ++i;
                    continue;
                default:
                    // Not a recognized escape — keep the backslash
                    result += raw[i];
                    continue;
            }
        }
        result += raw[i];
    }

    return result;
}

} // namespace cdo::build
