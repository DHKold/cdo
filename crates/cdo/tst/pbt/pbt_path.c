/*
 * pbt_path.c - Property: Path Normalization Idempotence
 * Validates: Requirements 15.5
 *
 * For any file path string (including mixed / and \ separators),
 * normalizing twice produces the same result as normalizing once:
 * normalize(normalize(p)) == normalize(p)
 */
#include "cdo_ut.h"
#include "vendor/theft.h"
#include "pal/pal.h"

/* Allocate a random path-like string with mixed separators */
static enum theft_alloc_res
alloc_path_string(struct theft *t, void *env, void **output) {
    (void)env;

    /* Generate a path of length 1..128 */
    size_t len = (size_t)(theft_random_choice(t, 128) + 1);
    char *path = malloc(len + 1);
    if (!path) return THEFT_ALLOC_ERROR;

    /* Characters that can appear in path strings */
    static const char path_chars[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        "/\\..._- ";

    size_t charset_len = sizeof(path_chars) - 1;

    for (size_t i = 0; i < len; i++) {
        uint64_t idx = theft_random_choice(t, charset_len);
        path[i] = path_chars[idx];
    }
    path[len] = '\0';

    *output = path;
    return THEFT_ALLOC_OK;
}

static void free_path_string(void *instance, void *env) {
    (void)env;
    free(instance);
}

static void print_path_string(FILE *f, const void *instance, void *env) {
    (void)env;
    fprintf(f, "\"%s\"", (const char *)instance);
}

static enum theft_trial_res
prop_path_normalize_idempotent(struct theft *t, void *arg1) {
    (void)t;
    const char *original = (const char *)arg1;

    /* First normalization */
    size_t len = strlen(original);
    char *once = malloc(len + 1);
    if (!once) return THEFT_TRIAL_ERROR;
    memcpy(once, original, len + 1);
    pal_path_normalize(once);

    /* Second normalization */
    size_t once_len = strlen(once);
    char *twice = malloc(once_len + 1);
    if (!twice) { free(once); return THEFT_TRIAL_ERROR; }
    memcpy(twice, once, once_len + 1);
    pal_path_normalize(twice);

    /* Idempotence: normalize(normalize(p)) == normalize(p) */
    enum theft_trial_res res = THEFT_TRIAL_PASS;
    if (strcmp(once, twice) != 0) {
        res = THEFT_TRIAL_FAIL;
    }

    free(once);
    free(twice);
    return res;
}

static struct theft_type_info path_string_type_info = {
    .alloc  = alloc_path_string,
    .free   = free_path_string,
    .print  = print_path_string,
    .hash   = NULL,
    .shrink = NULL,
    .env    = NULL,
};

TEST(prop_path_normalization_idempotence) {
    struct theft_run_config cfg = {
        .name = "path_normalization_idempotence",
        .prop = { .prop1 = prop_path_normalize_idempotent },
        .type_info = { &path_string_type_info },
        .seed = 54321,
        .trials = 1000,
    };
    enum theft_run_res res = theft_run(&cfg);
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}
