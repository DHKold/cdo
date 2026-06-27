#ifndef CDO_CORE_CLI_H
#define CDO_CORE_CLI_H

#include <stdbool.h>
#include <stdio.h>
#include "output.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Command Enum ---
typedef enum {
    CDO_CMD_BUILD,
    CDO_CMD_RUN,
    CDO_CMD_TEST,
    CDO_CMD_CLEAN,
    CDO_CMD_NEW,
    CDO_CMD_INIT,
    CDO_CMD_SOURCE,
    CDO_CMD_SHADER,
    CDO_CMD_TOOL,
    CDO_CMD_DOCTOR,
    CDO_CMD_SELF,
    CDO_CMD_CATALOG,
    CDO_CMD_DEPS,
    CDO_CMD_HELP,
    CDO_CMD_UNKNOWN,
} CdoCommand;

// --- Parsed Options ---
typedef struct {
    CdoCommand      command;
    CdoColorMode    color;
    CdoLogLevel     log_level;
    bool            verbose;
    bool            quiet;
    bool            help;
    bool            release;
    bool            dev;                // --dev flag (deps add/remove scope)
    bool            coverage;           // --coverage flag
    bool            list;               // --list flag (test: list test names)
    bool            filter_tools;       // --tools flag (catalog list filter)
    bool            filter_packages;    // --packages flag (catalog list filter)
    const char*     profile;
    const char*     filter;             // --filter <pattern> (test: filter pattern)
    const char*     version_constraint; // --version <constraint>
    int             jobs;           // 0 = auto-detect
    int             argc_rest;      // args after --
    const char**    argv_rest;
    int             positional_count;
    const char**    positional_args; // crate names, template name, etc.
} CdoOptions;

/// Parse argv into a CdoOptions struct.
/// All pointers in opts point into the original argv strings (no heap allocation).
/// Returns 0 on success, non-zero on error.
int cdo_cli_parse(int argc, char** argv, CdoOptions* opts);

/// Print usage for a given command (or general help if CDO_CMD_HELP).
/// Stub — full implementation in a later task.
void cdo_cli_print_help(CdoCommand cmd, FILE* out);

/// Suggest similar commands for a typo. Returns number of suggestions written.
/// Stub — full implementation in a later task.
int cdo_cli_suggest(const char* input, char suggestions[][32], int max_suggestions);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_CLI_H
