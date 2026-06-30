#include "commands/cmd_new.h"
#include "core/log.h"
#include "core/template.h"
#include "core/handler_ctx.h"
#include "cmd/cli_cmd.h"
#include "commons/http.h"
#include "pal/pal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Internal constants
// ---------------------------------------------------------------------------

#define MAX_PATH_LEN    1024
#define MAX_VARS        32
#define TEMPLATE_REGISTRY_URL "https://registry.cdo.dev/templates"

// ---------------------------------------------------------------------------
// Helper: find a named argument in CliParseResult
// ---------------------------------------------------------------------------

static const CliArgValue* find_arg(const CliParseResult* result, const char* name) {
    for (int i = 0; i < result->arg_value_count; i++) {
        if (result->arg_values[i].name && strcmp(result->arg_values[i].name, name) == 0) {
            return &result->arg_values[i];
        }
    }
    return NULL;
}

/// Get a bool argument value. Returns false if not present.
static bool get_arg_bool(const CliParseResult* result, const char* name) {
    const CliArgValue* v = find_arg(result, name);
    return (v && v->present && v->type == CLI_ARG_BOOL) ? v->value.bool_val : false;
}

/// Get a string argument value. Returns NULL if not present.
static const char* get_arg_str(const CliParseResult* result, const char* name) {
    const CliArgValue* v = find_arg(result, name);
    return (v && v->present && (v->type == CLI_ARG_STRING || v->type == CLI_ARG_ENUM)) ? v->value.str_val : NULL;
}

// ---------------------------------------------------------------------------
// Helper: check if a flag is present in positional args (legacy)
// ---------------------------------------------------------------------------

static bool has_flag_in_positionals(const CliParseResult* result, const char* flag) {
    for (int i = 0; i < result->positional_count; i++) {
        if (strcmp(result->positional_values[i], flag) == 0) {
            return true;
        }
    }
    for (int i = 0; i < result->rest_count; i++) {
        if (strcmp(result->rest_args[i], flag) == 0) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helper: get templates directory path
// ---------------------------------------------------------------------------

static int get_templates_dir(char* buf, size_t buf_size) {
    char home[MAX_PATH_LEN];
    if (pal_get_home_dir(home, sizeof(home)) != 0) {
        cdo_log_error("Failed to determine home directory");
        return -1;
    }
    if (pal_path_join(buf, buf_size, home, ".cdo/templates") != 0) {
        cdo_log_error("Path too long for templates directory");
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Walk callback context for directory emptiness check
// ---------------------------------------------------------------------------

typedef struct {
    int file_count;
} DirCheckCtx;

static void dir_check_callback(const char* path, bool is_dir, void* ctx) {
    (void)path;
    (void)is_dir;
    DirCheckCtx* dc = (DirCheckCtx*)ctx;
    dc->file_count++;
}

/// Returns true if the directory exists and contains any entries.
static bool is_directory_non_empty(const char* path) {
    if (pal_path_exists(path) != 0) {
        return false; // doesn't exist, so not non-empty
    }
    DirCheckCtx ctx = { .file_count = 0 };
    pal_dir_walk(path, dir_check_callback, &ctx);
    return ctx.file_count > 0;
}

// ---------------------------------------------------------------------------
// Walk callback context for listing templates
// ---------------------------------------------------------------------------

typedef struct {
    bool first_level_only;
    const char* base_path;
    int base_len;
} ListCtx;

static void list_templates_callback(const char* path, bool is_dir, void* ctx) {
    if (!is_dir) return;
    ListCtx* lc = (ListCtx*)ctx;

    // Only show immediate subdirectories (template names)
    const char* rel = path + lc->base_len;
    // Skip leading separator
    if (*rel == '/' || *rel == '\\') rel++;

    // Skip entries that contain a sub-path separator (nested directories)
    for (const char* p = rel; *p; p++) {
        if (*p == '/' || *p == '\\') return;
    }

    if (*rel != '\0') {
        cdo_log_info("  %s", rel);
    }
}

// ---------------------------------------------------------------------------
// list_available_templates: scan ~/.cdo/templates/ and print names
// ---------------------------------------------------------------------------

static int list_available_templates(void) {
    char templates_dir[MAX_PATH_LEN];
    if (get_templates_dir(templates_dir, sizeof(templates_dir)) != 0) {
        return -1;
    }

    if (pal_path_exists(templates_dir) != 0) {
        cdo_log_info("No local templates found.");
        cdo_log_info("Template directory: %s", templates_dir);
        cdo_log_info("Place template directories there, or use a remote registry.");
        return 0;
    }

    cdo_log_info("Available templates:");
    cdo_log_info("");

    ListCtx ctx = {
        .first_level_only = true,
        .base_path = templates_dir,
        .base_len = (int)strlen(templates_dir),
    };
    pal_dir_walk(templates_dir, list_templates_callback, &ctx);

    cdo_log_info("");
    cdo_log_info("Use: cdo new <template> <project-name>");
    return 0;
}

// ---------------------------------------------------------------------------
// fetch_template: get template from local catalog or remote registry
// ---------------------------------------------------------------------------

static int fetch_template(const char* template_name, char* template_dir_out, size_t out_size) {
    char templates_dir[MAX_PATH_LEN];
    if (get_templates_dir(templates_dir, sizeof(templates_dir)) != 0) {
        return -1;
    }

    // Check local templates first
    if (pal_path_join(template_dir_out, out_size, templates_dir, template_name) != 0) {
        cdo_log_error("Path too long for template: %s", template_name);
        return -1;
    }

    if (pal_path_exists(template_dir_out) == 0) {
        cdo_log_debug("Using local template: %s", template_dir_out);
        return 0;
    }

    // Try to download from remote registry
    cdo_log_info("Template '%s' not found locally, fetching from registry...", template_name);

    char url[MAX_PATH_LEN];
    snprintf(url, sizeof(url), "%s/%s.zip", TEMPLATE_REGISTRY_URL, template_name);

    // Ensure templates directory exists
    pal_mkdir_p(templates_dir);

    char archive_path[MAX_PATH_LEN];
    snprintf(archive_path, sizeof(archive_path), "%s/%s.zip", templates_dir, template_name);

    int rc = http_download(url, archive_path, 3, NULL, NULL);
    if (rc != 0) {
        cdo_log_error("Failed to download template '%s' from registry", template_name);
        cdo_log_error("Tried: %s", url);
        cdo_log_info("Hint: place a template directory at %s/%s/", templates_dir, template_name);
        return -1;
    }

    // TODO: extract the downloaded archive into template_dir_out
    // For now, we assume the download places files directly
    cdo_log_info("Downloaded template '%s'", template_name);
    return 0;
}

// ---------------------------------------------------------------------------
// Walk callback context for template instantiation
// ---------------------------------------------------------------------------

typedef struct {
    const char*         src_base;
    int                 src_base_len;
    const char*         dest_base;
    const TemplateVar*  vars;
    int                 var_count;
    int                 error;
} InstantiateCtx;

static void instantiate_callback(const char* path, bool is_dir, void* ctx) {
    InstantiateCtx* ic = (InstantiateCtx*)ctx;
    if (ic->error != 0) return;

    // Compute relative path from source base
    const char* rel = path + ic->src_base_len;
    if (*rel == '/' || *rel == '\\') rel++;

    // Build destination path
    char dest_path[MAX_PATH_LEN];
    if (pal_path_join(dest_path, sizeof(dest_path), ic->dest_base, rel) != 0) {
        cdo_log_error("Path too long: %s/%s", ic->dest_base, rel);
        ic->error = -1;
        return;
    }

    if (is_dir) {
        // Create directory at destination
        if (pal_mkdir_p(dest_path) != 0) {
            cdo_log_error("Failed to create directory: %s", dest_path);
            ic->error = -1;
        }
        return;
    }

    // Read template file
    char* content = NULL;
    size_t content_len = 0;
    if (pal_file_read(path, &content, &content_len) != 0) {
        cdo_log_error("Failed to read template file: %s", path);
        ic->error = -1;
        return;
    }

    // Render template with variable substitution
    char* rendered = NULL;
    size_t rendered_len = 0;
    int rc = template_render(content, content_len, ic->vars, ic->var_count,
                             &rendered, &rendered_len);
    free(content);

    if (rc != 0) {
        cdo_log_warn("Template rendering failed for: %s (copying as-is)", rel);
        // Fall back to copying file as-is
        if (pal_file_read(path, &content, &content_len) != 0) {
            ic->error = -1;
            return;
        }
        rendered = content;
        rendered_len = content_len;
    }

    // Write rendered file to destination
    if (pal_file_write(dest_path, rendered, rendered_len) != 0) {
        cdo_log_error("Failed to write file: %s", dest_path);
        free(rendered);
        ic->error = -1;
        return;
    }

    free(rendered);
    cdo_log_debug("  Created: %s", rel);
}

// ---------------------------------------------------------------------------
// instantiate_template: walk template dir, render files into target
// ---------------------------------------------------------------------------

static int instantiate_template(const char* template_dir, const char* dest_dir,
                                const TemplateVar* vars, int var_count) {
    // Ensure destination exists
    if (pal_mkdir_p(dest_dir) != 0) {
        cdo_log_error("Failed to create project directory: %s", dest_dir);
        return -1;
    }

    InstantiateCtx ctx = {
        .src_base = template_dir,
        .src_base_len = (int)strlen(template_dir),
        .dest_base = dest_dir,
        .vars = vars,
        .var_count = var_count,
        .error = 0,
    };

    pal_dir_walk(template_dir, instantiate_callback, &ctx);
    return ctx.error;
}

// ---------------------------------------------------------------------------
// build_default_vars: populate standard template variables
// ---------------------------------------------------------------------------

static int build_default_vars(const char* project_name, TemplateVar* vars, int* var_count) {
    int n = 0;

    vars[n].key = "project_name";
    vars[n].value = project_name;
    n++;

    // crate_name: same as project_name but with hyphens replaced by underscores
    static char crate_name_buf[256];
    strncpy(crate_name_buf, project_name, sizeof(crate_name_buf) - 1);
    crate_name_buf[sizeof(crate_name_buf) - 1] = '\0';
    for (char* p = crate_name_buf; *p; p++) {
        if (*p == '-') *p = '_';
    }
    vars[n].key = "crate_name";
    vars[n].value = crate_name_buf;
    n++;

    // author: from environment or default
    const char* author = getenv("CDO_AUTHOR");
    if (!author) author = getenv("USER");
    if (!author) author = getenv("USERNAME");
    if (!author) author = "unknown";
    vars[n].key = "author";
    vars[n].value = author;
    n++;

    // year: current year
    static char year_buf[8];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    snprintf(year_buf, sizeof(year_buf), "%d", 1900 + tm_info->tm_year);
    vars[n].key = "year";
    vars[n].value = year_buf;
    n++;

    *var_count = n;
    return 0;
}

// ---------------------------------------------------------------------------
// cmd_new: create a new project from a template in a new directory
// ---------------------------------------------------------------------------

int cmd_new(const CliParseResult* result, void* ctx) {
    (void)ctx;

    // Handle --list flag
    if (get_arg_bool(result, "list") || has_flag_in_positionals(result, "--list")) {
        return list_available_templates();
    }

    // Validate arguments: need at least a template name (positional[0])
    if (result->positional_count < 1) {
        cdo_log_error("Usage: cdo new <template> [project-name]");
        cdo_log_info("Use --list to see available templates.");
        return -1;
    }

    const char* template_name = result->positional_values[0];
    const char* project_name = (result->positional_count >= 2)
        ? result->positional_values[1]
        : template_name;

    bool force = get_arg_bool(result, "force") || has_flag_in_positionals(result, "--force");

    // Determine target directory (project_name as subdirectory of cwd)
    char dest_dir[MAX_PATH_LEN];
    snprintf(dest_dir, sizeof(dest_dir), "%s", project_name);

    // Check if target directory is non-empty
    if (is_directory_non_empty(dest_dir)) {
        if (!force) {
            cdo_log_error("Directory '%s' is not empty.", dest_dir);
            cdo_log_info("Use --force to create the project anyway.");
            return -1;
        }
        cdo_log_warn("Directory '%s' is not empty, proceeding with --force.", dest_dir);
    }

    // Fetch the template
    char template_dir[MAX_PATH_LEN];
    if (fetch_template(template_name, template_dir, sizeof(template_dir)) != 0) {
        return -1;
    }

    // Build template variables
    TemplateVar vars[MAX_VARS];
    int var_count = 0;
    build_default_vars(project_name, vars, &var_count);

    // Instantiate template
    cdo_log_info("Creating project '%s' from template '%s'...", project_name, template_name);
    if (instantiate_template(template_dir, dest_dir, vars, var_count) != 0) {
        cdo_log_error("Failed to instantiate template.");
        return -1;
    }

    cdo_log_info("Project '%s' created successfully.", project_name);
    cdo_log_info("  cd %s", dest_dir);
    cdo_log_info("  cdo build");
    return 0;
}

// ---------------------------------------------------------------------------
// Venv: activation script generators
// ---------------------------------------------------------------------------

static int venv_generate_activate_bat(const char* cdo_dir) {
    char path[MAX_PATH_LEN];
    if (pal_path_join(path, sizeof(path), cdo_dir, "activate.bat") != 0) {
        cdo_log_error("Path too long for activate.bat");
        return -1;
    }

    const char* content =
        "@echo off\r\n"
        "REM CDo Virtual Environment Activation (Windows CMD)\r\n"
        "set \"CDO_VENV_OLD_PATH=%PATH%\"\r\n"
        "set \"CDO_VENV_OLD_PROMPT=%PROMPT%\"\r\n"
        "set \"CDO_HOME=%~dp0\"\r\n"
        "set \"CDO_VENV=%~dp0\"\r\n"
        "set \"PATH=%~dp0;%PATH%\"\r\n"
        "set \"PROMPT=(cdo) %PROMPT%\"\r\n"
        "echo CDo virtual environment activated.\r\n"
        "echo Use 'deactivate' to restore the original environment.\r\n"
        "doskey deactivate=set \"PATH=%CDO_VENV_OLD_PATH%\" $T set \"PROMPT=%CDO_VENV_OLD_PROMPT%\" $T set \"CDO_HOME=\" $T set \"CDO_VENV=\" $T set \"CDO_VENV_OLD_PATH=\" $T set \"CDO_VENV_OLD_PROMPT=\"\r\n";

    if (pal_file_write(path, content, strlen(content)) != 0) {
        cdo_log_error("Failed to write activate.bat: %s", path);
        return -1;
    }

    cdo_log_debug("Generated: activate.bat");
    return 0;
}

static int venv_generate_activate_ps1(const char* cdo_dir) {
    char path[MAX_PATH_LEN];
    if (pal_path_join(path, sizeof(path), cdo_dir, "activate.ps1") != 0) {
        cdo_log_error("Path too long for activate.ps1");
        return -1;
    }

    const char* content =
        "# CDo Virtual Environment Activation (PowerShell)\r\n"
        "$script:CDO_VENV_OLD_PATH = $env:PATH\r\n"
        "$script:CDO_VENV_OLD_PROMPT = $function:prompt\r\n"
        "\r\n"
        "$env:CDO_HOME = $PSScriptRoot\r\n"
        "$env:CDO_VENV = $PSScriptRoot\r\n"
        "$env:PATH = \"$PSScriptRoot;$env:PATH\"\r\n"
        "\r\n"
        "function global:prompt {\r\n"
        "    \"(cdo) \" + (& $script:CDO_VENV_OLD_PROMPT)\r\n"
        "}\r\n"
        "\r\n"
        "function global:deactivate {\r\n"
        "    $env:PATH = $script:CDO_VENV_OLD_PATH\r\n"
        "    Remove-Item Env:CDO_HOME -ErrorAction SilentlyContinue\r\n"
        "    Remove-Item Env:CDO_VENV -ErrorAction SilentlyContinue\r\n"
        "    $function:global:prompt = $script:CDO_VENV_OLD_PROMPT\r\n"
        "    Remove-Item Function:deactivate\r\n"
        "}\r\n"
        "\r\n"
        "Write-Host \"CDo virtual environment activated.\"\r\n"
        "Write-Host \"Use 'deactivate' to restore the original environment.\"\r\n";

    if (pal_file_write(path, content, strlen(content)) != 0) {
        cdo_log_error("Failed to write activate.ps1: %s", path);
        return -1;
    }

    cdo_log_debug("Generated: activate.ps1");
    return 0;
}

static int venv_generate_activate_sh(const char* cdo_dir) {
    char path[MAX_PATH_LEN];
    if (pal_path_join(path, sizeof(path), cdo_dir, "activate.sh") != 0) {
        cdo_log_error("Path too long for activate.sh");
        return -1;
    }

    const char* content =
        "#!/bin/sh\n"
        "# CDo Virtual Environment Activation (POSIX)\n"
        "_CDO_VENV_OLD_PATH=\"$PATH\"\n"
        "_CDO_VENV_OLD_PS1=\"${PS1:-}\"\n"
        "\n"
        "CDO_DIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
        "export CDO_HOME=\"$CDO_DIR\"\n"
        "export CDO_VENV=\"$CDO_DIR\"\n"
        "export PATH=\"$CDO_DIR:$PATH\"\n"
        "PS1=\"(cdo) ${PS1:-}\"\n"
        "export PS1\n"
        "\n"
        "deactivate() {\n"
        "    export PATH=\"$_CDO_VENV_OLD_PATH\"\n"
        "    PS1=\"$_CDO_VENV_OLD_PS1\"\n"
        "    export PS1\n"
        "    unset CDO_HOME\n"
        "    unset CDO_VENV\n"
        "    unset _CDO_VENV_OLD_PATH\n"
        "    unset _CDO_VENV_OLD_PS1\n"
        "    unset -f deactivate\n"
        "}\n"
        "\n"
        "echo \"CDo virtual environment activated.\"\n"
        "echo \"Use 'deactivate' to restore the original environment.\"\n";

    if (pal_file_write(path, content, strlen(content)) != 0) {
        cdo_log_error("Failed to write activate.sh: %s", path);
        return -1;
    }

    cdo_log_debug("Generated: activate.sh");
    return 0;
}

// ---------------------------------------------------------------------------
// venv_init: create .cdo virtual environment structure
// ---------------------------------------------------------------------------

int venv_init(const char* workspace_root) {
    char cdo_dir[MAX_PATH_LEN];

    // 1. Build .cdo directory path
    if (pal_path_join(cdo_dir, sizeof(cdo_dir), workspace_root, ".cdo") != 0) {
        cdo_log_error("Path too long for .cdo directory");
        return -1;
    }

    // 2. Create .cdo directory (pal_mkdir_p preserves existing content)
    if (pal_mkdir_p(cdo_dir) != 0) {
        cdo_log_error("Failed to create .cdo directory: %s", cdo_dir);
        return -1;
    }

    // 3. Get the path of the currently running executable
    char self_path[MAX_PATH_LEN];
    if (pal_get_executable_path(self_path, sizeof(self_path)) != 0) {
        cdo_log_error("Failed to determine current executable path");
        return -1;
    }

    // 4. Copy current executable into .cdo/
    char dest_exe[MAX_PATH_LEN];
#ifdef _WIN32
    if (pal_path_join(dest_exe, sizeof(dest_exe), cdo_dir, "cdo.exe") != 0) {
#else
    if (pal_path_join(dest_exe, sizeof(dest_exe), cdo_dir, "cdo") != 0) {
#endif
        cdo_log_error("Path too long for destination executable");
        return -1;
    }

    if (pal_file_copy(self_path, dest_exe) != 0) {
        cdo_log_error("Failed to copy executable to %s", dest_exe);
        return -1;
    }

    // 5. Generate activation scripts for all platforms
    if (venv_generate_activate_bat(cdo_dir) != 0) {
        cdo_log_error("Failed to generate activate.bat");
        return -1;
    }
    if (venv_generate_activate_ps1(cdo_dir) != 0) {
        cdo_log_error("Failed to generate activate.ps1");
        return -1;
    }
    if (venv_generate_activate_sh(cdo_dir) != 0) {
        cdo_log_error("Failed to generate activate.sh");
        return -1;
    }

    cdo_log_info("Virtual environment initialized in %s", cdo_dir);
    return 0;
}

// ---------------------------------------------------------------------------
// cmd_init: initialize a new project in the current directory
// ---------------------------------------------------------------------------

int cmd_init(const CliParseResult* result, void* ctx) {
    (void)ctx;

    // Handle --list flag
    if (get_arg_bool(result, "list") || has_flag_in_positionals(result, "--list")) {
        return list_available_templates();
    }

    bool want_venv = get_arg_bool(result, "venv");
    const char* template_name = (result->positional_count >= 1) ? result->positional_values[0] : NULL;

    // Validate: need at least a template name OR --venv flag
    if (!template_name && !want_venv) {
        cdo_log_error("Usage: cdo init <template> [--venv]");
        cdo_log_info("Use --list to see available templates.");
        cdo_log_info("Use --venv to create a virtual environment without a template.");
        return -1;
    }

    // Template instantiation (only if a template name was provided)
    if (template_name) {
        bool force = get_arg_bool(result, "force") || has_flag_in_positionals(result, "--force");

        // Use current directory as target
        const char* dest_dir = ".";

        // Check if current directory is non-empty
        if (is_directory_non_empty(dest_dir)) {
            if (!force) {
                cdo_log_error("Current directory is not empty.");
                cdo_log_info("Use --force to initialize the project anyway.");
                return -1;
            }
            cdo_log_warn("Current directory is not empty, proceeding with --force.");
        }

        // Fetch the template
        char template_dir[MAX_PATH_LEN];
        if (fetch_template(template_name, template_dir, sizeof(template_dir)) != 0) {
            return -1;
        }

        // Determine project name from positional args or fall back to template name
        const char* project_name = (result->positional_count >= 2) ? result->positional_values[1] : template_name;

        // Build template variables
        TemplateVar vars[MAX_VARS];
        int var_count = 0;
        build_default_vars(project_name, vars, &var_count);

        // Instantiate template
        cdo_log_info("Initializing project from template '%s'...", template_name);
        if (instantiate_template(template_dir, dest_dir, vars, var_count) != 0) {
            cdo_log_error("Failed to instantiate template.");
            return -1;
        }

        cdo_log_info("Project initialized successfully.");
        cdo_log_info("  cdo build");
    }

    // If --venv flag is present, initialize virtual environment
    if (want_venv) {
        int rc = venv_init(".");
        if (rc != 0) return rc;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// End of cmd_new.c
