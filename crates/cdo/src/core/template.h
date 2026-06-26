#ifndef CDO_CORE_TEMPLATE_H
#define CDO_CORE_TEMPLATE_H

#include <stddef.h>

typedef struct {
    const char* key;
    const char* value;
} TemplateVar;

/**
 * Process a template string, performing conditional section evaluation
 * and variable substitution. Output written to *out (caller frees).
 *
 * Supported syntax:
 *   {{variable_name}}           - replaced with value or empty string
 *   {{#if variable_name}}...{{/if}}       - included if variable is truthy
 *   {{#unless variable_name}}...{{/unless}} - included if variable is falsy
 *
 * A variable is "truthy" if it exists in vars with a non-empty value
 * that is not "false" and not "0".
 *
 * Processing order: conditionals first, then variable substitution.
 *
 * Returns 0 on success, non-zero on error (e.g., malformed template).
 */
int template_render(const char* input, size_t input_len,
                    const TemplateVar* vars, int var_count,
                    char** out, size_t* out_len);

#endif /* CDO_CORE_TEMPLATE_H */
