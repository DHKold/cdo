#include "core/template.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* --------------------------------------------------------------------------
 * memmem polyfill (not available on all platforms, especially Windows/MSVC)
 * -------------------------------------------------------------------------- */

static const void* tmpl_memmem(const void* haystack, size_t haystack_len,
                               const void* needle, size_t needle_len) {
    if (needle_len == 0) return haystack;
    if (haystack_len < needle_len) return NULL;

    const char* h = (const char*)haystack;
    const char* n = (const char*)needle;
    size_t limit = haystack_len - needle_len;

    for (size_t i = 0; i <= limit; i++) {
        if (memcmp(h + i, n, needle_len) == 0) {
            return (const void*)(h + i);
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/* Dynamic buffer for building output */
typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} Buffer;

static int buf_init(Buffer* b, size_t initial_cap) {
    b->data = (char*)malloc(initial_cap);
    if (!b->data) return -1;
    b->len = 0;
    b->cap = initial_cap;
    return 0;
}

static int buf_ensure(Buffer* b, size_t additional) {
    if (b->len + additional <= b->cap) return 0;
    size_t new_cap = b->cap * 2;
    while (new_cap < b->len + additional) {
        new_cap *= 2;
    }
    char* tmp = (char*)realloc(b->data, new_cap);
    if (!tmp) return -1;
    b->data = tmp;
    b->cap = new_cap;
    return 0;
}

static int buf_append(Buffer* b, const char* src, size_t n) {
    if (n == 0) return 0;
    if (buf_ensure(b, n) != 0) return -1;
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return 0;
}

static void buf_free(Buffer* b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

/* Look up a variable by key. Returns NULL if not found. */
static const char* var_lookup(const TemplateVar* vars, int var_count,
                              const char* key, size_t key_len) {
    for (int i = 0; i < var_count; i++) {
        if (strlen(vars[i].key) == key_len &&
            memcmp(vars[i].key, key, key_len) == 0) {
            return vars[i].value;
        }
    }
    return NULL;
}

/* Check if a variable is truthy:
 * - exists in vars
 * - value is not empty string
 * - value is not "false"
 * - value is not "0"
 */
static bool var_is_truthy(const TemplateVar* vars, int var_count,
                          const char* key, size_t key_len) {
    const char* val = var_lookup(vars, var_count, key, key_len);
    if (!val) return false;
    if (val[0] == '\0') return false;
    if (strcmp(val, "false") == 0) return false;
    if (strcmp(val, "0") == 0) return false;
    return true;
}

/* --------------------------------------------------------------------------
 * Phase 1: Process conditional sections ({{#if ...}} and {{#unless ...}})
 *
 * Supports nesting. Processes from input, writes result to output buffer.
 * Returns 0 on success, non-zero on error (unclosed conditionals).
 * -------------------------------------------------------------------------- */

static int process_conditionals(const char* input, size_t input_len,
                                const TemplateVar* vars, int var_count,
                                Buffer* out) {
    const char* p = input;
    const char* end = input + input_len;

    while (p < end) {
        /* Look for {{ */
        const char* open = (const char*)tmpl_memmem(p, (size_t)(end - p), "{{", 2);
        if (!open) {
            /* No more tags, copy remainder */
            if (buf_append(out, p, (size_t)(end - p)) != 0) return -1;
            break;
        }

        /* Check if this is a conditional tag */
        const char* tag_start = open + 2;
        size_t remaining = (size_t)(end - tag_start);

        /* Check for {{#if ...}} */
        if (remaining >= 4 && memcmp(tag_start, "#if ", 4) == 0) {
            /* Copy text before this tag */
            if (buf_append(out, p, (size_t)(open - p)) != 0) return -1;

            /* Extract variable name */
            const char* name_start = tag_start + 4;
            /* skip leading whitespace */
            while (name_start < end && *name_start == ' ') name_start++;
            const char* close = (const char*)tmpl_memmem(name_start,
                (size_t)(end - name_start), "}}", 2);
            if (!close) return -1; /* Malformed: unclosed {{ */

            /* Trim trailing whitespace from name */
            const char* name_end = close;
            while (name_end > name_start && *(name_end - 1) == ' ') name_end--;

            size_t name_len = (size_t)(name_end - name_start);
            bool truthy = var_is_truthy(vars, var_count, name_start, name_len);

            /* Find matching {{/if}} accounting for nesting */
            const char* body_start = close + 2;
            const char* scan = body_start;
            int depth = 1;
            const char* body_end = NULL;
            const char* after_end_tag = NULL;

            while (scan < end && depth > 0) {
                const char* next = (const char*)tmpl_memmem(scan,
                    (size_t)(end - scan), "{{", 2);
                if (!next) return -1; /* Unclosed conditional */

                const char* nt = next + 2;
                size_t nt_rem = (size_t)(end - nt);

                if (nt_rem >= 4 && memcmp(nt, "#if ", 4) == 0) {
                    depth++;
                    scan = nt + 4;
                } else if (nt_rem >= 3 && memcmp(nt, "/if", 3) == 0) {
                    /* Find the closing }} */
                    const char* closing = (const char*)tmpl_memmem(nt,
                        (size_t)(end - nt), "}}", 2);
                    if (!closing) return -1;
                    depth--;
                    if (depth == 0) {
                        body_end = next;
                        after_end_tag = closing + 2;
                    } else {
                        scan = closing + 2;
                    }
                } else {
                    scan = next + 2;
                }
            }

            if (depth != 0) return -1; /* Unclosed {{#if}} */

            if (truthy) {
                /* Recursively process the body for nested conditionals */
                if (process_conditionals(body_start, (size_t)(body_end - body_start),
                                         vars, var_count, out) != 0) {
                    return -1;
                }
            }
            /* If falsy, skip the body entirely */

            p = after_end_tag;

        } else if (remaining >= 8 && memcmp(tag_start, "#unless ", 8) == 0) {
            /* Copy text before this tag */
            if (buf_append(out, p, (size_t)(open - p)) != 0) return -1;

            /* Extract variable name */
            const char* name_start = tag_start + 8;
            while (name_start < end && *name_start == ' ') name_start++;
            const char* close = (const char*)tmpl_memmem(name_start,
                (size_t)(end - name_start), "}}", 2);
            if (!close) return -1;

            const char* name_end = close;
            while (name_end > name_start && *(name_end - 1) == ' ') name_end--;

            size_t name_len = (size_t)(name_end - name_start);
            bool truthy = var_is_truthy(vars, var_count, name_start, name_len);

            /* Find matching {{/unless}} accounting for nesting */
            const char* body_start = close + 2;
            const char* scan = body_start;
            int depth = 1;
            const char* body_end = NULL;
            const char* after_end_tag = NULL;

            while (scan < end && depth > 0) {
                const char* next = (const char*)tmpl_memmem(scan,
                    (size_t)(end - scan), "{{", 2);
                if (!next) return -1;

                const char* nt = next + 2;
                size_t nt_rem = (size_t)(end - nt);

                if (nt_rem >= 8 && memcmp(nt, "#unless ", 8) == 0) {
                    depth++;
                    scan = nt + 8;
                } else if (nt_rem >= 7 && memcmp(nt, "/unless", 7) == 0) {
                    const char* closing = (const char*)tmpl_memmem(nt,
                        (size_t)(end - nt), "}}", 2);
                    if (!closing) return -1;
                    depth--;
                    if (depth == 0) {
                        body_end = next;
                        after_end_tag = closing + 2;
                    } else {
                        scan = closing + 2;
                    }
                } else {
                    scan = next + 2;
                }
            }

            if (depth != 0) return -1; /* Unclosed {{#unless}} */

            if (!truthy) {
                /* Include body when variable is falsy */
                if (process_conditionals(body_start, (size_t)(body_end - body_start),
                                         vars, var_count, out) != 0) {
                    return -1;
                }
            }

            p = after_end_tag;

        } else {
            /* Not a conditional tag — copy the {{ and continue scanning */
            if (buf_append(out, p, (size_t)(open + 2 - p)) != 0) return -1;
            p = open + 2;
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * Phase 2: Variable substitution
 *
 * Replace all remaining {{variable_name}} with their values or empty string.
 * -------------------------------------------------------------------------- */

static int substitute_variables(const char* input, size_t input_len,
                                const TemplateVar* vars, int var_count,
                                Buffer* out) {
    const char* p = input;
    const char* end = input + input_len;

    while (p < end) {
        const char* open = (const char*)tmpl_memmem(p, (size_t)(end - p), "{{", 2);
        if (!open) {
            if (buf_append(out, p, (size_t)(end - p)) != 0) return -1;
            break;
        }

        /* Copy text before {{ */
        if (buf_append(out, p, (size_t)(open - p)) != 0) return -1;

        /* Find closing }} */
        const char* name_start = open + 2;
        const char* close = (const char*)tmpl_memmem(name_start,
            (size_t)(end - name_start), "}}", 2);
        if (!close) {
            /* No closing }}, copy the {{ literally */
            if (buf_append(out, "{{", 2) != 0) return -1;
            p = open + 2;
            continue;
        }

        /* Extract and trim variable name */
        const char* ns = name_start;
        while (ns < close && *ns == ' ') ns++;
        const char* ne = close;
        while (ne > ns && *(ne - 1) == ' ') ne--;

        size_t name_len = (size_t)(ne - ns);

        /* Look up and substitute */
        const char* val = var_lookup(vars, var_count, ns, name_len);
        if (val) {
            size_t val_len = strlen(val);
            if (buf_append(out, val, val_len) != 0) return -1;
        }
        /* If not found, replace with empty string (just skip) */

        p = close + 2;
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int template_render(const char* input, size_t input_len,
                    const TemplateVar* vars, int var_count,
                    char** out, size_t* out_len) {
    if (!input || !out || !out_len) return -1;
    if (var_count > 0 && !vars) return -1;

    /* Handle empty input */
    if (input_len == 0) {
        *out = (char*)malloc(1);
        if (!*out) return -1;
        (*out)[0] = '\0';
        *out_len = 0;
        return 0;
    }

    /* Phase 1: Process conditional sections */
    Buffer phase1;
    if (buf_init(&phase1, input_len + 64) != 0) return -1;

    int rc = process_conditionals(input, input_len, vars, var_count, &phase1);
    if (rc != 0) {
        buf_free(&phase1);
        return rc;
    }

    /* Phase 2: Substitute variables */
    Buffer phase2;
    if (buf_init(&phase2, phase1.len + 64) != 0) {
        buf_free(&phase1);
        return -1;
    }

    rc = substitute_variables(phase1.data, phase1.len, vars, var_count, &phase2);
    buf_free(&phase1);

    if (rc != 0) {
        buf_free(&phase2);
        return rc;
    }

    /* Null-terminate for convenience */
    if (buf_ensure(&phase2, 1) != 0) {
        buf_free(&phase2);
        return -1;
    }
    phase2.data[phase2.len] = '\0';

    *out = phase2.data;
    *out_len = phase2.len;
    return 0;
}
