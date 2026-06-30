/**
 * cli_cmd_suggest.c - Command suggestion engine using Levenshtein distance.
 *
 * Provides "did you mean?" suggestions for unrecognized command tokens by
 * iterating over all registered commands in a CliCmdRegistry and ranking
 * them by edit distance.
 */
#include "cmd_internal.h"
#include <string.h>

/* --- Levenshtein distance helpers --- */

static int suggest_min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

/**
 * Compute Levenshtein edit distance between two strings.
 * Uses a single-row DP approach for minimal stack usage.
 * Command names are clamped to 32 chars (matching the suggestion buffer size).
 */
static int levenshtein(const char* s, int len_s, const char* t, int len_t) {
    if (len_s == 0) return len_t;
    if (len_t == 0) return len_s;

    /* Safety clamp: command names should never exceed 32 chars */
    if (len_t >= 33) len_t = 32;
    if (len_s >= 33) len_s = 32;

    int row[33];
    for (int j = 0; j <= len_t; j++) {
        row[j] = j;
    }

    for (int i = 1; i <= len_s; i++) {
        int prev_diag = row[0];
        row[0] = i;
        for (int j = 1; j <= len_t; j++) {
            int old_diag = row[j];
            int cost = (s[i - 1] == t[j - 1]) ? 0 : 1;
            row[j] = suggest_min3(
                row[j] + 1,        /* deletion */
                row[j - 1] + 1,    /* insertion */
                prev_diag + cost   /* substitution */
            );
            prev_diag = old_diag;
        }
    }

    return row[len_t];
}

/* --- Public API --- */

int cli_cmd_suggest(const CliCmdRegistry* reg, const char* input, char suggestions[][32], int max_suggestions) {
    if (!reg || !input || !suggestions || max_suggestions <= 0) {
        return 0;
    }

    int input_len = (int)strlen(input);
    if (input_len == 0) {
        return 0;
    }

    /* Adaptive threshold: max(2, input_length / 2), capped at 3 */
    int threshold = input_len / 2;
    if (threshold < 2) threshold = 2;
    if (threshold > 3) threshold = 3;

    /* Collect candidates with their distances. Use a fixed buffer sized to maximum realistic command count. */
    typedef struct {
        int index;
        int distance;
    } Candidate;

    Candidate candidates[64];
    int candidate_count = 0;

    for (int i = 0; i < reg->command_count && candidate_count < 64; i++) {
        const char* cmd_name = reg->root_commands[i].spec.name;
        if (!cmd_name) continue;

        int cmd_len = (int)strlen(cmd_name);
        int dist = levenshtein(input, input_len, cmd_name, cmd_len);

        if (dist <= threshold && dist > 0) { /* dist > 0 excludes exact matches */
            candidates[candidate_count].index = i;
            candidates[candidate_count].distance = dist;
            candidate_count++;
        }
    }

    if (candidate_count == 0) {
        return 0;
    }

    /* Insertion sort by distance (candidate_count is small) */
    for (int i = 1; i < candidate_count; i++) {
        Candidate tmp = candidates[i];
        int j = i - 1;
        while (j >= 0 && candidates[j].distance > tmp.distance) {
            candidates[j + 1] = candidates[j];
            j--;
        }
        candidates[j + 1] = tmp;
    }

    /* Write results */
    int count = candidate_count < max_suggestions ? candidate_count : max_suggestions;
    for (int i = 0; i < count; i++) {
        const char* name = reg->root_commands[candidates[i].index].spec.name;
        strncpy(suggestions[i], name, 31);
        suggestions[i][31] = '\0';
    }

    return count;
}
