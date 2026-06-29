/*
 * catalog_resolve.c — Catalog resolution and search.
 */

#include "core/catalog.h"
#include "core/output.h"
#include "commons/semver.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Internal Helpers
 * -------------------------------------------------------------------------- */

static int catalog_stricmp(const char* a, const char* b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static bool catalog_stristr(const char* haystack, const char* needle)
{
    if (!haystack || !needle) return false;
    if (needle[0] == '\0') return true;
    size_t hay_len = strlen(haystack);
    size_t ndl_len = strlen(needle);
    if (ndl_len > hay_len) return false;

    for (size_t i = 0; i <= hay_len - ndl_len; i++) {
        bool match = true;
        for (size_t j = 0; j < ndl_len; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

/* --------------------------------------------------------------------------
 * Tool Resolution
 * -------------------------------------------------------------------------- */

int catalog_resolve_tool(const Catalog* cat, const char* name,
                         const char* version_constraint,
                         const CatalogPlatform* platform,
                         CatalogResolveResult* out)
{
    if (!cat || !name || !platform || !out) return 1;
    memset(out, 0, sizeof(*out));

    SemverConstraint constraint;
    bool has_constraint = false;

    if (version_constraint && version_constraint[0] != '\0') {
        if (semver_constraint_parse(version_constraint, &constraint) != 0) {
            cdo_error("invalid version constraint '%s'", version_constraint);
            cdo_info("  supported formats: 1.2.3, ^1.2.3, ~1.2.3, >=1.2.3, <2.0.0, *");
            return 1;
        }
        has_constraint = true;
    }

    int best_idx = -1;
    Semver best_version;
    memset(&best_version, 0, sizeof(best_version));

    for (int i = 0; i < cat->tool_count; i++) {
        if (catalog_stricmp(cat->tools[i].name, name) != 0) continue;
        Semver entry_ver;
        if (semver_parse(cat->tools[i].version, &entry_ver) != 0) continue;
        if (has_constraint && !semver_satisfies(&entry_ver, &constraint)) continue;
        if (best_idx < 0 || semver_compare(&entry_ver, &best_version) > 0) {
            best_idx = i;
            best_version = entry_ver;
        }
    }

    if (best_idx < 0) {
        bool name_found = false;
        for (int i = 0; i < cat->tool_count; i++) {
            if (catalog_stricmp(cat->tools[i].name, name) == 0) {
                name_found = true;
                break;
            }
        }
        if (!name_found) {
            cdo_error("tool '%s' not found in any loaded catalog", name);
            cdo_info("  hint: use --url to specify a download URL manually");
        } else {
            cdo_error("no version of tool '%s' satisfies constraint '%s'",
                      name, version_constraint ? version_constraint : "");
        }
        return 1;
    }

    const CatalogToolEntry* tool = &cat->tools[best_idx];
    int plat_idx = -1;
    for (int i = 0; i < tool->platform_count; i++) {
        if (strcmp(tool->platforms[i].triple, platform->triple) == 0) {
            plat_idx = i;
            break;
        }
    }

    if (plat_idx < 0) {
        cdo_error("tool '%s' is not available for platform '%s'",
                  name, platform->triple);
        cdo_info("  available platforms:");
        for (int i = 0; i < tool->platform_count; i++) {
            cdo_info("    - %s", tool->platforms[i].triple);
        }
        return 1;
    }

    snprintf(out->url, sizeof(out->url), "%s", tool->platforms[plat_idx].url);
    snprintf(out->checksum, sizeof(out->checksum), "%s", tool->platforms[plat_idx].checksum);
    snprintf(out->version, sizeof(out->version), "%s", tool->version);
    return 0;
}

/* --------------------------------------------------------------------------
 * Package Resolution
 * -------------------------------------------------------------------------- */

int catalog_resolve_package(const Catalog* cat, const char* name,
                            const char* version_constraint,
                            const CatalogPlatform* platform,
                            CatalogResolveResult* out)
{
    if (!cat || !name || !platform || !out) return 1;
    memset(out, 0, sizeof(*out));

    SemverConstraint constraint;
    bool has_constraint = false;

    if (version_constraint && version_constraint[0] != '\0') {
        if (semver_constraint_parse(version_constraint, &constraint) != 0) {
            cdo_error("invalid version constraint '%s'", version_constraint);
            cdo_info("  supported formats: 1.2.3, ^1.2.3, ~1.2.3, >=1.2.3, <2.0.0, *");
            return 1;
        }
        has_constraint = true;
    }

    int best_idx = -1;
    Semver best_version;
    memset(&best_version, 0, sizeof(best_version));

    for (int i = 0; i < cat->package_count; i++) {
        if (catalog_stricmp(cat->packages[i].name, name) != 0) continue;
        Semver entry_ver;
        if (semver_parse(cat->packages[i].version, &entry_ver) != 0) continue;
        if (has_constraint && !semver_satisfies(&entry_ver, &constraint)) continue;
        if (best_idx < 0 || semver_compare(&entry_ver, &best_version) > 0) {
            best_idx = i;
            best_version = entry_ver;
        }
    }

    if (best_idx < 0) {
        bool name_found = false;
        for (int i = 0; i < cat->package_count; i++) {
            if (catalog_stricmp(cat->packages[i].name, name) == 0) {
                name_found = true;
                break;
            }
        }
        if (!name_found) {
            cdo_error("package '%s' not found in any loaded catalog", name);
            const char* suggestions[5];
            int suggestion_count = 0;
            for (int i = 0; i < cat->package_count && suggestion_count < 5; i++) {
                if (!catalog_stristr(cat->packages[i].name, name)) continue;
                bool dup = false;
                for (int s = 0; s < suggestion_count; s++) {
                    if (catalog_stricmp(suggestions[s], cat->packages[i].name) == 0) {
                        dup = true; break;
                    }
                }
                if (!dup) suggestions[suggestion_count++] = cat->packages[i].name;
            }
            if (suggestion_count > 0) {
                cdo_info("  did you mean:");
                for (int s = 0; s < suggestion_count; s++) {
                    cdo_info("    - %s", suggestions[s]);
                }
            }
        } else {
            cdo_error("no version of package '%s' satisfies constraint '%s'",
                      name, version_constraint ? version_constraint : "");
        }
        return 1;
    }

    const CatalogPackageEntry* pkg = &cat->packages[best_idx];
    int plat_idx = -1;
    for (int i = 0; i < pkg->platform_count; i++) {
        if (strcmp(pkg->platforms[i].triple, platform->triple) == 0) {
            plat_idx = i;
            break;
        }
    }

    if (plat_idx < 0) {
        cdo_error("package '%s' is not available for platform '%s'",
                  name, platform->triple);
        cdo_info("  available platforms:");
        for (int i = 0; i < pkg->platform_count; i++) {
            cdo_info("    - %s", pkg->platforms[i].triple);
        }
        return 1;
    }

    snprintf(out->url, sizeof(out->url), "%s", pkg->platforms[plat_idx].url);
    snprintf(out->checksum, sizeof(out->checksum), "%s", pkg->platforms[plat_idx].checksum);
    snprintf(out->version, sizeof(out->version), "%s", pkg->version);

    out->include_dir_count = 0;
    for (int i = 0; i < pkg->include_dir_count; i++) {
        if (pkg->include_dirs[i]) {
            out->include_dirs[out->include_dir_count] = _strdup(pkg->include_dirs[i]);
            if (out->include_dirs[out->include_dir_count]) out->include_dir_count++;
        }
    }

    out->link_lib_count = 0;
    for (int i = 0; i < pkg->link_lib_count; i++) {
        if (pkg->link_libs[i]) {
            out->link_libs[out->link_lib_count] = _strdup(pkg->link_libs[i]);
            if (out->link_libs[out->link_lib_count]) out->link_lib_count++;
        }
    }

    out->define_count = 0;
    for (int i = 0; i < pkg->define_count; i++) {
        if (pkg->defines[i]) {
            out->defines[out->define_count] = _strdup(pkg->defines[i]);
            if (out->defines[out->define_count]) out->define_count++;
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * Resolution Result Free
 * -------------------------------------------------------------------------- */

void catalog_resolve_result_free(CatalogResolveResult* result)
{
    if (!result) return;

    for (int i = 0; i < result->include_dir_count; i++) {
        free(result->include_dirs[i]);
        result->include_dirs[i] = NULL;
    }
    result->include_dir_count = 0;

    for (int i = 0; i < result->link_lib_count; i++) {
        free(result->link_libs[i]);
        result->link_libs[i] = NULL;
    }
    result->link_lib_count = 0;

    for (int i = 0; i < result->define_count; i++) {
        free(result->defines[i]);
        result->defines[i] = NULL;
    }
    result->define_count = 0;
}

/* --------------------------------------------------------------------------
 * Catalog Search
 * -------------------------------------------------------------------------- */

int catalog_search(const Catalog* cat, const char* query,
                   bool tools_only, bool packages_only,
                   int* out_tool_indices, int* tool_match_count,
                   int* out_pkg_indices, int* pkg_match_count)
{
    if (!cat || !tool_match_count || !pkg_match_count) return 1;

    *tool_match_count = 0;
    *pkg_match_count = 0;

    if (!query || query[0] == '\0') return 0;

    if (!packages_only) {
        for (int i = 0; i < cat->tool_count; i++) {
            if (catalog_stristr(cat->tools[i].name, query) ||
                catalog_stristr(cat->tools[i].description, query)) {
                if (out_tool_indices) out_tool_indices[*tool_match_count] = i;
                (*tool_match_count)++;
            }
        }
    }

    if (!tools_only) {
        for (int i = 0; i < cat->package_count; i++) {
            if (catalog_stristr(cat->packages[i].name, query) ||
                catalog_stristr(cat->packages[i].description, query)) {
                if (out_pkg_indices) out_pkg_indices[*pkg_match_count] = i;
                (*pkg_match_count)++;
            }
        }
    }

    return 0;
}
