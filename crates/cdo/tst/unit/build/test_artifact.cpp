// crates/cdo/tst/unit/build/test_artifact.cpp
// Unit tests for FileArtifact construction, existence, mtime, path, and type.
// Validates: Requirements 6.2, 6.3
#include "cdo_ut.h"
#include "build/artifact.h"
#include "pal/pal.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>

using namespace cdo::build;

// =============================================================================
// Helpers
// =============================================================================

static void get_temp_path(char* buf, size_t size, const char* suffix) {
    const char* tmp = std::getenv("TEMP");
    if (!tmp) tmp = std::getenv("TMP");
    if (!tmp) tmp = "/tmp";
    std::snprintf(buf, size, "%s/cdo_test_artifact_%s", tmp, suffix);
    pal_path_normalize(buf);
}

/// Create a temporary file with some content and return its path in buf.
static int create_temp_file(char* buf, size_t size, const char* suffix) {
    get_temp_path(buf, size, suffix);
    const char* content = "test content";
    return pal_file_write(buf, content, std::strlen(content));
}

// =============================================================================
// Construction Tests
// =============================================================================

TEST(file_artifact_construct_with_path_and_default_type) {
    FileArtifact fa("some/path/file.c");
    TEST_ASSERT_STR_EQ(fa.path().c_str(), "some/path/file.c");
    TEST_ASSERT_EQ((int)fa.type(), (int)ArtifactType::Source);
    return 0;
}

TEST(file_artifact_construct_with_path_and_explicit_type) {
    FileArtifact fa("build/debug/libfoo.a", ArtifactType::StaticLibrary);
    TEST_ASSERT_STR_EQ(fa.path().c_str(), "build/debug/libfoo.a");
    TEST_ASSERT_EQ((int)fa.type(), (int)ArtifactType::StaticLibrary);
    return 0;
}

TEST(file_artifact_construct_with_empty_path) {
    FileArtifact fa("", ArtifactType::Object);
    TEST_ASSERT_STR_EQ(fa.path().c_str(), "");
    TEST_ASSERT_EQ((int)fa.type(), (int)ArtifactType::Object);
    return 0;
}

// =============================================================================
// ArtifactType Storage and Retrieval
// =============================================================================

TEST(file_artifact_type_source) {
    FileArtifact fa("x.c", ArtifactType::Source);
    TEST_ASSERT_EQ((int)fa.type(), (int)ArtifactType::Source);
    return 0;
}

TEST(file_artifact_type_object) {
    FileArtifact fa("x.o", ArtifactType::Object);
    TEST_ASSERT_EQ((int)fa.type(), (int)ArtifactType::Object);
    return 0;
}

TEST(file_artifact_type_executable) {
    FileArtifact fa("app.exe", ArtifactType::Executable);
    TEST_ASSERT_EQ((int)fa.type(), (int)ArtifactType::Executable);
    return 0;
}

TEST(file_artifact_type_shared_library) {
    FileArtifact fa("lib.dll", ArtifactType::SharedLibrary);
    TEST_ASSERT_EQ((int)fa.type(), (int)ArtifactType::SharedLibrary);
    return 0;
}

TEST(file_artifact_type_shader_output) {
    FileArtifact fa("shader.dxil", ArtifactType::ShaderOutput);
    TEST_ASSERT_EQ((int)fa.type(), (int)ArtifactType::ShaderOutput);
    return 0;
}

TEST(file_artifact_type_depfile) {
    FileArtifact fa("main.d", ArtifactType::DepFile);
    TEST_ASSERT_EQ((int)fa.type(), (int)ArtifactType::DepFile);
    return 0;
}

TEST(file_artifact_type_header) {
    FileArtifact fa("util.h", ArtifactType::Header);
    TEST_ASSERT_EQ((int)fa.type(), (int)ArtifactType::Header);
    return 0;
}

// =============================================================================
// exists() Tests
// =============================================================================

TEST(file_artifact_exists_returns_true_for_real_file) {
    char path[512];
    int rc = create_temp_file(path, sizeof(path), "exists_test.txt");
    TEST_ASSERT_EQ(rc, PAL_OK);

    FileArtifact fa(path);
    TEST_ASSERT(fa.exists() == true);
    return 0;
}

TEST(file_artifact_exists_returns_false_for_nonexistent_path) {
    FileArtifact fa("__nonexistent_path_artifact_xyz_123456__");
    TEST_ASSERT(fa.exists() == false);
    return 0;
}

TEST(file_artifact_exists_returns_false_for_empty_path) {
    FileArtifact fa("");
    TEST_ASSERT(fa.exists() == false);
    return 0;
}

// =============================================================================
// mtime() Tests
// =============================================================================

TEST(file_artifact_mtime_returns_nonzero_for_existing_file) {
    char path[512];
    int rc = create_temp_file(path, sizeof(path), "mtime_test.txt");
    TEST_ASSERT_EQ(rc, PAL_OK);

    FileArtifact fa(path);
    uint64_t mt = fa.mtime();
    TEST_ASSERT(mt > 0);
    return 0;
}

TEST(file_artifact_mtime_returns_zero_for_missing_file) {
    FileArtifact fa("__nonexistent_mtime_path_987654__");
    uint64_t mt = fa.mtime();
    TEST_ASSERT_EQ(mt, (uint64_t)0);
    return 0;
}

TEST(file_artifact_mtime_returns_zero_for_empty_path) {
    FileArtifact fa("");
    uint64_t mt = fa.mtime();
    TEST_ASSERT_EQ(mt, (uint64_t)0);
    return 0;
}

TEST(file_artifact_mtime_consistent_with_pal) {
    char path[512];
    int rc = create_temp_file(path, sizeof(path), "mtime_pal_test.txt");
    TEST_ASSERT_EQ(rc, PAL_OK);

    uint64_t pal_mtime = 0;
    rc = pal_file_mtime(path, &pal_mtime);
    TEST_ASSERT_EQ(rc, PAL_OK);

    FileArtifact fa(path);
    uint64_t artifact_mtime = fa.mtime();

    // Should return the same value as direct PAL call
    TEST_ASSERT_EQ(artifact_mtime, pal_mtime);
    return 0;
}

// =============================================================================
// path() Tests
// =============================================================================

TEST(file_artifact_path_returns_constructed_path) {
    FileArtifact fa("crates/cdo/lib/main.c");
    TEST_ASSERT_STR_EQ(fa.path().c_str(), "crates/cdo/lib/main.c");
    return 0;
}

TEST(file_artifact_path_preserves_absolute_path) {
    FileArtifact fa("/usr/local/include/stdio.h", ArtifactType::Header);
    TEST_ASSERT_STR_EQ(fa.path().c_str(), "/usr/local/include/stdio.h");
    return 0;
}

TEST(file_artifact_path_preserves_windows_style_path) {
    FileArtifact fa("C:\\Users\\dev\\project\\main.c", ArtifactType::Source);
    TEST_ASSERT_STR_EQ(fa.path().c_str(), "C:\\Users\\dev\\project\\main.c");
    return 0;
}

// =============================================================================
// MSVC Registration (only needed on MSVC)
// =============================================================================

#ifdef _MSC_VER
void register_test_artifact_tests(void) {
    REGISTER_TEST(file_artifact_construct_with_path_and_default_type);
    REGISTER_TEST(file_artifact_construct_with_path_and_explicit_type);
    REGISTER_TEST(file_artifact_construct_with_empty_path);
    REGISTER_TEST(file_artifact_type_source);
    REGISTER_TEST(file_artifact_type_object);
    REGISTER_TEST(file_artifact_type_executable);
    REGISTER_TEST(file_artifact_type_shared_library);
    REGISTER_TEST(file_artifact_type_shader_output);
    REGISTER_TEST(file_artifact_type_depfile);
    REGISTER_TEST(file_artifact_type_header);
    REGISTER_TEST(file_artifact_exists_returns_true_for_real_file);
    REGISTER_TEST(file_artifact_exists_returns_false_for_nonexistent_path);
    REGISTER_TEST(file_artifact_exists_returns_false_for_empty_path);
    REGISTER_TEST(file_artifact_mtime_returns_nonzero_for_existing_file);
    REGISTER_TEST(file_artifact_mtime_returns_zero_for_missing_file);
    REGISTER_TEST(file_artifact_mtime_returns_zero_for_empty_path);
    REGISTER_TEST(file_artifact_mtime_consistent_with_pal);
    REGISTER_TEST(file_artifact_path_returns_constructed_path);
    REGISTER_TEST(file_artifact_path_preserves_absolute_path);
    REGISTER_TEST(file_artifact_path_preserves_windows_style_path);
}
#endif
