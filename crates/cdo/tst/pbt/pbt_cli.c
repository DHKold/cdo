/*
 * pbt_cli.c - CLI Property-Based Tests
 *
 * Property 8: CLI Suggestion Relevance (Requirements 1.2)
 * Property 9: Global Options Parsing (Requirements 1.3)
 */
#include "cdo_ut.h"
#include "vendor/theft.h"
#include "core/cli.h"

/*============================================================================
 * Property 8: CLI Suggestion Relevance
 *============================================================================*/

static const char *KNOWN_COMMANDS[] = {
    "build", "run", "test", "clean", "new", "init",
    "tool", "doctor",
    "deps", "catalog", "help",
};
#define KNOWN_COMMAND_COUNT 11

static const char *SUGGESTABLE_COMMANDS[] = {
    "build", "run", "test", "clean", "new", "init",
    "tool", "doctor",
    "deps", "catalog",
};
#define SUGGESTABLE_COMMAND_COUNT 10

static int cli_suggest_threshold(const char *input) {
    int input_len = (int)strlen(input);
    int threshold = input_len / 2;
    if (threshold < 2) threshold = 2;
    if (threshold > 3) threshold = 3;
    return threshold;
}

static int levenshtein_distance(const char *s, const char *t_str) {
    size_t s_len = strlen(s);
    size_t t_len = strlen(t_str);

    if (s_len == 0) return (int)t_len;
    if (t_len == 0) return (int)s_len;

    int *prev = (int *)malloc((t_len + 1) * sizeof(int));
    int *curr = (int *)malloc((t_len + 1) * sizeof(int));
    if (!prev || !curr) { free(prev); free(curr); return -1; }

    for (size_t j = 0; j <= t_len; j++) prev[j] = (int)j;

    for (size_t i = 1; i <= s_len; i++) {
        curr[0] = (int)i;
        for (size_t j = 1; j <= t_len; j++) {
            int cost = (s[i - 1] == t_str[j - 1]) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = curr[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            int min_val = del;
            if (ins < min_val) min_val = ins;
            if (sub < min_val) min_val = sub;
            curr[j] = min_val;
        }
        int *tmp = prev; prev = curr; curr = tmp;
    }

    int result = prev[t_len];
    free(prev); free(curr);
    return result;
}

static bool is_known_command(const char *input) {
    for (int i = 0; i < KNOWN_COMMAND_COUNT; i++) {
        if (strcmp(input, KNOWN_COMMANDS[i]) == 0) return true;
    }
    return false;
}

static int min_distance_to_suggestable_command(const char *input) {
    int min_dist = 9999;
    for (int i = 0; i < SUGGESTABLE_COMMAND_COUNT; i++) {
        int d = levenshtein_distance(input, SUGGESTABLE_COMMANDS[i]);
        if (d < min_dist) min_dist = d;
    }
    return min_dist;
}

static enum theft_alloc_res
alloc_cli_input(struct theft *t, void *env, void **output) {
    (void)env;

    bool make_typo = theft_random_choice(t, 2) == 0;
    char *str = NULL;

    if (make_typo) {
        int cmd_idx = (int)theft_random_choice(t, KNOWN_COMMAND_COUNT);
        const char *base = KNOWN_COMMANDS[cmd_idx];
        size_t base_len = strlen(base);

        int mutation = (int)theft_random_choice(t, 4);

        switch (mutation) {
        case 0: {
            if (base_len < 2) {
                str = (char *)malloc(base_len + 1);
                if (!str) return THEFT_ALLOC_ERROR;
                memcpy(str, base, base_len + 1);
                size_t pos = (size_t)theft_random_choice(t, base_len);
                str[pos] = (char)('a' + theft_random_choice(t, 26));
            } else {
                str = (char *)malloc(base_len + 1);
                if (!str) return THEFT_ALLOC_ERROR;
                memcpy(str, base, base_len + 1);
                size_t pos = (size_t)theft_random_choice(t, base_len - 1);
                char tmp = str[pos]; str[pos] = str[pos + 1]; str[pos + 1] = tmp;
            }
            break;
        }
        case 1: {
            str = (char *)malloc(base_len + 2);
            if (!str) return THEFT_ALLOC_ERROR;
            size_t pos = (size_t)theft_random_choice(t, base_len + 1);
            memcpy(str, base, pos);
            str[pos] = (char)('a' + theft_random_choice(t, 26));
            memcpy(str + pos + 1, base + pos, base_len - pos + 1);
            break;
        }
        case 2: {
            if (base_len < 2) {
                str = (char *)malloc(base_len + 1);
                if (!str) return THEFT_ALLOC_ERROR;
                memcpy(str, base, base_len + 1);
                str[0] = (char)('a' + theft_random_choice(t, 26));
            } else {
                str = (char *)malloc(base_len);
                if (!str) return THEFT_ALLOC_ERROR;
                size_t pos = (size_t)theft_random_choice(t, base_len);
                memcpy(str, base, pos);
                memcpy(str + pos, base + pos + 1, base_len - pos - 1);
                str[base_len - 1] = '\0';
            }
            break;
        }
        case 3: {
            str = (char *)malloc(base_len + 1);
            if (!str) return THEFT_ALLOC_ERROR;
            memcpy(str, base, base_len + 1);
            size_t pos = (size_t)theft_random_choice(t, base_len);
            char new_c = (char)('a' + theft_random_choice(t, 26));
            if (new_c == str[pos]) new_c = (char)('a' + ((new_c - 'a' + 1) % 26));
            str[pos] = new_c;
            break;
        }
        }
    } else {
        size_t len = (size_t)(theft_random_choice(t, 8) + 3);
        str = (char *)malloc(len + 1);
        if (!str) return THEFT_ALLOC_ERROR;
        for (size_t i = 0; i < len; i++) {
            str[i] = (char)('a' + theft_random_choice(t, 26));
        }
        str[len] = '\0';
    }

    if (is_known_command(str)) { free(str); return THEFT_ALLOC_SKIP; }

    *output = str;
    return THEFT_ALLOC_OK;
}

static void free_cli_input(void *instance, void *env) { (void)env; free(instance); }

static void print_cli_input(FILE *f, const void *instance, void *env) {
    (void)env;
    fprintf(f, "\"%s\"", (const char *)instance);
}

static struct theft_type_info cli_input_type_info = {
    .alloc  = alloc_cli_input,
    .free   = free_cli_input,
    .print  = print_cli_input,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

static enum theft_trial_res
prop_cli_suggestion_relevance(struct theft *t, void *arg1) {
    (void)t;
    const char *input = (const char *)arg1;
    int threshold = cli_suggest_threshold(input);

    char suggestions[8][32];
    int count = cdo_cli_suggest(input, suggestions, 8);

    for (int i = 0; i < count; i++) {
        if (!is_known_command(suggestions[i])) {
            fprintf(stderr, "  FAIL: suggestion \"%s\" is not a known command\n", suggestions[i]);
            return THEFT_TRIAL_FAIL;
        }
    }

    for (int i = 0; i < count; i++) {
        int dist = levenshtein_distance(input, suggestions[i]);
        if (dist > threshold) {
            fprintf(stderr, "  FAIL: suggestion \"%s\" has distance %d > %d from \"%s\"\n",
                    suggestions[i], dist, threshold, input);
            return THEFT_TRIAL_FAIL;
        }
    }

    int min_dist = min_distance_to_suggestable_command(input);
    if (min_dist <= threshold && min_dist > 0 && count == 0) {
        fprintf(stderr, "  FAIL: suggestable command exists within distance %d of \"%s\" "
                "(threshold=%d) but no suggestions returned\n", min_dist, input, threshold);
        return THEFT_TRIAL_FAIL;
    }

    return THEFT_TRIAL_PASS;
}

TEST(prop_cli_suggestion_relevance) {
    struct theft_run_config cfg = {
        .name = "cli_suggestion_relevance",
        .prop = { .prop1 = prop_cli_suggestion_relevance },
        .type_info = { &cli_input_type_info },
        .seed = 88888,
        .trials = 500,
    };
    enum theft_run_res res = theft_run(&cfg);
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}

/*============================================================================
 * Property 9: Global Options Parsing
 *============================================================================*/

typedef struct {
    int argc;
    char **argv;
    char *argv_storage;
    CdoCommand expected_command;
    bool       expect_verbose;
    bool       expect_quiet;
    bool       expect_help;
    bool       expect_release;
    CdoColorMode expected_color;
    CdoLogLevel  expected_log_level;
    int          expected_jobs;
    bool         options_before_command;
} CliInvocation;

static const struct { const char* name; CdoCommand cmd; } GEN_COMMANDS[] = {
    { "build",   CDO_CMD_BUILD   },
    { "run",     CDO_CMD_RUN     },
    { "test",    CDO_CMD_TEST    },
    { "clean",   CDO_CMD_CLEAN   },
    { "new",     CDO_CMD_NEW     },
    { "init",    CDO_CMD_INIT    },
    { "shader",  CDO_CMD_SHADER  },
    { "tool",    CDO_CMD_TOOL    },
    { "doctor",  CDO_CMD_DOCTOR  },
};
#define GEN_COMMAND_COUNT (int)(sizeof(GEN_COMMANDS) / sizeof(GEN_COMMANDS[0]))

static const char* COLOR_VALUES[] = { "auto", "always", "never" };
static const CdoColorMode COLOR_ENUMS[] = { CDO_COLOR_AUTO, CDO_COLOR_ALWAYS, CDO_COLOR_NEVER };

static const char* LOG_LEVEL_VALUES[] = { "error", "warn", "info", "debug", "trace" };
static const CdoLogLevel LOG_LEVEL_ENUMS[] = { CDO_LOG_ERROR, CDO_LOG_WARN, CDO_LOG_INFO, CDO_LOG_DEBUG, CDO_LOG_TRACE };

static enum theft_alloc_res
alloc_cli_invocation(struct theft *t, void *env, void **output) {
    (void)env;

    CliInvocation *inv = calloc(1, sizeof(CliInvocation));
    if (!inv) return THEFT_ALLOC_ERROR;

    int cmd_idx = (int)theft_random_choice(t, (uint64_t)GEN_COMMAND_COUNT);
    const char *cmd_name = GEN_COMMANDS[cmd_idx].name;
    inv->expected_command = GEN_COMMANDS[cmd_idx].cmd;

    inv->expect_verbose = theft_random_choice(t, 2) != 0;
    inv->expect_quiet   = theft_random_choice(t, 2) != 0;
    inv->expect_help    = theft_random_choice(t, 2) != 0;
    inv->expect_release = theft_random_choice(t, 2) != 0;

    int color_choice = (int)theft_random_choice(t, 4);
    inv->expected_color = (color_choice == 0) ? CDO_COLOR_AUTO : COLOR_ENUMS[color_choice - 1];

    int log_choice = (int)theft_random_choice(t, 6);

    int jobs_specified = (int)theft_random_choice(t, 2);
    inv->expected_jobs = jobs_specified ? (int)(theft_random_choice(t, 16) + 1) : 0;

    inv->options_before_command = theft_random_choice(t, 2) != 0;

    #define MAX_CLI_ARGS 16
    const char *args[MAX_CLI_ARGS];
    int arg_count = 0;
    args[arg_count++] = "cdo";

    const char *opts_arr[8];
    int opt_count = 0;
    char *color_buf = NULL, *log_buf = NULL, *jobs_buf = NULL;

    if (inv->expect_verbose) opts_arr[opt_count++] = "--verbose";
    if (inv->expect_quiet)   opts_arr[opt_count++] = "--quiet";
    if (inv->expect_help)    opts_arr[opt_count++] = "--help";
    if (inv->expect_release) opts_arr[opt_count++] = "--release";

    if (color_choice > 0) {
        color_buf = malloc(32);
        if (!color_buf) { free(inv); return THEFT_ALLOC_ERROR; }
        snprintf(color_buf, 32, "--color=%s", COLOR_VALUES[color_choice - 1]);
        opts_arr[opt_count++] = color_buf;
    }

    if (log_choice > 0) {
        log_buf = malloc(32);
        if (!log_buf) { free(color_buf); free(inv); return THEFT_ALLOC_ERROR; }
        snprintf(log_buf, 32, "--log-level=%s", LOG_LEVEL_VALUES[log_choice - 1]);
        opts_arr[opt_count++] = log_buf;
    }

    if (jobs_specified) {
        jobs_buf = malloc(32);
        if (!jobs_buf) { free(color_buf); free(log_buf); free(inv); return THEFT_ALLOC_ERROR; }
        snprintf(jobs_buf, 32, "--jobs=%d", inv->expected_jobs);
        opts_arr[opt_count++] = jobs_buf;
    }

    if (inv->expect_quiet) {
        inv->expected_log_level = CDO_LOG_ERROR;
    } else if (log_choice > 0) {
        inv->expected_log_level = LOG_LEVEL_ENUMS[log_choice - 1];
    } else if (inv->expect_verbose) {
        inv->expected_log_level = CDO_LOG_DEBUG;
    } else {
        inv->expected_log_level = CDO_LOG_INFO;
    }

    if (inv->options_before_command) {
        for (int i = 0; i < opt_count; i++) args[arg_count++] = opts_arr[i];
        args[arg_count++] = cmd_name;
    } else {
        args[arg_count++] = cmd_name;
        for (int i = 0; i < opt_count; i++) args[arg_count++] = opts_arr[i];
    }

    inv->argc = arg_count;
    inv->argv = calloc((size_t)(arg_count + 1), sizeof(char*));
    if (!inv->argv) { free(color_buf); free(log_buf); free(jobs_buf); free(inv); return THEFT_ALLOC_ERROR; }

    size_t total_len = 0;
    for (int i = 0; i < arg_count; i++) total_len += strlen(args[i]) + 1;
    inv->argv_storage = malloc(total_len);
    if (!inv->argv_storage) { free(inv->argv); free(color_buf); free(log_buf); free(jobs_buf); free(inv); return THEFT_ALLOC_ERROR; }

    char *ptr = inv->argv_storage;
    for (int i = 0; i < arg_count; i++) {
        size_t slen = strlen(args[i]);
        memcpy(ptr, args[i], slen + 1);
        inv->argv[i] = ptr;
        ptr += slen + 1;
    }
    inv->argv[arg_count] = NULL;

    free(color_buf); free(log_buf); free(jobs_buf);
    *output = inv;
    return THEFT_ALLOC_OK;
    #undef MAX_CLI_ARGS
}

static void free_cli_invocation(void *instance, void *env) {
    (void)env;
    CliInvocation *inv = (CliInvocation *)instance;
    if (inv) { free(inv->argv_storage); free(inv->argv); free(inv); }
}

static void print_cli_invocation(FILE *f, const void *instance, void *env) {
    (void)env;
    const CliInvocation *inv = (const CliInvocation *)instance;
    fprintf(f, "CliInvocation(argc=%d, opts_%s_cmd): ",
            inv->argc, inv->options_before_command ? "before" : "after");
    for (int i = 0; i < inv->argc; i++) {
        fprintf(f, "%s%s", i > 0 ? " " : "", inv->argv[i]);
    }
}

static struct theft_type_info cli_invocation_type_info = {
    .alloc  = alloc_cli_invocation,
    .free   = free_cli_invocation,
    .print  = print_cli_invocation,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

static enum theft_trial_res
prop_global_options_parsing(struct theft *t, void *arg1) {
    (void)t;
    CliInvocation *inv = (CliInvocation *)arg1;

    CdoOptions parsed_opts;
    int rc = cdo_cli_parse(inv->argc, inv->argv, &parsed_opts);

    if (rc != 0) {
        fprintf(stderr, "  cdo_cli_parse returned %d for: ", rc);
        for (int i = 0; i < inv->argc; i++) fprintf(stderr, "%s ", inv->argv[i]);
        fprintf(stderr, "\n");
        return THEFT_TRIAL_FAIL;
    }

    if (parsed_opts.command != inv->expected_command) {
        fprintf(stderr, "  Command mismatch: got %d, expected %d\n",
                parsed_opts.command, inv->expected_command);
        return THEFT_TRIAL_FAIL;
    }
    if (parsed_opts.verbose != inv->expect_verbose) {
        fprintf(stderr, "  verbose mismatch: got %d, expected %d\n",
                parsed_opts.verbose, inv->expect_verbose);
        return THEFT_TRIAL_FAIL;
    }
    if (parsed_opts.quiet != inv->expect_quiet) {
        fprintf(stderr, "  quiet mismatch: got %d, expected %d\n",
                parsed_opts.quiet, inv->expect_quiet);
        return THEFT_TRIAL_FAIL;
    }
    if (parsed_opts.help != inv->expect_help) {
        fprintf(stderr, "  help mismatch: got %d, expected %d\n",
                parsed_opts.help, inv->expect_help);
        return THEFT_TRIAL_FAIL;
    }
    if (parsed_opts.release != inv->expect_release) {
        fprintf(stderr, "  release mismatch: got %d, expected %d\n",
                parsed_opts.release, inv->expect_release);
        return THEFT_TRIAL_FAIL;
    }
    if (parsed_opts.color != inv->expected_color) {
        fprintf(stderr, "  color mismatch: got %d, expected %d\n",
                parsed_opts.color, inv->expected_color);
        return THEFT_TRIAL_FAIL;
    }
    if (parsed_opts.log_level != inv->expected_log_level) {
        fprintf(stderr, "  log_level mismatch: got %d, expected %d\n",
                parsed_opts.log_level, inv->expected_log_level);
        return THEFT_TRIAL_FAIL;
    }
    if (parsed_opts.jobs != inv->expected_jobs) {
        fprintf(stderr, "  jobs mismatch: got %d, expected %d\n",
                parsed_opts.jobs, inv->expected_jobs);
        return THEFT_TRIAL_FAIL;
    }

    return THEFT_TRIAL_PASS;
}

TEST(prop_global_options_parsing) {
    struct theft_run_config cfg = {
        .name = "global_options_parsing",
        .prop = { .prop1 = prop_global_options_parsing },
        .type_info = { &cli_invocation_type_info },
        .seed = 19830,
        .trials = 1000,
    };
    enum theft_run_res res = theft_run(&cfg);
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}
