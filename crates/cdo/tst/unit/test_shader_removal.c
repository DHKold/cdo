// crates/cdo/tst/unit/test_shader_removal.c
// Unit tests for shader command removal / deprecation
// Requirements: 5.1, 5.2
//
// The old tests verified that cdo_cli_parse recognized "shader" as CDO_CMD_SHADER.
// With the removal of CdoOptions/CdoCommand (task 5.5), the shader command is no
// longer in the new registry at all — it was removed entirely as per the design.
// The relevant behavior (error on unknown command + suggestions) is tested in
// test_parse_integration.c.
#include "cdo_ut.h"
#include "cmd/cli_cmd.h"
#include "core/registry_setup.h"

#include <stdio.h>
#include <string.h>

/* ============================================================
 * Requirement 5.1: `cdo shader` is not a recognized command
 * in the new registry (it was removed, not deprecated).
 * Parsing should return an error.
 * ============================================================ */

TEST(shader_removal_not_in_registry) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = {"cdo", "shader"};
    CliArgValue arg_buf[32];
    CliParseResult result = {0};
    int rc = cli_cmd_parse(reg, 2, argv, arg_buf, 32, &result);

    // shader is not registered — parse should fail
    TEST_ASSERT(rc != 0);

    cli_cmd_registry_destroy(reg);
    return 0;
}

/* ============================================================
 * Requirement 5.2: `cdo shader build` also fails — shader
 * is fully removed, not just a deprecated entry.
 * ============================================================ */

TEST(shader_removal_with_subarg_fails) {
    CliCmdRegistry* reg = cdo_registry_create();
    TEST_ASSERT(reg != NULL);

    const char* argv[] = {"cdo", "shader", "build"};
    CliArgValue arg_buf[32];
    CliParseResult result = {0};
    int rc = cli_cmd_parse(reg, 3, argv, arg_buf, 32, &result);

    TEST_ASSERT(rc != 0);

    cli_cmd_registry_destroy(reg);
    return 0;
}
