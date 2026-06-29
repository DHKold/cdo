/**
 * help.c - Help text formatting and generation.
 *
 * Generates formatted help text for commands including usage synopsis,
 * argument descriptions, and subcommand listings. When a CliTermInfo with
 * color support is provided, section headers are rendered bold and
 * command/subcommand names are rendered in cyan using ANSI escape codes.
 */
#include "cmd_internal.h"
#include "../../api/cli_errors.h"

#include <stdio.h>
#include <string.h>

/* Column at which descriptions are aligned */
#define DESC_COLUMN 20

/* ANSI escape code helpers */
#define ANSI_BOLD_ON  "\033[1m"
#define ANSI_CYAN_ON  "\033[36m"
#define ANSI_RESET    "\033[0m"

/**
 * Returns true if the given term info indicates color output should be used.
 */
static bool use_color(const CliTermInfo* term) {
    return term != NULL && term->color_level != CLI_COLOR_NONE;
}

/**
 * Append a section header, optionally wrapped in bold ANSI codes.
 * Returns bytes written, or -1 if buffer is exhausted.
 */
static int append_header(const char* header, bool colored, char* buf, int remaining) {
    if (remaining <= 0) return -1;
    int written;
    if (colored) {
        written = snprintf(buf, (size_t)remaining, ANSI_BOLD_ON "%s" ANSI_RESET, header);
    } else {
        written = snprintf(buf, (size_t)remaining, "%s", header);
    }
    if (written < 0 || written >= remaining) return -1;
    return written;
}

/**
 * Append a formatted line with left-aligned name padded to DESC_COLUMN, then description.
 * When colored is true, the name portion is wrapped in cyan ANSI codes.
 * Returns number of bytes written, or -1 if buffer is exhausted.
 */
static int append_padded_line(const char* name, const char* description, bool colored, char* buf, int remaining) {
    if (remaining <= 0) return -1;

    int name_len = (int)strlen(name);
    int written;

    if (colored) {
        if (name_len >= DESC_COLUMN - 2) {
            written = snprintf(buf, (size_t)remaining, "  " ANSI_CYAN_ON "%s" ANSI_RESET " %s\n", name, description ? description : "");
        } else {
            int pad = DESC_COLUMN - 2 - name_len;
            written = snprintf(buf, (size_t)remaining, "  " ANSI_CYAN_ON "%s" ANSI_RESET "%*s%s\n", name, pad, "", description ? description : "");
        }
    } else {
        if (name_len >= DESC_COLUMN - 2) {
            written = snprintf(buf, (size_t)remaining, "  %s %s\n", name, description ? description : "");
        } else {
            int pad = DESC_COLUMN - 2 - name_len;
            written = snprintf(buf, (size_t)remaining, "  %s%*s%s\n", name, pad, "", description ? description : "");
        }
    }

    if (written < 0 || written >= remaining) return -1;
    return written;
}

/**
 * Generate top-level help: list all registered root commands with descriptions.
 */
static int generate_top_level_help(const CliCmdRegistry* reg, bool colored, char* buf, int buf_size) {
    int pos = 0;
    int remaining = buf_size;

    /* Header */
    int n = append_header("Available commands:\n\n", colored, buf + pos, remaining);
    if (n < 0) return -1;
    pos += n;
    remaining -= n;

    /* List each root command */
    for (int i = 0; i < reg->command_count; i++) {
        const CliCmdSpec* spec = &reg->root_commands[i].spec;
        int wrote = append_padded_line(spec->name, spec->description, colored, buf + pos, remaining);
        if (wrote < 0) return -1;
        pos += wrote;
        remaining -= wrote;
    }

    return pos;
}

/**
 * Generate command-specific help: usage synopsis, arguments, and subcommands.
 */
static int generate_command_help(const CliCmdRegistry* reg, const CliCmdSpec* cmd, bool colored, char* buf, int buf_size) {
    int pos = 0;
    int remaining = buf_size;
    int n;

    /* Usage header (bold when colored) */
    if (colored) {
        if (cmd->arg_count > 0 || cmd->positional_count > 0) {
            n = snprintf(buf + pos, (size_t)remaining, ANSI_BOLD_ON "Usage:" ANSI_RESET " %s [OPTIONS]", cmd->name);
        } else {
            n = snprintf(buf + pos, (size_t)remaining, ANSI_BOLD_ON "Usage:" ANSI_RESET " %s", cmd->name);
        }
    } else {
        if (cmd->arg_count > 0 || cmd->positional_count > 0) {
            n = snprintf(buf + pos, (size_t)remaining, "Usage: %s [OPTIONS]", cmd->name);
        } else {
            n = snprintf(buf + pos, (size_t)remaining, "Usage: %s", cmd->name);
        }
    }
    if (n < 0 || n >= remaining) return -1;
    pos += n;
    remaining -= n;

    /* Append positional names to synopsis */
    for (int i = 0; i < cmd->positional_count; i++) {
        const CliPositionalSpec* p = &cmd->positionals[i];
        const char* fmt;
        if (p->cardinality == CLI_CARD_EXACTLY_ONE) {
            fmt = " <%s>";
        } else if (p->cardinality == CLI_CARD_ZERO_OR_MORE) {
            fmt = " [%s...]";
        } else {
            fmt = " [%s]";
        }
        n = snprintf(buf + pos, (size_t)remaining, fmt, p->name);
        if (n < 0 || n >= remaining) return -1;
        pos += n;
        remaining -= n;
    }

    /* If no positionals but has args, add generic [ARGS...] */
    if (cmd->positional_count == 0 && cmd->arg_count > 0) {
        n = snprintf(buf + pos, (size_t)remaining, " [ARGS...]");
        if (n < 0 || n >= remaining) return -1;
        pos += n;
        remaining -= n;
    }

    n = snprintf(buf + pos, (size_t)remaining, "\n\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n;
    remaining -= n;

    /* Description */
    if (cmd->long_description) {
        n = snprintf(buf + pos, (size_t)remaining, "%s\n\n", cmd->long_description);
    } else if (cmd->description) {
        n = snprintf(buf + pos, (size_t)remaining, "%s\n\n", cmd->description);
    } else {
        n = 0;
    }
    if (n < 0 || n >= remaining) return -1;
    pos += n;
    remaining -= n;

    /* Arguments section */
    if (cmd->arg_count > 0) {
        n = append_header("Options:\n", colored, buf + pos, remaining);
        if (n < 0) return -1;
        pos += n;
        remaining -= n;

        for (int i = 0; i < cmd->arg_count; i++) {
            const CliArgSpec* arg = &cmd->args[i];
            char name_buf[128];

            if (arg->short_name) {
                snprintf(name_buf, sizeof(name_buf), "-%c, --%s", arg->short_name, arg->long_name);
            } else {
                snprintf(name_buf, sizeof(name_buf), "    --%s", arg->long_name);
            }

            int wrote = append_padded_line(name_buf, arg->description, false, buf + pos, remaining);
            if (wrote < 0) return -1;
            pos += wrote;
            remaining -= wrote;
        }

        n = snprintf(buf + pos, (size_t)remaining, "\n");
        if (n < 0 || n >= remaining) return -1;
        pos += n;
        remaining -= n;
    }

    /* Subcommands section: find this command in the registry and list children */
    CmdNode* node = NULL;
    for (int i = 0; i < reg->command_count; i++) {
        if (strcmp(reg->root_commands[i].spec.name, cmd->name) == 0) {
            node = &reg->root_commands[i];
            break;
        }
    }

    if (node != NULL && node->child_count > 0) {
        n = append_header("Subcommands:\n", colored, buf + pos, remaining);
        if (n < 0) return -1;
        pos += n;
        remaining -= n;

        for (int i = 0; i < node->child_count; i++) {
            const CliCmdSpec* child_spec = &node->children[i].spec;
            int wrote = append_padded_line(child_spec->name, child_spec->description, colored, buf + pos, remaining);
            if (wrote < 0) return -1;
            pos += wrote;
            remaining -= wrote;
        }
    }

    return pos;
}

int cli_cmd_help(const CliCmdRegistry* reg, const CliCmdSpec* cmd, const CliTermInfo* term, char* buf, int buf_size) {
    if (reg == NULL || buf == NULL || buf_size < 16) {
        return -1;
    }

    buf[0] = '\0';
    bool colored = use_color(term);

    int result;
    if (cmd == NULL) {
        result = generate_top_level_help(reg, colored, buf, buf_size);
    } else {
        result = generate_command_help(reg, cmd, colored, buf, buf_size);
    }

    return result;
}
