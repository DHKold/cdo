// crates/cdo_pbt/src/unit/test_catalog.c
// Unit tests for Catalog loading, resolution, search, and serialization
#include "cdo_ut.h"
#include "core/catalog.h"

// --- catalog_load ---

TEST(catalog_load_valid) {
    Catalog cat = {0};
    int rc = catalog_load(&cat, ".");
    TEST_ASSERT_EQ(rc, 0);
    // Workspace has catalogs/tools.toml with at least one tool (w64devkit)
    TEST_ASSERT(cat.tool_count >= 1);
    // Verify first tool has a non-empty name
    TEST_ASSERT(cat.tools[0].name[0] != '\0');
    catalog_free(&cat);
    return 0;
}

// --- catalog_resolve_tool ---

TEST(catalog_resolve_tool_found) {
    Catalog cat = {0};
    int rc = catalog_load(&cat, ".");
    TEST_ASSERT_EQ(rc, 0);

    CatalogPlatform platform = {0};
    rc = catalog_detect_platform(&platform);
    TEST_ASSERT_EQ(rc, 0);

    CatalogResolveResult result = {0};
    rc = catalog_resolve_tool(&cat, "w64devkit", NULL, &platform, &result);
    TEST_ASSERT_EQ(rc, 0);
    // Should have a valid URL
    TEST_ASSERT(result.url[0] != '\0');
    // Should have a version
    TEST_ASSERT(result.version[0] != '\0');

    catalog_resolve_result_free(&result);
    catalog_free(&cat);
    return 0;
}

TEST(catalog_resolve_tool_not_found) {
    Catalog cat = {0};
    int rc = catalog_load(&cat, ".");
    TEST_ASSERT_EQ(rc, 0);

    CatalogPlatform platform = {0};
    rc = catalog_detect_platform(&platform);
    TEST_ASSERT_EQ(rc, 0);

    CatalogResolveResult result = {0};
    rc = catalog_resolve_tool(&cat, "nonexistent_tool_xyz", NULL, &platform, &result);
    TEST_ASSERT(rc != 0);

    catalog_free(&cat);
    return 0;
}

// --- DXC catalog resolution (Requirements 1.4, 1.6) ---

TEST(catalog_resolve_dxc_windows_x86_64) {
    // Requirement 1.4: resolving "dxc" for windows-x86_64 returns correct URL and checksum
    Catalog cat = {0};
    int rc = catalog_load(&cat, ".");
    TEST_ASSERT_EQ(rc, 0);

    // Construct the windows-x86_64 platform explicitly
    CatalogPlatform platform = {0};
    strcpy(platform.os, "windows");
    strcpy(platform.arch, "x86_64");
    strcpy(platform.triple, "windows-x86_64");

    CatalogResolveResult result = {0};
    rc = catalog_resolve_tool(&cat, "dxc", NULL, &platform, &result);
    TEST_ASSERT_EQ(rc, 0);

    // Verify the URL matches the catalog entry
    TEST_ASSERT_STR_EQ(result.url,
        "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505/dxc_2025_05_24.zip");

    // Verify the checksum matches the catalog entry
    TEST_ASSERT_STR_EQ(result.checksum,
        "sha256:81380f3eca156d902d6404fd6df9f4b0886f576ff3e18b2cc10d3075ffc9d119");

    // Verify the version is populated
    TEST_ASSERT_STR_EQ(result.version, "1.8.2505");

    catalog_resolve_result_free(&result);
    catalog_free(&cat);
    return 0;
}

TEST(catalog_resolve_dxc_not_in_catalog) {
    // Requirement 1.6: resolving a tool name not in the catalog returns non-zero
    Catalog cat = {0};
    int rc = catalog_load(&cat, ".");
    TEST_ASSERT_EQ(rc, 0);

    CatalogPlatform platform = {0};
    strcpy(platform.os, "windows");
    strcpy(platform.arch, "x86_64");
    strcpy(platform.triple, "windows-x86_64");

    CatalogResolveResult result = {0};
    rc = catalog_resolve_tool(&cat, "totally_fake_tool_12345", NULL, &platform, &result);
    TEST_ASSERT(rc != 0);

    catalog_free(&cat);
    return 0;
}

TEST(catalog_resolve_dxc_unsupported_platform) {
    // Requirement 1.6: resolving a tool for an unsupported platform returns non-zero
    Catalog cat = {0};
    int rc = catalog_load(&cat, ".");
    TEST_ASSERT_EQ(rc, 0);

    // Construct a platform that DXC does NOT have an entry for
    CatalogPlatform platform = {0};
    strcpy(platform.os, "linux");
    strcpy(platform.arch, "arm64");
    strcpy(platform.triple, "linux-arm64");

    CatalogResolveResult result = {0};
    rc = catalog_resolve_tool(&cat, "dxc", NULL, &platform, &result);
    TEST_ASSERT(rc != 0);

    catalog_free(&cat);
    return 0;
}

// --- catalog_search ---

TEST(catalog_search_match) {
    Catalog cat = {0};
    int rc = catalog_load(&cat, ".");
    TEST_ASSERT_EQ(rc, 0);

    int tool_indices[CATALOG_MAX_ARRAY_ITEMS] = {0};
    int pkg_indices[CATALOG_MAX_ARRAY_ITEMS] = {0};
    int tool_match_count = 0;
    int pkg_match_count = 0;

    // "w64" is a substring of "w64devkit"
    rc = catalog_search(&cat, "w64", false, false,
                        tool_indices, &tool_match_count,
                        pkg_indices, &pkg_match_count);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(tool_match_count >= 1);

    catalog_free(&cat);
    return 0;
}

TEST(catalog_search_no_match) {
    Catalog cat = {0};
    int rc = catalog_load(&cat, ".");
    TEST_ASSERT_EQ(rc, 0);

    int tool_indices[CATALOG_MAX_ARRAY_ITEMS] = {0};
    int pkg_indices[CATALOG_MAX_ARRAY_ITEMS] = {0};
    int tool_match_count = 0;
    int pkg_match_count = 0;

    rc = catalog_search(&cat, "zzzznonexistent999", false, false,
                        tool_indices, &tool_match_count,
                        pkg_indices, &pkg_match_count);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(tool_match_count, 0);
    TEST_ASSERT_EQ(pkg_match_count, 0);

    catalog_free(&cat);
    return 0;
}

// --- catalog_serialize ---

TEST(catalog_serialize_populated) {
    Catalog cat = {0};
    int rc = catalog_load(&cat, ".");
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(cat.tool_count >= 1);

    char* buf = NULL;
    size_t len = 0;
    rc = catalog_serialize(&cat, &buf, &len);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(buf != NULL);
    TEST_ASSERT(len > 0);
    // The serialized TOML should contain the tool name
    TEST_ASSERT(strstr(buf, "w64devkit") != NULL);

    free(buf);
    catalog_free(&cat);
    return 0;
}

TEST(catalog_serialize_empty) {
    Catalog cat = {0};
    // Empty catalog: zero tools, zero packages
    cat.tools = NULL;
    cat.tool_count = 0;
    cat.tool_capacity = 0;
    cat.packages = NULL;
    cat.package_count = 0;
    cat.package_capacity = 0;

    char* buf = NULL;
    size_t len = 0;
    int rc = catalog_serialize(&cat, &buf, &len);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(buf != NULL);

    free(buf);
    return 0;
}
