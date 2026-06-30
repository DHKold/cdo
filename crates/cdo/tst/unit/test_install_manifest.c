// crates/cdo/tst/unit/test_install_manifest.c
// Unit tests for install manifest read/write and global index functions.
// Validates: REQ-INSTALL-6, REQ-INSTALL-7
#include "cdo_ut.h"
#include "commands/cmd_install_internal.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

typedef struct {
    char root[512];
} ManifestTestFixture;

static int mf_fixture_init(ManifestTestFixture* f, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(f->root, sizeof(f->root), "%s/cdo_test_manifest_%s", tmp, suffix);
    pal_path_normalize(f->root);
    pal_rmdir_r(f->root);
    pal_mkdir_p(f->root);
    return 0;
}

static void mf_fixture_destroy(ManifestTestFixture* f) {
    pal_rmdir_r(f->root);
}

/// Build a sample manifest with all fields populated.
static void mf_build_sample(InstallManifest* m) {
    memset(m, 0, sizeof(*m));
    strncpy(m->name, "my-app", sizeof(m->name) - 1);
    strncpy(m->version, "1.0.0", sizeof(m->version) - 1);
    strncpy(m->crate_name, "my_app", sizeof(m->crate_name) - 1);
    strncpy(m->source_workspace, "C:/projects/ws", sizeof(m->source_workspace) - 1);
    strncpy(m->installed_at, "2026-06-29T10:00:00Z", sizeof(m->installed_at) - 1);
    strncpy(m->cdo_version, "0.5.0", sizeof(m->cdo_version) - 1);
    strncpy(m->profile, "release", sizeof(m->profile) - 1);
    strncpy(m->executable, "my-app.exe", sizeof(m->executable) - 1);
    m->has_resources = true;
    m->has_shaders = false;
    strncpy(m->resource_base, ".", sizeof(m->resource_base) - 1);
    strncpy(m->shader_base, ".", sizeof(m->shader_base) - 1);
}

// ---------------------------------------------------------------------------
// Test: install_write_manifest writes correct TOML format
// Validates: REQ-INSTALL-6
// ---------------------------------------------------------------------------

TEST_SERIAL(install_manifest_write_correct_toml) {
    ManifestTestFixture f;
    TEST_ASSERT_EQ(mf_fixture_init(&f, "write_toml"), 0);

    InstallManifest m;
    mf_build_sample(&m);

    char manifest_path[1024];
    pal_path_join(manifest_path, sizeof(manifest_path), f.root, "manifest.toml");
    pal_path_normalize(manifest_path);

    int rc = install_write_manifest(&m, manifest_path);
    TEST_ASSERT_EQ(rc, 0);

    // Verify file was created
    TEST_ASSERT_EQ(pal_path_exists(manifest_path), 0);

    // Read content and verify key TOML fields
    char* content = NULL;
    size_t content_len = 0;
    TEST_ASSERT_EQ(pal_file_read(manifest_path, &content, &content_len), 0);
    TEST_ASSERT(content != NULL);
    TEST_ASSERT(content_len > 0);

    // Verify [app] section fields
    TEST_ASSERT(strstr(content, "[app]") != NULL);
    TEST_ASSERT(strstr(content, "name = \"my-app\"") != NULL);
    TEST_ASSERT(strstr(content, "version = \"1.0.0\"") != NULL);
    TEST_ASSERT(strstr(content, "crate = \"my_app\"") != NULL);
    TEST_ASSERT(strstr(content, "source_workspace = \"C:/projects/ws\"") != NULL);
    TEST_ASSERT(strstr(content, "installed_at = \"2026-06-29T10:00:00Z\"") != NULL);
    TEST_ASSERT(strstr(content, "cdo_version = \"0.5.0\"") != NULL);
    TEST_ASSERT(strstr(content, "profile = \"release\"") != NULL);

    // Verify [contents] section fields
    TEST_ASSERT(strstr(content, "[contents]") != NULL);
    TEST_ASSERT(strstr(content, "executable = \"my-app.exe\"") != NULL);
    TEST_ASSERT(strstr(content, "has_resources = true") != NULL);
    TEST_ASSERT(strstr(content, "has_shaders = false") != NULL);
    TEST_ASSERT(strstr(content, "resource_base = \".\"") != NULL);
    TEST_ASSERT(strstr(content, "shader_base = \".\"") != NULL);

    free(content);
    mf_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: install_read_manifest parses all fields correctly
// Validates: REQ-INSTALL-6
// ---------------------------------------------------------------------------

TEST_SERIAL(install_manifest_read_all_fields) {
    ManifestTestFixture f;
    TEST_ASSERT_EQ(mf_fixture_init(&f, "read_fields"), 0);

    // Write a known TOML manifest file manually
    const char* toml_content =
        "[app]\n"
        "name = \"test-app\"\n"
        "version = \"2.3.4\"\n"
        "crate = \"test_app\"\n"
        "source_workspace = \"D:/work/myproject\"\n"
        "installed_at = \"2025-12-01T08:30:00Z\"\n"
        "cdo_version = \"1.2.0\"\n"
        "profile = \"debug\"\n"
        "\n"
        "[contents]\n"
        "executable = \"test-app.exe\"\n"
        "has_resources = true\n"
        "has_shaders = true\n"
        "resource_base = \"data\"\n"
        "shader_base = \"shaders\"\n";

    char manifest_path[1024];
    pal_path_join(manifest_path, sizeof(manifest_path), f.root, "manifest.toml");
    pal_path_normalize(manifest_path);
    TEST_ASSERT_EQ(pal_file_write(manifest_path, toml_content, strlen(toml_content)), 0);

    InstallManifest out;
    int rc = install_read_manifest(manifest_path, &out);
    TEST_ASSERT_EQ(rc, 0);

    // Verify all fields parsed correctly
    TEST_ASSERT_STR_EQ(out.name, "test-app");
    TEST_ASSERT_STR_EQ(out.version, "2.3.4");
    TEST_ASSERT_STR_EQ(out.crate_name, "test_app");
    TEST_ASSERT_STR_EQ(out.source_workspace, "D:/work/myproject");
    TEST_ASSERT_STR_EQ(out.installed_at, "2025-12-01T08:30:00Z");
    TEST_ASSERT_STR_EQ(out.cdo_version, "1.2.0");
    TEST_ASSERT_STR_EQ(out.profile, "debug");
    TEST_ASSERT_STR_EQ(out.executable, "test-app.exe");
    TEST_ASSERT_EQ(out.has_resources, true);
    TEST_ASSERT_EQ(out.has_shaders, true);
    TEST_ASSERT_STR_EQ(out.resource_base, "data");
    TEST_ASSERT_STR_EQ(out.shader_base, "shaders");

    mf_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: Round-trip — write then read produces identical manifest
// Validates: REQ-INSTALL-6
// ---------------------------------------------------------------------------

TEST_SERIAL(install_manifest_roundtrip) {
    ManifestTestFixture f;
    TEST_ASSERT_EQ(mf_fixture_init(&f, "roundtrip"), 0);

    InstallManifest original;
    mf_build_sample(&original);

    char manifest_path[1024];
    pal_path_join(manifest_path, sizeof(manifest_path), f.root, "manifest.toml");
    pal_path_normalize(manifest_path);

    // Write manifest
    int rc = install_write_manifest(&original, manifest_path);
    TEST_ASSERT_EQ(rc, 0);

    // Read it back
    InstallManifest loaded;
    rc = install_read_manifest(manifest_path, &loaded);
    TEST_ASSERT_EQ(rc, 0);

    // Verify all fields match the original
    TEST_ASSERT_STR_EQ(loaded.name, original.name);
    TEST_ASSERT_STR_EQ(loaded.version, original.version);
    TEST_ASSERT_STR_EQ(loaded.crate_name, original.crate_name);
    TEST_ASSERT_STR_EQ(loaded.source_workspace, original.source_workspace);
    TEST_ASSERT_STR_EQ(loaded.installed_at, original.installed_at);
    TEST_ASSERT_STR_EQ(loaded.cdo_version, original.cdo_version);
    TEST_ASSERT_STR_EQ(loaded.profile, original.profile);
    TEST_ASSERT_STR_EQ(loaded.executable, original.executable);
    TEST_ASSERT_EQ(loaded.has_resources, original.has_resources);
    TEST_ASSERT_EQ(loaded.has_shaders, original.has_shaders);
    TEST_ASSERT_STR_EQ(loaded.resource_base, original.resource_base);
    TEST_ASSERT_STR_EQ(loaded.shader_base, original.shader_base);

    mf_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: install_update_global_index adds new entry
// Validates: REQ-INSTALL-7
// ---------------------------------------------------------------------------

TEST_SERIAL(install_manifest_global_index_add_new_entry) {
    ManifestTestFixture f;
    TEST_ASSERT_EQ(mf_fixture_init(&f, "idx_add"), 0);

    InstallManifest m;
    mf_build_sample(&m);

    // Update global index (apps_dir = f.root, no prior install.toml)
    int rc = install_update_global_index(f.root, &m);
    TEST_ASSERT_EQ(rc, 0);

    // Verify install.toml was created
    char index_path[1024];
    pal_path_join(index_path, sizeof(index_path), f.root, "install.toml");
    pal_path_normalize(index_path);
    TEST_ASSERT_EQ(pal_path_exists(index_path), 0);

    // Read and verify content
    char* content = NULL;
    size_t content_len = 0;
    TEST_ASSERT_EQ(pal_file_read(index_path, &content, &content_len), 0);
    TEST_ASSERT(content != NULL);

    TEST_ASSERT(strstr(content, "[[app]]") != NULL);
    TEST_ASSERT(strstr(content, "name = \"my-app\"") != NULL);
    TEST_ASSERT(strstr(content, "version = \"1.0.0\"") != NULL);
    TEST_ASSERT(strstr(content, "source_workspace = \"C:/projects/ws\"") != NULL);
    TEST_ASSERT(strstr(content, "installed_at = \"2026-06-29T10:00:00Z\"") != NULL);
    TEST_ASSERT(strstr(content, "path = \"my-app\"") != NULL);

    free(content);
    mf_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: install_update_global_index replaces existing entry (reinstall)
// Validates: REQ-INSTALL-7
// ---------------------------------------------------------------------------

TEST_SERIAL(install_manifest_global_index_replace_existing) {
    ManifestTestFixture f;
    TEST_ASSERT_EQ(mf_fixture_init(&f, "idx_replace"), 0);

    // First install version 1.0.0
    InstallManifest m1;
    mf_build_sample(&m1);
    int rc = install_update_global_index(f.root, &m1);
    TEST_ASSERT_EQ(rc, 0);

    // Reinstall with version 2.0.0
    InstallManifest m2;
    mf_build_sample(&m2);
    strncpy(m2.version, "2.0.0", sizeof(m2.version) - 1);
    strncpy(m2.installed_at, "2026-07-01T12:00:00Z", sizeof(m2.installed_at) - 1);
    rc = install_update_global_index(f.root, &m2);
    TEST_ASSERT_EQ(rc, 0);

    // Verify install.toml has only one entry with version 2.0.0
    char index_path[1024];
    pal_path_join(index_path, sizeof(index_path), f.root, "install.toml");
    pal_path_normalize(index_path);

    char* content = NULL;
    size_t content_len = 0;
    TEST_ASSERT_EQ(pal_file_read(index_path, &content, &content_len), 0);
    TEST_ASSERT(content != NULL);

    // Should have version 2.0.0 (the replacement)
    TEST_ASSERT(strstr(content, "version = \"2.0.0\"") != NULL);
    // Should NOT have the old version
    TEST_ASSERT(strstr(content, "version = \"1.0.0\"") == NULL);

    // Verify there is exactly one [[app]] entry
    int app_count = 0;
    const char* p = content;
    while ((p = strstr(p, "[[app]]")) != NULL) {
        app_count++;
        p += 7;
    }
    TEST_ASSERT_EQ(app_count, 1);

    free(content);
    mf_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: install_remove_from_global_index removes entry
// Validates: REQ-INSTALL-7
// ---------------------------------------------------------------------------

TEST_SERIAL(install_manifest_global_index_remove_entry) {
    ManifestTestFixture f;
    TEST_ASSERT_EQ(mf_fixture_init(&f, "idx_remove"), 0);

    // Add two apps to the index
    InstallManifest m1;
    mf_build_sample(&m1);
    strncpy(m1.name, "app-one", sizeof(m1.name) - 1);
    int rc = install_update_global_index(f.root, &m1);
    TEST_ASSERT_EQ(rc, 0);

    InstallManifest m2;
    mf_build_sample(&m2);
    strncpy(m2.name, "app-two", sizeof(m2.name) - 1);
    strncpy(m2.version, "3.0.0", sizeof(m2.version) - 1);
    rc = install_update_global_index(f.root, &m2);
    TEST_ASSERT_EQ(rc, 0);

    // Remove app-one
    rc = install_remove_from_global_index(f.root, "app-one");
    TEST_ASSERT_EQ(rc, 0);

    // Verify only app-two remains
    char index_path[1024];
    pal_path_join(index_path, sizeof(index_path), f.root, "install.toml");
    pal_path_normalize(index_path);

    char* content = NULL;
    size_t content_len = 0;
    TEST_ASSERT_EQ(pal_file_read(index_path, &content, &content_len), 0);
    TEST_ASSERT(content != NULL);

    TEST_ASSERT(strstr(content, "name = \"app-two\"") != NULL);
    TEST_ASSERT(strstr(content, "name = \"app-one\"") == NULL);

    // Verify exactly one [[app]] entry remains
    int app_count = 0;
    const char* p = content;
    while ((p = strstr(p, "[[app]]")) != NULL) {
        app_count++;
        p += 7;
    }
    TEST_ASSERT_EQ(app_count, 1);

    free(content);
    mf_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: install_remove_from_global_index with non-existent name is no-op
// Validates: REQ-INSTALL-7
// ---------------------------------------------------------------------------

TEST_SERIAL(install_manifest_global_index_remove_nonexistent_noop) {
    ManifestTestFixture f;
    TEST_ASSERT_EQ(mf_fixture_init(&f, "idx_noop"), 0);

    // Add one app
    InstallManifest m;
    mf_build_sample(&m);
    int rc = install_update_global_index(f.root, &m);
    TEST_ASSERT_EQ(rc, 0);

    // Try to remove a non-existent app — should succeed (no-op)
    rc = install_remove_from_global_index(f.root, "does-not-exist");
    TEST_ASSERT_EQ(rc, 0);

    // Verify the original entry is still there
    char index_path[1024];
    pal_path_join(index_path, sizeof(index_path), f.root, "install.toml");
    pal_path_normalize(index_path);

    char* content = NULL;
    size_t content_len = 0;
    TEST_ASSERT_EQ(pal_file_read(index_path, &content, &content_len), 0);
    TEST_ASSERT(content != NULL);
    TEST_ASSERT(strstr(content, "name = \"my-app\"") != NULL);

    // Still exactly one entry
    int app_count = 0;
    const char* p = content;
    while ((p = strstr(p, "[[app]]")) != NULL) {
        app_count++;
        p += 7;
    }
    TEST_ASSERT_EQ(app_count, 1);

    free(content);
    mf_fixture_destroy(&f);
    return 0;
}

// ---------------------------------------------------------------------------
// Test: install_manifest_set_timestamp produces valid ISO-8601 format
// Validates: REQ-INSTALL-6
// ---------------------------------------------------------------------------

TEST_SERIAL(install_manifest_set_timestamp_iso8601) {
    InstallManifest m;
    memset(&m, 0, sizeof(m));

    install_manifest_set_timestamp(&m);

    // installed_at should be non-empty
    TEST_ASSERT(m.installed_at[0] != '\0');

    // Verify ISO-8601 format: YYYY-MM-DDTHH:MM:SSZ (20 chars)
    size_t len = strlen(m.installed_at);
    TEST_ASSERT_EQ((int)len, 20);

    // Verify structure: digits and separators in correct positions
    // Format: 2026-06-29T10:00:00Z
    TEST_ASSERT(m.installed_at[4] == '-');   // Year-Month separator
    TEST_ASSERT(m.installed_at[7] == '-');   // Month-Day separator
    TEST_ASSERT(m.installed_at[10] == 'T');  // Date-Time separator
    TEST_ASSERT(m.installed_at[13] == ':');  // Hour-Minute separator
    TEST_ASSERT(m.installed_at[16] == ':');  // Minute-Second separator
    TEST_ASSERT(m.installed_at[19] == 'Z');  // UTC indicator

    // Verify year is reasonable (2020-2099)
    int year = atoi(m.installed_at);
    TEST_ASSERT(year >= 2020);
    TEST_ASSERT(year <= 2099);

    // Verify month is valid (01-12)
    int month = atoi(m.installed_at + 5);
    TEST_ASSERT(month >= 1);
    TEST_ASSERT(month <= 12);

    // Verify day is valid (01-31)
    int day = atoi(m.installed_at + 8);
    TEST_ASSERT(day >= 1);
    TEST_ASSERT(day <= 31);

    // Verify hour is valid (00-23)
    int hour = atoi(m.installed_at + 11);
    TEST_ASSERT(hour >= 0);
    TEST_ASSERT(hour <= 23);

    // Verify minute is valid (00-59)
    int minute = atoi(m.installed_at + 14);
    TEST_ASSERT(minute >= 0);
    TEST_ASSERT(minute <= 59);

    // Verify second is valid (00-59)
    int second = atoi(m.installed_at + 17);
    TEST_ASSERT(second >= 0);
    TEST_ASSERT(second <= 59);

    return 0;
}
