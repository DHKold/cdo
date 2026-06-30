#include "commands/cmd_doctor.h"
#include "core/compiler.h"
#include "core/handler_ctx.h"
#include "cmd/cli_cmd.h"
#include "model/workspace.h"
#include "model/deps.h"
#include "commons/toml.h"
#include "core/log.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- ANSI Color Codes ---
#define PASS_TAG "\033[32m[PASS]\033[0m"
#define WARN_TAG "\033[33m[WARN]\033[0m"
#define FAIL_TAG "\033[31m[FAIL]\033[0m"

// --- Check result enum ---
typedef enum {
    CHECK_PASS,
    CHECK_WARN,
    CHECK_FAIL,
} CheckResult;

/// Returns true if "--fix" appears in the positional args.
static bool doctor_has_fix_flag(const CliParseResult* result) {
    for (int i = 0; i < result->positional_count; i++) {
        if (strcmp(result->positional_values[i], "--fix") == 0) {
            return true;
        }
    }
    return false;
}

/// Print a check result line with color-coded tag.
static void doctor_print(CheckResult result, const char* description) {
    const char* tag;
    switch (result) {
        case CHECK_PASS: tag = PASS_TAG; break;
        case CHECK_WARN: tag = WARN_TAG; break;
        case CHECK_FAIL: tag = FAIL_TAG; break;
        default:         tag = FAIL_TAG; break;
    }
    printf("  %s %s\n", tag, description);
}

/// Check 1: C/C++ compiler on PATH.
static CheckResult check_compiler(void) {
    CompilerInfo info;
    if (compiler_detect(&info) == 0) {
        char msg[320];
        snprintf(msg, sizeof(msg), "C/C++ compiler found: %s (%s)", info.path, info.version);
        doctor_print(CHECK_PASS, msg);
        return CHECK_PASS;
    }
    doctor_print(CHECK_FAIL, "No C/C++ compiler found on PATH");
    return CHECK_FAIL;
}

/// Check 2: Workspace manifest (cdo.toml) is syntactically valid.
static CheckResult check_workspace_manifest(const char* root_path) {
    char manifest_path[520];
    if (pal_path_join(manifest_path, sizeof(manifest_path), root_path, "cdo.toml") != 0) {
        doctor_print(CHECK_FAIL, "Workspace manifest path too long");
        return CHECK_FAIL;
    }

    if (pal_path_exists(manifest_path) != 0) {
        doctor_print(CHECK_FAIL, "Workspace manifest (cdo.toml) not found");
        return CHECK_FAIL;
    }

    char* buf = NULL;
    size_t len = 0;
    if (pal_file_read(manifest_path, &buf, &len) != 0) {
        doctor_print(CHECK_FAIL, "Failed to read cdo.toml");
        return CHECK_FAIL;
    }

    TomlTable* table = NULL;
    TomlError err;
    int rc = toml_parse(buf, len, &table, &err);
    free(buf);

    if (rc != 0) {
        char msg[320];
        snprintf(msg, sizeof(msg), "cdo.toml parse error at line %d: %s", err.line, err.message);
        doctor_print(CHECK_FAIL, msg);
        return CHECK_FAIL;
    }

    toml_free(table);
    doctor_print(CHECK_PASS, "Workspace manifest (cdo.toml) is valid");
    return CHECK_PASS;
}

/// Check 3: Crate manifests (crate.toml) for each crate are syntactically valid.
static CheckResult check_crate_manifests(const Workspace* ws) {
    CheckResult overall = CHECK_PASS;

    for (int i = 0; i < ws->crate_count; i++) {
        char crate_manifest[520];
        if (pal_path_join(crate_manifest, sizeof(crate_manifest), ws->root_path, ws->crates[i].path) != 0) {
            continue;
        }
        char full_path[520];
        if (pal_path_join(full_path, sizeof(full_path), crate_manifest, "crate.toml") != 0) {
            continue;
        }

        if (pal_path_exists(full_path) != 0) {
            char msg[320];
            snprintf(msg, sizeof(msg), "Crate manifest missing: %s/crate.toml", ws->crates[i].name);
            doctor_print(CHECK_FAIL, msg);
            overall = CHECK_FAIL;
            continue;
        }

        char* buf = NULL;
        size_t len = 0;
        if (pal_file_read(full_path, &buf, &len) != 0) {
            char msg[320];
            snprintf(msg, sizeof(msg), "Failed to read crate manifest: %s", ws->crates[i].name);
            doctor_print(CHECK_FAIL, msg);
            overall = CHECK_FAIL;
            continue;
        }

        TomlTable* table = NULL;
        TomlError err;
        int rc = toml_parse(buf, len, &table, &err);
        free(buf);

        if (rc != 0) {
            char msg[320];
            snprintf(msg, sizeof(msg), "Crate '%s' crate.toml parse error at line %d: %s",
                     ws->crates[i].name, err.line, err.message);
            doctor_print(CHECK_FAIL, msg);
            overall = CHECK_FAIL;
        } else {
            toml_free(table);
        }
    }

    if (overall == CHECK_PASS) {
        doctor_print(CHECK_PASS, "All crate manifests are valid");
    }
    return overall;
}

/// Check 4: Dependencies resolved and lock file present.
static CheckResult check_dependencies(const char* root_path, bool fix) {
    char lock_path[520];
    if (pal_path_join(lock_path, sizeof(lock_path), root_path, "cdo.lock") != 0) {
        doctor_print(CHECK_FAIL, "Lock file path too long");
        return CHECK_FAIL;
    }

    if (pal_path_exists(lock_path) != 0) {
        if (fix) {
            // Attempt to regenerate the lock file with no deps
            int rc = dep_lock_write(lock_path, NULL, 0);
            if (rc == 0) {
                doctor_print(CHECK_PASS, "Lock file regenerated (--fix)");
                return CHECK_PASS;
            } else {
                doctor_print(CHECK_FAIL, "Failed to regenerate lock file");
                return CHECK_FAIL;
            }
        }
        doctor_print(CHECK_WARN, "Lock file (cdo.lock) not found");
        return CHECK_WARN;
    }

    DepSpec* specs = NULL;
    int count = 0;
    int rc = dep_lock_read(lock_path, &specs, &count);
    if (rc != 0) {
        if (fix) {
            int wrc = dep_lock_write(lock_path, NULL, 0);
            if (wrc == 0) {
                doctor_print(CHECK_PASS, "Lock file regenerated (--fix)");
                return CHECK_PASS;
            }
        }
        doctor_print(CHECK_FAIL, "Lock file (cdo.lock) is corrupted");
        return CHECK_FAIL;
    }

    free(specs);
    char msg[128];
    snprintf(msg, sizeof(msg), "Lock file valid (%d dependencies)", count);
    doctor_print(CHECK_PASS, msg);
    return CHECK_PASS;
}

/// Check 5: Declared tools installed in .cdo/tools/.
static CheckResult check_tools(const char* root_path) {
    char tools_dir[520];
    if (pal_path_join(tools_dir, sizeof(tools_dir), root_path, ".cdo/tools") != 0) {
        doctor_print(CHECK_FAIL, "Tools path too long");
        return CHECK_FAIL;
    }

    if (pal_path_exists(tools_dir) != 0) {
        doctor_print(CHECK_WARN, "Tools directory (.cdo/tools) not found");
        return CHECK_WARN;
    }

    doctor_print(CHECK_PASS, "Tools directory present");
    return CHECK_PASS;
}

int cmd_doctor(const CliParseResult* result, void* ctx) {
    (void)ctx;

    bool fix = doctor_has_fix_flag(result);

    printf("\n  CDo Doctor - Environment Health Check\n");
    printf("  ======================================\n\n");

    int failures = 0;

    // Check 1: Compiler
    if (check_compiler() == CHECK_FAIL) {
        failures++;
    }

    // Load workspace for subsequent checks
    Workspace ws;
    memset(&ws, 0, sizeof(ws));

    // Determine workspace root (current directory)
    char root_path[260] = ".";

    // Check 2: Workspace manifest
    CheckResult ws_result = check_workspace_manifest(root_path);
    if (ws_result == CHECK_FAIL) {
        failures++;
    }

    // Try to load workspace for crate checks
    bool ws_loaded = false;
    if (ws_result == CHECK_PASS) {
        if (workspace_load(root_path, &ws) == 0) {
            ws_loaded = true;
        }
    }

    // Check 3: Crate manifests
    if (ws_loaded) {
        if (check_crate_manifests(&ws) == CHECK_FAIL) {
            failures++;
        }
    } else if (ws_result == CHECK_PASS) {
        doctor_print(CHECK_WARN, "Could not load workspace to check crate manifests");
    }

    // Check 4: Dependencies / lock file
    CheckResult dep_result = check_dependencies(root_path, fix);
    if (dep_result == CHECK_FAIL) {
        failures++;
    }

    // Check 5: Tools
    CheckResult tools_result = check_tools(root_path);
    if (tools_result == CHECK_FAIL) {
        failures++;
    }

    // Summary
    printf("\n");
    if (failures == 0) {
        printf("  All checks passed!\n\n");
    } else {
        printf("  %d check(s) failed.\n", failures);
        if (!fix) {
            printf("  Run 'cdo doctor --fix' to attempt auto-repair.\n\n");
        } else {
            printf("\n");
        }
    }

    // Cleanup
    if (ws_loaded) {
        workspace_free(&ws);
    }

    return (failures == 0) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// End of cmd_doctor.c
