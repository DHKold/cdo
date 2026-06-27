#include "core/cli.h"
#include <string.h>

// --- Command name table (needed for suggestion lookups) ---
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

    { "source",  CDO_CMD_SOURCE  },
    { "shader",  CDO_CMD_SHADER  },
    { "tool",    CDO_CMD_TOOL    },
    { "doctor",  CDO_CMD_DOCTOR  },
    { "self",    CDO_CMD_SELF    },
    { "deps",    CDO_CMD_DEPS    },
    { "catalog", CDO_CMD_CATALOG },
    { "help",    CDO_CMD_HELP    },
};

#define COMMAND_TABLE_SIZE (sizeof(command_table) / sizeof(command_table[0]))

// --- Levenshtein distance helpers ---

static int cdo_min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

static int levenshtein(const char* s, int len_s, const char* t, int len_t) {
    // Use a single-row DP approach to avoid large stack allocations.
    // Max command length is small (< 32), so a stack buffer is fine.
    if (len_s == 0) return len_t;
    if (len_t == 0) return len_s;

    // prev row buffer (len_t + 1 entries)
    int row[33]; // max command name is 32 chars + 1
    if (len_t >= 33) len_t = 32; // safety clamp

    for (int j = 0; j <= len_t; j++) {
        row[j] = j;
    }

    for (int i = 1; i <= len_s; i++) {
        int prev_diag = row[0];
        row[0] = i;
        for (int j = 1; j <= len_t; j++) {
            int old_diag = row[j];
            int cost = (s[i - 1] == t[j - 1]) ? 0 : 1;
            row[j] = cdo_min3(
                row[j] + 1,        // deletion
                row[j - 1] + 1,    // insertion
                prev_diag + cost   // substitution
            );
            prev_diag = old_diag;
        }
    }

    return row[len_t];
}

// --- Command suggestion ---

int cdo_cli_suggest(const char* input, char suggestions[][32], int max_suggestions) {
    if (!input || !suggestions || max_suggestions <= 0) {
        return 0;
    }

    int input_len = (int)strlen(input);
    if (input_len == 0) {
        return 0;
    }

    // Calculate threshold: max(2, input_len / 2), capped at 3
    int threshold = input_len / 2;
    if (threshold < 2) threshold = 2;
    if (threshold > 3) threshold = 3;

    // Collect candidates with their distances
    typedef struct {
        int index;
        int distance;
    } Candidate;

    Candidate candidates[COMMAND_TABLE_SIZE];
    int candidate_count = 0;

    // Skip the "help" entry (last in table) — don't suggest "help" as a typo fix
    int table_limit = (int)COMMAND_TABLE_SIZE;
    if (table_limit > 0 && strcmp(command_table[table_limit - 1].name, "help") == 0) {
        table_limit--;
    }

    for (int i = 0; i < table_limit; i++) {
        const char* cmd_name = command_table[i].name;
        int cmd_len = (int)strlen(cmd_name);
        int dist = levenshtein(input, input_len, cmd_name, cmd_len);

        if (dist <= threshold && dist > 0) { // dist > 0 excludes exact matches
            candidates[candidate_count].index = i;
            candidates[candidate_count].distance = dist;
            candidate_count++;
        }
    }

    if (candidate_count == 0) {
        return 0;
    }

    // Simple insertion sort by distance (candidate_count is small)
    for (int i = 1; i < candidate_count; i++) {
        Candidate tmp = candidates[i];
        int j = i - 1;
        while (j >= 0 && candidates[j].distance > tmp.distance) {
            candidates[j + 1] = candidates[j];
            j--;
        }
        candidates[j + 1] = tmp;
    }

    // Write results
    int count = candidate_count < max_suggestions ? candidate_count : max_suggestions;
    for (int i = 0; i < count; i++) {
        const char* name = command_table[candidates[i].index].name;
        strncpy(suggestions[i], name, 31);
        suggestions[i][31] = '\0';
    }

    return count;
}
