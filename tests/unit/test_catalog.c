// crates/cdo_pbt/src/unit/test_catalog.c
// Unit tests for Catalog loading, resolution, search, and serialization
#include "test_harness.h"
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
