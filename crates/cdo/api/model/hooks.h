#ifndef CDO_MODEL_HOOKS_H
#define CDO_MODEL_HOOKS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration for TOML table (full definition in commons/toml.h)
typedef struct TomlTable TomlTable;

/// Lifecycle points where hooks can execute.
typedef enum {
    HOOK_PRE_BUILD,
    HOOK_POST_BUILD,
    HOOK_PRE_TEST,
    HOOK_POST_TEST,
    HOOK_PRE_E2E,
    HOOK_POST_E2E,
} HookLifecycle;

#define HOOK_LIFECYCLE_COUNT 6

/// A single hook definition.
typedef struct {
    HookLifecycle   lifecycle;
    char            command[1024];   // shell command to execute
    int             timeout_sec;     // 0 = no timeout, default 120
    bool            present;         // true if this hook is defined
} HookDef;

/// Set of hooks for a single scope (workspace or one crate).
typedef struct {
    HookDef hooks[HOOK_LIFECYCLE_COUNT]; // indexed by HookLifecycle
} HookSet;

/// Environment passed to hook processes.
typedef struct {
    const char* ws_root;
    const char* profile;
    const char* build_dir;
    const char* crate_name;      // NULL for workspace hooks
    const char* crate_path;      // NULL for workspace hooks
    const char* crate_build_dir; // NULL for workspace hooks
} HookEnv;

/// Parse hooks from a TOML [hooks] table.
/// Supports both string shorthand and table form:
///   pre-build = "command"
///   pre-build = { command = "cmd", timeout = 300 }
/// Returns 0 on success, non-zero on parse error.
int hooks_parse(const char* toml_content, HookSet* out);

/// Parse hooks from an already-parsed TOML table (the hooks section).
/// This is used by workspace_load which already has the parsed tree.
/// Returns 0 on success, non-zero on parse error.
int hooks_parse_table(const TomlTable* hooks_table, HookSet* out);

/// Return a human-readable name for a lifecycle point (e.g., "pre-build").
const char* hook_lifecycle_name(HookLifecycle lifecycle);

#ifdef __cplusplus
}
#endif

#endif // CDO_MODEL_HOOKS_H
