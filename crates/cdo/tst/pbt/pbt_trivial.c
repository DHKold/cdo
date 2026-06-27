/*
 * pbt_trivial.c - Trivial property-based tests (sanity checks)
 *
 * Verifies the test framework and theft PBT library work correctly.
 */
#include "cdo_ut.h"
#include "vendor/theft.h"

/*============================================================================
 * Trivial test to verify the framework works
 *============================================================================*/

/* Property: addition is commutative for random integers */
static enum theft_alloc_res
alloc_int(struct theft *t, void *env, void **output) {
    (void)env;
    int *val = malloc(sizeof(int));
    if (!val) return THEFT_ALLOC_ERROR;
    *val = (int)theft_random_bits(t, 31);
    /* Mix in sign bit */
    if (theft_random_bits(t, 1)) *val = -*val;
    *output = val;
    return THEFT_ALLOC_OK;
}

static void free_int(void *instance, void *env) {
    (void)env;
    free(instance);
}

static void print_int(FILE *f, const void *instance, void *env) {
    (void)env;
    fprintf(f, "%d", *(const int *)instance);
}

static enum theft_trial_res
prop_addition_commutative(struct theft *t, void *arg1, void *arg2) {
    (void)t;
    int a = *(int *)arg1;
    int b = *(int *)arg2;
    /* a + b == b + a */
    if (a + b == b + a) {
        return THEFT_TRIAL_PASS;
    }
    return THEFT_TRIAL_FAIL;
}

static struct theft_type_info int_type_info = {
    .alloc = alloc_int,
    .free  = free_int,
    .print = print_int,
    .hash  = NULL,
    .shrink = NULL,
    .env   = NULL,
};

TEST(trivial_addition_commutative) {
    struct theft_run_config cfg = {
        .name = "addition_commutative",
        .prop = { .prop2 = prop_addition_commutative },
        .type_info = { &int_type_info, &int_type_info },
        .seed = 12345,
        .trials = 100,
    };
    enum theft_run_res res = theft_run(&cfg);
    return (res == THEFT_RUN_PASS) ? 0 : 1;
}

/* Simple example-based test: 2 + 2 == 4 */
TEST(trivial_arithmetic) {
    if (2 + 2 != 4) return 1;
    if (0 + 0 != 0) return 1;
    if (-1 + 1 != 0) return 1;
    return 0;
}
