/*
 * cdo_ut_filter.c — Test name filter matching for the cdo_ut framework.
 *
 * Supports two matching modes:
 *   1. Substring match: when pattern contains no '*', uses strstr.
 *   2. Glob match: when pattern contains '*', treats each '*' as matching
 *      zero or more arbitrary characters. All other characters match literally.
 */

#include "cdo_ut_filter.h"
#include <string.h>

/* Forward declaration of the recursive glob matcher. */
static bool glob_match(const char *text, const char *pattern);

/*
 * glob_match — Recursive glob pattern matching.
 *
 * '*' matches zero or more characters.
 * All other characters must match exactly.
 */
static bool glob_match(const char *text, const char *pattern) {
    while (*pattern != '\0') {
        if (*pattern == '*') {
            /* Skip consecutive '*' characters (they're equivalent to one). */
            while (*pattern == '*') {
                pattern++;
            }
            /* Trailing '*' matches everything remaining. */
            if (*pattern == '\0') {
                return true;
            }
            /* Try matching '*' against zero or more characters of text. */
            while (*text != '\0') {
                if (glob_match(text, pattern)) {
                    return true;
                }
                text++;
            }
            /* Reached end of text without a match. */
            return glob_match(text, pattern);
        } else {
            /* Literal character — must match exactly. */
            if (*text != *pattern) {
                return false;
            }
            text++;
            pattern++;
        }
    }
    /* Pattern exhausted — match only if text is also exhausted. */
    return *text == '\0';
}

bool cdo_ut_filter_matches(const char *test_name, const char *pattern) {
    if (test_name == NULL || pattern == NULL) {
        return false;
    }

    /* Check if pattern contains a '*' character. */
    if (strchr(pattern, '*') != NULL) {
        /* Glob mode. */
        return glob_match(test_name, pattern);
    }

    /* Substring mode. */
    return strstr(test_name, pattern) != NULL;
}
