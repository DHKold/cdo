// crates/cdo_pbt/src/test_harness.h
#ifndef CDO_TEST_HARNESS_H
#define CDO_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward-declare register_test (defined in test_main.c) */
extern void register_test(const char *name, int (*func)(void));

/* Auto-registering test macro (GCC/Clang constructor attribute) */
#define TEST(test_name)                                             \
    static int test_name##_impl(void);                              \
    static void __attribute__((constructor)) test_name##_register(void) { \
        register_test(#test_name, test_name##_impl);                \
    }                                                               \
    static int test_name##_impl(void)

/* MSVC fallback: manual registration required */
#ifdef _MSC_VER
#undef TEST
#define TEST(test_name) static int test_name##_impl(void)
#define REGISTER_TEST(test_name) register_test(#test_name, test_name##_impl)
#endif

/* Simple assertion helpers */
#define TEST_ASSERT(cond) \
    do { if (!(cond)) { \
        fprintf(stderr, "  ASSERT FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } } while(0)

#define TEST_ASSERT_EQ(a, b) \
    do { if ((a) != (b)) { \
        fprintf(stderr, "  ASSERT_EQ FAILED: %s != %s (%s:%d)\n", #a, #b, __FILE__, __LINE__); \
        return 1; \
    } } while(0)

#define TEST_ASSERT_STR_EQ(a, b) \
    do { if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  ASSERT_STR_EQ FAILED: \"%s\" != \"%s\" (%s:%d)\n", (a), (b), __FILE__, __LINE__); \
        return 1; \
    } } while(0)

#endif // CDO_TEST_HARNESS_H
