#include "core/cli.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    const char* name;
    CdoCommand  cmd;
} CommandEntry;

static const CommandEntry command_table[] = {
    { "build",   CDO_CMD_BUILD   },
    { "run",     CDO_CMD_RUN     },
    { "test",    CDO_CMD_TEST    },
    { "clean",   CDO_CMD_CLEAN   },
    { "new",     CDO_CMD_NEW     },
    { "init",    CDO_CMD_INIT    },
    { "shader",  CDO_CMD_SHADER  },
    { "tool",    CDO_CMD_TOOL    },
    { "doctor",  CDO_CMD_DOCTOR  },
    { "deps",    CDO_CMD_DEPS    },
    { "catalog", CDO_CMD_CATALOG },
    { "cache",   CDO_CMD_CACHE   },
    { "hook",    CDO_CMD_HOOK    },
    { "fmt",     CDO_CMD_FMT     },
    { "help",    CDO_CMD_HELP    },
};

#define COMMAND_TABLE_SIZE (sizeof(command_table) / sizeof(command_table[0]))

static CdoCommand match_command(const char* arg) {
    for (int i = 0; i < (int)COMMAND_TABLE_SIZE; i++) {
        if (strcmp(arg, command_table[i].name) == 0) {
            return command_table[i].cmd;
        }
    }
    return CDO_CMD_UNKNOWN;
}

static bool is_option(const char* arg) {
    return arg[0] == '-';
}

static CdoLogLevel parse_log_level(const char* str) {
    if (strcmp(str, "error") == 0 || strcmp(str, "ERROR") == 0) return CDO_LOG_ERROR;
    if (strcmp(str, "warn") == 0  || strcmp(str, "WARN") == 0)  return CDO_LOG_WARN;
    if (strcmp(str, "info") == 0  || strcmp(str, "INFO") == 0)  return CDO_LOG_INFO;
    if (strcmp(str, "debug") == 0 || strcmp(str, "DEBUG") == 0) return CDO_LOG_DEBUG;
    if (strcmp(str, "trace") == 0 || strcmp(str, "TRACE") == 0) return CDO_LOG_TRACE;
    return CDO_LOG_INFO;
}

static CdoColorMode parse_color_mode(const char* str) {
    if (strcmp(str, "auto") == 0)   return CDO_COLOR_AUTO;
    if (strcmp(str, "always") == 0) return CDO_COLOR_ALWAYS;
    if (strcmp(str, "never") == 0)  return CDO_COLOR_NEVER;
    return CDO_COLOR_AUTO;
}

static int parse_int(const char* str) {
    char* end = NULL;
    long val = strtol(str, &end, 10);
    if (end == str || val < 0) return 0;
    return (int)val;
}

int cdo_cli_parse(int argc, char** argv, CdoOptions* opts) {
    memset(opts, 0, sizeof(*opts));
    opts->color        = CDO_COLOR_AUTO;
    opts->log_level    = CDO_LOG_INFO;
    opts->command      = CDO_CMD_HELP;
    opts->lock_timeout = -1;

    if (argc < 2) return 0;

    #define MAX_POSITIONAL 64
    #define MAX_REST       64
    static const char* positional_buf[MAX_POSITIONAL];
    static const char* rest_buf[MAX_REST];
    int positional_count = 0;
    int rest_count = 0;
    bool command_found = false;
    bool rest_mode = false;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (rest_mode) {
            if (rest_count < MAX_REST) {
                rest_buf[rest_count++] = arg;
            }
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            rest_mode = true;
            continue;
        }
        if (is_option(arg)) {
            if (strcmp(arg, "--verbose") == 0 || strcmp(arg, "-v") == 0) {
                opts->verbose = true;
                opts->log_level = CDO_LOG_DEBUG;
                continue;
            }
            if (strcmp(arg, "--quiet") == 0 || strcmp(arg, "-q") == 0) {
                opts->quiet = true;
                opts->log_level = CDO_LOG_ERROR;
                continue;
            }
            if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
                opts->help = true;
                continue;
            }
            if (strcmp(arg, "--release") == 0 || strcmp(arg, "-r") == 0) {
                opts->release = true;
                continue;
            }
            if (strncmp(arg, "--log-level=", 12) == 0) {
                opts->log_level = parse_log_level(arg + 12);
                continue;
            }
            if (strcmp(arg, "--log-level") == 0) {
                if (i + 1 < argc) {
                    opts->log_level = parse_log_level(argv[++i]);
                }
                continue;
            }
            if (strncmp(arg, "--color=", 8) == 0) {
                opts->color = parse_color_mode(arg + 8);
                continue;
            }
            if (strcmp(arg, "--color") == 0) {
                if (i + 1 < argc) {
                    opts->color = parse_color_mode(argv[++i]);
                }
                continue;
            }
            if (strncmp(arg, "--profile=", 10) == 0) {
                opts->profile = arg + 10;
                continue;
            }
            if (strcmp(arg, "--profile") == 0) {
                if (i + 1 < argc) {
                    opts->profile = argv[++i];
                }
                continue;
            }
            if (strncmp(arg, "--jobs=", 7) == 0) {
                opts->jobs = parse_int(arg + 7);
                continue;
            }
            if (strcmp(arg, "--jobs") == 0) {
                if (i + 1 < argc) {
                    opts->jobs = parse_int(argv[++i]);
                }
                continue;
            }
            if (strncmp(arg, "-j", 2) == 0) {
                if (arg[2] != '\0') {
                    opts->jobs = parse_int(arg + 2);
                } else {
                    if (i + 1 < argc) {
                        opts->jobs = parse_int(argv[++i]);
                    }
                }
                continue;
            }
            if (strcmp(arg, "--coverage") == 0) {
                opts->coverage = true;
                continue;
            }
            if (strcmp(arg, "--list") == 0) {
                opts->list = true;
                continue;
            }
            if (strncmp(arg, "--filter=", 9) == 0) {
                opts->filter = arg + 9;
                continue;
            }
            if (strcmp(arg, "--filter") == 0) {
                if (i + 1 < argc) {
                    opts->filter = argv[++i];
                }
                continue;
            }
            if (strcmp(arg, "--dev") == 0) {
                opts->dev = true;
                continue;
            }
            if (strncmp(arg, "--version=", 10) == 0) {
                opts->version_constraint = arg + 10;
                continue;
            }
            if (strcmp(arg, "--version") == 0) {
                if (i + 1 < argc) {
                    opts->version_constraint = argv[++i];
                }
                continue;
            }
            if (strcmp(arg, "--tools") == 0) {
                opts->filter_tools = true;
                continue;
            }
            if (strcmp(arg, "--packages") == 0) {
                opts->filter_packages = true;
                continue;
            }
            if (strcmp(arg, "--venv") == 0) {
                opts->venv = true;
                continue;
            }
            if (strcmp(arg, "--no-cache") == 0) {
                opts->no_cache = true;
                continue;
            }
            if (strcmp(arg, "--cache") == 0) {
                opts->cache = true;
                continue;
            }
            if (strcmp(arg, "--check") == 0) {
                opts->check = true;
                continue;
            }
            if (strncmp(arg, "--lock-timeout=", 15) == 0) {
                opts->lock_timeout = parse_int(arg + 15);
                continue;
            }
            if (strcmp(arg, "--lock-timeout") == 0) {
                if (i + 1 < argc) {
                    opts->lock_timeout = parse_int(argv[++i]);
                }
                continue;
            }
            // Unrecognized option — after command is found, pass to command handler
            // via positional args; before command, skip silently
            if (command_found) {
                if (positional_count < MAX_POSITIONAL) {
                    positional_buf[positional_count++] = arg;
                }
            }
            continue;
        }

        // Non-option argument
        if (!command_found) {
            // First non-option arg is the command
            CdoCommand cmd = match_command(arg);
            opts->command = cmd;
            command_found = true;
        } else {
            // Subsequent non-option args are positional
            if (positional_count < MAX_POSITIONAL) {
                positional_buf[positional_count++] = arg;
            }
        }
    }

    opts->positional_count = positional_count;
    opts->positional_args  = positional_count > 0 ? positional_buf : NULL;
    opts->argc_rest        = rest_count;
    opts->argv_rest        = rest_count > 0 ? rest_buf : NULL;
    if (opts->quiet) {
        opts->log_level = CDO_LOG_ERROR;
    }

    return 0;

    #undef MAX_POSITIONAL
    #undef MAX_REST
}

void cdo_cli_print_help(CdoCommand cmd, FILE* out) {
    switch (cmd) {
    case CDO_CMD_HELP:
    case CDO_CMD_UNKNOWN:
        fprintf(out,
            "CDo - C/C++ Development Ops\n\n"
            "Usage: cdo [OPTIONS] <COMMAND> [ARGS...]\n\n"
            "Commands:\n"
            "  build     Build crates in the workspace\n"
            "  run       Build and run an executable\n"
            "  test      Build and run test crates\n"
            "  clean     Remove build artifacts\n"
            "  new       Create a new project from template\n"
            "  init      Initialize a project in current directory\n"
            "  deps      Manage dependencies (add, remove, list)\n"
            "  catalog   Browse and search the package/tool catalog\n"
            "  cache     Manage the build cache (stats, clear)\n"
            "  tool      Manage vendored tools\n"
            "  fmt       Format source files\n"
            "  doctor    Diagnose environment issues\n\n"
            "Options:\n"
            "  -h, --help             Print help information\n"
            "  -v, --verbose          Enable verbose output\n"
            "  -q, --quiet            Suppress all output except errors\n"
            "      --log-level LEVEL  Set log level (error|warn|info|debug|trace)\n"
            "      --color MODE       Set color output (auto|always|never)\n"
            "  -j, --jobs N           Number of parallel jobs\n\n"
            "Run 'cdo <COMMAND> --help' for more information on a specific command.\n"
        );
        break;
    case CDO_CMD_BUILD:
        fprintf(out,
            "Build crates in the workspace\n\n"
            "Usage: cdo build [OPTIONS] [CRATE...]\n\n"
            "Arguments:\n"
            "  [CRATE...]  Crates to build (default: all)\n\n"
            "Options:\n"
            "  -r, --release          Build with optimizations\n"
            "      --profile NAME     Use a named build profile\n"
            "  -j, --jobs N           Number of parallel jobs\n"
            "  -v, --verbose          Enable verbose output\n"
            "  -h, --help             Print help information\n\n"
            "Examples:\n"
            "  cdo build              Build all crates\n"
            "  cdo build my-app       Build a specific crate\n"
            "  cdo build --release    Build with optimizations\n"
        );
        break;
    case CDO_CMD_RUN:
        fprintf(out,
            "Build and run an executable\n\n"
            "Usage: cdo run [OPTIONS] [CRATE] [-- ARGS...]\n\n"
            "Arguments:\n"
            "  [CRATE]    Executable crate to run (default: auto-detect)\n"
            "  [ARGS...]  Arguments forwarded to the program (after --)\n\n"
            "Options:\n"
            "  -r, --release          Build with optimizations\n"
            "      --profile NAME     Use a named build profile\n"
            "  -v, --verbose          Enable verbose output\n"
            "  -h, --help             Print help information\n\n"
            "Examples:\n"
            "  cdo run                Build and run the default executable\n"
            "  cdo run my-app         Build and run a specific crate\n"
            "  cdo run -- --port 8080 Forward arguments to the program\n"
        );
        break;
    case CDO_CMD_TEST:
        fprintf(out,
            "Build and run test crates\n\n"
            "Usage: cdo test [OPTIONS] [CRATE...]\n\n"
            "Arguments:\n"
            "  [CRATE...]  Test crates to run (default: all)\n\n"
            "Options:\n"
            "      --filter PATTERN   Run only tests matching PATTERN (substring or glob)\n"
            "      --list             List available tests without running them\n"
            "      --coverage         Build with coverage instrumentation and report\n"
            "  -j, --jobs N           Number of parallel test jobs\n"
            "  -v, --verbose          Enable verbose output\n"
            "  -h, --help             Print help information\n\n"
            "Examples:\n"
            "  cdo test               Run all tests\n"
            "  cdo test my-tests      Run a specific test crate\n"
            "  cdo test --filter parse  Run tests with 'parse' in the name\n"
            "  cdo test --list          List all test names\n"
            "  cdo test --coverage      Run with coverage reporting\n"
            "  cdo test --jobs 4        Run tests with 4 parallel jobs\n"
        );
        break;
    case CDO_CMD_CLEAN:
        fprintf(out,
            "Remove build artifacts\n\n"
            "Usage: cdo clean [OPTIONS] [CRATE]\n\n"
            "Arguments:\n"
            "  [CRATE]  Clean only a specific crate (default: all)\n\n"
            "Options:\n"
            "      --cache            Also clear the build cache\n"
            "  -v, --verbose          Enable verbose output\n"
            "  -h, --help             Print help information\n\n"
            "Examples:\n"
            "  cdo clean              Remove all build artifacts\n"
            "  cdo clean my-lib       Clean a specific crate\n"
            "  cdo clean --cache      Remove build artifacts and cache\n"
        );
        break;
    case CDO_CMD_NEW:
        fprintf(out,
            "Create a new project from template\n\n"
            "Usage: cdo new [OPTIONS] <NAME> [TEMPLATE]\n\n"
            "Arguments:\n"
            "  <NAME>      Project name\n"
            "  [TEMPLATE]  Template to use (default: executable)\n\n"
            "Options:\n"
            "      --list             List available templates\n"
            "      --force            Overwrite existing directory\n"
            "  -h, --help             Print help information\n\n"
            "Examples:\n"
            "  cdo new my-app         Create an executable project\n"
            "  cdo new my-lib lib     Create a library project\n"
            "  cdo new --list         List available templates\n"
        );
        break;
    case CDO_CMD_INIT:
        fprintf(out,
            "Initialize a project in current directory\n\n"
            "Usage: cdo init [OPTIONS] [TEMPLATE]\n\n"
            "Arguments:\n"
            "  [TEMPLATE]  Template to use (optional)\n\n"
            "Options:\n"
            "      --venv             Create a virtual environment (.cdo/ with local binary)\n"
            "      --force            Overwrite existing files\n"
            "  -h, --help             Print help information\n\n"
            "Examples:\n"
            "  cdo init hello         Initialize from 'hello' template\n"
            "  cdo init --venv        Create virtual environment only\n"
            "  cdo init hello --venv  Template + virtual environment\n"
        );
        break;
    case CDO_CMD_SHADER:
        // Deprecated: shader command removed, handled by deprecation message in dispatch
        break;
    case CDO_CMD_TOOL:
        fprintf(out,
            "Manage vendored tools\n\n"
            "Usage: cdo tool <SUBCOMMAND> [OPTIONS]\n\n"
            "Subcommands:\n"
            "  install    Install a tool\n"
            "  list       List installed tools\n"
            "  remove     Remove a tool\n\n"
            "Options:\n"
            "      --refresh          Force re-download\n"
            "  -h, --help             Print help information\n\n"
            "Examples:\n"
            "  cdo tool install dxc   Install DXC shader compiler\n"
            "  cdo tool list          Show installed tools\n"
        );
        break;
    case CDO_CMD_DOCTOR:
        fprintf(out,
            "Diagnose environment issues\n\n"
            "Usage: cdo doctor [OPTIONS]\n\n"
            "Options:\n"
            "      --fix              Attempt to fix detected issues\n"
            "  -v, --verbose          Enable verbose output\n"
            "  -h, --help             Print help information\n\n"
            "Checks performed:\n"
            "  - C/C++ compiler availability\n"
            "  - Dependency resolution status\n"
            "  - Manifest file validity\n"
            "  - Tool installation status\n\n"
            "Examples:\n"
            "  cdo doctor             Run all diagnostics\n"
            "  cdo doctor --fix       Fix detected issues\n"
        );
        break;
    case CDO_CMD_CATALOG:
        fprintf(out,
            "Browse and search the package/tool catalog\n\n"
            "Usage: cdo catalog <SUBCOMMAND> [OPTIONS]\n\n"
            "Subcommands:\n"
            "  list       List available catalog entries\n"
            "  search     Search entries by name or description\n\n"
            "Options:\n"
            "      --tools            Show only tool entries\n"
            "      --packages         Show only package entries\n"
            "  -h, --help             Print help information\n\n"
            "Examples:\n"
            "  cdo catalog list               List all entries\n"
            "  cdo catalog list --tools       List only tools\n"
            "  cdo catalog search sdl         Search for 'sdl'\n"
        );
        break;
    case CDO_CMD_DEPS:
        fprintf(out,
            "Manage project dependencies\n\n"
            "Usage: cdo deps <SUBCOMMAND> [OPTIONS]\n\n"
            "Subcommands:\n"
            "  add        Add a dependency\n"
            "  remove     Remove a dependency\n"
            "  list       List all dependencies\n\n"
            "Options:\n"
            "      --dev              Target dev-dependencies\n"
            "      --version VER      Version constraint\n"
            "  -h, --help             Print help information\n\n"
            "Examples:\n"
            "  cdo deps add sdl3              Add latest sdl3\n"
            "  cdo deps add sdl3 --version ^3.2.0\n"
            "  cdo deps add theft --dev       Add as dev dependency\n"
            "  cdo deps remove sdl3           Remove a dependency\n"
            "  cdo deps list                  List all dependencies\n"
        );
        break;
    case CDO_CMD_CACHE:
        fprintf(out,
            "Manage the build cache\n\n"
            "Usage: cdo cache <SUBCOMMAND>\n\n"
            "Subcommands:\n"
            "  stats      Show cache size, entry count, and hit rate\n"
            "  clear      Remove all cached objects\n\n"
            "Options:\n"
            "  -h, --help             Print help information\n\n"
            "Examples:\n"
            "  cdo cache stats        Show cache statistics\n"
            "  cdo cache clear        Clear all cached objects\n"
        );
        break;
    case CDO_CMD_FMT:
        fprintf(out,
            "Format source files\n\n"
            "Usage: cdo fmt [OPTIONS] [CRATE...]\n\n"
            "Arguments:\n"
            "  [CRATE...]  Crates to format (default: all)\n\n"
            "Options:\n"
            "      --check            Check formatting without modifying files (exit 1 if non-conformant)\n"
            "  -v, --verbose          Enable verbose output (log skipped files)\n"
            "  -q, --quiet            Suppress all output except errors\n"
            "  -h, --help             Print help information\n\n"
            "Examples:\n"
            "  cdo fmt                Format all source files in the workspace\n"
            "  cdo fmt my-lib         Format only the 'my-lib' crate\n"
            "  cdo fmt --check        Verify formatting without changes (CI mode)\n"
            "  cdo fmt --check my-lib Check formatting for a specific crate\n"
        );
        break;
    }
}
