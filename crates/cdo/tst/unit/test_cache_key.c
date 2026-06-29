// crates/cdo/tst/unit/test_cache_key.c
// Unit tests for cache key computation and dep file parsing
// Validates: Requirements 1.1, 1.2, 1.3, 1.4, 6.1, 6.2, 6.4
#include "cdo_ut.h"
#include "core/cache.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
// Helper utilities
// =============================================================================

static void get_temp_dir(char* buf, size_t size, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(buf, size, "%s/cdo_test_cachekey_%s", tmp, suffix);
    pal_path_normalize(buf);
}

/// Write a text file at the given path.
static int write_file(const char* path, const char* content) {
    return pal_file_write(path, content, strlen(content));
}

/// Create a simple GCC-format dep file listing given headers.
/// Format: "target.o: source.c header1.h header2.h\n"
static int write_dep_file_gcc(const char* dep_path, const char* target, const char* source, const char** headers, int header_count) {
    char buf[4096];
    int off = snprintf(buf, sizeof(buf), "%s: %s", target, source);
    for (int i = 0; i < header_count; i++) {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, " %s", headers[i]);
    }
    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "\n");
    return pal_file_write(dep_path, buf, (size_t)off);
}

/// Build standard CacheKeyInputs pointing to files in a temp dir.
static CacheKeyInputs make_inputs(const char* source_path, const char* dep_path) {
    CacheKeyInputs inputs = {0};
    inputs.source_path = source_path;
    inputs.compiler_path = "/usr/bin/gcc";
    inputs.compiler_version = "13.2.0";
    inputs.language_standard = "c17";
    inputs.optimize = false;
    inputs.debug_info = false;
    inputs.defines = NULL;
    inputs.define_count = 0;
    inputs.include_paths = NULL;
    inputs.include_path_count = 0;
    inputs.dep_file_path = dep_path;
    return inputs;
}

// =============================================================================
// Requirement 1.3: Identical inputs → same key
// =============================================================================

TEST_SERIAL(cache_key_identical_inputs_same_key) {
    char root[520];
    get_temp_dir(root, sizeof(root), "identical");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create source file
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "main.c");
    write_file(src_path, "int main() { return 0; }\n");

    // Create a header
    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), root, "main.h");
    write_file(hdr_path, "#pragma once\n");

    // Create dep file referencing the header
    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "main.d");
    const char* hdrs[] = { hdr_path };
    write_dep_file_gcc(dep_path, "main.o", src_path, hdrs, 1);

    CacheKeyInputs inputs = make_inputs(src_path, dep_path);

    char key1[CACHE_KEY_HEX_LEN + 1] = {0};
    char key2[CACHE_KEY_HEX_LEN + 1] = {0};
    int rc1 = cache_compute_key(&inputs, key1);
    int rc2 = cache_compute_key(&inputs, key2);

    TEST_ASSERT_EQ(rc1, 0);
    TEST_ASSERT_EQ(rc2, 0);
    TEST_ASSERT_STR_EQ(key1, key2);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Requirement 1.1: Different source content → different key
// =============================================================================

TEST_SERIAL(cache_key_different_source_content) {
    char root[520];
    get_temp_dir(root, sizeof(root), "diffsrc");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "main.c");
    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), root, "main.h");
    write_file(hdr_path, "#pragma once\n");

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "main.d");
    const char* hdrs[] = { hdr_path };
    write_dep_file_gcc(dep_path, "main.o", src_path, hdrs, 1);

    CacheKeyInputs inputs = make_inputs(src_path, dep_path);

    // First key with content A
    write_file(src_path, "int main() { return 0; }\n");
    char key_a[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_a), 0);

    // Second key with content B
    write_file(src_path, "int main() { return 42; }\n");
    char key_b[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_b), 0);

    TEST_ASSERT(strcmp(key_a, key_b) != 0);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Requirement 1.1: Different compiler version → different key
// =============================================================================

TEST_SERIAL(cache_key_different_compiler_version) {
    char root[520];
    get_temp_dir(root, sizeof(root), "diffver");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "main.c");
    write_file(src_path, "int x = 1;\n");

    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), root, "a.h");
    write_file(hdr_path, "// header\n");

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "main.d");
    const char* hdrs[] = { hdr_path };
    write_dep_file_gcc(dep_path, "main.o", src_path, hdrs, 1);

    CacheKeyInputs inputs = make_inputs(src_path, dep_path);

    char key_v1[CACHE_KEY_HEX_LEN + 1] = {0};
    inputs.compiler_version = "13.2.0";
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_v1), 0);

    char key_v2[CACHE_KEY_HEX_LEN + 1] = {0};
    inputs.compiler_version = "14.1.0";
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_v2), 0);

    TEST_ASSERT(strcmp(key_v1, key_v2) != 0);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Requirement 1.1: Different defines → different key
// =============================================================================

TEST_SERIAL(cache_key_different_defines) {
    char root[520];
    get_temp_dir(root, sizeof(root), "diffdef");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "main.c");
    write_file(src_path, "int x = 1;\n");

    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), root, "a.h");
    write_file(hdr_path, "// header\n");

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "main.d");
    const char* hdrs[] = { hdr_path };
    write_dep_file_gcc(dep_path, "main.o", src_path, hdrs, 1);

    CacheKeyInputs inputs = make_inputs(src_path, dep_path);

    const char* defs_a[] = { "DEBUG=1", "VERSION=2" };
    inputs.defines = defs_a;
    inputs.define_count = 2;
    char key_a[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_a), 0);

    const char* defs_b[] = { "RELEASE=1", "VERSION=3" };
    inputs.defines = defs_b;
    inputs.define_count = 2;
    char key_b[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_b), 0);

    TEST_ASSERT(strcmp(key_a, key_b) != 0);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Requirement 1.1: Different include paths → different key
// =============================================================================

TEST_SERIAL(cache_key_different_include_paths) {
    char root[520];
    get_temp_dir(root, sizeof(root), "diffinc");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "main.c");
    write_file(src_path, "int x = 1;\n");

    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), root, "a.h");
    write_file(hdr_path, "// header\n");

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "main.d");
    const char* hdrs[] = { hdr_path };
    write_dep_file_gcc(dep_path, "main.o", src_path, hdrs, 1);

    CacheKeyInputs inputs = make_inputs(src_path, dep_path);

    const char* inc_a[] = { "/usr/include", "/opt/local/include" };
    inputs.include_paths = inc_a;
    inputs.include_path_count = 2;
    char key_a[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_a), 0);

    const char* inc_b[] = { "/usr/include", "/home/user/mylibs" };
    inputs.include_paths = inc_b;
    inputs.include_path_count = 2;
    char key_b[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_b), 0);

    TEST_ASSERT(strcmp(key_a, key_b) != 0);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Requirement 1.1: Different optimization flag → different key
// =============================================================================

TEST_SERIAL(cache_key_different_optimize_flag) {
    char root[520];
    get_temp_dir(root, sizeof(root), "diffopt");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "main.c");
    write_file(src_path, "int x = 1;\n");

    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), root, "a.h");
    write_file(hdr_path, "// header\n");

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "main.d");
    const char* hdrs[] = { hdr_path };
    write_dep_file_gcc(dep_path, "main.o", src_path, hdrs, 1);

    CacheKeyInputs inputs = make_inputs(src_path, dep_path);

    inputs.optimize = false;
    char key_noopt[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_noopt), 0);

    inputs.optimize = true;
    char key_opt[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_opt), 0);

    TEST_ASSERT(strcmp(key_noopt, key_opt) != 0);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Requirement 1.1: Sorted defines/includes – order shouldn't matter
// =============================================================================

TEST_SERIAL(cache_key_sorted_defines_order_independent) {
    char root[520];
    get_temp_dir(root, sizeof(root), "sortdef");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "main.c");
    write_file(src_path, "int x = 1;\n");

    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), root, "a.h");
    write_file(hdr_path, "// header\n");

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "main.d");
    const char* hdrs[] = { hdr_path };
    write_dep_file_gcc(dep_path, "main.o", src_path, hdrs, 1);

    CacheKeyInputs inputs = make_inputs(src_path, dep_path);

    // Defines in order A, B
    const char* defs_ab[] = { "A=1", "B=2" };
    inputs.defines = defs_ab;
    inputs.define_count = 2;
    char key_ab[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_ab), 0);

    // Defines in order B, A (should sort internally and produce same key)
    const char* defs_ba[] = { "B=2", "A=1" };
    inputs.defines = defs_ba;
    inputs.define_count = 2;
    char key_ba[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_ba), 0);

    TEST_ASSERT_STR_EQ(key_ab, key_ba);

    pal_rmdir_r(root);
    return 0;
}

TEST_SERIAL(cache_key_sorted_includes_order_independent) {
    char root[520];
    get_temp_dir(root, sizeof(root), "sortinc");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "main.c");
    write_file(src_path, "int x = 1;\n");

    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), root, "a.h");
    write_file(hdr_path, "// header\n");

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "main.d");
    const char* hdrs[] = { hdr_path };
    write_dep_file_gcc(dep_path, "main.o", src_path, hdrs, 1);

    CacheKeyInputs inputs = make_inputs(src_path, dep_path);

    // Includes in order X, Y
    const char* inc_xy[] = { "/path/x", "/path/y" };
    inputs.include_paths = inc_xy;
    inputs.include_path_count = 2;
    char key_xy[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_xy), 0);

    // Includes in order Y, X
    const char* inc_yx[] = { "/path/y", "/path/x" };
    inputs.include_paths = inc_yx;
    inputs.include_path_count = 2;
    char key_yx[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, key_yx), 0);

    TEST_ASSERT_STR_EQ(key_xy, key_yx);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Requirement 6.1, 6.2: Dep file parsing — GCC format: simple
// =============================================================================

TEST_SERIAL(depfile_parse_gcc_simple) {
    char root[520];
    get_temp_dir(root, sizeof(root), "depgcc");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "test.d");
    write_file(dep_path, "main.o: main.c utils.h config.h\n");

    char** headers = NULL;
    int count = 0;
    int rc = depfile_parse(dep_path, &headers, &count);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(count, 3);
    TEST_ASSERT_STR_EQ(headers[0], "main.c");
    TEST_ASSERT_STR_EQ(headers[1], "utils.h");
    TEST_ASSERT_STR_EQ(headers[2], "config.h");

    for (int i = 0; i < count; i++) free(headers[i]);
    free(headers);
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Requirement 6.1: Dep file parsing — GCC format: multiline (backslash continuation)
// =============================================================================

TEST_SERIAL(depfile_parse_gcc_multiline) {
    char root[520];
    get_temp_dir(root, sizeof(root), "depgccml");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "multi.d");
    write_file(dep_path,
        "main.o: main.c \\\n"
        "  utils.h \\\n"
        "  config.h\n"
    );

    char** headers = NULL;
    int count = 0;
    int rc = depfile_parse(dep_path, &headers, &count);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(count, 3);
    TEST_ASSERT_STR_EQ(headers[0], "main.c");
    TEST_ASSERT_STR_EQ(headers[1], "utils.h");
    TEST_ASSERT_STR_EQ(headers[2], "config.h");

    for (int i = 0; i < count; i++) free(headers[i]);
    free(headers);
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Requirement 6.1: Dep file parsing — GCC format: paths with escaped spaces
// =============================================================================

TEST_SERIAL(depfile_parse_gcc_escaped_spaces) {
    char root[520];
    get_temp_dir(root, sizeof(root), "depgccsp");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "spaces.d");
    write_file(dep_path, "main.o: main.c path\\ with\\ spaces/header.h normal.h\n");

    char** headers = NULL;
    int count = 0;
    int rc = depfile_parse(dep_path, &headers, &count);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(count, 3);
    TEST_ASSERT_STR_EQ(headers[0], "main.c");
    TEST_ASSERT_STR_EQ(headers[1], "path with spaces/header.h");
    TEST_ASSERT_STR_EQ(headers[2], "normal.h");

    for (int i = 0; i < count; i++) free(headers[i]);
    free(headers);
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Requirement 6.1: Dep file parsing — MSVC format
// =============================================================================

TEST_SERIAL(depfile_parse_msvc_format) {
    char root[520];
    get_temp_dir(root, sizeof(root), "depmsvc");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "msvc.d");
    write_file(dep_path,
        "Note: including file: C:\\Program Files\\include\\stdio.h\n"
        "Note: including file:   C:\\myproject\\utils.h\n"
        "Note: including file:     C:\\myproject\\config.h\n"
    );

    char** headers = NULL;
    int count = 0;
    int rc = depfile_parse(dep_path, &headers, &count);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(count, 3);
    TEST_ASSERT_STR_EQ(headers[0], "C:\\Program Files\\include\\stdio.h");
    TEST_ASSERT_STR_EQ(headers[1], "C:\\myproject\\utils.h");
    TEST_ASSERT_STR_EQ(headers[2], "C:\\myproject\\config.h");

    for (int i = 0; i < count; i++) free(headers[i]);
    free(headers);
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Requirement 1.2: Missing dep file (NULL) → non-zero return
// =============================================================================

TEST(cache_key_null_dep_file_returns_nonzero) {
    // With dep_file_path = NULL, cache_compute_key should return non-zero (treat as miss)
    CacheKeyInputs inputs = {0};
    inputs.source_path = "some_source.c";
    inputs.compiler_path = "/usr/bin/gcc";
    inputs.compiler_version = "13.2.0";
    inputs.language_standard = "c17";
    inputs.dep_file_path = NULL;

    char key[CACHE_KEY_HEX_LEN + 1] = {0};
    int rc = cache_compute_key(&inputs, key);
    TEST_ASSERT(rc != 0);

    return 0;
}

// =============================================================================
// Requirement 6.4: Header in dep file deleted → non-zero return
// =============================================================================

TEST_SERIAL(cache_key_header_deleted_returns_nonzero) {
    char root[520];
    get_temp_dir(root, sizeof(root), "hdrdel");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "main.c");
    write_file(src_path, "int x = 1;\n");

    // Reference a header that does NOT exist on disk
    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "main.d");

    char missing_hdr[520];
    pal_path_join(missing_hdr, sizeof(missing_hdr), root, "deleted.h");
    // Do NOT create deleted.h — it should be missing

    char dep_content[1024];
    snprintf(dep_content, sizeof(dep_content), "main.o: %s %s\n", src_path, missing_hdr);
    write_file(dep_path, dep_content);

    CacheKeyInputs inputs = make_inputs(src_path, dep_path);

    char key[CACHE_KEY_HEX_LEN + 1] = {0};
    int rc = cache_compute_key(&inputs, key);
    TEST_ASSERT(rc != 0);

    pal_rmdir_r(root);
    return 0;
}
