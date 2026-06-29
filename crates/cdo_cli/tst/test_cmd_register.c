/**
 * test_cmd_register.c - Unit tests for command registration and tree building.
 *
 * Tests the command registry lifecycle (create/destroy), top-level command
 * registration with duplicate detection, and subcommand registration under
 * parent paths.
 *
 * Validates: Requirements 1.1, 1.2, 1.3
 */

#include "cdo_ut.h"
#include "../api/cmd/cli_cmd.h"
#include "../api/cli_errors.h"

#include <string.h>

// =============================================================================
// Helper: Create a minimal CliCmdSpec with just a name and description.
// =============================================================================

static CliCmdSpec make_spec(const char* name, const char* description) {
    CliCmdSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.name = name;
    spec.description = description;
    spec.handler = NULL;
    spec.handler_ctx = NULL;
    spec.args = NULL;
    spec.arg_count = 0;
    spec.positionals = NULL;
    spec.positional_count = 0;
    return spec;
}

// =============================================================================
// Test: Registry create returns a valid (non-NULL) handle.
// Requirement 1.1: API for registering commands implies a registry exists.
// =============================================================================

TEST(cmd_registry_create_returns_non_null) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Registry destroy accepts NULL gracefully (no crash).
// Defensive: destroy(NULL) should be a no-op.
// =============================================================================

TEST(cmd_registry_destroy_null_is_safe) {
    cli_cmd_registry_destroy(NULL);
    // If we reach here without crashing, the test passes.
    return 0;
}

// =============================================================================
// Test: Registry create/destroy lifecycle completes without errors.
// Validates basic lifecycle with no commands registered (no leaks).
// Requirement 1.1: Registry exists and can be cleaned up.
// =============================================================================

TEST(cmd_registry_lifecycle_empty) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    // Destroy immediately with no registrations - should not leak.
    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Register a single top-level command returns CLI_OK.
// Requirement 1.1: C API for registering a Command_Spec.
// =============================================================================

TEST(cmd_register_single_command_success) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec = make_spec("build", "Build the project");

    int rc = cli_cmd_register(reg, &spec);
    TEST_ASSERT_EQ(rc, CLI_OK);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Register duplicate name at same level returns CLI_ERR_DUPLICATE.
// Requirement 1.3: Duplicate name at same level returns error code.
// =============================================================================

TEST(cmd_register_duplicate_returns_error) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec1 = make_spec("build", "Build the project");
    CliCmdSpec spec2 = make_spec("build", "Build something else");

    int rc1 = cli_cmd_register(reg, &spec1);
    TEST_ASSERT_EQ(rc1, CLI_OK);

    int rc2 = cli_cmd_register(reg, &spec2);
    TEST_ASSERT_EQ(rc2, CLI_ERR_DUPLICATE);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Register subcommand under existing parent path returns CLI_OK.
// Requirement 1.2: Recursive nesting of commands to arbitrary depth.
// =============================================================================

TEST(cmd_register_sub_under_parent_success) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    // Register parent top-level command
    CliCmdSpec parent = make_spec("deps", "Dependency management");
    int rc = cli_cmd_register(reg, &parent);
    TEST_ASSERT_EQ(rc, CLI_OK);

    // Register subcommand under "deps"
    CliCmdSpec child = make_spec("add", "Add a dependency");
    rc = cli_cmd_register_sub(reg, "deps", &child);
    TEST_ASSERT_EQ(rc, CLI_OK);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Register subcommand under non-existent parent returns CLI_ERR_NOT_FOUND.
// Requirement 1.2: Parent path must exist for subcommand registration.
// =============================================================================

TEST(cmd_register_sub_nonexistent_parent_returns_error) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    // Try to register subcommand under a parent that doesn't exist
    CliCmdSpec child = make_spec("add", "Add something");
    int rc = cli_cmd_register_sub(reg, "nonexistent", &child);
    TEST_ASSERT_EQ(rc, CLI_ERR_NOT_FOUND);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Register multiple commands at top level and verify all succeed.
// Requirement 1.1: Register multiple commands; count is implicit through
// successful return codes on all registrations.
// =============================================================================

TEST(cmd_register_multiple_commands_success) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    CliCmdSpec spec_build = make_spec("build", "Build the project");
    CliCmdSpec spec_test = make_spec("test", "Run tests");
    CliCmdSpec spec_clean = make_spec("clean", "Clean build artifacts");
    CliCmdSpec spec_init = make_spec("init", "Initialize a workspace");
    CliCmdSpec spec_deps = make_spec("deps", "Manage dependencies");

    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec_build), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec_test), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec_clean), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec_init), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register(reg, &spec_deps), CLI_OK);

    // All 5 commands registered without error - count is verified implicitly.
    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Register nested subcommands at multiple depth levels.
// Requirement 1.2: Recursive nesting to arbitrary depth (no hardcoded limit).
// =============================================================================

TEST(cmd_register_sub_nested_depth) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    // Level 0: "deps"
    CliCmdSpec deps = make_spec("deps", "Dependency management");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &deps), CLI_OK);

    // Level 1: "deps" -> "add"
    CliCmdSpec add = make_spec("add", "Add a dependency");
    TEST_ASSERT_EQ(cli_cmd_register_sub(reg, "deps", &add), CLI_OK);

    // Level 2: "deps.add" -> "local"
    CliCmdSpec local = make_spec("local", "Add a local dependency");
    TEST_ASSERT_EQ(cli_cmd_register_sub(reg, "deps.add", &local), CLI_OK);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Duplicate subcommand under same parent returns CLI_ERR_DUPLICATE.
// Requirement 1.3: Duplicate detection applies at each nesting level.
// =============================================================================

TEST(cmd_register_sub_duplicate_returns_error) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    // Register parent
    CliCmdSpec deps = make_spec("deps", "Dependency management");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &deps), CLI_OK);

    // Register first subcommand
    CliCmdSpec add1 = make_spec("add", "Add a dependency");
    TEST_ASSERT_EQ(cli_cmd_register_sub(reg, "deps", &add1), CLI_OK);

    // Register duplicate subcommand under same parent
    CliCmdSpec add2 = make_spec("add", "Add something else");
    int rc = cli_cmd_register_sub(reg, "deps", &add2);
    TEST_ASSERT_EQ(rc, CLI_ERR_DUPLICATE);

    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Registry create/destroy lifecycle with commands registered (no leaks).
// Validates that destroy properly frees the command tree.
// =============================================================================

TEST(cmd_registry_lifecycle_with_commands) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    // Register several commands and subcommands
    CliCmdSpec build = make_spec("build", "Build the project");
    CliCmdSpec test = make_spec("test", "Run tests");
    CliCmdSpec deps = make_spec("deps", "Manage dependencies");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &build), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register(reg, &test), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register(reg, &deps), CLI_OK);

    CliCmdSpec add = make_spec("add", "Add a dependency");
    CliCmdSpec remove = make_spec("remove", "Remove a dependency");
    TEST_ASSERT_EQ(cli_cmd_register_sub(reg, "deps", &add), CLI_OK);
    TEST_ASSERT_EQ(cli_cmd_register_sub(reg, "deps", &remove), CLI_OK);

    // Destroy should free all nodes without leaking.
    cli_cmd_registry_destroy(reg);
    return 0;
}

// =============================================================================
// Test: Same command name at different levels does NOT conflict.
// "build" at top-level and "deps.build" should both succeed.
// Requirement 1.3: Duplicate detection is per-level, not global.
// =============================================================================

TEST(cmd_register_same_name_different_levels_ok) {
    CliCmdRegistry* reg = cli_cmd_registry_create();
    TEST_ASSERT(reg != NULL);

    // Top-level "build"
    CliCmdSpec build_top = make_spec("build", "Top-level build");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &build_top), CLI_OK);

    // Register "deps" parent
    CliCmdSpec deps = make_spec("deps", "Dependency management");
    TEST_ASSERT_EQ(cli_cmd_register(reg, &deps), CLI_OK);

    // Sub-level "deps.build" - same name "build" but at a different level
    CliCmdSpec build_sub = make_spec("build", "Build dependencies");
    int rc = cli_cmd_register_sub(reg, "deps", &build_sub);
    TEST_ASSERT_EQ(rc, CLI_OK);

    cli_cmd_registry_destroy(reg);
    return 0;
}

