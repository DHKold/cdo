#ifndef CDO_MODEL_FMT_SETTINGS_H
#define CDO_MODEL_FMT_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/// Format settings parsed from [workspace.settings.format] in cdo.toml.
/// All fields have safe defaults when the section is absent.
typedef struct {
    char tool_path[260];            // Override formatter path (empty = auto-discover)
    char style[64];                 // --style value (empty = use .clang-format file)
    char exclude_patterns[32][260]; // Glob patterns to exclude from formatting
    int  exclude_count;             // Number of patterns in exclude_patterns[]
} FmtSettings;

#ifdef __cplusplus
}
#endif

#endif // CDO_MODEL_FMT_SETTINGS_H
