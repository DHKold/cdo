// crates/cdo/tst/unit/test_fmt_discover.c
// Unit tests for source file discovery logic in fmt_discover_sources
// Validates: Requirements 1.1, 2.1, 2.2, 6.2, 6.3
#include "cdo_ut.h"
#include "commands/cmd_fmt.h"
#include "model/workspace.h"
#include "model/fmt_settings.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
// Helpers
// =============================================================================

static void get_temp_dir(char* buf, size_t size, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(buf, size, "%s/cdo_test_fmt_disc_%s", tmp, suffix);
    pal_path_normalize(buf);
}

/// Create a dummy file with minimal content at the given path.
/// Automatically creates parent directories.
static int create_dummy_file(const char* dir, const char* filename) {
    char filepath[520];
    if (pal_path_join(filepath, sizeof(filepath), dir, filename) != 0) return -1;
    const char* content = "/* dummy */\n";
    return pal_file_write(filepath, content, strlen(content));
}

/// Create directory structure and dummy files for a crate.
/// crate_dir is the absolute path where the crate lives.
static int setup_crate_dir(const char* crate_dir) {
    return pal_mkdir_p(crate_dir);
}

static void cleanup_dir(const char* root) {
    pal_rmdir_r(root);
}

// =============================================================================
// Test: All valid source extensions are discovered
// Requirement 1.1: discover .c, .cpp, .cxx, .cc, .h, .hpp, .hxx
// =============================================================================

TEST_SERIAL(fmt_discover_all_extensions) {
    char root[520];
    get_temp_dir(root, sizeof(root), "all_ext");
    cleanup_dir(root);

    // Create workspace root and a crate directory
    char crate_dir[520];
    pal_path_join(crate_dir, sizeof(crate_dir), root, "my_crate");
    pal_mkdir_p(crate_dir);

    // Create one file for each valid extension
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "main.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "app.cpp"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "util.cxx"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "lib.cc"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "api.h"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "types.hpp"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "compat.hxx"), 0);

    // Setup workspace and crate structs
    Workspace ws = {0};
    strncpy(ws.root_path, root, sizeof(ws.root_path) - 1);

    Crate crate = {0};
    strncpy(crate.name, "my_crate", sizeof(crate.name) - 1);
    strncpy(crate.path, "my_crate", sizeof(crate.path) - 1);

    const Crate* crate_ptrs[1] = { &crate };
    FmtSettings settings = {0};

    char out_files[64][260];
    int count = fmt_discover_sources(&ws, crate_ptrs, 1, &settings, false, out_files, 64);

    TEST_ASSERT_EQ(count, 7);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: Non-source files are skipped
// Requirement 1.1: only .c, .cpp, .cxx, .cc, .h, .hpp, .hxx are matched
// =============================================================================

TEST_SERIAL(fmt_discover_skips_non_source) {
    char root[520];
    get_temp_dir(root, sizeof(root), "non_src");
    cleanup_dir(root);

    char crate_dir[520];
    pal_path_join(crate_dir, sizeof(crate_dir), root, "my_crate");
    pal_mkdir_p(crate_dir);

    // Create non-source files only
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "readme.txt"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "config.toml"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "notes.md"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "output.o"), 0);

    Workspace ws = {0};
    strncpy(ws.root_path, root, sizeof(ws.root_path) - 1);

    Crate crate = {0};
    strncpy(crate.name, "my_crate", sizeof(crate.name) - 1);
    strncpy(crate.path, "my_crate", sizeof(crate.path) - 1);

    const Crate* crate_ptrs[1] = { &crate };
    FmtSettings settings = {0};

    char out_files[64][260];
    int count = fmt_discover_sources(&ws, crate_ptrs, 1, &settings, false, out_files, 64);

    TEST_ASSERT_EQ(count, 0);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: Nested directories are traversed
// Requirement 1.1: recursive discovery within crate directories
// =============================================================================

TEST_SERIAL(fmt_discover_nested_dirs) {
    char root[520];
    get_temp_dir(root, sizeof(root), "nested");
    cleanup_dir(root);

    char crate_dir[520];
    pal_path_join(crate_dir, sizeof(crate_dir), root, "my_crate");

    // Create nested subdirectories
    char sub1[520], sub2[520], sub3[520];
    pal_path_join(sub1, sizeof(sub1), crate_dir, "lib");
    pal_path_join(sub2, sizeof(sub2), crate_dir, "lib/internal");
    pal_path_join(sub3, sizeof(sub3), crate_dir, "api/public");
    pal_mkdir_p(sub1);
    pal_mkdir_p(sub2);
    pal_mkdir_p(sub3);

    // Place source files at various nesting depths
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "root.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(sub1, "lib.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(sub2, "internal.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(sub3, "public.h"), 0);

    Workspace ws = {0};
    strncpy(ws.root_path, root, sizeof(ws.root_path) - 1);

    Crate crate = {0};
    strncpy(crate.name, "my_crate", sizeof(crate.name) - 1);
    strncpy(crate.path, "my_crate", sizeof(crate.path) - 1);

    const Crate* crate_ptrs[1] = { &crate };
    FmtSettings settings = {0};

    char out_files[64][260];
    int count = fmt_discover_sources(&ws, crate_ptrs, 1, &settings, false, out_files, 64);

    TEST_ASSERT_EQ(count, 4);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: build/ directory is always excluded
// Requirement 6.3: always exclude build/
// =============================================================================

TEST_SERIAL(fmt_discover_excludes_build_dir) {
    char root[520];
    get_temp_dir(root, sizeof(root), "excl_build");
    cleanup_dir(root);

    char crate_dir[520];
    pal_path_join(crate_dir, sizeof(crate_dir), root, "my_crate");

    // Create files both in normal and build/ directories
    char build_dir[520];
    pal_path_join(build_dir, sizeof(build_dir), crate_dir, "build");
    pal_mkdir_p(crate_dir);
    pal_mkdir_p(build_dir);

    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "main.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(build_dir, "generated.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(build_dir, "output.h"), 0);

    Workspace ws = {0};
    strncpy(ws.root_path, root, sizeof(ws.root_path) - 1);

    Crate crate = {0};
    strncpy(crate.name, "my_crate", sizeof(crate.name) - 1);
    strncpy(crate.path, "my_crate", sizeof(crate.path) - 1);

    const Crate* crate_ptrs[1] = { &crate };
    FmtSettings settings = {0};

    char out_files[64][260];
    int count = fmt_discover_sources(&ws, crate_ptrs, 1, &settings, false, out_files, 64);

    // Only main.c should be found; build/ contents excluded
    TEST_ASSERT_EQ(count, 1);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: .cdo/ directory is always excluded
// Requirement 6.3: always exclude .cdo/
// =============================================================================

TEST_SERIAL(fmt_discover_excludes_cdo_dir) {
    char root[520];
    get_temp_dir(root, sizeof(root), "excl_cdo");
    cleanup_dir(root);

    char crate_dir[520];
    pal_path_join(crate_dir, sizeof(crate_dir), root, "my_crate");

    // Create files both in normal and .cdo/ directories
    char cdo_dir[520];
    pal_path_join(cdo_dir, sizeof(cdo_dir), crate_dir, ".cdo");
    pal_mkdir_p(crate_dir);
    pal_mkdir_p(cdo_dir);

    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "main.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(cdo_dir, "cached.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(cdo_dir, "tool.h"), 0);

    Workspace ws = {0};
    strncpy(ws.root_path, root, sizeof(ws.root_path) - 1);

    Crate crate = {0};
    strncpy(crate.name, "my_crate", sizeof(crate.name) - 1);
    strncpy(crate.path, "my_crate", sizeof(crate.path) - 1);

    const Crate* crate_ptrs[1] = { &crate };
    FmtSettings settings = {0};

    char out_files[64][260];
    int count = fmt_discover_sources(&ws, crate_ptrs, 1, &settings, false, out_files, 64);

    // Only main.c should be found; .cdo/ contents excluded
    TEST_ASSERT_EQ(count, 1);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: Config exclude patterns filter matching files
// Requirement 6.2: configured exclude glob patterns applied
// =============================================================================

TEST_SERIAL(fmt_discover_config_exclude_pattern) {
    char root[520];
    get_temp_dir(root, sizeof(root), "cfg_excl");
    cleanup_dir(root);

    char crate_dir[520];
    pal_path_join(crate_dir, sizeof(crate_dir), root, "my_crate");

    char vendor_dir[520];
    pal_path_join(vendor_dir, sizeof(vendor_dir), crate_dir, "vendor");
    pal_mkdir_p(crate_dir);
    pal_mkdir_p(vendor_dir);

    // Create source files: some in vendor/, some outside
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "main.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_dir, "app.cpp"), 0);
    TEST_ASSERT_EQ(create_dummy_file(vendor_dir, "third_party.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(vendor_dir, "third_party.h"), 0);

    Workspace ws = {0};
    strncpy(ws.root_path, root, sizeof(ws.root_path) - 1);

    Crate crate = {0};
    strncpy(crate.name, "my_crate", sizeof(crate.name) - 1);
    strncpy(crate.path, "my_crate", sizeof(crate.path) - 1);

    const Crate* crate_ptrs[1] = { &crate };

    // Configure exclude pattern: my_crate/vendor/**
    FmtSettings settings = {0};
    strncpy(settings.exclude_patterns[0], "my_crate/vendor/**", sizeof(settings.exclude_patterns[0]) - 1);
    settings.exclude_count = 1;

    char out_files[64][260];
    int count = fmt_discover_sources(&ws, crate_ptrs, 1, &settings, false, out_files, 64);

    // Only main.c and app.cpp should be found; vendor/ excluded by config pattern
    TEST_ASSERT_EQ(count, 2);

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: Per-crate targeting — only targeted crate's files found
// Requirement 2.1: format only specified crate(s)
// =============================================================================

TEST_SERIAL(fmt_discover_per_crate_target) {
    char root[520];
    get_temp_dir(root, sizeof(root), "per_crate");
    cleanup_dir(root);

    // Create two crate directories
    char crate_a_dir[520], crate_b_dir[520];
    pal_path_join(crate_a_dir, sizeof(crate_a_dir), root, "crate_a");
    pal_path_join(crate_b_dir, sizeof(crate_b_dir), root, "crate_b");
    pal_mkdir_p(crate_a_dir);
    pal_mkdir_p(crate_b_dir);

    // Put source files in both crates
    TEST_ASSERT_EQ(create_dummy_file(crate_a_dir, "a1.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_a_dir, "a2.h"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_b_dir, "b1.c"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_b_dir, "b2.cpp"), 0);
    TEST_ASSERT_EQ(create_dummy_file(crate_b_dir, "b3.h"), 0);

    Workspace ws = {0};
    strncpy(ws.root_path, root, sizeof(ws.root_path) - 1);

    Crate crate_a = {0};
    strncpy(crate_a.name, "crate_a", sizeof(crate_a.name) - 1);
    strncpy(crate_a.path, "crate_a", sizeof(crate_a.path) - 1);

    Crate crate_b = {0};
    strncpy(crate_b.name, "crate_b", sizeof(crate_b.name) - 1);
    strncpy(crate_b.path, "crate_b", sizeof(crate_b.path) - 1);

    FmtSettings settings = {0};

    // Target only crate_a — should find 2 files
    {
        const Crate* ptrs[1] = { &crate_a };
        char out_files[64][260];
        int count = fmt_discover_sources(&ws, ptrs, 1, &settings, false, out_files, 64);
        TEST_ASSERT_EQ(count, 2);
    }

    // Target only crate_b — should find 3 files
    {
        const Crate* ptrs[1] = { &crate_b };
        char out_files[64][260];
        int count = fmt_discover_sources(&ws, ptrs, 1, &settings, false, out_files, 64);
        TEST_ASSERT_EQ(count, 3);
    }

    // Target both crates — should find all 5 files
    {
        const Crate* ptrs[2] = { &crate_a, &crate_b };
        char out_files[64][260];
        int count = fmt_discover_sources(&ws, ptrs, 2, &settings, false, out_files, 64);
        TEST_ASSERT_EQ(count, 5);
    }

    cleanup_dir(root);
    return 0;
}

// =============================================================================
// Test: Unknown crate (invalid crate pointer) → proper error handling
// Requirement 2.2: unknown crate name → error
// Note: fmt_discover_sources takes Crate pointers directly; passing NULL crate
// pointer should be handled gracefully (skipped). The "unknown crate" validation
// happens at the cmd_fmt level, but discovery should not crash on NULL.
// =============================================================================

TEST_SERIAL(fmt_discover_unknown_crate_error) {
    char root[520];
    get_temp_dir(root, sizeof(root), "unknown_crate");
    cleanup_dir(root);
    pal_mkdir_p(root);

    Workspace ws = {0};
    strncpy(ws.root_path, root, sizeof(ws.root_path) - 1);

    FmtSettings settings = {0};

    // Pass NULL crate pointer — should be skipped gracefully, returning 0 files
    const Crate* ptrs[1] = { NULL };
    char out_files[64][260];
    int count = fmt_discover_sources(&ws, ptrs, 1, &settings, false, out_files, 64);
    TEST_ASSERT_EQ(count, 0);

    // Pass 0 crate_count — should return 0
    count = fmt_discover_sources(&ws, ptrs, 0, &settings, false, out_files, 64);
    TEST_ASSERT_EQ(count, 0);

    // Pass NULL crates array — should return 0
    count = fmt_discover_sources(&ws, NULL, 1, &settings, false, out_files, 64);
    TEST_ASSERT_EQ(count, 0);

    cleanup_dir(root);
    return 0;
}
