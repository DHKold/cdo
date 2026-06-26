#include "core/errors.h"
#include "core/output.h"

#include <string.h>
#include <stdbool.h>

// --- Pattern detection helpers ---

/// Check if the error text contains a missing header pattern.
/// Matches: "fatal error: *.h: No such file" or "*.h: No such file or directory"
static bool has_missing_header(const char* text) {
    // Look for ".h: No such file" which covers both GCC/Clang patterns:
    //   "fatal error: foo.h: No such file or directory"
    //   "foo.h: No such file or directory"
    const char* pos = text;
    while ((pos = strstr(pos, ".h: No such file")) != NULL) {
        return true;
    }
    // Also check for .hpp headers
    pos = text;
    while ((pos = strstr(pos, ".hpp: No such file")) != NULL) {
        return true;
    }
    return false;
}

/// Check if the error text contains an undefined reference (linker error).
static bool has_undefined_reference(const char* text) {
    return strstr(text, "undefined reference to") != NULL;
}

/// Check if the error text contains a multiple definition error.
static bool has_multiple_definition(const char* text) {
    return strstr(text, "multiple definition of") != NULL;
}

/// Check if the error text contains a missing library linker error.
/// Matches: "cannot find -l"
static bool has_missing_library(const char* text) {
    return strstr(text, "cannot find -l") != NULL;
}

// --- Public API ---

void error_hint_from_compiler_output(const char* error_text) {
    if (!error_text || error_text[0] == '\0') {
        return;
    }

    bool printed_any = false;

    if (has_missing_header(error_text)) {
        cdo_info("hint: Try: `cdo add <package>` or check include paths in crate.toml");
        printed_any = true;
    }

    if (has_undefined_reference(error_text)) {
        cdo_info("hint: Missing library? Check [dependencies] in crate.toml");
        printed_any = true;
    }

    if (has_multiple_definition(error_text)) {
        cdo_info("hint: Duplicate symbol — check for multiply-defined globals across translation units");
        printed_any = true;
    }

    if (has_missing_library(error_text)) {
        cdo_info("hint: Missing library. Try: `cdo add <library-name>`");
        printed_any = true;
    }

    (void)printed_any;
}

void error_internal(const char* context, const char* detail) {
    if (context) {
        cdo_error("Internal CDo error: %s", context);
    } else {
        cdo_error("Internal CDo error");
    }

    if (detail) {
        cdo_error("  %s", detail);
    }

    cdo_error("This is a bug. Please report it at: https://github.com/cdo-dev/cdo/issues");
}
