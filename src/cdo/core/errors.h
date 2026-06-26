#ifndef CDO_CORE_ERRORS_H
#define CDO_CORE_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

/// Scan compiler/linker error text and print helpful CDo-specific hints.
/// The original error text is NOT modified or suppressed — this function
/// should be called AFTER displaying the raw compiler output.
///
/// Recognized patterns:
///   - Missing header ("No such file or directory" for .h files)
///   - Undefined reference (linker: "undefined reference to")
///   - Multiple definition ("multiple definition of")
///   - Missing library ("cannot find -l")
///
/// If error_text is NULL or empty, this function does nothing.
void error_hint_from_compiler_output(const char* error_text);

/// Report an internal CDo error with context and bug report suggestion.
/// Prints in red: "Internal CDo error: <context>"
/// Followed by the detail message and a link to file a bug report.
///
/// If context is NULL, a generic message is used.
/// If detail is NULL, the detail line is omitted.
void error_internal(const char* context, const char* detail);

#ifdef __cplusplus
}
#endif

#endif // CDO_CORE_ERRORS_H
