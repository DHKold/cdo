#ifndef CDO_COMMONS_SEMVER_H
#define CDO_COMMONS_SEMVER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parsed semantic version */
typedef struct {
    int  major;
    int  minor;
    int  patch;
    char prerelease[64]; /* e.g. "alpha", "beta.1" — empty if release */
} Semver;

/* Version constraint kinds */
typedef enum {
    SEMVER_EXACT,       /* 1.2.3 */
    SEMVER_CARET,       /* ^1.2.3 */
    SEMVER_TILDE,       /* ~1.2.3 */
    SEMVER_GTE,         /* >=1.2.3 */
    SEMVER_LT,          /* <2.0.0 */
    SEMVER_WILDCARD,    /* * */
} SemverConstraintKind;

typedef struct {
    SemverConstraintKind kind;
    Semver               version; /* reference version (unused for WILDCARD) */
} SemverConstraint;

/**
 * Parse a version string "major.minor.patch[-prerelease]" into a Semver.
 * Returns 0 on success, non-zero if malformed.
 */
int semver_parse(const char* str, Semver* out);

/**
 * Parse a version constraint string into a SemverConstraint.
 * Supports: "1.2.3", "^1.2.3", "~1.2.3", ">=1.2.3", "<2.0.0", "*"
 * Returns 0 on success, non-zero if malformed.
 */
int semver_constraint_parse(const char* str, SemverConstraint* out);

/**
 * Check if a version satisfies a constraint.
 * Returns true if the version is within the acceptable range.
 */
bool semver_satisfies(const Semver* version, const SemverConstraint* constraint);

/**
 * Compare two versions. Returns:
 *   <0 if a < b, 0 if a == b, >0 if a > b
 * Pre-release versions are lower than their release counterparts.
 */
int semver_compare(const Semver* a, const Semver* b);

#ifdef __cplusplus
}
#endif

#endif /* CDO_COMMONS_SEMVER_H */
