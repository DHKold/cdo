// test_depfile_parser.cpp — Unit tests for DepFileParser.
// Validates: Requirements 10.1, 10.3
//
// Tests GCC/Clang Makefile-style .d format, MSVC /showIncludes format,
// escaped paths, multi-line continuation, edge cases, and round-trip parsing.

#include "cdo_ut.h"
#include "build/depfile_parser.h"
#include "pal/pal.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

using namespace cdo::build;

// =============================================================================
// Helpers
// =============================================================================

#define DEPFILE_TEST_DIR "build/test_tmp_depfile"

/// Ensure test temp directory exists.
static int ensure_depfile_test_dir() {
    return pal_mkdir_p(DEPFILE_TEST_DIR);
}

/// Write content to a temp dep file and return the path.
static std::string write_dep_file(const char* name, const char* content) {
    ensure_depfile_test_dir();
    std::string path = std::string(DEPFILE_TEST_DIR) + "/" + name;
    pal_file_write(path.c_str(), content, std::strlen(content));
    return path;
}

// =============================================================================
// GCC Single-line Format
// =============================================================================

TEST(depfile_gcc_single_line_basic) {
    const char* content = "target.o: source.c header.h\n";
    std::string path = write_dep_file("gcc_single.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "target.o");
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 2);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "source.c");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "header.h");
    return 0;
}

TEST(depfile_gcc_single_line_multiple_deps) {
    const char* content = "main.o: main.c stdio.h stdlib.h string.h\n";
    std::string path = write_dep_file("gcc_multi_deps.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "main.o");
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 4);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "main.c");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "stdio.h");
    TEST_ASSERT_STR_EQ(parser.dependencies()[2].c_str(), "stdlib.h");
    TEST_ASSERT_STR_EQ(parser.dependencies()[3].c_str(), "string.h");
    return 0;
}

// =============================================================================
// GCC Multi-line with Backslash Continuation
// =============================================================================

TEST(depfile_gcc_multiline_backslash_continuation) {
    const char* content =
        "main.o: main.c \\\n"
        "  include/header1.h \\\n"
        "  include/header2.h\n";
    std::string path = write_dep_file("gcc_multiline.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "main.o");
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 3);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "main.c");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "include/header1.h");
    TEST_ASSERT_STR_EQ(parser.dependencies()[2].c_str(), "include/header2.h");
    return 0;
}

TEST(depfile_gcc_multiline_many_continuations) {
    const char* content =
        "build/obj/foo.o: \\\n"
        " src/foo.c \\\n"
        " src/foo.h \\\n"
        " src/bar.h \\\n"
        " src/baz.h\n";
    std::string path = write_dep_file("gcc_many_cont.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "build/obj/foo.o");
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 4);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "src/foo.c");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "src/foo.h");
    TEST_ASSERT_STR_EQ(parser.dependencies()[2].c_str(), "src/bar.h");
    TEST_ASSERT_STR_EQ(parser.dependencies()[3].c_str(), "src/baz.h");
    return 0;
}

// =============================================================================
// Paths with Escaped Spaces
// =============================================================================

TEST(depfile_gcc_escaped_spaces_in_paths) {
    // `\ ` in dep files represents a literal space in the path
    const char* content = "target.o: my\\ project/source.c my\\ headers/util.h\n";
    std::string path = write_dep_file("gcc_escaped_spaces.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "target.o");
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 2);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "my project/source.c");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "my headers/util.h");
    return 0;
}

TEST(depfile_gcc_escaped_special_chars) {
    // Test \# and \$ escaping
    const char* content = "out.o: file\\#1.c path\\$var/header.h\n";
    std::string path = write_dep_file("gcc_escaped_special.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 2);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "file#1.c");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "path$var/header.h");
    return 0;
}

// =============================================================================
// MSVC /showIncludes Format
// =============================================================================

TEST(depfile_msvc_basic) {
    const char* content =
        "Note: including file:   C:\\Program Files\\include\\stdio.h\n"
        "Note: including file:   C:\\Project\\src\\myheader.h\n";
    std::string path = write_dep_file("msvc_basic.d", content);

    DepFileParser parser(DepFileParser::Format::Msvc);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 2);
    // Paths should have backslashes normalized to forward slashes
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "C:/Program Files/include/stdio.h");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "C:/Project/src/myheader.h");
    // MSVC has no explicit target
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "");
    return 0;
}

TEST(depfile_msvc_deduplicates) {
    const char* content =
        "Note: including file:   C:\\include\\common.h\n"
        "Note: including file:   C:\\include\\types.h\n"
        "Note: including file:   C:\\include\\common.h\n"
        "Note: including file:   C:\\include\\types.h\n"
        "Note: including file:   C:\\include\\unique.h\n";
    std::string path = write_dep_file("msvc_dedup.d", content);

    DepFileParser parser(DepFileParser::Format::Msvc);
    TEST_ASSERT(parser.parse(path) == true);
    // Should deduplicate: only 3 unique paths
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 3);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "C:/include/common.h");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "C:/include/types.h");
    TEST_ASSERT_STR_EQ(parser.dependencies()[2].c_str(), "C:/include/unique.h");
    return 0;
}

TEST(depfile_msvc_ignores_non_include_lines) {
    const char* content =
        "Compiling...\n"
        "source.cpp\n"
        "Note: including file:   C:\\sdk\\header.h\n"
        "Generating code...\n";
    std::string path = write_dep_file("msvc_mixed.d", content);

    DepFileParser parser(DepFileParser::Format::Msvc);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 1);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "C:/sdk/header.h");
    return 0;
}

// =============================================================================
// Empty Dep File → Success with Empty Dependency List
// =============================================================================

TEST(depfile_empty_file_gcc_returns_success) {
    const char* content = "";
    std::string path = write_dep_file("empty_gcc.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 0);
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "");
    return 0;
}

TEST(depfile_empty_file_msvc_returns_success) {
    const char* content = "";
    std::string path = write_dep_file("empty_msvc.d", content);

    DepFileParser parser(DepFileParser::Format::Msvc);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 0);
    return 0;
}

TEST(depfile_empty_file_auto_returns_success) {
    const char* content = "";
    std::string path = write_dep_file("empty_auto.d", content);

    DepFileParser parser(DepFileParser::Format::Auto);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 0);
    return 0;
}

// =============================================================================
// Malformed/Unreadable File → parse Returns False, lastError() Populated
// =============================================================================

TEST(depfile_nonexistent_file_returns_false) {
    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse("__nonexistent_depfile_xyz_999__.d") == false);
    TEST_ASSERT(parser.lastError().size() > 0);
    return 0;
}

TEST(depfile_nonexistent_file_auto_returns_false) {
    DepFileParser parser(DepFileParser::Format::Auto);
    TEST_ASSERT(parser.parse("__nonexistent_depfile_auto__.d") == false);
    TEST_ASSERT(parser.lastError().size() > 0);
    return 0;
}

// =============================================================================
// Paths with Mixed Line Endings (CRLF, LF)
// =============================================================================

TEST(depfile_gcc_crlf_line_endings) {
    const char* content = "out.o: src/a.c src/b.h\r\n";
    std::string path = write_dep_file("gcc_crlf.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "out.o");
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 2);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "src/a.c");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "src/b.h");
    return 0;
}

TEST(depfile_gcc_multiline_crlf_continuation) {
    // Backslash followed by CRLF
    const char* content = "out.o: src/a.c \\\r\n  src/b.h \\\r\n  src/c.h\r\n";
    std::string path = write_dep_file("gcc_crlf_cont.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "out.o");
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 3);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "src/a.c");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "src/b.h");
    TEST_ASSERT_STR_EQ(parser.dependencies()[2].c_str(), "src/c.h");
    return 0;
}

TEST(depfile_msvc_crlf_line_endings) {
    const char* content =
        "Note: including file:   C:\\inc\\a.h\r\n"
        "Note: including file:   C:\\inc\\b.h\r\n";
    std::string path = write_dep_file("msvc_crlf.d", content);

    DepFileParser parser(DepFileParser::Format::Msvc);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 2);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "C:/inc/a.h");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "C:/inc/b.h");
    return 0;
}

// =============================================================================
// Very Long Paths (>260 chars)
// =============================================================================

TEST(depfile_gcc_very_long_path) {
    // Build a path that exceeds 260 characters
    std::string long_dir;
    for (int i = 0; i < 30; ++i) {
        long_dir += "subdir_ab/";
    }
    std::string long_path = long_dir + "very_long_filename_header.h";
    // Verify it's actually > 260 chars
    TEST_ASSERT((int)long_path.size() > 260);

    std::string content = "output.o: source.c " + long_path + "\n";
    std::string path = write_dep_file("gcc_long_path.d", content.c_str());

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 2);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "source.c");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), long_path.c_str());
    return 0;
}

TEST(depfile_msvc_very_long_path) {
    // Build a MSVC-style long path
    std::string long_dir = "C:\\";
    for (int i = 0; i < 30; ++i) {
        long_dir += "subdir_ab\\";
    }
    std::string long_path = long_dir + "very_long_filename_header.h";
    TEST_ASSERT((int)long_path.size() > 260);

    std::string content = "Note: including file:   " + long_path + "\n";
    std::string path = write_dep_file("msvc_long_path.d", content.c_str());

    DepFileParser parser(DepFileParser::Format::Msvc);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 1);
    // Backslashes should be normalized to forward slashes
    std::string expected = long_path;
    for (char& c : expected) {
        if (c == '\\') c = '/';
    }
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), expected.c_str());
    return 0;
}

// =============================================================================
// Round-trip: Known Deps Formatted as .d Content → Parsed Back to Original List
// =============================================================================

TEST(depfile_gcc_round_trip) {
    // Write a .d file with known dependencies, parse it, verify we get them back
    std::vector<std::string> expected_deps = {
        "src/main.c",
        "include/config.h",
        "include/utils.h",
        "lib/external.h"
    };

    // Format as GCC .d content
    std::string content = "build/main.o:";
    for (const auto& dep : expected_deps) {
        content += " " + dep;
    }
    content += "\n";

    std::string path = write_dep_file("gcc_roundtrip.d", content.c_str());

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "build/main.o");
    TEST_ASSERT_EQ((int)parser.dependencies().size(), (int)expected_deps.size());

    for (size_t i = 0; i < expected_deps.size(); ++i) {
        TEST_ASSERT_STR_EQ(parser.dependencies()[i].c_str(), expected_deps[i].c_str());
    }
    return 0;
}

TEST(depfile_gcc_round_trip_with_escaped_spaces) {
    // Paths with spaces need escaping in .d format
    std::vector<std::string> expected_deps = {
        "my project/source.c",
        "my headers/config.h"
    };

    // Format with escaped spaces (as GCC would write them)
    std::string content = "target.o: my\\ project/source.c my\\ headers/config.h\n";
    std::string path = write_dep_file("gcc_roundtrip_spaces.d", content.c_str());

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), (int)expected_deps.size());

    for (size_t i = 0; i < expected_deps.size(); ++i) {
        TEST_ASSERT_STR_EQ(parser.dependencies()[i].c_str(), expected_deps[i].c_str());
    }
    return 0;
}

// =============================================================================
// Auto Mode Tests
// =============================================================================

TEST(depfile_auto_detects_gcc_format) {
    const char* content = "main.o: main.c header.h\n";
    std::string path = write_dep_file("auto_gcc.d", content);

    DepFileParser parser(DepFileParser::Format::Auto);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "main.o");
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 2);
    return 0;
}

TEST(depfile_auto_detects_msvc_format) {
    const char* content =
        "Note: including file:   C:\\inc\\stdio.h\n"
        "Note: including file:   C:\\inc\\stdlib.h\n";
    std::string path = write_dep_file("auto_msvc.d", content);

    DepFileParser parser(DepFileParser::Format::Auto);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 2);
    return 0;
}

// =============================================================================
// Additional Edge Cases
// =============================================================================

TEST(depfile_gcc_target_only_no_deps) {
    // Target with colon but no dependencies
    const char* content = "output.o:\n";
    std::string path = write_dep_file("gcc_no_deps.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "output.o");
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 0);
    return 0;
}

TEST(depfile_gcc_backslash_path_separators_normalized) {
    // GCC sometimes emits Windows-style backslash paths
    const char* content = "obj\\main.o: src\\main.c inc\\header.h\n";
    std::string path = write_dep_file("gcc_backslash.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    // Backslashes in target and deps should be normalized to forward slashes
    TEST_ASSERT_STR_EQ(parser.target().c_str(), "obj/main.o");
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 2);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "src/main.c");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "inc/header.h");
    return 0;
}

TEST(depfile_gcc_extra_whitespace_between_deps) {
    // Multiple spaces/tabs between dependencies
    const char* content = "out.o:   a.c    b.h\t\tc.h\n";
    std::string path = write_dep_file("gcc_extra_ws.d", content);

    DepFileParser parser(DepFileParser::Format::GccClang);
    TEST_ASSERT(parser.parse(path) == true);
    TEST_ASSERT_EQ((int)parser.dependencies().size(), 3);
    TEST_ASSERT_STR_EQ(parser.dependencies()[0].c_str(), "a.c");
    TEST_ASSERT_STR_EQ(parser.dependencies()[1].c_str(), "b.h");
    TEST_ASSERT_STR_EQ(parser.dependencies()[2].c_str(), "c.h");
    return 0;
}

// =============================================================================
// MSVC Registration (only needed on MSVC)
// =============================================================================

#ifdef _MSC_VER
#include <windows.h>

static void __cdecl register_depfile_parser_tests(void) {
    REGISTER_TEST(depfile_gcc_single_line_basic);
    REGISTER_TEST(depfile_gcc_single_line_multiple_deps);
    REGISTER_TEST(depfile_gcc_multiline_backslash_continuation);
    REGISTER_TEST(depfile_gcc_multiline_many_continuations);
    REGISTER_TEST(depfile_gcc_escaped_spaces_in_paths);
    REGISTER_TEST(depfile_gcc_escaped_special_chars);
    REGISTER_TEST(depfile_msvc_basic);
    REGISTER_TEST(depfile_msvc_deduplicates);
    REGISTER_TEST(depfile_msvc_ignores_non_include_lines);
    REGISTER_TEST(depfile_empty_file_gcc_returns_success);
    REGISTER_TEST(depfile_empty_file_msvc_returns_success);
    REGISTER_TEST(depfile_empty_file_auto_returns_success);
    REGISTER_TEST(depfile_nonexistent_file_returns_false);
    REGISTER_TEST(depfile_nonexistent_file_auto_returns_false);
    REGISTER_TEST(depfile_gcc_crlf_line_endings);
    REGISTER_TEST(depfile_gcc_multiline_crlf_continuation);
    REGISTER_TEST(depfile_msvc_crlf_line_endings);
    REGISTER_TEST(depfile_gcc_very_long_path);
    REGISTER_TEST(depfile_msvc_very_long_path);
    REGISTER_TEST(depfile_gcc_round_trip);
    REGISTER_TEST(depfile_gcc_round_trip_with_escaped_spaces);
    REGISTER_TEST(depfile_auto_detects_gcc_format);
    REGISTER_TEST(depfile_auto_detects_msvc_format);
    REGISTER_TEST(depfile_gcc_target_only_no_deps);
    REGISTER_TEST(depfile_gcc_backslash_path_separators_normalized);
    REGISTER_TEST(depfile_gcc_extra_whitespace_between_deps);
}

#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void(__cdecl* _reg_depfile)(void) = register_depfile_parser_tests;
#endif
