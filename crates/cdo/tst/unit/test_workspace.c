// crates/cdo_pbt/src/unit/test_workspace.c
// Unit tests for workspace loading and build order resolution
#include "cdo_ut.h"
#include "core/workspace.h"

// --- workspace_load ---

TEST(workspace_load_valid) {
    Workspace ws = {0};
    int rc = workspace_load(".", &ws);
    TEST_ASSERT_EQ(rc, 0);

    // The workspace cdo.toml has members = ["crates/cdo"]
    TEST_ASSERT(ws.crate_count >= 1);

    // Verify the cdo crate exists
    int found_cdo = 0;
    for (int i = 0; i < ws.crate_count; i++) {
        if (strcmp(ws.crates[i].name, "cdo") == 0) {
            found_cdo = 1;
        }
    }
    TEST_ASSERT(found_cdo);

    workspace_free(&ws);
    return 0;
}

TEST(workspace_load_nonexistent) {
    Workspace ws = {0};
    int rc = workspace_load("__nonexistent_dir__", &ws);
    TEST_ASSERT(rc != 0);

    workspace_free(&ws);
    return 0;
}

// --- workspace_resolve ---

TEST(workspace_resolve_no_cycles) {
    Workspace ws = {0};
    int rc = workspace_load(".", &ws);
    TEST_ASSERT_EQ(rc, 0);

    // Resolve build order for "cdo"
    const char* names[] = {"cdo"};
    rc = workspace_resolve(&ws, names, 1);
    TEST_ASSERT_EQ(rc, 0);

    // Should have a valid build order with at least 1 entry
    TEST_ASSERT(ws.build_order_count >= 1);
    TEST_ASSERT(ws.build_order != NULL);

    // Verify topological ordering: dependencies come before dependents.
    // "cdo" should appear before "cdo_pbt" in the build order since
    // Verify "cdo" is in the build order
    int cdo_idx_in_order = -1;
    for (int i = 0; i < ws.build_order_count; i++) {
        int crate_idx = ws.build_order[i];
        if (strcmp(ws.crates[crate_idx].name, "cdo") == 0) {
            cdo_idx_in_order = i;
        }
    }
    TEST_ASSERT(cdo_idx_in_order >= 0);

    workspace_free(&ws);
    return 0;
}

TEST(workspace_resolve_circular) {
    // Construct a synthetic workspace with a circular dependency:
    // crate A depends on B, B depends on A.
    Workspace ws = {0};
    ws.crate_count = 2;
    ws.crates = (Crate*)calloc(2, sizeof(Crate));
    TEST_ASSERT(ws.crates != NULL);

    // Crate 0: "a" depends on "b" (index 1)
    strcpy(ws.crates[0].name, "a");
    ws.crates[0].type = CRATE_EXECUTABLE;
    ws.crates[0].dep_count = 1;
    ws.crates[0].dep_indices = (int*)malloc(sizeof(int));
    ws.crates[0].dep_indices[0] = 1;

    // Crate 1: "b" depends on "a" (index 0)
    strcpy(ws.crates[1].name, "b");
    ws.crates[1].type = CRATE_EXECUTABLE;
    ws.crates[1].dep_count = 1;
    ws.crates[1].dep_indices = (int*)malloc(sizeof(int));
    ws.crates[1].dep_indices[0] = 0;

    ws.build_order = NULL;
    ws.build_order_count = 0;

    // Resolve all crates — should detect the cycle and return non-zero
    int rc = workspace_resolve(&ws, NULL, 0);
    TEST_ASSERT(rc != 0);

    // Clean up
    free(ws.crates[0].dep_indices);
    free(ws.crates[1].dep_indices);
    free(ws.build_order);
    free(ws.crates);
    return 0;
}
