// test_sha256_cache.cpp — Unit tests for SHA256CacheLayer.
// Validates: Requirements 8.1, 8.2, 8.3, 8.4
//
// Tests the SHA-256 content-addressable cache layer:
//   - tryRestore with cache hit → returns true, file copied to output path
//   - tryRestore with cache miss → returns false, misses incremented
//   - store after successful build → artifact stored at SHA-256 keyed path
//   - disabled config → tryRestore returns false without filesystem access
//   - hits/misses counters accurate

#include "cdo_ut.h"
#include "build/sha256_cache.h"
#include "build/task.h"
#include "build/artifact.h"
#include "build/condition.h"
#include "commons/checksum.h"
#include "pal/pal.h"

#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>

using namespace cdo::build;

// =============================================================================
// Helpers
// =============================================================================

/// Get the platform temp directory.
static std::string get_temp_dir() {
    const char* tmp = std::getenv("TEMP");
    if (!tmp) tmp = std::getenv("TMP");
    if (!tmp) tmp = "/tmp";
    return std::string(tmp);
}

/// Build a unique temp path for a test scenario.
static std::string make_test_dir(const char* scenario) {
    std::string dir = get_temp_dir() + "/cdo_test_sha256_cache_" + scenario;
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s", dir.c_str());
    pal_path_normalize(buf);
    return std::string(buf);
}

/// Create a directory tree (cache root).
static int ensure_dir(const std::string& path) {
    return pal_mkdir_p(path.c_str());
}

/// Write content to a file at the given path.
static int write_file(const std::string& path, const char* content) {
    return pal_file_write(path.c_str(), content, std::strlen(content));
}

/// Check if a file exists.
static bool file_exists(const std::string& path) {
    return pal_path_exists(path.c_str()) == 0;
}

/// Read file content into a string.
static std::string read_file_content(const std::string& path) {
    char* buf = nullptr;
    size_t len = 0;
    if (pal_file_read(path.c_str(), &buf, &len) == 0 && buf) {
        std::string result(buf, len);
        free(buf);
        return result;
    }
    return "";
}

/// Compute the SHA-256 hash of a string (matches what the cache layer does internally).
static std::string compute_sha256(const std::string& data) {
    char hex_digest[129] = {};
    checksum_compute(data.data(), data.size(), CHECKSUM_SHA256, hex_digest);
    return std::string(hex_digest);
}

/// Clean up a directory tree (best-effort).
static void cleanup_dir(const std::string& path) {
    pal_rmdir_r(path.c_str());
}

// =============================================================================
// MockCondition: always returns Build or Skip as configured
// =============================================================================

class CacheTestCondition : public TaskCondition {
public:
    explicit CacheTestCondition(ConditionResult::Decision decision)
        : result_{ decision, decision == ConditionResult::Build ? "does not exist" : "up-to-date" } {}

    ConditionResult evaluate(const std::vector<const Artifact*>& /*inputs*/, const Artifact& /*output*/) const override {
        return result_;
    }
private:
    ConditionResult result_;
};

// =============================================================================
// StubTask: minimal Task implementation for cache layer testing.
// Exposes configurable inputs and a primary output path.
// =============================================================================

class StubTask : public Task {
public:
    StubTask(std::vector<std::string> input_paths, std::string output_path)
        : condition_(ConditionResult::Build)
        , output_artifact_(std::move(output_path), ArtifactType::Object) {
        for (auto& p : input_paths) {
            input_artifacts_.emplace_back(std::move(p), ArtifactType::Source);
        }
        for (auto& a : input_artifacts_) {
            inputs_cache_.push_back(&a);
        }
        outputs_cache_.push_back(&output_artifact_);
    }

    const std::vector<const Artifact*>& inputs() const override { return inputs_cache_; }
    const std::vector<const Artifact*>& outputs() const override { return outputs_cache_; }
    const Artifact& primaryOutput() const override { return output_artifact_; }
    const TaskCondition& condition() const override { return condition_; }

protected:
    int execute() override { return 0; }

private:
    CacheTestCondition condition_;
    FileArtifact output_artifact_;
    std::vector<FileArtifact> input_artifacts_;
    std::vector<const Artifact*> inputs_cache_;
    std::vector<const Artifact*> outputs_cache_;
};

// =============================================================================
// Test: tryRestore with cache hit → returns true, file copied to output path
// =============================================================================

TEST(sha256_cache_try_restore_hit_copies_to_output) {
    std::string test_dir = make_test_dir("hit");
    cleanup_dir(test_dir);
    std::string cache_root = test_dir + "/cache";
    std::string output_dir = test_dir + "/build";
    ensure_dir(cache_root);
    ensure_dir(output_dir);

    // Create a stub task with known inputs
    std::vector<std::string> inputs = { "src/main.c", "include/util.h" };
    std::string output_path = output_dir + "/main.o";
    StubTask task(inputs, output_path);

    // Compute the expected hash key (same logic as the cache layer)
    std::string combined;
    for (auto& p : inputs) combined += p;
    std::string hash = compute_sha256(combined);

    // Place a cached artifact at the expected cache path
    std::string prefix = hash.substr(0, 2);
    std::string prefix_dir = cache_root + "/" + prefix;
    ensure_dir(prefix_dir);
    std::string cached_file = prefix_dir + "/" + hash + ".o";
    write_file(cached_file, "cached_object_content");

    // Create cache layer and try restore
    SHA256CacheLayer::Config cfg;
    cfg.cache_root = cache_root;
    cfg.enabled = true;
    SHA256CacheLayer cache(cfg);

    bool restored = cache.tryRestore(task);

    TEST_ASSERT(restored == true);
    TEST_ASSERT(file_exists(output_path));
    TEST_ASSERT_STR_EQ(read_file_content(output_path).c_str(), "cached_object_content");
    TEST_ASSERT_EQ(cache.hits(), 1);
    TEST_ASSERT_EQ(cache.misses(), 0);

    cleanup_dir(test_dir);
    return 0;
}

// =============================================================================
// Test: tryRestore with cache miss → returns false, misses incremented
// =============================================================================

TEST(sha256_cache_try_restore_miss_returns_false) {
    std::string test_dir = make_test_dir("miss");
    cleanup_dir(test_dir);
    std::string cache_root = test_dir + "/cache";
    ensure_dir(cache_root);

    // Create a stub task — no cached artifact exists
    std::vector<std::string> inputs = { "src/app.c" };
    std::string output_path = test_dir + "/build/app.o";
    StubTask task(inputs, output_path);

    SHA256CacheLayer::Config cfg;
    cfg.cache_root = cache_root;
    cfg.enabled = true;
    SHA256CacheLayer cache(cfg);

    bool restored = cache.tryRestore(task);

    TEST_ASSERT(restored == false);
    TEST_ASSERT(file_exists(output_path) == false);
    TEST_ASSERT_EQ(cache.hits(), 0);
    TEST_ASSERT_EQ(cache.misses(), 1);

    cleanup_dir(test_dir);
    return 0;
}

// =============================================================================
// Test: store after successful build → artifact stored at SHA-256 keyed path
// =============================================================================

TEST(sha256_cache_store_places_artifact_at_hash_path) {
    std::string test_dir = make_test_dir("store");
    cleanup_dir(test_dir);
    std::string cache_root = test_dir + "/cache";
    std::string output_dir = test_dir + "/build";
    ensure_dir(cache_root);
    ensure_dir(output_dir);

    // Create a stub task with known output
    std::vector<std::string> inputs = { "src/lib.c", "include/lib.h" };
    std::string output_path = output_dir + "/lib.o";
    StubTask task(inputs, output_path);

    // Simulate a successful build by writing the primary output
    write_file(output_path, "freshly_built_object");

    SHA256CacheLayer::Config cfg;
    cfg.cache_root = cache_root;
    cfg.enabled = true;
    SHA256CacheLayer cache(cfg);

    cache.store(task);

    // Verify the file was stored at the expected cache path
    std::string combined;
    for (auto& p : inputs) combined += p;
    std::string hash = compute_sha256(combined);
    std::string prefix = hash.substr(0, 2);
    std::string expected_cache_path = cache_root + "/" + prefix + "/" + hash + ".o";

    TEST_ASSERT(file_exists(expected_cache_path));
    TEST_ASSERT_STR_EQ(read_file_content(expected_cache_path).c_str(), "freshly_built_object");

    cleanup_dir(test_dir);
    return 0;
}

// =============================================================================
// Test: disabled config → tryRestore returns false without filesystem access
// =============================================================================

TEST(sha256_cache_disabled_try_restore_returns_false) {
    SHA256CacheLayer::Config cfg;
    cfg.cache_root = "/nonexistent/path/should/not/be/accessed";
    cfg.enabled = false;
    SHA256CacheLayer cache(cfg);

    // Create a task with arbitrary inputs
    std::vector<std::string> inputs = { "src/main.c" };
    StubTask task(inputs, "/nonexistent/output.o");

    bool restored = cache.tryRestore(task);

    TEST_ASSERT(restored == false);
    // Counters should not be incremented when disabled
    TEST_ASSERT_EQ(cache.hits(), 0);
    TEST_ASSERT_EQ(cache.misses(), 0);

    return 0;
}

// =============================================================================
// Test: disabled config → store does nothing
// =============================================================================

TEST(sha256_cache_disabled_store_does_nothing) {
    std::string test_dir = make_test_dir("disabled_store");
    cleanup_dir(test_dir);
    std::string cache_root = test_dir + "/cache";
    std::string output_dir = test_dir + "/build";
    ensure_dir(output_dir);
    // Intentionally do NOT create cache_root

    std::vector<std::string> inputs = { "src/main.c" };
    std::string output_path = output_dir + "/main.o";
    StubTask task(inputs, output_path);
    write_file(output_path, "built_content");

    SHA256CacheLayer::Config cfg;
    cfg.cache_root = cache_root;
    cfg.enabled = false;
    SHA256CacheLayer cache(cfg);

    cache.store(task);

    // Cache root should not have been created
    TEST_ASSERT(file_exists(cache_root) == false);

    cleanup_dir(test_dir);
    return 0;
}

// =============================================================================
// Test: hits/misses counters accurate over multiple operations
// =============================================================================

TEST(sha256_cache_counters_accurate_over_multiple_ops) {
    std::string test_dir = make_test_dir("counters");
    cleanup_dir(test_dir);
    std::string cache_root = test_dir + "/cache";
    std::string output_dir = test_dir + "/build";
    ensure_dir(cache_root);
    ensure_dir(output_dir);

    SHA256CacheLayer::Config cfg;
    cfg.cache_root = cache_root;
    cfg.enabled = true;
    SHA256CacheLayer cache(cfg);

    // First: miss (nothing in cache)
    std::vector<std::string> inputs1 = { "src/a.c" };
    StubTask task1(inputs1, output_dir + "/a.o");
    cache.tryRestore(task1);
    TEST_ASSERT_EQ(cache.hits(), 0);
    TEST_ASSERT_EQ(cache.misses(), 1);

    // Second: another miss (different inputs)
    std::vector<std::string> inputs2 = { "src/b.c" };
    StubTask task2(inputs2, output_dir + "/b.o");
    cache.tryRestore(task2);
    TEST_ASSERT_EQ(cache.hits(), 0);
    TEST_ASSERT_EQ(cache.misses(), 2);

    // Store task1's output, then try to restore — should be a hit
    write_file(output_dir + "/a.o", "a_content");
    StubTask task1b(inputs1, output_dir + "/a.o");
    cache.store(task1b);

    // Now tryRestore with same inputs should hit
    std::string output_a_restored = output_dir + "/a_restored.o";
    StubTask task1c(inputs1, output_a_restored);
    bool hit = cache.tryRestore(task1c);
    TEST_ASSERT(hit == true);
    TEST_ASSERT_EQ(cache.hits(), 1);
    TEST_ASSERT_EQ(cache.misses(), 2);

    cleanup_dir(test_dir);
    return 0;
}

// =============================================================================
// Test: store does nothing if primary output doesn't exist
// =============================================================================

TEST(sha256_cache_store_skips_if_output_missing) {
    std::string test_dir = make_test_dir("store_missing");
    cleanup_dir(test_dir);
    std::string cache_root = test_dir + "/cache";
    ensure_dir(cache_root);

    // Output path does NOT exist on disk
    std::vector<std::string> inputs = { "src/missing.c" };
    std::string output_path = test_dir + "/build/missing.o";
    StubTask task(inputs, output_path);

    SHA256CacheLayer::Config cfg;
    cfg.cache_root = cache_root;
    cfg.enabled = true;
    SHA256CacheLayer cache(cfg);

    // Should not crash, just do nothing
    cache.store(task);

    // Verify nothing was placed in cache
    std::string combined;
    for (auto& p : inputs) combined += p;
    std::string hash = compute_sha256(combined);
    std::string prefix = hash.substr(0, 2);
    std::string cache_path = cache_root + "/" + prefix + "/" + hash + ".o";
    TEST_ASSERT(file_exists(cache_path) == false);

    cleanup_dir(test_dir);
    return 0;
}

// =============================================================================
// Test: tryRestore then store round-trip works correctly
// =============================================================================

TEST(sha256_cache_round_trip_miss_then_store_then_hit) {
    std::string test_dir = make_test_dir("roundtrip");
    cleanup_dir(test_dir);
    std::string cache_root = test_dir + "/cache";
    std::string output_dir = test_dir + "/build";
    ensure_dir(cache_root);
    ensure_dir(output_dir);

    std::vector<std::string> inputs = { "src/round.c" };
    std::string output_path = output_dir + "/round.o";

    SHA256CacheLayer::Config cfg;
    cfg.cache_root = cache_root;
    cfg.enabled = true;
    SHA256CacheLayer cache(cfg);

    // Step 1: tryRestore → miss
    StubTask task_miss(inputs, output_path);
    bool restored = cache.tryRestore(task_miss);
    TEST_ASSERT(restored == false);
    TEST_ASSERT_EQ(cache.misses(), 1);

    // Step 2: "build" the artifact, then store
    write_file(output_path, "round_trip_content");
    StubTask task_store(inputs, output_path);
    cache.store(task_store);

    // Step 3: tryRestore again with a new output path → hit
    std::string new_output = output_dir + "/round_restored.o";
    StubTask task_hit(inputs, new_output);
    restored = cache.tryRestore(task_hit);
    TEST_ASSERT(restored == true);
    TEST_ASSERT_EQ(cache.hits(), 1);
    TEST_ASSERT_STR_EQ(read_file_content(new_output).c_str(), "round_trip_content");

    cleanup_dir(test_dir);
    return 0;
}

// =============================================================================
// MSVC Registration
// =============================================================================

#ifdef _MSC_VER
#include <windows.h>

static void __cdecl register_sha256_cache_tests(void) {
    REGISTER_TEST(sha256_cache_try_restore_hit_copies_to_output);
    REGISTER_TEST(sha256_cache_try_restore_miss_returns_false);
    REGISTER_TEST(sha256_cache_store_places_artifact_at_hash_path);
    REGISTER_TEST(sha256_cache_disabled_try_restore_returns_false);
    REGISTER_TEST(sha256_cache_disabled_store_does_nothing);
    REGISTER_TEST(sha256_cache_counters_accurate_over_multiple_ops);
    REGISTER_TEST(sha256_cache_store_skips_if_output_missing);
    REGISTER_TEST(sha256_cache_round_trip_miss_then_store_then_hit);
}

#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static void(__cdecl* _reg_sha256_cache)(void) = register_sha256_cache_tests;
#endif
