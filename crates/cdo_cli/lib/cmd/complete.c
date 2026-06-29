/**
 * complete.c - Shell completion script and candidate generation.
 *
 * Generates completion scripts for bash, zsh, and PowerShell.
 * Also provides runtime candidate generation for partial tokens,
 * supporting command prefix matching, option prefix matching,
 * enum value listing, and subcommand traversal.
 */
#include "cmd_internal.h"
#include "../../api/cli_errors.h"

#include <stdio.h>
#include <string.h>

/* --- Completion Script Generation --- */

static int generate_bash_script(const CliCmdRegistry* reg, const char* program_name, char* buf, int buf_size) {
    int pos = 0;
    int remaining = buf_size;
    int n;

    /* Function header */
    n = snprintf(buf + pos, (size_t)remaining, "_%s_completions() {\n", program_name);
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "    local cur prev commands\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "    prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    /* Build command list */
    n = snprintf(buf + pos, (size_t)remaining, "    commands=\"");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    for (int i = 0; i < reg->command_count; i++) {
        const char* name = reg->root_commands[i].spec.name;
        if (i > 0) {
            n = snprintf(buf + pos, (size_t)remaining, " %s", name);
        } else {
            n = snprintf(buf + pos, (size_t)remaining, "%s", name);
        }
        if (n < 0 || n >= remaining) return -1;
        pos += n; remaining -= n;
    }

    n = snprintf(buf + pos, (size_t)remaining, "\"\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "    COMPREPLY=($(compgen -W \"${commands}\" -- \"${cur}\"))\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "}\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "complete -F _%s_completions %s\n", program_name, program_name);
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    return pos;
}

static int generate_zsh_script(const CliCmdRegistry* reg, const char* program_name, char* buf, int buf_size) {
    int pos = 0;
    int remaining = buf_size;
    int n;

    n = snprintf(buf + pos, (size_t)remaining, "#compdef %s\n\n", program_name);
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "_%s() {\n", program_name);
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "    local commands\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "    commands=(\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    for (int i = 0; i < reg->command_count; i++) {
        const CliCmdSpec* spec = &reg->root_commands[i].spec;
        n = snprintf(buf + pos, (size_t)remaining, "        '%s:%s'\n", spec->name, spec->description ? spec->description : "");
        if (n < 0 || n >= remaining) return -1;
        pos += n; remaining -= n;
    }

    n = snprintf(buf + pos, (size_t)remaining, "    )\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "    compadd -a commands\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "}\n\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "compdef _%s %s\n", program_name, program_name);
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    return pos;
}

static int generate_powershell_script(const CliCmdRegistry* reg, const char* program_name, char* buf, int buf_size) {
    int pos = 0;
    int remaining = buf_size;
    int n;

    n = snprintf(buf + pos, (size_t)remaining, "Register-ArgumentCompleter -Native -CommandName %s -ScriptBlock {\n", program_name);
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "    param($wordToComplete, $commandAst, $cursorPosition)\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "    $commands = @(");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    for (int i = 0; i < reg->command_count; i++) {
        const char* name = reg->root_commands[i].spec.name;
        if (i > 0) {
            n = snprintf(buf + pos, (size_t)remaining, ", '%s'", name);
        } else {
            n = snprintf(buf + pos, (size_t)remaining, "'%s'", name);
        }
        if (n < 0 || n >= remaining) return -1;
        pos += n; remaining -= n;
    }

    n = snprintf(buf + pos, (size_t)remaining, ")\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "    $commands | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "        [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "    }\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    n = snprintf(buf + pos, (size_t)remaining, "}\n");
    if (n < 0 || n >= remaining) return -1;
    pos += n; remaining -= n;

    return pos;
}

int cli_cmd_completion_script(const CliCmdRegistry* reg, const char* program_name, CliShellType shell, char* buf, int buf_size) {
    if (reg == NULL || program_name == NULL || buf == NULL || buf_size < 16) {
        return -1;
    }

    buf[0] = '\0';

    switch (shell) {
        case CLI_SHELL_BASH:
            return generate_bash_script(reg, program_name, buf, buf_size);
        case CLI_SHELL_ZSH:
            return generate_zsh_script(reg, program_name, buf, buf_size);
        case CLI_SHELL_POWERSHELL:
            return generate_powershell_script(reg, program_name, buf, buf_size);
        default:
            return -1;
    }
}

/* --- Runtime Completion --- */

/**
 * Find a root CmdNode by exact name match.
 */
static CmdNode* find_root_node(const CliCmdRegistry* reg, const char* name) {
    for (int i = 0; i < reg->command_count; i++) {
        if (strcmp(reg->root_commands[i].spec.name, name) == 0) {
            return &reg->root_commands[i];
        }
    }
    return NULL;
}

/**
 * Find a child CmdNode by exact name match.
 */
static CmdNode* find_child_node(CmdNode* parent, const char* name) {
    for (int i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i].spec.name, name) == 0) {
            return &parent->children[i];
        }
    }
    return NULL;
}

/**
 * Append a candidate to the buffer as a newline-separated entry.
 * Returns bytes written or -1 if buffer full.
 */
static int append_candidate(const char* candidate, char* buf, int pos, int buf_size, bool first) {
    int remaining = buf_size - pos;
    int n;
    if (first) {
        n = snprintf(buf + pos, (size_t)remaining, "%s", candidate);
    } else {
        n = snprintf(buf + pos, (size_t)remaining, "\n%s", candidate);
    }
    if (n < 0 || n >= remaining) return -1;
    return n;
}

int cli_cmd_complete(const CliCmdRegistry* reg, int argc, const char** argv, int cursor_pos, char* buf, int buf_size) {
    if (reg == NULL || buf == NULL || buf_size < 2) {
        return -1;
    }

    buf[0] = '\0';

    if (argc <= 0 || argv == NULL) {
        return 0;
    }

    int pos = 0;
    int count = 0;

    /* Determine the token being completed */
    const char* prefix = "";
    if (cursor_pos >= 0 && cursor_pos < argc) {
        prefix = argv[cursor_pos];
    }
    int prefix_len = (int)strlen(prefix);

    /* Case 1: Completing the first token (command name) */
    if (cursor_pos == 0) {
        for (int i = 0; i < reg->command_count; i++) {
            const char* cmd_name = reg->root_commands[i].spec.name;
            if (prefix_len == 0 || strncmp(cmd_name, prefix, (size_t)prefix_len) == 0) {
                int wrote = append_candidate(cmd_name, buf, pos, buf_size, count == 0);
                if (wrote < 0) return -1;
                pos += wrote;
                count++;
            }
        }
        return count;
    }

    /* Case 2+: We have at least a command token. Resolve the command path. */
    CmdNode* current = find_root_node(reg, argv[0]);
    if (current == NULL) {
        /* Command not found, nothing to complete */
        return 0;
    }

    /* Walk subcommands for intermediate tokens (between argv[1] and cursor_pos-1) */
    int token_idx = 1;
    while (token_idx < cursor_pos) {
        CmdNode* child = find_child_node(current, argv[token_idx]);
        if (child == NULL) break;
        current = child;
        token_idx++;
    }

    /* Check if completing after an option that expects a value (enum completion) */
    if (cursor_pos >= 2) {
        const char* prev_token = argv[cursor_pos - 1];
        if (prev_token[0] == '-' && prev_token[1] == '-') {
            /* Previous token is a --option, check if it's an enum arg */
            const char* opt_name = prev_token + 2;
            const CliCmdSpec* spec = &current->spec;
            for (int i = 0; i < spec->arg_count; i++) {
                if (strcmp(spec->args[i].long_name, opt_name) == 0 && spec->args[i].type == CLI_ARG_ENUM && spec->args[i].enum_values != NULL) {
                    /* List enum values filtered by prefix */
                    const char** vals = spec->args[i].enum_values;
                    for (int v = 0; vals[v] != NULL; v++) {
                        if (prefix_len == 0 || strncmp(vals[v], prefix, (size_t)prefix_len) == 0) {
                            int wrote = append_candidate(vals[v], buf, pos, buf_size, count == 0);
                            if (wrote < 0) return -1;
                            pos += wrote;
                            count++;
                        }
                    }
                    return count;
                }
            }
        }
    }

    /* If prefix starts with "--", complete options */
    if (prefix_len >= 2 && prefix[0] == '-' && prefix[1] == '-') {
        const char* opt_prefix = prefix + 2;
        int opt_prefix_len = prefix_len - 2;
        const CliCmdSpec* spec = &current->spec;

        for (int i = 0; i < spec->arg_count; i++) {
            const char* long_name = spec->args[i].long_name;
            if (opt_prefix_len == 0 || strncmp(long_name, opt_prefix, (size_t)opt_prefix_len) == 0) {
                char option_str[128];
                snprintf(option_str, sizeof(option_str), "--%s", long_name);
                int wrote = append_candidate(option_str, buf, pos, buf_size, count == 0);
                if (wrote < 0) return -1;
                pos += wrote;
                count++;
            }
        }
        return count;
    }

    /* Otherwise, complete subcommands */
    if (current->child_count > 0) {
        for (int i = 0; i < current->child_count; i++) {
            const char* child_name = current->children[i].spec.name;
            if (prefix_len == 0 || strncmp(child_name, prefix, (size_t)prefix_len) == 0) {
                int wrote = append_candidate(child_name, buf, pos, buf_size, count == 0);
                if (wrote < 0) return -1;
                pos += wrote;
                count++;
            }
        }
        return count;
    }

    /* No subcommands, try completing options for the current command */
    if (current->spec.arg_count > 0 && prefix_len == 0) {
        for (int i = 0; i < current->spec.arg_count; i++) {
            char option_str[128];
            snprintf(option_str, sizeof(option_str), "--%s", current->spec.args[i].long_name);
            int wrote = append_candidate(option_str, buf, pos, buf_size, count == 0);
            if (wrote < 0) return -1;
            pos += wrote;
            count++;
        }
    }

    return count;
}
