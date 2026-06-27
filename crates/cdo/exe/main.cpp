/**
 * CDo entry point.
 *
 * Parses CLI arguments, initializes the output subsystem, and dispatches
 * to the appropriate command handler. Configuration file loading is deferred
 * until a command handler explicitly requests it (lazy loading).
 */

extern "C" {
#include "core/cli.h"
#include "core/output.h"
#include "pal/pal.h"
#include "commands/cmd_build.h"
#include "commands/cmd_run.h"
#include "commands/cmd_test.h"
#include "commands/cmd_clean.h"
#include "commands/cmd_new.h"
#include "commands/cmd_deps.h"
#include "commands/cmd_tool.h"
#include "commands/cmd_doctor.h"
#include "commands/cmd_shader.h"
#include "commands/cmd_catalog.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void print_usage(FILE* out) {
    std::fprintf(out,
        "cdo - C/C++ build system and project manager\n"
        "\n"
        "Usage: cdo [OPTIONS] <COMMAND> [ARGS...]\n"
        "\n"
        "Commands:\n"
        "  build     Build one or more crates\n"
        "  run       Build and run an executable crate\n"
        "  test      Build and run tests\n"
        "  clean     Remove build artifacts\n"
        "  new       Create a new project from a template\n"
        "  init      Initialize a project in the current directory\n"
        "  deps      Manage dependencies (add, remove, list)\n"
        "  catalog   Browse and search the package/tool catalog\n"
        "  tool      Manage local tool installations\n"
        "  doctor    Check environment health\n"
        "  shader    Compile HLSL shaders\n"
        "\n"
        "Options:\n"
        "  --help          Show help for a command\n"
        "  --color <mode>  Color output: auto, always, never\n"
        "  --verbose       Enable verbose output\n"
        "  --quiet         Suppress non-error output\n"
    );
}

/// Find the first non-option argument in argv (the command token).
/// Returns NULL if no non-option argument exists.
static const char* find_command_token(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            return argv[i];
        }
        // Skip option values for known options that take a parameter
        if (std::strcmp(argv[i], "--color") == 0 ||
            std::strcmp(argv[i], "--log-level") == 0 ||
            std::strcmp(argv[i], "--profile") == 0 ||
            std::strcmp(argv[i], "--jobs") == 0 ||
            std::strcmp(argv[i], "--version") == 0) {
            i++; // skip next arg (the value)
        }
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    CdoOptions opts = {};

    // Parse CLI arguments
    int parse_result = cdo_cli_parse(argc, argv, &opts);
    if (parse_result != 0) {
        // Parse failed — attempt to show suggestions for unknown command
        const char* token = find_command_token(argc, argv);
        if (token) {
            char suggestions[4][32];
            int n = cdo_cli_suggest(token, suggestions, 4);
            if (n > 0) {
                std::fprintf(stderr, "error: unknown command '%s'\n\nDid you mean:\n", token);
                for (int i = 0; i < n; i++) {
                    std::fprintf(stderr, "  %s\n", suggestions[i]);
                }
            } else {
                std::fprintf(stderr, "error: failed to parse arguments\n");
            }
        } else {
            std::fprintf(stderr, "error: failed to parse arguments\n");
        }
        return 1;
    }

    // Initialize output subsystem with parsed settings
    output_init(opts.color, opts.log_level, pal_is_tty(1));

    // Handle --help flag
    if (opts.help) {
        cdo_cli_print_help(opts.command, stdout);
        return 0;
    }

    // Dispatch to command handler
    switch (opts.command) {
        case CDO_CMD_BUILD:
            return cmd_build(&opts);
        case CDO_CMD_RUN:
            return cmd_run(&opts);
        case CDO_CMD_TEST:
            return cmd_test(&opts);
        case CDO_CMD_CLEAN:
            return cmd_clean(&opts);
        case CDO_CMD_NEW:
            return cmd_new(&opts);
        case CDO_CMD_INIT:
            return cmd_init(&opts);
        case CDO_CMD_TOOL:
            return cmd_tool(&opts);
        case CDO_CMD_DOCTOR:
            return cmd_doctor(&opts);
        case CDO_CMD_SHADER:
            return cmd_shader(&opts);
        case CDO_CMD_CATALOG:
            return cmd_catalog(&opts);
        case CDO_CMD_DEPS:
            return cmd_deps(&opts);
        case CDO_CMD_HELP:
            cdo_cli_print_help(CDO_CMD_HELP, stdout);
            return 0;
        case CDO_CMD_UNKNOWN: {
            // The CLI parser consumed the unknown command token but we still
            // have access to argv to find it for error messages.
            const char* token = find_command_token(argc, argv);
            if (token) {
                char suggestions[4][32];
                int n = cdo_cli_suggest(token, suggestions, 4);
                std::fprintf(stderr, "error: unknown command '%s'\n", token);
                if (n > 0) {
                    std::fprintf(stderr, "\nDid you mean:\n");
                    for (int i = 0; i < n; i++) {
                        std::fprintf(stderr, "  %s\n", suggestions[i]);
                    }
                }
                std::fprintf(stderr, "\nAvailable commands: build, run, test, clean, new, init, "
                    "deps, tool, catalog, doctor, shader\n");
                std::fprintf(stderr, "Run 'cdo --help' for full usage information.\n");
            } else {
                print_usage(stdout);
                return 0;
            }
            return 1;
        }
        default:
            print_usage(stdout);
            return 0;
    }
}
