#include "semver.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// --- Internal helpers ---

/**
 * Parse a non-negative integer from a string, advancing the pointer.
 * Returns 0 on success, non-zero if no digits found or overflow.
 */
static int parse_int(const char** str, int* out) {
    const char* s = *str;
    if (!isdigit((unsigned char)*s)) return 1;

    long val = 0;
    while (isdigit((unsigned char)*s)) {
        val = val * 10 + (*s - '0');
        if (val > 999999999) return 1; /* overflow guard */
        s++;
    }
    *out = (int)val;
    *str = s;
    return 0;
}

// --- Public API ---

int semver_parse(const char* str, Semver* out) {
    if (!str || !out) return 1;

    memset(out, 0, sizeof(Semver));

    const char* p = str;

    // Parse major
    if (parse_int(&p, &out->major) != 0) return 1;
    if (*p != '.') return 1;
    p++;

    // Parse minor
    if (parse_int(&p, &out->minor) != 0) return 1;
    if (*p != '.') return 1;
    p++;

    // Parse patch
    if (parse_int(&p, &out->patch) != 0) return 1;

    // Parse optional prerelease
    if (*p == '-') {
        p++;
        size_t len = strlen(p);
        if (len == 0 || len >= sizeof(out->prerelease)) return 1;
        memcpy(out->prerelease, p, len + 1);
        // Validate prerelease chars (alphanumeric, dot, hyphen)
        for (size_t i = 0; i < len; i++) {
            char c = out->prerelease[i];
            if (!isalnum((unsigned char)c) && c != '.' && c != '-') return 1;
        }
    } else if (*p != '\0') {
        return 1; /* trailing garbage */
    }

    return 0;
}

int semver_compare(const Semver* a, const Semver* b) {
    if (!a || !b) return 0;

    // Compare major.minor.patch numerically
    if (a->major != b->major) return a->major - b->major;
    if (a->minor != b->minor) return a->minor - b->minor;
    if (a->patch != b->patch) return a->patch - b->patch;

    // Pre-release comparison:
    // A version with no prerelease is GREATER than one with prerelease.
    bool a_has_pre = (a->prerelease[0] != '\0');
    bool b_has_pre = (b->prerelease[0] != '\0');

    if (!a_has_pre && !b_has_pre) return 0;
    if (!a_has_pre &&  b_has_pre) return 1;   /* a is release, b is pre-release */
    if ( a_has_pre && !b_has_pre) return -1;  /* a is pre-release, b is release */

    // Both have prerelease — compare lexicographically
    return strcmp(a->prerelease, b->prerelease);
}

int semver_constraint_parse(const char* str, SemverConstraint* out) {
    if (!str || !out) return 1;

    memset(out, 0, sizeof(SemverConstraint));

    // Skip leading whitespace
    while (isspace((unsigned char)*str)) str++;

    // Wildcard
    if (str[0] == '*' && str[1] == '\0') {
        out->kind = SEMVER_WILDCARD;
        return 0;
    }

    // Caret: ^x.y.z
    if (str[0] == '^') {
        out->kind = SEMVER_CARET;
        return semver_parse(str + 1, &out->version);
    }

    // Tilde: ~x.y.z
    if (str[0] == '~') {
        out->kind = SEMVER_TILDE;
        return semver_parse(str + 1, &out->version);
    }

    // Greater-or-equal: >=x.y.z
    if (str[0] == '>' && str[1] == '=') {
        out->kind = SEMVER_GTE;
        return semver_parse(str + 2, &out->version);
    }

    // Less-than: <x.y.z
    if (str[0] == '<') {
        out->kind = SEMVER_LT;
        return semver_parse(str + 1, &out->version);
    }

    // Exact: x.y.z[-prerelease]
    out->kind = SEMVER_EXACT;
    return semver_parse(str, &out->version);
}

bool semver_satisfies(const Semver* version, const SemverConstraint* constraint) {
    if (!version || !constraint) return false;

    switch (constraint->kind) {
    case SEMVER_WILDCARD:
        return true;

    case SEMVER_EXACT:
        return semver_compare(version, &constraint->version) == 0;

    case SEMVER_GTE:
        return semver_compare(version, &constraint->version) >= 0;

    case SEMVER_LT:
        return semver_compare(version, &constraint->version) < 0;

    case SEMVER_CARET: {
        // ^X.Y.Z means >=X.Y.Z and <(X+1).0.0
        if (semver_compare(version, &constraint->version) < 0) return false;
        Semver upper = {0};
        upper.major = constraint->version.major + 1;
        upper.minor = 0;
        upper.patch = 0;
        return semver_compare(version, &upper) < 0;
    }

    case SEMVER_TILDE: {
        // ~X.Y.Z means >=X.Y.Z and <X.(Y+1).0
        if (semver_compare(version, &constraint->version) < 0) return false;
        Semver upper = {0};
        upper.major = constraint->version.major;
        upper.minor = constraint->version.minor + 1;
        upper.patch = 0;
        return semver_compare(version, &upper) < 0;
    }
    }

    return false;
}
