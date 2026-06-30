/**
 * CDo entry point.
 *
 * Linear flow: term detect -> registry -> parse -> dispatch
 * All commands use the cdo_cli framework for parsing, help, and suggestions.
 */

extern "C" {
#include "core/registry_setup.h"
#include "core/log.h"
#include "core/handler_ctx.h"
#include "term/cli_term.h"
#include "cmd/cli_cmd.h"
#include "out/cli_out.h"
#include "core/cli_arg_access.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

/// Resolve log level from parsed CLI result.
/// Precedence: --quiet > --verbose > --log-level > default (INFO).
static CdoLogLevel resolve_log_level(const CliParseResult* result) {
    if (cli_arg_get_bool(result, "quiet")) {
        return CDO_LOG_LEVEL_ERROR;
    }
    if (cli_arg_get_bool(result, "verbose")) {
        return CDO_LOG_LEVEL_DEBUG;
    }
    const char* level_str = cli_arg_get_str(result, "log-level");
    if (level_str) {
        if (std::strcmp(level_str, "error") == 0) return CDO_LOG_LEVEL_ERROR;
        if (std::strcmp(level_str, "warn") == 0)  return CDO_LOG_LEVEL_WARN;
        if (std::strcmp(level_str, "info") == 0)  return CDO_LOG_LEVEL_INFO;
        if (std::strcmp(level_str, "debug") == 0) return CDO_LOG_LEVEL_DEBUG;
        if (std::strcmp(level_str, "trace") == 0) return CDO_LOG_LEVEL_TRACE;
    }
    return CDO_LOG_LEVEL_INFO;
}

int main(int argc, char* argv[]) {
    // 1. Detect terminal capabilities
    CliTermInfo term;
    cli_term_detect(&term);

    // 2. Create CLI registry with all commands
    CliCmdRegistry* reg = cdo_registry_create();
    if (!reg) {
        std::fprintf(stderr, "fatal: failed to initialize CLI registry\n");
        return 1;
    }

    // 3. Parse command line
    CliArgValue arg_buf[64];
    std::memset(arg_buf, 0, sizeof(arg_buf));
    CliParseResult result;
    std::memset(&result, 0, sizeof(result));

    int parse_rc = cli_cmd_parse(reg, argc, (const char**)argv, arg_buf, 64, &result);

    // 4. Handle parse errors
    if (parse_rc != 0) {
        // Show error message
        if (result.error_msg[0] != '\0') {
            std::fprintf(stderr, "error: %s\n", result.error_msg);
        }
        // Suggest alternatives for unknown commands
        if (result.error_token) {
            char suggestions[4][32];
            int n = cli_cmd_suggest(reg, result.error_token, suggestions, 4);
            if (n > 0) {
                std::fprintf(stderr, "\nDid you mean:\n");
                for (int i = 0; i < n; i++) {
                    std::fprintf(stderr, "  %s\n", suggestions[i]);
                }
            }
        }
        cli_cmd_registry_destroy(reg);
        return 1;
    }

    // 5. Handle --help flag
    if (cli_arg_get_bool(&result, "help")) {
        char help_buf[4096];
        cli_cmd_help(reg, result.matched_cmd, &term, help_buf, sizeof(help_buf));
        std::printf("%s", help_buf);
        cli_cmd_registry_destroy(reg);
        return 0;
    }

    // 6. Initialize output subsystem
    CliOutCtx* out = cli_out_init(&term);

    // 7. Resolve and set log level
    CdoLogLevel level = resolve_log_level(&result);
    cdo_log_init(out, level);

    // 8. Dispatch to command handler
    CdoHandlerCtx handler_ctx = { .out = out };
    int exit_code = 0;

    if (result.matched_cmd && result.matched_cmd->handler) {
        exit_code = result.matched_cmd->handler(&result, &handler_ctx);
    } else {
        // No command matched or command has no handler — show top-level help
        char help_buf[4096];
        cli_cmd_help(reg, nullptr, &term, help_buf, sizeof(help_buf));
        std::printf("%s", help_buf);
    }

    // 9. Cleanup
    cli_out_destroy(out);
    cli_cmd_registry_destroy(reg);
    return exit_code;
}
