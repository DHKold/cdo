// crates/cdo/tst/unit/test_link_freshness.c
// Unit tests for compiler_link_is_fresh() - artifact link freshness check
// Validates: Requirements 7.1, 7.2, 7.3, 7.4, 7.5
#include "cdo_ut.h"
#include "core/compiler.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <sys/utime.h>
#include <windows.h>
#else
#include <utime.h>
#include <sys/stat.h>
#endif

// =============================================================================
// Helpers
// =============================================================================

static void get_temp_dir(char* buf, size_t size, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
    snprintf(buf, size, "%s/cdo_test_link_fresh_%s", tmp, suffix);
    pal_path_normalize(buf);
}

/// Set the modification time of a file to a specific Unix timestamp (seconds).
static int set_mtime(const char* path, time_t mtime_sec) {
#ifdef _WIN32
    struct _utimbuf times;
    times.actime  = mtime_sec;
    times.modtime = mtime_sec;
    return _utime(path, &times);
#else
    struct utimbuf times;
    times.actime  = mtime_sec;
    times.modtime = mtime_sec;
    return utime(path, &times);
#endif
}

/// Create a file with content. Parent directory must already exist.
static int create_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) return 1;
    if (content) fputs(content, f);
    fclose(f);
    return 0;
}

// =============================================================================
// Test: All objects older than output -> skip link (return true)
// Requirement 7.1: all inputs older than output -> skip link
// =============================================================================

TEST_SERIAL(link_fresh_all_inputs_older) {
    char root[520];
    get_temp_dir(root, sizeof(root), "all_older");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create input object files
    char obj1[520], obj2[520], obj3[520];
    pal_path_join(obj1, sizeof(obj1), root, "a.o");
    pal_path_join(obj2, sizeof(obj2), root, "b.o");
    pal_path_join(obj3, sizeof(obj3), root, "c.o");
    create_file(obj1, "obj1");
    create_file(obj2, "obj2");
    create_file(obj3, "obj3");

    // Create output artifact
    char output[520];
    pal_path_join(output, sizeof(output), root, "app.exe");
    create_file(output, "output");

    // Set inputs to older timestamps, output to newer
    set_mtime(obj1, 1577836800);   // 2020-01-01
    set_mtime(obj2, 1577836800);   // 2020-01-01
    set_mtime(obj3, 1577836800);   // 2020-01-01
    set_mtime(output, 1704067200); // 2024-01-01

    const char* inputs[] = { obj1, obj2, obj3 };
    bool fresh = compiler_link_is_fresh(output, inputs, 3);

    // All inputs older than output -> link is fresh (skip)
    TEST_ASSERT(fresh == true);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: One object newer than output -> re-link (return false)
// Requirement 7.2: any input newer than output -> must re-link
// =============================================================================

TEST_SERIAL(link_fresh_one_input_newer) {
    char root[520];
    get_temp_dir(root, sizeof(root), "one_newer");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create input object files
    char obj1[520], obj2[520], obj3[520];
    pal_path_join(obj1, sizeof(obj1), root, "a.o");
    pal_path_join(obj2, sizeof(obj2), root, "b.o");
    pal_path_join(obj3, sizeof(obj3), root, "c.o");
    create_file(obj1, "obj1");
    create_file(obj2, "obj2");
    create_file(obj3, "obj3");

    // Create output artifact
    char output[520];
    pal_path_join(output, sizeof(output), root, "app.exe");
    create_file(output, "output");

    // Set most inputs older, but obj2 is NEWER than output
    set_mtime(obj1, 1577836800);   // 2020-01-01
    set_mtime(obj2, 1735689600);   // 2025-01-01 (newer than output)
    set_mtime(obj3, 1577836800);   // 2020-01-01
    set_mtime(output, 1704067200); // 2024-01-01

    const char* inputs[] = { obj1, obj2, obj3 };
    bool fresh = compiler_link_is_fresh(output, inputs, 3);

    // One input is newer than output -> not fresh (must re-link)
    TEST_ASSERT(fresh == false);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: Output artifact missing -> always link (return false)
// Requirement 7.3: output doesn't exist -> must link
// =============================================================================

TEST_SERIAL(link_fresh_output_missing) {
    char root[520];
    get_temp_dir(root, sizeof(root), "out_missing");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create input object files
    char obj1[520], obj2[520];
    pal_path_join(obj1, sizeof(obj1), root, "a.o");
    pal_path_join(obj2, sizeof(obj2), root, "b.o");
    create_file(obj1, "obj1");
    create_file(obj2, "obj2");

    // Output does NOT exist
    char output[520];
    pal_path_join(output, sizeof(output), root, "app.exe");
    // Not creating the output file

    const char* inputs[] = { obj1, obj2 };
    bool fresh = compiler_link_is_fresh(output, inputs, 2);

    // Output missing -> not fresh (must link)
    TEST_ASSERT(fresh == false);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: Dependency library newer than output -> re-link (return false)
// Requirement 7.5: dependency lib artifacts also considered in freshness
// =============================================================================

TEST_SERIAL(link_fresh_dep_lib_newer) {
    char root[520];
    get_temp_dir(root, sizeof(root), "dep_newer");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create object files (older than output)
    char obj1[520];
    pal_path_join(obj1, sizeof(obj1), root, "main.o");
    create_file(obj1, "obj_main");

    // Create dependency library (newer than output)
    char dep_lib[520];
    pal_path_join(dep_lib, sizeof(dep_lib), root, "libdep.a");
    create_file(dep_lib, "dep_lib");

    // Create output artifact
    char output[520];
    pal_path_join(output, sizeof(output), root, "app.exe");
    create_file(output, "output");

    // Object is older, dep lib is NEWER than output
    set_mtime(obj1, 1577836800);    // 2020-01-01
    set_mtime(dep_lib, 1735689600); // 2025-01-01 (newer than output)
    set_mtime(output, 1704067200);  // 2024-01-01

    // inputs[] includes both objects and dependency libraries
    const char* inputs[] = { obj1, dep_lib };
    bool fresh = compiler_link_is_fresh(output, inputs, 2);

    // Dependency lib is newer -> not fresh (must re-link)
    TEST_ASSERT(fresh == false);

    pal_rmdir_r(root);
    return 0;
}
