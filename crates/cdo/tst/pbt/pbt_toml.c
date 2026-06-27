/*
 * pbt_toml.c - TOML Property-Based Tests
 *
 * Property 1: TOML Round-Trip (Requirements 3.5, 3.4)
 * Property 2: TOML Error Location Accuracy (Requirements 3.3)
 */
#include "cdo_ut.h"
#include "vendor/theft.h"
#include "core/toml.h"
#include <math.h>
#include <inttypes.h>

/*============================================================================
 * Property 2: TOML Error Location Accuracy
 *============================================================================*/

/* A set of known-valid TOML documents used as base cases for corruption */
static const char *VALID_TOML_DOCS[] = {
    "name = \"test\"\nvalue = 42\nflag = true\n",
    "[section]\nkey = \"hello\"\narray = [1, 2, 3]\n",
    "[workspace]\nmembers = [\"crates/*\"]\n\n[workspace.settings]\nc-standard = 17\n",
    "title = \"example\"\n\n[owner]\nname = \"Tom\"\n\n[database]\nserver = \"192.168.1.1\"\nports = [8001, 8001, 8002]\nenabled = true\n",
};
static const int VALID_TOML_DOC_COUNT = 4;

typedef struct {
    char *doc;
    size_t len;
    int total_lines;
} CorruptedToml;

static int count_lines(const char *doc, size_t len) {
    int lines = 1;
    for (size_t i = 0; i < len; i++) {
        if (doc[i] == '\n') lines++;
    }
    return lines;
}

static enum theft_alloc_res
alloc_corrupted_toml(struct theft *t, void *env, void **output) {
    (void)env;

    int doc_idx = (int)theft_random_choice(t, (uint64_t)VALID_TOML_DOC_COUNT);
    const char *base = VALID_TOML_DOCS[doc_idx];
    size_t base_len = strlen(base);

    if (base_len == 0) return THEFT_ALLOC_SKIP;

    int corruption_type = (int)theft_random_choice(t, 3);

    CorruptedToml *ct = malloc(sizeof(CorruptedToml));
    if (!ct) return THEFT_ALLOC_ERROR;

    switch (corruption_type) {
    case 0: {
        size_t pos = (size_t)theft_random_choice(t, base_len + 1);
        char insert_char = (char)(theft_random_choice(t, 94) + 33);
        ct->len = base_len + 1;
        ct->doc = malloc(ct->len + 1);
        if (!ct->doc) { free(ct); return THEFT_ALLOC_ERROR; }
        memcpy(ct->doc, base, pos);
        ct->doc[pos] = insert_char;
        memcpy(ct->doc + pos + 1, base + pos, base_len - pos);
        ct->doc[ct->len] = '\0';
        break;
    }
    case 1: {
        if (base_len < 2) { free(ct); return THEFT_ALLOC_SKIP; }
        size_t pos = (size_t)theft_random_choice(t, base_len);
        ct->len = base_len - 1;
        ct->doc = malloc(ct->len + 1);
        if (!ct->doc) { free(ct); return THEFT_ALLOC_ERROR; }
        memcpy(ct->doc, base, pos);
        memcpy(ct->doc + pos, base + pos + 1, base_len - pos - 1);
        ct->doc[ct->len] = '\0';
        break;
    }
    case 2: {
        size_t pos = (size_t)theft_random_choice(t, base_len);
        ct->len = base_len;
        ct->doc = malloc(ct->len + 1);
        if (!ct->doc) { free(ct); return THEFT_ALLOC_ERROR; }
        memcpy(ct->doc, base, base_len);
        ct->doc[ct->len] = '\0';
        char new_char = (char)(theft_random_choice(t, 94) + 33);
        if (new_char == ct->doc[pos]) {
            new_char = (char)(((new_char - 33 + 1) % 94) + 33);
        }
        ct->doc[pos] = new_char;
        break;
    }
    default:
        free(ct);
        return THEFT_ALLOC_ERROR;
    }

    ct->total_lines = count_lines(ct->doc, ct->len);
    *output = ct;
    return THEFT_ALLOC_OK;
}

static void free_corrupted_toml(void *instance, void *env) {
    (void)env;
    CorruptedToml *ct = (CorruptedToml *)instance;
    if (ct) { free(ct->doc); free(ct); }
}

static void print_corrupted_toml(FILE *f, const void *instance, void *env) {
    (void)env;
    const CorruptedToml *ct = (const CorruptedToml *)instance;
    fprintf(f, "CorruptedToml(len=%zu, lines=%d): \"", ct->len, ct->total_lines);
    size_t print_len = ct->len < 80 ? ct->len : 80;
    for (size_t i = 0; i < print_len; i++) {
        char c = ct->doc[i];
        if (c == '\n') fprintf(f, "\\n");
        else if (c == '\t') fprintf(f, "\\t");
        else if (c >= 32 && c < 127) fputc(c, f);
        else fprintf(f, "\\x%02x", (unsigned char)c);
    }
    if (ct->len > 80) fprintf(f, "...");
    fprintf(f, "\"");
}

static struct theft_type_info corrupted_toml_type_info = {
    .alloc  = alloc_corrupted_toml,
    .free   = free_corrupted_toml,
    .print  = print_corrupted_toml,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

static enum theft_trial_res
prop_toml_error_location_accuracy(struct theft *t, void *arg1) {
    (void)t;
    CorruptedToml *ct = (CorruptedToml *)arg1;

    TomlTable *out = NULL;
    TomlError err = {0};

    int result = toml_parse(ct->doc, ct->len, &out, &err);

    if (result == 0) {
        if (out) toml_free(out);
        return THEFT_TRIAL_SKIP;
    }

    if (err.line < 1 || err.line > ct->total_lines) {
        fprintf(stderr, "  ERROR: line=%d out of range [1, %d]\n",
                err.line, ct->total_lines);
        return THEFT_TRIAL_FAIL;
    }

    if (err.col < 1) {
        fprintf(stderr, "  ERROR: col=%d < 1\n", err.col);
        return THEFT_TRIAL_FAIL;
    }

    if (err.message[0] == '\0') {
        fprintf(stderr, "  ERROR: empty error message\n");
        return THEFT_TRIAL_FAIL;
    }

    if (out) toml_free(out);
    return THEFT_TRIAL_PASS;
}

TEST(prop_toml_error_location_accuracy) {
    struct theft_run_config cfg = {
        .name = "toml_error_location_accuracy",
        .prop = { .prop1 = prop_toml_error_location_accuracy },
        .type_info = { &corrupted_toml_type_info },
        .seed = 20250101,
        .trials = 500,
    };
    enum theft_run_res res = theft_run(&cfg);
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}

/*============================================================================
 * Property 1: TOML Round-Trip
 *============================================================================*/

/*--- Helper: compare two TomlValue trees for structural equality ---*/

static bool toml_values_equal(const TomlValue* a, const TomlValue* b);

static bool toml_tables_equal(const TomlTable* a, const TomlTable* b) {
    if (a == NULL && b == NULL) return true;
    if (a == NULL || b == NULL) return false;
    if (a->count != b->count) return false;

    for (TomlEntry* ea = a->head; ea; ea = ea->next) {
        TomlEntry* eb = NULL;
        for (TomlEntry* e = b->head; e; e = e->next) {
            if (strcmp(e->key, ea->key) == 0) { eb = e; break; }
        }
        if (!eb) return false;
        if (!toml_values_equal(ea->value, eb->value)) return false;
    }
    return true;
}

static bool toml_arrays_equal(const TomlArray* a, const TomlArray* b) {
    if (a == NULL && b == NULL) return true;
    if (a == NULL || b == NULL) return false;
    if (a->count != b->count) return false;
    for (int i = 0; i < a->count; i++) {
        if (!toml_values_equal(a->items[i], b->items[i])) return false;
    }
    return true;
}

static bool toml_values_equal(const TomlValue* a, const TomlValue* b) {
    if (a == NULL && b == NULL) return true;
    if (a == NULL || b == NULL) return false;

    bool a_is_table = (a->type == TOML_TABLE || a->type == TOML_INLINE_TABLE);
    bool b_is_table = (b->type == TOML_TABLE || b->type == TOML_INLINE_TABLE);
    if (a_is_table && b_is_table) {
        return toml_tables_equal(a->as.table, b->as.table);
    }

    if (a->type != b->type) return false;

    switch (a->type) {
    case TOML_STRING:
    case TOML_DATETIME:
        return strcmp(a->as.string, b->as.string) == 0;
    case TOML_INTEGER:
        return a->as.integer == b->as.integer;
    case TOML_FLOAT:
        if (isnan(a->as.floating) && isnan(b->as.floating)) return true;
        return a->as.floating == b->as.floating;
    case TOML_BOOL:
        return a->as.boolean == b->as.boolean;
    case TOML_ARRAY:
        return toml_arrays_equal(a->as.array, b->as.array);
    case TOML_TABLE:
    case TOML_INLINE_TABLE:
        return toml_tables_equal(a->as.table, b->as.table);
    }
    return false;
}

/*--- Generator: produce random valid TOML document text ---*/

typedef struct {
    char*  buf;
    size_t len;
    size_t cap;
} GenBuf;

static void genbuf_init(GenBuf* g) { g->buf = NULL; g->len = 0; g->cap = 0; }

static void genbuf_append(GenBuf* g, const char* str, size_t slen) {
    while (g->len + slen + 1 > g->cap) {
        size_t new_cap = g->cap == 0 ? 256 : g->cap * 2;
        char* nb = (char*)realloc(g->buf, new_cap);
        if (!nb) return;
        g->buf = nb;
        g->cap = new_cap;
    }
    memcpy(g->buf + g->len, str, slen);
    g->len += slen;
    g->buf[g->len] = '\0';
}

static void genbuf_append_str(GenBuf* g, const char* s) {
    genbuf_append(g, s, strlen(s));
}

static void genbuf_free(GenBuf* g) { free(g->buf); g->buf = NULL; g->len = 0; g->cap = 0; }

static void gen_bare_key(struct theft *t, GenBuf* g) {
    static const char key_chars[] = "abcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t key_len = (size_t)(theft_random_choice(t, 8) + 1);
    char first = (char)('a' + theft_random_choice(t, 26));
    genbuf_append(g, &first, 1);
    for (size_t i = 1; i < key_len; i++) {
        char c = key_chars[theft_random_choice(t, sizeof(key_chars) - 1)];
        genbuf_append(g, &c, 1);
    }
}

static void gen_string_value(struct theft *t, GenBuf* g) {
    static const char safe_chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789 .,;:!?()-_/";
    genbuf_append_str(g, "\"");
    size_t len = (size_t)theft_random_choice(t, 20);
    for (size_t i = 0; i < len; i++) {
        char c = safe_chars[theft_random_choice(t, sizeof(safe_chars) - 1)];
        genbuf_append(g, &c, 1);
    }
    genbuf_append_str(g, "\"");
}

static void gen_value(struct theft *t, GenBuf* g, int depth) {
    int max_type = (depth < 2) ? 5 : 4;
    int vtype = (int)theft_random_choice(t, (uint64_t)max_type);

    switch (vtype) {
    case 0:
        gen_string_value(t, g);
        break;
    case 1: {
        char num[32];
        int64_t val = (int64_t)theft_random_bits(t, 20) - 500000;
        snprintf(num, sizeof(num), "%" PRId64, val);
        genbuf_append_str(g, num);
        break;
    }
    case 2: {
        char num[64];
        int int_part = (int)theft_random_choice(t, 2000) - 1000;
        int frac_part = (int)theft_random_choice(t, 100);
        snprintf(num, sizeof(num), "%d.%02d", int_part, frac_part);
        genbuf_append_str(g, num);
        break;
    }
    case 3:
        genbuf_append_str(g, theft_random_choice(t, 2) ? "true" : "false");
        break;
    case 4: {
        genbuf_append_str(g, "[");
        int arr_len = (int)theft_random_choice(t, 4);
        for (int i = 0; i < arr_len; i++) {
            if (i > 0) genbuf_append_str(g, ", ");
            gen_value(t, g, depth + 1);
        }
        genbuf_append_str(g, "]");
        break;
    }
    default:
        genbuf_append_str(g, "0");
        break;
    }
}

static void gen_toml_doc(struct theft *t, GenBuf* g) {
    int top_count = (int)(theft_random_choice(t, 4) + 1);
    for (int i = 0; i < top_count; i++) {
        gen_bare_key(t, g);
        genbuf_append_str(g, " = ");
        gen_value(t, g, 0);
        genbuf_append_str(g, "\n");
    }

    int table_count = (int)theft_random_choice(t, 3);
    for (int i = 0; i < table_count; i++) {
        genbuf_append_str(g, "\n[");
        gen_bare_key(t, g);
        genbuf_append_str(g, "]\n");
        int kv_count = (int)(theft_random_choice(t, 3) + 1);
        for (int j = 0; j < kv_count; j++) {
            gen_bare_key(t, g);
            genbuf_append_str(g, " = ");
            gen_value(t, g, 0);
            genbuf_append_str(g, "\n");
        }
    }
}

static enum theft_alloc_res
alloc_toml_doc(struct theft *t, void *env, void **output) {
    (void)env;
    GenBuf g;
    genbuf_init(&g);
    gen_toml_doc(t, &g);
    if (!g.buf) return THEFT_ALLOC_ERROR;
    *output = g.buf;
    return THEFT_ALLOC_OK;
}

static void free_toml_doc(void *instance, void *env) {
    (void)env;
    free(instance);
}

static void print_toml_doc(FILE *f, const void *instance, void *env) {
    (void)env;
    const char *doc = (const char *)instance;
    size_t len = strlen(doc);
    if (len > 200) {
        fprintf(f, "\"%.200s...\" (len=%zu)", doc, len);
    } else {
        fprintf(f, "\"%s\"", doc);
    }
}

static struct theft_type_info toml_doc_type_info = {
    .alloc  = alloc_toml_doc,
    .free   = free_toml_doc,
    .print  = print_toml_doc,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

static enum theft_trial_res
prop_toml_round_trip(struct theft *t, void *arg1) {
    (void)t;
    const char *toml_text = (const char *)arg1;
    size_t text_len = strlen(toml_text);

    TomlTable *parsed1 = NULL;
    TomlError err1 = {0};
    int rc = toml_parse(toml_text, text_len, &parsed1, &err1);
    if (rc != 0) return THEFT_TRIAL_SKIP;

    char *serialized = NULL;
    size_t serialized_len = 0;
    rc = toml_serialize(parsed1, &serialized, &serialized_len);
    if (rc != 0) {
        toml_free(parsed1);
        return THEFT_TRIAL_SKIP;
    }

    TomlTable *parsed2 = NULL;
    TomlError err2 = {0};
    rc = toml_parse(serialized, serialized_len, &parsed2, &err2);
    if (rc != 0) {
        fprintf(stderr, "  Re-parse failed: %s (line %d, col %d)\n",
                err2.message, err2.line, err2.col);
        fprintf(stderr, "  Serialized text:\n%s\n", serialized);
        free(serialized);
        toml_free(parsed1);
        return THEFT_TRIAL_FAIL;
    }

    TomlValue v1 = { .type = TOML_TABLE, .as = { .table = parsed1 } };
    TomlValue v2 = { .type = TOML_TABLE, .as = { .table = parsed2 } };
    bool equal = toml_values_equal(&v1, &v2);

    free(serialized);
    toml_free(parsed1);
    toml_free(parsed2);

    return equal ? THEFT_TRIAL_PASS : THEFT_TRIAL_FAIL;
}

TEST(prop_toml_round_trip) {
    struct theft_run_config cfg = {
        .name = "toml_round_trip",
        .prop = { .prop1 = prop_toml_round_trip },
        .type_info = { &toml_doc_type_info },
        .seed = 77777,
        .trials = 500,
    };
    enum theft_run_res res = theft_run(&cfg);
    if (res == THEFT_RUN_SKIP) return 0;
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}
