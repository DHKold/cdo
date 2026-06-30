/*
 * pbt_cli.c - CLI Property-Based Tests
 *
 * Property 8: CLI Suggestion Relevance (Requirements 1.2)
 *
 * Updated to use the new cdo_cli framework (cli_cmd_suggest via registry)
 * after the removal of CdoOptions/CdoCommand in task 5.5.
 */
#include "cdo_ut.h"
#include "vendor/theft.h"
#include "cmd/cli_cmd.h"
#include "core/registry_setup.h"

#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Property 8: CLI Suggestion Relevance
 *============================================================================*/

static const char *KNOWN_COMMANDS[] = {
    "build", "run", "test", "clean", "new", "init",
    "tool", "doctor",
    "deps", "fmt", "catalog", "cache", "hook", "help",
    "install", "uninstall", "e2e",
};
#define KNOWN_COMMAND_COUNT 17

static const char *SUGGESTABLE_COMMANDS[] = {
    "build", "run", "test", "clean", "new", "init",
    "tool", "doctor",
    "deps", "fmt", "catalog", "cache", "hook",
    "install", "uninstall", "e2e",
};
#define SUGGESTABLE_COMMAND_COUNT 16

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

    // Use the new registry-based suggestion engine
    CliCmdRegistry* reg = cdo_registry_create();
    if (!reg) return THEFT_TRIAL_ERROR;

    char suggestions[8][32];
    int count = cli_cmd_suggest(reg, input, suggestions, 8);

    for (int i = 0; i < count; i++) {
        if (!is_known_command(suggestions[i])) {
            fprintf(stderr, "  FAIL: suggestion \"%s\" is not a known command\n", suggestions[i]);
            cli_cmd_registry_destroy(reg);
            return THEFT_TRIAL_FAIL;
        }
    }

    for (int i = 0; i < count; i++) {
        int dist = levenshtein_distance(input, suggestions[i]);
        if (dist > threshold) {
            fprintf(stderr, "  FAIL: suggestion \"%s\" has distance %d > %d from \"%s\"\n",
                    suggestions[i], dist, threshold, input);
            cli_cmd_registry_destroy(reg);
            return THEFT_TRIAL_FAIL;
        }
    }

    int min_dist = min_distance_to_suggestable_command(input);
    if (min_dist <= threshold && min_dist > 0 && count == 0) {
        fprintf(stderr, "  FAIL: suggestable command exists within distance %d of \"%s\" "
                "(threshold=%d) but no suggestions returned\n", min_dist, input, threshold);
        cli_cmd_registry_destroy(reg);
        return THEFT_TRIAL_FAIL;
    }

    cli_cmd_registry_destroy(reg);
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
