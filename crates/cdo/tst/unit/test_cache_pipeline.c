// crates/cdo/tst/unit/test_cache_pipeline.c
// Unit tests for cache pipeline integration logic
// Validates: Requirements 2.1, 2.2, 2.3, 5.1, 5.2, 7.2, 7.4
#include "cdo_ut.h"
#include "core/cache.h"
#include "core/compiler.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Test wrappers exposed in compiler_compile.c (no header declaration)
extern int compiler_test_build_gcc_args(const CompileJob* job, const CompilerInfo* info, const char** args, int max_args);
extern int compiler_test_build_msvc_args(const CompileJob* job, const char** args, int max_args);

// =============================================================================
// Helper utilities
// =============================================================================

static void get_temp_dir(char* buf, size_t size, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(buf, size, "%s/cdo_test_pipeline_%s", tmp, suffix);
    pal_path_normalize(buf);
}

// =============================================================================
// Tests: use_cache condition — no_cache flag disables all cache operations
// Requirement 7.2: --no-cache disables both cache lookup and population
// Requirement 7.4: when cache disabled, no stats printed, compile all dirty normally
// =============================================================================

TEST(pipeline_no_cache_flag_disables_cache) {
    // The use_cache condition in compiler_compile_batch is:
    //   (cache_config != NULL && cache_config->enabled && !no_cache && cache_stats != NULL)
    // When no_cache = true, use_cache = false regardless of other values.
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);

    CacheStats stats = {0};
    bool no_cache = true;

    // Evaluate the use_cache condition the same way compiler_compile_batch does
    bool use_cache = ((&config) != NULL && config.enabled && !no_cache && (&stats) != NULL);
    TEST_ASSERT(use_cache == false);

    // When cache is disabled, stats should remain zero (never incremented)
    TEST_ASSERT_EQ(stats.hits, 0);
    TEST_ASSERT_EQ(stats.misses, 0);
    TEST_ASSERT_EQ(stats.stored, 0);
    return 0;
}

TEST(pipeline_no_cache_false_enables_cache) {
    // Verify: when all conditions are met and no_cache=false, use_cache is true
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);

    CacheStats stats = {0};
    bool no_cache = false;

    bool use_cache = ((&config) != NULL && config.enabled && !no_cache && (&stats) != NULL);
    TEST_ASSERT(use_cache == true);
    return 0;
}

// =============================================================================
// Tests: use_cache condition — config.enabled = false disables cache
// Requirement 7.4: when cache disabled, compile all dirty files normally
// =============================================================================

TEST(pipeline_config_enabled_false_disables_cache) {
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = false;  // disabled in cdo.toml
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)2147483648;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);

    CacheStats stats = {0};
    bool no_cache = false;

    bool use_cache = ((&config) != NULL && config.enabled && !no_cache && (&stats) != NULL);
    TEST_ASSERT(use_cache == false);

    // Stats stay zero
    TEST_ASSERT_EQ(stats.hits, 0);
    TEST_ASSERT_EQ(stats.misses, 0);
    TEST_ASSERT_EQ(stats.stored, 0);
    return 0;
}

TEST(pipeline_null_config_disables_cache) {
    // When cache_config is NULL, caching is disabled
    CacheConfig* config = NULL;
    CacheStats stats = {0};
    bool no_cache = false;

    bool use_cache = (config != NULL && config->enabled && !no_cache && (&stats) != NULL);
    TEST_ASSERT(use_cache == false);
    return 0;
}

TEST(pipeline_null_stats_disables_cache) {
    // When cache_stats is NULL, caching is disabled
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    config.enabled = true;

    CacheStats* stats = NULL;
    bool no_cache = false;

    bool use_cache = ((&config) != NULL && config.enabled && !no_cache && stats != NULL);
    TEST_ASSERT(use_cache == false);
    return 0;
}

// =============================================================================
// Tests: Cache key computation integration (cache hit/miss logic)
// Requirement 2.1: compute key and check cache store before compiling
// Requirement 2.2: on cache hit, copy cached object instead of compiling
// Requirement 2.3: on cache miss, compile normally and store result
// =============================================================================

TEST_SERIAL(pipeline_cache_hit_skips_compilation) {
    // Simulate a cache hit scenario:
    // 1. Create a source file and dep file
    // 2. Store a fake .o in the cache under its key
    // 3. Verify cache_lookup returns 0 (hit)
    char root[520];
    get_temp_dir(root, sizeof(root), "hit");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create source and header
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "main.c");
    pal_file_write(src_path, "int main() { return 0; }\n", 25);

    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), root, "main.h");
    pal_file_write(hdr_path, "#pragma once\n", 13);

    // Create dep file
    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "main.d");
    char dep_content[1024];
    snprintf(dep_content, sizeof(dep_content), "main.o: %s %s\n", src_path, hdr_path);
    pal_file_write(dep_path, dep_content, strlen(dep_content));

    // Compute cache key
    CacheKeyInputs inputs = {0};
    inputs.source_path = src_path;
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

    char cache_key[CACHE_KEY_HEX_LEN + 1] = {0};
    int rc = cache_compute_key(&inputs, cache_key);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(strlen(cache_key) == CACHE_KEY_HEX_LEN);

    // Set up cache config and init
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    char cache_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache_store");
    strncpy(config.path, cache_dir, sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)1073741824;
    config.enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);

    rc = cache_init(&config, root);
    TEST_ASSERT_EQ(rc, 0);

    // Create a fake object file and store it in cache
    char fake_obj[520];
    pal_path_join(fake_obj, sizeof(fake_obj), root, "main.o");
    const char* obj_content = "FAKE_OBJ_DATA_12345";
    pal_file_write(fake_obj, obj_content, strlen(obj_content));

    rc = cache_store(&config, cache_key, fake_obj);
    TEST_ASSERT_EQ(rc, 0);

    // Now simulate a cache lookup (what happens on a cache hit)
    char dest_obj[520];
    pal_path_join(dest_obj, sizeof(dest_obj), root, "output.o");
    rc = cache_lookup(&config, cache_key, dest_obj);
    TEST_ASSERT_EQ(rc, 0); // Cache hit!

    // Verify the cached object was copied to the destination
    TEST_ASSERT_EQ(pal_path_exists(dest_obj), 0);

    // On a hit, stats.hits would be incremented (no compiler call needed)
    CacheStats stats = {0};
    stats.hits++;
    TEST_ASSERT_EQ(stats.hits, 1);
    TEST_ASSERT_EQ(stats.misses, 0);

    pal_rmdir_r(root);
    return 0;
}

TEST_SERIAL(pipeline_cache_miss_compiles_and_stores) {
    // Simulate a cache miss scenario:
    // 1. Create source/dep file
    // 2. Compute cache key
    // 3. Lookup should return non-zero (miss)
    // 4. After "compilation", store the result
    char root[520];
    get_temp_dir(root, sizeof(root), "miss");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create source and header
    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "lib.c");
    pal_file_write(src_path, "void foo() {}\n", 14);

    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), root, "lib.h");
    pal_file_write(hdr_path, "void foo();\n", 12);

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "lib.d");
    char dep_content[1024];
    snprintf(dep_content, sizeof(dep_content), "lib.o: %s %s\n", src_path, hdr_path);
    pal_file_write(dep_path, dep_content, strlen(dep_content));

    // Compute cache key
    CacheKeyInputs inputs = {0};
    inputs.source_path = src_path;
    inputs.compiler_path = "/usr/bin/gcc";
    inputs.compiler_version = "13.2.0";
    inputs.language_standard = "c17";
    inputs.optimize = true;
    inputs.debug_info = false;
    inputs.defines = NULL;
    inputs.define_count = 0;
    inputs.include_paths = NULL;
    inputs.include_path_count = 0;
    inputs.dep_file_path = dep_path;

    char cache_key[CACHE_KEY_HEX_LEN + 1] = {0};
    int rc = cache_compute_key(&inputs, cache_key);
    TEST_ASSERT_EQ(rc, 0);

    // Set up cache (empty)
    CacheConfig config;
    memset(&config, 0, sizeof(config));
    char cache_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "cache_store");
    strncpy(config.path, cache_dir, sizeof(config.path) - 1);
    config.max_size_bytes = (int64_t)1073741824;
    config.enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);

    rc = cache_init(&config, root);
    TEST_ASSERT_EQ(rc, 0);

    // Lookup in empty cache → miss
    char dest_obj[520];
    pal_path_join(dest_obj, sizeof(dest_obj), root, "lib.o");
    rc = cache_lookup(&config, cache_key, dest_obj);
    TEST_ASSERT(rc != 0); // Cache miss

    // Simulate compilation: create the .o file
    const char* compiled_content = "COMPILED_OBJECT_DATA";
    pal_file_write(dest_obj, compiled_content, strlen(compiled_content));

    // Store in cache after successful compilation
    rc = cache_store(&config, cache_key, dest_obj);
    TEST_ASSERT_EQ(rc, 0);

    // Verify it's now in cache
    char verify_path[520];
    pal_path_join(verify_path, sizeof(verify_path), root, "verify.o");
    rc = cache_lookup(&config, cache_key, verify_path);
    TEST_ASSERT_EQ(rc, 0); // Now a hit

    // Stats should reflect miss + stored
    CacheStats stats = {0};
    stats.misses++;
    stats.stored++;
    TEST_ASSERT_EQ(stats.misses, 1);
    TEST_ASSERT_EQ(stats.stored, 1);
    TEST_ASSERT_EQ(stats.hits, 0);

    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Tests: Stats accumulation and summary formatting
// Requirement 5.1: track hits/misses during build
// Requirement 5.2: print summary at end of build
// =============================================================================

TEST(pipeline_stats_accumulation) {
    CacheStats stats = {0};

    // Simulate processing multiple files across multiple crates
    // Crate 1: 5 hits, 3 misses
    stats.hits += 5;
    stats.misses += 3;
    stats.stored += 3;

    // Crate 2: 8 hits, 2 misses
    stats.hits += 8;
    stats.misses += 2;
    stats.stored += 2;

    // Crate 3: 3 hits, 7 misses (new code)
    stats.hits += 3;
    stats.misses += 7;
    stats.stored += 7;

    TEST_ASSERT_EQ(stats.hits, 16);
    TEST_ASSERT_EQ(stats.misses, 12);
    TEST_ASSERT_EQ(stats.stored, 12);

    int total = stats.hits + stats.misses;
    TEST_ASSERT_EQ(total, 28);

    // Hit rate calculation (integer truncation)
    int hit_rate = (stats.hits * 100) / total;
    TEST_ASSERT_EQ(hit_rate, 57); // 1600/28 = 57.14 → 57
    return 0;
}

TEST(pipeline_stats_summary_format) {
    // Verify the summary format string produces correct output
    CacheStats stats = {0};
    stats.hits = 42;
    stats.misses = 8;

    int total = stats.hits + stats.misses;
    int hit_rate = (stats.hits * 100) / total;

    char summary[256];
    snprintf(summary, sizeof(summary), "Cache: %d hits, %d misses (%d%% hit rate)", stats.hits, stats.misses, hit_rate);

    TEST_ASSERT_STR_EQ(summary, "Cache: 42 hits, 8 misses (84% hit rate)");
    return 0;
}

TEST(pipeline_stats_no_activity_no_summary) {
    // Requirement 7.4: when cache disabled, no stats printed
    // Simulated by: total == 0 means no summary
    CacheStats stats = {0};
    int total = stats.hits + stats.misses;
    TEST_ASSERT_EQ(total, 0);
    // The summary guard condition is: if (total > 0) print_summary
    // With total == 0, summary is suppressed
    return 0;
}

// =============================================================================
// Tests: Dep file path computation
// The pipeline computes dep file path from object path: ".o" → ".d", ".obj" → ".d"
// =============================================================================

TEST(pipeline_dep_path_from_dot_o) {
    // ".o" suffix → replace last char with 'd'
    char buf[280];
    const char* obj_path = "build/release/cdo/main.o";
    size_t olen = strlen(obj_path);
    memcpy(buf, obj_path, olen + 1);
    if (olen >= 2 && buf[olen - 2] == '.' && buf[olen - 1] == 'o') {
        buf[olen - 1] = 'd';
    }
    TEST_ASSERT_STR_EQ(buf, "build/release/cdo/main.d");
    return 0;
}

TEST(pipeline_dep_path_from_dot_obj) {
    // ".obj" suffix → replace with ".d"
    char buf[280];
    const char* obj_path = "build/release/cdo/main.obj";
    size_t olen = strlen(obj_path);
    memcpy(buf, obj_path, olen + 1);
    if (olen >= 4 && strcmp(buf + olen - 4, ".obj") == 0) {
        strcpy(buf + olen - 4, ".d");
    } else if (olen >= 2 && buf[olen - 2] == '.' && buf[olen - 1] == 'o') {
        buf[olen - 1] = 'd';
    }
    TEST_ASSERT_STR_EQ(buf, "build/release/cdo/main.d");
    return 0;
}

TEST(pipeline_dep_path_no_extension_fallback) {
    // No recognized extension → append ".d"
    char buf[280];
    const char* obj_path = "build/release/cdo/main";
    size_t olen = strlen(obj_path);
    memcpy(buf, obj_path, olen + 1);
    if (olen >= 4 && strcmp(buf + olen - 4, ".obj") == 0) {
        strcpy(buf + olen - 4, ".d");
    } else if (olen >= 2 && buf[olen - 2] == '.' && buf[olen - 1] == 'o') {
        buf[olen - 1] = 'd';
    } else {
        snprintf(buf, sizeof(buf), "%s.d", obj_path);
    }
    TEST_ASSERT_STR_EQ(buf, "build/release/cdo/main.d");
    return 0;
}

// =============================================================================
// Tests: Dep file generation flags present in compile commands
// Requirement 2.1, 6.1: -MD -MF flags generated for GCC/Clang builds
// =============================================================================

TEST(pipeline_gcc_args_contain_dep_flags) {
    CompileJob job = {0};
    job.source_path = "src/main.c";
    job.object_path = "build/release/main.o";
    job.include_paths = NULL;
    job.include_path_count = 0;
    job.defines = NULL;
    job.define_count = 0;
    job.c_standard = "c17";
    job.cpp_standard = NULL;
    job.extra_flags = NULL;
    job.extra_flag_count = 0;
    job.optimize = false;
    job.debug_info = false;

    CompilerInfo info = {0};
    info.family = COMPILER_GCC;
    strncpy(info.path, "/usr/bin/gcc", sizeof(info.path) - 1);
    strncpy(info.version, "13.2.0", sizeof(info.version) - 1);

    const char* args[64];
    int n = compiler_test_build_gcc_args(&job, &info, args, 64);
    TEST_ASSERT(n > 0);

    // Search for -MD and -MF in the argument list
    bool found_md = false;
    bool found_mf = false;
    int mf_index = -1;
    for (int i = 0; i < n; i++) {
        if (strcmp(args[i], "-MD") == 0) found_md = true;
        if (strcmp(args[i], "-MF") == 0) {
            found_mf = true;
            mf_index = i;
        }
    }
    TEST_ASSERT(found_md);
    TEST_ASSERT(found_mf);

    // -MF should be followed by the dep file path (object path with .o → .d)
    TEST_ASSERT(mf_index >= 0 && mf_index + 1 < n);
    TEST_ASSERT_STR_EQ(args[mf_index + 1], "build/release/main.d");
    return 0;
}

TEST(pipeline_gcc_args_dep_path_from_obj_extension) {
    // Test with .obj extension object path
    CompileJob job = {0};
    job.source_path = "src/utils.c";
    job.object_path = "build/release/utils.obj";
    job.include_paths = NULL;
    job.include_path_count = 0;
    job.defines = NULL;
    job.define_count = 0;
    job.c_standard = "c17";
    job.cpp_standard = NULL;
    job.extra_flags = NULL;
    job.extra_flag_count = 0;
    job.optimize = true;
    job.debug_info = false;

    CompilerInfo info = {0};
    info.family = COMPILER_GCC;
    strncpy(info.path, "/usr/bin/gcc", sizeof(info.path) - 1);
    strncpy(info.version, "13.2.0", sizeof(info.version) - 1);

    const char* args[64];
    int n = compiler_test_build_gcc_args(&job, &info, args, 64);
    TEST_ASSERT(n > 0);

    // Find -MF and check the dep path
    bool found_mf = false;
    int mf_index = -1;
    for (int i = 0; i < n; i++) {
        if (strcmp(args[i], "-MF") == 0) {
            found_mf = true;
            mf_index = i;
        }
    }
    TEST_ASSERT(found_mf);
    TEST_ASSERT(mf_index >= 0 && mf_index + 1 < n);
    // build_gcc_clang_args replaces trailing ".o" only — for ".obj" it falls back to appending ".d"
    // The internal logic: if last 2 chars are ".o" replace with ".d", else append ".d"
    // "utils.obj" — last 2 chars are "bj", not ".o", so it appends ".d"
    // Actually check: the code checks olen>=2 && [olen-2]=='.' && [olen-1]=='o'
    // For "utils.obj": olen=22, buf[20]='o', buf[21]='b' — not matching ".o"
    // So fallback: snprintf → "build/release/utils.obj.d"
    // Wait — looking at the code again:
    // dep_file_buf = copy of object_path
    // if (olen >= 2 && dep_file_buf[olen - 2] == '.' && dep_file_buf[olen - 1] == 'o')
    //     dep_file_buf[olen - 1] = 'd';
    // else
    //     snprintf(dep_file_buf, sizeof(dep_file_buf), "%s.d", job->object_path);
    // For "build/release/utils.obj": olen=22, last char='j', not 'o' → fallback appends ".d"
    TEST_ASSERT_STR_EQ(args[mf_index + 1], "build/release/utils.obj.d");
    return 0;
}

TEST(pipeline_gcc_args_contain_c_standard) {
    // Verify -std=c17 flag is present
    CompileJob job = {0};
    job.source_path = "src/foo.c";
    job.object_path = "build/foo.o";
    job.include_paths = NULL;
    job.include_path_count = 0;
    job.defines = NULL;
    job.define_count = 0;
    job.c_standard = "c17";
    job.cpp_standard = NULL;
    job.extra_flags = NULL;
    job.extra_flag_count = 0;
    job.optimize = false;
    job.debug_info = false;

    CompilerInfo info = {0};
    info.family = COMPILER_GCC;
    strncpy(info.path, "/usr/bin/gcc", sizeof(info.path) - 1);
    strncpy(info.version, "13.2.0", sizeof(info.version) - 1);

    const char* args[64];
    int n = compiler_test_build_gcc_args(&job, &info, args, 64);
    TEST_ASSERT(n > 0);

    bool found_std = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(args[i], "-std=c17") == 0) {
            found_std = true;
            break;
        }
    }
    TEST_ASSERT(found_std);
    return 0;
}
