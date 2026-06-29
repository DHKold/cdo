// crates/cdo/tst/unit/test_cache_config.c
// Unit tests for cache configuration parsing and initialization
// Validates: Requirements 3.1, 3.4, 3.5, 4.2, 7.1, 7.3
#include "cdo_ut.h"
#include "model/workspace.h"
#include "core/cache.h"
#include "pal/pal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
// Helper: create a temporary workspace with a given cdo.toml content
// =============================================================================

static void get_temp_dir(char* buf, size_t size, const char* suffix) {
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(buf, size, "%s/cdo_test_cache_%s", tmp, suffix);
    pal_path_normalize(buf);
}

/// Create a minimal workspace in a temp directory with the given cdo.toml content.
/// Returns 0 on success.
static int create_test_workspace(const char* root, const char* cdo_toml_content) {
    if (pal_mkdir_p(root) != PAL_OK) return -1;

    // Write cdo.toml
    char toml_path[520];
    if (pal_path_join(toml_path, sizeof(toml_path), root, "cdo.toml") != 0) return -1;
    if (pal_file_write(toml_path, cdo_toml_content, strlen(cdo_toml_content)) != 0) return -1;

    return 0;
}

/// Clean up a test workspace directory.
static void cleanup_test_workspace(const char* root) {
    pal_rmdir_r(root);
}

// =============================================================================
// Tests: Default config when [workspace.settings.cache] section is absent
// Requirement 7.1: cache enabled by default
// Requirement 3.1: default path is .cdo/cache/objects
// Requirement 4.2: default max size is 2GB
// =============================================================================

TEST_SERIAL(cache_config_defaults_when_section_absent) {
    char root[520];
    get_temp_dir(root, sizeof(root), "defaults");

    const char* toml =
        "[workspace]\n"
        "members = []\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    // Requirement 7.1: enabled by default
    TEST_ASSERT(ws.cache_config.enabled == true);

    // Requirement 3.1: default path
    TEST_ASSERT_STR_EQ(ws.cache_config.path, ".cdo/cache/objects");

    // Requirement 4.2: default max size = 2GB = 2147483648
    TEST_ASSERT_EQ(ws.cache_config.max_size_bytes, (int64_t)2147483648);

    // Default backend = "builtin"
    TEST_ASSERT_STR_EQ(ws.cache_config.backend, "builtin");

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

// =============================================================================
// Tests: Custom path (relative and absolute)
// Requirement 3.4: cache path overridable in cdo.toml
// =============================================================================

TEST_SERIAL(cache_config_custom_path_relative) {
    char root[520];
    get_temp_dir(root, sizeof(root), "path_rel");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "path = \"my/custom/cache\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_STR_EQ(ws.cache_config.path, "my/custom/cache");

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

TEST_SERIAL(cache_config_custom_path_absolute) {
    char root[520];
    get_temp_dir(root, sizeof(root), "path_abs");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "path = \"C:/Users/shared/cache/objects\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_STR_EQ(ws.cache_config.path, "C:/Users/shared/cache/objects");

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

// =============================================================================
// Tests: Size parsing
// Requirement 4.2: max-size supports KB, MB, GB suffixes
// =============================================================================

TEST_SERIAL(cache_config_size_2gb) {
    char root[520];
    get_temp_dir(root, sizeof(root), "size_2gb");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "max-size = \"2GB\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_EQ(ws.cache_config.max_size_bytes, (int64_t)2147483648);

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

TEST_SERIAL(cache_config_size_500mb) {
    char root[520];
    get_temp_dir(root, sizeof(root), "size_500mb");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "max-size = \"500MB\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_EQ(ws.cache_config.max_size_bytes, (int64_t)524288000);

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

TEST_SERIAL(cache_config_size_100kb) {
    char root[520];
    get_temp_dir(root, sizeof(root), "size_100kb");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "max-size = \"100KB\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_EQ(ws.cache_config.max_size_bytes, (int64_t)102400);

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

// =============================================================================
// Tests: Invalid size string falls back to default
// =============================================================================

TEST_SERIAL(cache_config_size_invalid_abc) {
    char root[520];
    get_temp_dir(root, sizeof(root), "size_abc");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "max-size = \"abc\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    // Invalid size → keeps default 2GB
    TEST_ASSERT_EQ(ws.cache_config.max_size_bytes, (int64_t)2147483648);

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

TEST_SERIAL(cache_config_size_invalid_negative) {
    char root[520];
    get_temp_dir(root, sizeof(root), "size_neg");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "max-size = \"-5GB\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    // Negative size → keeps default 2GB
    TEST_ASSERT_EQ(ws.cache_config.max_size_bytes, (int64_t)2147483648);

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

TEST_SERIAL(cache_config_size_invalid_empty) {
    char root[520];
    get_temp_dir(root, sizeof(root), "size_empty");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "max-size = \"\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    // Empty size → keeps default 2GB
    TEST_ASSERT_EQ(ws.cache_config.max_size_bytes, (int64_t)2147483648);

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

// =============================================================================
// Tests: enabled = false
// Requirement 7.3: enabled = false disables cache persistently
// =============================================================================

TEST_SERIAL(cache_config_enabled_false) {
    char root[520];
    get_temp_dir(root, sizeof(root), "disabled");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "enabled = false\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT(ws.cache_config.enabled == false);

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

// =============================================================================
// Tests: Backend values
// Requirement 7.3, 8.1, 8.2, 8.5: backend can be "builtin", "ccache", "sccache"
// =============================================================================

TEST_SERIAL(cache_config_backend_builtin) {
    char root[520];
    get_temp_dir(root, sizeof(root), "be_builtin");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "backend = \"builtin\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_STR_EQ(ws.cache_config.backend, "builtin");

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

TEST_SERIAL(cache_config_backend_ccache) {
    char root[520];
    get_temp_dir(root, sizeof(root), "be_ccache");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "backend = \"ccache\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_STR_EQ(ws.cache_config.backend, "ccache");

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

TEST_SERIAL(cache_config_backend_sccache) {
    char root[520];
    get_temp_dir(root, sizeof(root), "be_sccache");

    const char* toml =
        "[workspace]\n"
        "members = []\n"
        "[workspace.settings.cache]\n"
        "backend = \"sccache\"\n";

    int rc = create_test_workspace(root, toml);
    TEST_ASSERT_EQ(rc, 0);

    Workspace ws = {0};
    rc = workspace_load(root, &ws);
    TEST_ASSERT_EQ(rc, 0);

    TEST_ASSERT_STR_EQ(ws.cache_config.backend, "sccache");

    workspace_free(&ws);
    cleanup_test_workspace(root);
    return 0;
}

// =============================================================================
// Tests: cache_init creates directory when it doesn't exist
// Requirement 3.5: cache directory created on first cache population
// =============================================================================

TEST_SERIAL(cache_init_creates_directory) {
    char root[520];
    get_temp_dir(root, sizeof(root), "init_mkdir");

    // Ensure root exists but cache subdir does not
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    CacheConfig config;
    strncpy(config.path, "test_cache_dir", sizeof(config.path) - 1);
    config.path[sizeof(config.path) - 1] = '\0';
    config.max_size_bytes = (int64_t)1024 * 1024 * 1024; // 1GB
    config.enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);
    config.backend[sizeof(config.backend) - 1] = '\0';

    int rc = cache_init(&config, root);
    TEST_ASSERT_EQ(rc, 0);

    // Verify the directory was created (cache_init resolves relative path and creates it)
    TEST_ASSERT_EQ(pal_path_exists(config.path), 0);

    // Cleanup
    pal_rmdir_r(root);
    // Also clean the resolved path in case it was created elsewhere
    // (config.path was updated in-place to the resolved absolute path)
    pal_rmdir_r(config.path);
    return 0;
}

TEST_SERIAL(cache_init_existing_directory_ok) {
    char root[520];
    get_temp_dir(root, sizeof(root), "init_exists");

    // Create both root and cache directory
    pal_rmdir_r(root);
    pal_mkdir_p(root);

    char cache_dir[520];
    pal_path_join(cache_dir, sizeof(cache_dir), root, "existing_cache");
    pal_mkdir_p(cache_dir);

    CacheConfig config;
    strncpy(config.path, "existing_cache", sizeof(config.path) - 1);
    config.path[sizeof(config.path) - 1] = '\0';
    config.max_size_bytes = (int64_t)2147483648;
    config.enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);
    config.backend[sizeof(config.backend) - 1] = '\0';

    int rc = cache_init(&config, root);
    TEST_ASSERT_EQ(rc, 0);

    // Cleanup
    pal_rmdir_r(root);
    return 0;
}

// =============================================================================
// Tests: cache_init validates config values
// =============================================================================

TEST(cache_init_invalid_backend) {
    CacheConfig config;
    strncpy(config.path, "/tmp/test_cache", sizeof(config.path) - 1);
    config.path[sizeof(config.path) - 1] = '\0';
    config.max_size_bytes = (int64_t)1024 * 1024 * 1024;
    config.enabled = true;
    strncpy(config.backend, "invalid_backend", sizeof(config.backend) - 1);
    config.backend[sizeof(config.backend) - 1] = '\0';

    int rc = cache_init(&config, ".");
    TEST_ASSERT(rc != 0);

    return 0;
}

TEST(cache_init_invalid_size_zero) {
    CacheConfig config;
    strncpy(config.path, "/tmp/test_cache", sizeof(config.path) - 1);
    config.path[sizeof(config.path) - 1] = '\0';
    config.max_size_bytes = 0;
    config.enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);
    config.backend[sizeof(config.backend) - 1] = '\0';

    int rc = cache_init(&config, ".");
    TEST_ASSERT(rc != 0);

    return 0;
}

TEST(cache_init_invalid_size_negative) {
    CacheConfig config;
    strncpy(config.path, "/tmp/test_cache", sizeof(config.path) - 1);
    config.path[sizeof(config.path) - 1] = '\0';
    config.max_size_bytes = -100;
    config.enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);
    config.backend[sizeof(config.backend) - 1] = '\0';

    int rc = cache_init(&config, ".");
    TEST_ASSERT(rc != 0);

    return 0;
}

TEST(cache_init_null_config) {
    int rc = cache_init(NULL, ".");
    TEST_ASSERT(rc != 0);

    return 0;
}

TEST(cache_init_null_ws_root) {
    CacheConfig config;
    strncpy(config.path, ".cdo/cache/objects", sizeof(config.path) - 1);
    config.path[sizeof(config.path) - 1] = '\0';
    config.max_size_bytes = (int64_t)2147483648;
    config.enabled = true;
    strncpy(config.backend, "builtin", sizeof(config.backend) - 1);
    config.backend[sizeof(config.backend) - 1] = '\0';

    int rc = cache_init(&config, NULL);
    TEST_ASSERT(rc != 0);

    return 0;
}
