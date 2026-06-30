// crates/cdo/tst/unit/test_parallel_hash.c
// Unit tests for parallel hash computation (serial vs parallel key equivalence)
// Validates: Requirements 8.1, 8.2, 8.3, 8.5, 8.6
#include "cdo_ut.h"
#include "core/cache_hash_parallel.h"
#include "core/cache.h"
#include "commons/threadpool.h"
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
    snprintf(buf, size, "%s/cdo_test_parallel_hash_%s", tmp, suffix);
    pal_path_normalize(buf);
}

/// Write a text file at the given path.
static int write_file(const char* path, const char* content) {
    return pal_file_write(path, content, strlen(content));
}

/// Create a GCC-format dep file listing given headers.
static int write_dep_file(const char* dep_path, const char* target,
                          const char* source, const char** headers, int hdr_count) {
    char buf[4096];
    int off = snprintf(buf, sizeof(buf), "%s: %s", target, source);
    for (int i = 0; i < hdr_count; i++) {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, " %s", headers[i]);
    }
    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "\n");
    return pal_file_write(dep_path, buf, (size_t)off);
}

/// Build CacheKeyInputs pointing to files in a temp dir.
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
// Test: Serial mode (pool=NULL) produces correct keys matching cache_compute_key
// Requirement 8.2: When jobs=1 or pool=NULL, compute on main thread
// Requirement 8.3: Parallel produces identical keys to serial for same inputs
// =============================================================================

TEST_SERIAL(parallel_hash_serial_fallback_produces_correct_keys) {
    char root[520];
    get_temp_dir(root, sizeof(root), "serial");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create source files
    char src1[520], src2[520];
    pal_path_join(src1, sizeof(src1), root, "a.c");
    pal_path_join(src2, sizeof(src2), root, "b.c");
    write_file(src1, "int a_func(void) { return 1; }\n");
    write_file(src2, "int b_func(void) { return 2; }\n");

    // Create headers
    char hdr1[520], hdr2[520];
    pal_path_join(hdr1, sizeof(hdr1), root, "a.h");
    pal_path_join(hdr2, sizeof(hdr2), root, "b.h");
    write_file(hdr1, "#pragma once\nint a_func(void);\n");
    write_file(hdr2, "#pragma once\nint b_func(void);\n");

    // Create dep files
    char dep1[520], dep2[520];
    pal_path_join(dep1, sizeof(dep1), root, "a.d");
    pal_path_join(dep2, sizeof(dep2), root, "b.d");
    const char* hdrs1[] = { hdr1 };
    const char* hdrs2[] = { hdr2 };
    write_dep_file(dep1, "a.o", src1, hdrs1, 1);
    write_dep_file(dep2, "b.o", src2, hdrs2, 1);

    // Compute keys directly via cache_compute_key (reference serial path)
    char expected_key1[CACHE_KEY_HEX_LEN + 1] = {0};
    char expected_key2[CACHE_KEY_HEX_LEN + 1] = {0};
    CacheKeyInputs inputs1 = make_inputs(src1, dep1);
    CacheKeyInputs inputs2 = make_inputs(src2, dep2);
    TEST_ASSERT_EQ(cache_compute_key(&inputs1, expected_key1), 0);
    TEST_ASSERT_EQ(cache_compute_key(&inputs2, expected_key2), 0);

    // Now use parallel dispatch with pool=NULL (serial fallback)
    HashResultSlot* results = cache_hash_results_alloc(2);
    TEST_ASSERT(results != NULL);
    HashJobCtx* jobs = cache_hash_jobs_alloc(2);
    TEST_ASSERT(jobs != NULL);

    jobs[0].inputs = inputs1;
    jobs[0].result = &results[0];
    results[0].job_index = 0;

    jobs[1].inputs = inputs2;
    jobs[1].result = &results[1];
    results[1].job_index = 1;

    int rc = cache_parallel_hash_dispatch(NULL, jobs, 2, results);
    TEST_ASSERT_EQ(rc, 0);

    // Verify results match serial computation
    TEST_ASSERT(results[0].valid == true);
    TEST_ASSERT(results[1].valid == true);
    TEST_ASSERT_STR_EQ(results[0].key, expected_key1);
    TEST_ASSERT_STR_EQ(results[1].key, expected_key2);

    free(results);
    free(jobs);
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: Parallel mode (real thread pool) produces identical keys to serial mode
// Requirement 8.1: Distribute hash computation across N threads
// Requirement 8.3: Parallel keys == serial keys for same inputs
// Requirement 8.5: Thread-safe concurrent reads
// =============================================================================

TEST_SERIAL(parallel_hash_threaded_produces_identical_keys) {
    char root[520];
    get_temp_dir(root, sizeof(root), "parallel");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create multiple source files to exercise parallelism
    const int FILE_COUNT = 4;
    char src_paths[4][520];
    char hdr_paths[4][520];
    char dep_paths[4][520];
    char expected_keys[4][CACHE_KEY_HEX_LEN + 1];

    for (int i = 0; i < FILE_COUNT; i++) {
        char name[32];
        snprintf(name, sizeof(name), "file%d.c", i);
        pal_path_join(src_paths[i], sizeof(src_paths[i]), root, name);

        char content[128];
        snprintf(content, sizeof(content), "int func%d(void) { return %d; }\n", i, i * 10);
        write_file(src_paths[i], content);

        snprintf(name, sizeof(name), "file%d.h", i);
        pal_path_join(hdr_paths[i], sizeof(hdr_paths[i]), root, name);
        snprintf(content, sizeof(content), "#pragma once\nint func%d(void);\n", i);
        write_file(hdr_paths[i], content);

        snprintf(name, sizeof(name), "file%d.d", i);
        pal_path_join(dep_paths[i], sizeof(dep_paths[i]), root, name);
        const char* hdrs[] = { hdr_paths[i] };
        write_dep_file(dep_paths[i], "file.o", src_paths[i], hdrs, 1);
    }

    // Compute expected keys serially
    for (int i = 0; i < FILE_COUNT; i++) {
        CacheKeyInputs inputs = make_inputs(src_paths[i], dep_paths[i]);
        memset(expected_keys[i], 0, sizeof(expected_keys[i]));
        TEST_ASSERT_EQ(cache_compute_key(&inputs, expected_keys[i]), 0);
    }

    // Dispatch via real thread pool (2 threads for parallelism)
    ThreadPool* pool = threadpool_create(2);
    TEST_ASSERT(pool != NULL);

    HashResultSlot* results = cache_hash_results_alloc(FILE_COUNT);
    TEST_ASSERT(results != NULL);
    HashJobCtx* jobs = cache_hash_jobs_alloc(FILE_COUNT);
    TEST_ASSERT(jobs != NULL);

    for (int i = 0; i < FILE_COUNT; i++) {
        jobs[i].inputs = make_inputs(src_paths[i], dep_paths[i]);
        jobs[i].result = &results[i];
        results[i].job_index = i;
    }

    int rc = cache_parallel_hash_dispatch(pool, jobs, FILE_COUNT, results);
    TEST_ASSERT_EQ(rc, 0);

    // All keys should be valid and match serial computation
    for (int i = 0; i < FILE_COUNT; i++) {
        TEST_ASSERT(results[i].valid == true);
        TEST_ASSERT_STR_EQ(results[i].key, expected_keys[i]);
    }

    free(results);
    free(jobs);
    threadpool_destroy(pool);
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: Fast-path resolved files (pre-marked valid) are NOT submitted to pool
// Requirement 8.6: Files resolved via mtime fast-path skip hash pool
// =============================================================================

TEST_SERIAL(parallel_hash_fastpath_resolved_not_submitted) {
    char root[520];
    get_temp_dir(root, sizeof(root), "fastpath_skip");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create 3 source files
    char src_paths[3][520];
    char hdr_paths[3][520];
    char dep_paths[3][520];

    for (int i = 0; i < 3; i++) {
        char name[32];
        snprintf(name, sizeof(name), "src%d.c", i);
        pal_path_join(src_paths[i], sizeof(src_paths[i]), root, name);
        char content[128];
        snprintf(content, sizeof(content), "int val%d(void) { return %d; }\n", i, i + 100);
        write_file(src_paths[i], content);

        snprintf(name, sizeof(name), "src%d.h", i);
        pal_path_join(hdr_paths[i], sizeof(hdr_paths[i]), root, name);
        snprintf(content, sizeof(content), "#pragma once\nint val%d(void);\n", i);
        write_file(hdr_paths[i], content);

        snprintf(name, sizeof(name), "src%d.d", i);
        pal_path_join(dep_paths[i], sizeof(dep_paths[i]), root, name);
        const char* hdrs[] = { hdr_paths[i] };
        write_dep_file(dep_paths[i], "src.o", src_paths[i], hdrs, 1);
    }

    // Simulate fast-path: mark file at index 1 as already resolved (valid=true with pre-set key)
    // This simulates the pipeline behavior where mtime fast-path resolves a file
    // BEFORE submitting to the hash pool — so it should NOT be re-hashed.

    HashResultSlot* results = cache_hash_results_alloc(3);
    TEST_ASSERT(results != NULL);
    HashJobCtx* jobs = cache_hash_jobs_alloc(3);
    TEST_ASSERT(jobs != NULL);

    // Set up all 3 jobs
    for (int i = 0; i < 3; i++) {
        jobs[i].inputs = make_inputs(src_paths[i], dep_paths[i]);
        jobs[i].result = &results[i];
        results[i].job_index = i;
    }

    // Pre-mark result slot 1 as resolved by fast-path
    const char* fastpath_key = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    strncpy(results[1].key, fastpath_key, CACHE_KEY_HEX_LEN);
    results[1].key[CACHE_KEY_HEX_LEN] = '\0';
    results[1].valid = true;  // Already resolved — should NOT be re-hashed

    // Compute expected keys for files 0 and 2 (the ones that SHOULD be hashed)
    char expected_key0[CACHE_KEY_HEX_LEN + 1] = {0};
    char expected_key2[CACHE_KEY_HEX_LEN + 1] = {0};
    CacheKeyInputs inputs0 = make_inputs(src_paths[0], dep_paths[0]);
    CacheKeyInputs inputs2 = make_inputs(src_paths[2], dep_paths[2]);
    TEST_ASSERT_EQ(cache_compute_key(&inputs0, expected_key0), 0);
    TEST_ASSERT_EQ(cache_compute_key(&inputs2, expected_key2), 0);

    // Dispatch with pool=NULL (serial, easier to reason about for this test)
    int rc = cache_parallel_hash_dispatch(NULL, jobs, 3, results);
    TEST_ASSERT_EQ(rc, 0);

    // File 0: should be hashed, key matches serial computation
    TEST_ASSERT(results[0].valid == true);
    TEST_ASSERT_STR_EQ(results[0].key, expected_key0);

    // File 1: should retain its fast-path key (NOT overwritten by hash pool)
    TEST_ASSERT(results[1].valid == true);
    TEST_ASSERT_STR_EQ(results[1].key, fastpath_key);

    // File 2: should be hashed, key matches serial computation
    TEST_ASSERT(results[2].valid == true);
    TEST_ASSERT_STR_EQ(results[2].key, expected_key2);

    free(results);
    free(jobs);
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: Fast-path resolved files skip hash pool with real thread pool
// Requirement 8.6: Even with parallelism > 1, pre-resolved files are untouched
// =============================================================================

TEST_SERIAL(parallel_hash_fastpath_resolved_not_submitted_threaded) {
    char root[520];
    get_temp_dir(root, sizeof(root), "fastpath_threaded");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create 4 source files, mark indices 0 and 2 as fast-path resolved
    char src_paths[4][520];
    char hdr_paths[4][520];
    char dep_paths[4][520];

    for (int i = 0; i < 4; i++) {
        char name[32];
        snprintf(name, sizeof(name), "mod%d.c", i);
        pal_path_join(src_paths[i], sizeof(src_paths[i]), root, name);
        char content[128];
        snprintf(content, sizeof(content), "int module%d(void) { return %d; }\n", i, i * 7);
        write_file(src_paths[i], content);

        snprintf(name, sizeof(name), "mod%d.h", i);
        pal_path_join(hdr_paths[i], sizeof(hdr_paths[i]), root, name);
        snprintf(content, sizeof(content), "#pragma once\nint module%d(void);\n", i);
        write_file(hdr_paths[i], content);

        snprintf(name, sizeof(name), "mod%d.d", i);
        pal_path_join(dep_paths[i], sizeof(dep_paths[i]), root, name);
        const char* hdrs[] = { hdr_paths[i] };
        write_dep_file(dep_paths[i], "mod.o", src_paths[i], hdrs, 1);
    }

    // Compute expected keys for files 1 and 3 (non-resolved ones)
    char expected_key1[CACHE_KEY_HEX_LEN + 1] = {0};
    char expected_key3[CACHE_KEY_HEX_LEN + 1] = {0};
    CacheKeyInputs inp1 = make_inputs(src_paths[1], dep_paths[1]);
    CacheKeyInputs inp3 = make_inputs(src_paths[3], dep_paths[3]);
    TEST_ASSERT_EQ(cache_compute_key(&inp1, expected_key1), 0);
    TEST_ASSERT_EQ(cache_compute_key(&inp3, expected_key3), 0);

    // Set up jobs and results
    HashResultSlot* results = cache_hash_results_alloc(4);
    TEST_ASSERT(results != NULL);
    HashJobCtx* jobs = cache_hash_jobs_alloc(4);
    TEST_ASSERT(jobs != NULL);

    const char* fp_key_0 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    const char* fp_key_2 = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

    for (int i = 0; i < 4; i++) {
        jobs[i].inputs = make_inputs(src_paths[i], dep_paths[i]);
        jobs[i].result = &results[i];
        results[i].job_index = i;
    }

    // Pre-mark indices 0 and 2 as fast-path resolved
    strncpy(results[0].key, fp_key_0, CACHE_KEY_HEX_LEN);
    results[0].key[CACHE_KEY_HEX_LEN] = '\0';
    results[0].valid = true;

    strncpy(results[2].key, fp_key_2, CACHE_KEY_HEX_LEN);
    results[2].key[CACHE_KEY_HEX_LEN] = '\0';
    results[2].valid = true;

    // Dispatch with a real thread pool
    ThreadPool* pool = threadpool_create(2);
    TEST_ASSERT(pool != NULL);

    int rc = cache_parallel_hash_dispatch(pool, jobs, 4, results);
    TEST_ASSERT_EQ(rc, 0);

    // Fast-path resolved files should retain their pre-set keys
    TEST_ASSERT(results[0].valid == true);
    TEST_ASSERT_STR_EQ(results[0].key, fp_key_0);

    TEST_ASSERT(results[2].valid == true);
    TEST_ASSERT_STR_EQ(results[2].key, fp_key_2);

    // Non-resolved files should be hashed and match serial computation
    TEST_ASSERT(results[1].valid == true);
    TEST_ASSERT_STR_EQ(results[1].key, expected_key1);

    TEST_ASSERT(results[3].valid == true);
    TEST_ASSERT_STR_EQ(results[3].key, expected_key3);

    free(results);
    free(jobs);
    threadpool_destroy(pool);
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: Allocation helpers return valid zero-initialized memory
// Requirement 8.5: Pre-allocated result slots for thread safety
// =============================================================================

TEST(parallel_hash_alloc_results_zero_initialized) {
    HashResultSlot* slots = cache_hash_results_alloc(5);
    TEST_ASSERT(slots != NULL);

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT(slots[i].valid == false);
        TEST_ASSERT_EQ(slots[i].key[0], '\0');
        TEST_ASSERT_EQ(slots[i].job_index, 0);
    }

    free(slots);
    return 0;
}

TEST(parallel_hash_alloc_jobs_zero_initialized) {
    HashJobCtx* jobs = cache_hash_jobs_alloc(3);
    TEST_ASSERT(jobs != NULL);

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT(jobs[i].inputs.source_path == NULL);
        TEST_ASSERT(jobs[i].inputs.compiler_path == NULL);
        TEST_ASSERT(jobs[i].result == NULL);
    }

    free(jobs);
    return 0;
}

// =============================================================================
// Test: Serial dispatch with zero jobs is a no-op (edge case)
// =============================================================================

TEST(parallel_hash_zero_jobs) {
    HashResultSlot* results = cache_hash_results_alloc(1);
    TEST_ASSERT(results != NULL);
    HashJobCtx* jobs = cache_hash_jobs_alloc(1);
    TEST_ASSERT(jobs != NULL);

    // Zero jobs is treated as an invalid call (returns non-zero)
    int rc = cache_parallel_hash_dispatch(NULL, jobs, 0, results);
    TEST_ASSERT(rc != 0);

    free(results);
    free(jobs);
    return 0;
}

// =============================================================================
// Test: Single file serial dispatch produces valid key
// Requirement 8.2: Serial mode (jobs=1) correct behavior
// =============================================================================

TEST_SERIAL(parallel_hash_single_file_serial) {
    char root[520];
    get_temp_dir(root, sizeof(root), "single");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char src_path[520];
    pal_path_join(src_path, sizeof(src_path), root, "only.c");
    write_file(src_path, "void only_func(void) {}\n");

    char hdr_path[520];
    pal_path_join(hdr_path, sizeof(hdr_path), root, "only.h");
    write_file(hdr_path, "#pragma once\nvoid only_func(void);\n");

    char dep_path[520];
    pal_path_join(dep_path, sizeof(dep_path), root, "only.d");
    const char* hdrs[] = { hdr_path };
    write_dep_file(dep_path, "only.o", src_path, hdrs, 1);

    CacheKeyInputs inputs = make_inputs(src_path, dep_path);
    char expected_key[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs, expected_key), 0);

    HashResultSlot* results = cache_hash_results_alloc(1);
    TEST_ASSERT(results != NULL);
    HashJobCtx* jobs = cache_hash_jobs_alloc(1);
    TEST_ASSERT(jobs != NULL);

    jobs[0].inputs = inputs;
    jobs[0].result = &results[0];
    results[0].job_index = 0;

    int rc = cache_parallel_hash_dispatch(NULL, jobs, 1, results);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(results[0].valid == true);
    TEST_ASSERT_STR_EQ(results[0].key, expected_key);

    free(results);
    free(jobs);
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: Parallel dispatch with many threads still produces correct keys
// Requirement 8.1, 8.3: More threads than files — still produces correct keys
// =============================================================================

TEST_SERIAL(parallel_hash_more_threads_than_files) {
    char root[520];
    get_temp_dir(root, sizeof(root), "many_threads");
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    // Create 2 files but use 4 threads
    char src1[520], src2[520];
    pal_path_join(src1, sizeof(src1), root, "x.c");
    pal_path_join(src2, sizeof(src2), root, "y.c");
    write_file(src1, "int x_val = 42;\n");
    write_file(src2, "int y_val = 99;\n");

    char hdr1[520], hdr2[520];
    pal_path_join(hdr1, sizeof(hdr1), root, "x.h");
    pal_path_join(hdr2, sizeof(hdr2), root, "y.h");
    write_file(hdr1, "#pragma once\nextern int x_val;\n");
    write_file(hdr2, "#pragma once\nextern int y_val;\n");

    char dep1[520], dep2[520];
    pal_path_join(dep1, sizeof(dep1), root, "x.d");
    pal_path_join(dep2, sizeof(dep2), root, "y.d");
    const char* h1[] = { hdr1 };
    const char* h2[] = { hdr2 };
    write_dep_file(dep1, "x.o", src1, h1, 1);
    write_dep_file(dep2, "y.o", src2, h2, 1);

    CacheKeyInputs inputs1 = make_inputs(src1, dep1);
    CacheKeyInputs inputs2 = make_inputs(src2, dep2);
    char expected1[CACHE_KEY_HEX_LEN + 1] = {0};
    char expected2[CACHE_KEY_HEX_LEN + 1] = {0};
    TEST_ASSERT_EQ(cache_compute_key(&inputs1, expected1), 0);
    TEST_ASSERT_EQ(cache_compute_key(&inputs2, expected2), 0);

    // Use 4 threads for 2 files
    ThreadPool* pool = threadpool_create(4);
    TEST_ASSERT(pool != NULL);

    HashResultSlot* results = cache_hash_results_alloc(2);
    TEST_ASSERT(results != NULL);
    HashJobCtx* jobs = cache_hash_jobs_alloc(2);
    TEST_ASSERT(jobs != NULL);

    jobs[0].inputs = inputs1;
    jobs[0].result = &results[0];
    results[0].job_index = 0;

    jobs[1].inputs = inputs2;
    jobs[1].result = &results[1];
    results[1].job_index = 1;

    int rc = cache_parallel_hash_dispatch(pool, jobs, 2, results);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(results[0].valid == true);
    TEST_ASSERT_STR_EQ(results[0].key, expected1);
    TEST_ASSERT(results[1].valid == true);
    TEST_ASSERT_STR_EQ(results[1].key, expected2);

    free(results);
    free(jobs);
    threadpool_destroy(pool);
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Test: All files resolved by fast-path — no hashing performed at all
// Requirement 8.6: If all files are fast-path resolved, dispatch is effectively a no-op
// =============================================================================

TEST(parallel_hash_all_fastpath_resolved) {
    HashResultSlot* results = cache_hash_results_alloc(3);
    TEST_ASSERT(results != NULL);
    HashJobCtx* jobs = cache_hash_jobs_alloc(3);
    TEST_ASSERT(jobs != NULL);

    const char* keys[] = {
        "1111111111111111111111111111111111111111111111111111111111111111",
        "2222222222222222222222222222222222222222222222222222222222222222",
        "3333333333333333333333333333333333333333333333333333333333333333"
    };

    // Pre-mark ALL results as valid (all resolved by fast-path)
    for (int i = 0; i < 3; i++) {
        strncpy(results[i].key, keys[i], CACHE_KEY_HEX_LEN);
        results[i].key[CACHE_KEY_HEX_LEN] = '\0';
        results[i].valid = true;
        results[i].job_index = i;
        // inputs can be empty/invalid since they should never be read
        jobs[i].result = &results[i];
    }

    int rc = cache_parallel_hash_dispatch(NULL, jobs, 3, results);
    TEST_ASSERT_EQ(rc, 0);

    // All keys should be untouched
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT(results[i].valid == true);
        TEST_ASSERT_STR_EQ(results[i].key, keys[i]);
    }

    free(results);
    free(jobs);
    return 0;
}
