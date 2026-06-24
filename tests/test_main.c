/*
 * test_main.c - CDo property-based test runner
 *
 * Discovers and runs test functions registered with the TEST() macro.
 * Returns 0 if all tests pass, non-zero otherwise.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vendor/theft.h"

/*============================================================================
 * Test runner infrastructure
 *============================================================================*/

#define MAX_TESTS 256

typedef struct {
    const char *name;
    int (*func)(void);
} test_entry_t;

static test_entry_t g_tests[MAX_TESTS];
static int g_test_count = 0;

/* Register a test function. Called before main via constructor or explicit init. */
static void register_test(const char *name, int (*func)(void)) {
    if (g_test_count < MAX_TESTS) {
        g_tests[g_test_count].name = name;
        g_tests[g_test_count].func = func;
        g_test_count++;
    }
}

/* Macro for defining and auto-registering a test */
#define TEST(test_name)                                             \
    static int test_name##_impl(void);                              \
    static void __attribute__((constructor)) test_name##_register(void) { \
        register_test(#test_name, test_name##_impl);                \
    }                                                               \
    static int test_name##_impl(void)

/* For compilers that don't support __attribute__((constructor)),
 * provide a manual registration fallback */
#ifdef _MSC_VER
#undef TEST
#define TEST(test_name) static int test_name##_impl(void)
#define REGISTER_TEST(test_name) register_test(#test_name, test_name##_impl)
#endif

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

/*============================================================================
 * Manual test registration for MSVC
 *============================================================================*/

#ifdef _MSC_VER
static void register_all_tests(void) {
    REGISTER_TEST(trivial_addition_commutative);
    REGISTER_TEST(trivial_arithmetic);
}
#else
static void register_all_tests(void) {
    /* Tests auto-register via __attribute__((constructor)) */
}
#endif

/*============================================================================
 * Main: discover and run all registered tests
 *============================================================================*/

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    register_all_tests();

    printf("=== CDo Property-Based Test Runner ===\n");
    printf("Running %d test(s)...\n\n", g_test_count);

    int passed = 0;
    int failed = 0;

    for (int i = 0; i < g_test_count; i++) {
        printf("  [RUN ] %s\n", g_tests[i].name);
        int result = g_tests[i].func();
        if (result == 0) {
            printf("  [PASS] %s\n", g_tests[i].name);
            passed++;
        } else {
            printf("  [FAIL] %s (returned %d)\n", g_tests[i].name, result);
            failed++;
        }
    }

    printf("\n=== Results: %d passed, %d failed, %d total ===\n",
           passed, failed, g_test_count);

    return (failed > 0) ? 1 : 0;
}
