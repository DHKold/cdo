/*
 * cdo_ut_filter.h — Internal filter matching for the cdo_ut test framework.
 *
 * Provides substring and glob pattern matching for test name filtering.
 * This is an internal header — not part of the public API.
 */

#ifndef CDO_UT_FILTER_H
#define CDO_UT_FILTER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns true if test_name matches the filter pattern.
 *
 * If pattern contains no '*': performs substring match (strstr).
 * If pattern contains '*': treats '*' as a glob wildcard matching
 * zero or more characters. All other characters match literally.
 */
bool cdo_ut_filter_matches(const char *test_name, const char *pattern);

#ifdef __cplusplus
}
#endif

#endif /* CDO_UT_FILTER_H */
