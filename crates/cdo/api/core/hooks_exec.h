#ifndef CDO_CORE_HOOKS_EXEC_H
#define CDO_CORE_HOOKS_EXEC_H

#include "model/hooks.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Execute a single hook. Spawns the command with injected env vars.
/// Returns 0 if hook succeeds (exit code 0), non-zero otherwise.
/// If hook is not present (hook->present == false), returns 0 immediately.
int hook_execute(const HookDef* hook, const HookEnv* env);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_HOOKS_EXEC_H
