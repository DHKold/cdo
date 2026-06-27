#ifndef CDO_UT_H
#define CDO_UT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*--- Test Registration ---*/

extern void cdo_ut_register(const char *name, int (*func)(void), bool serial);

#define TEST(test_name)                                                \
    static int test_name##_impl(void);                                 \
    static void __attribute__((constructor)) test_name##_ctor(void) {  \
        cdo_ut_register(#test_name, test_name##_impl, false);          \
    }                                                                  \
    static int test_name##_impl(void)

#define TEST_SERIAL(test_name)                                         \
    static int test_name##_impl(void);                                 \
    static void __attribute__((constructor)) test_name##_ctor(void) {  \
        cdo_ut_register(#test_name, test_name##_impl, true);           \
    }                                                                  \
    static int test_name##_impl(void)

/* MSVC fallback */
#ifdef _MSC_VER
#undef TEST
#undef TEST_SERIAL
#define TEST(test_name) static int test_name##_impl(void)
#define TEST_SERIAL(test_name) static int test_name##_impl(void)
#define REGISTER_TEST(test_name) cdo_ut_register(#test_name, test_name##_impl, false)
#define REGISTER_TEST_SERIAL(test_name) cdo_ut_register(#test_name, test_name##_impl, true)
#endif

/*--- Assertion Macros ---*/

extern void cdo_ut_record_failure(const char *file, int line,
                                   const char *expr,
                                   const char *actual, const char *expected);

#define TEST_ASSERT(cond)                                              \
    do { if (!(cond)) {                                                \
        cdo_ut_record_failure(__FILE__, __LINE__, #cond, NULL, NULL);   \
        return 1;                                                      \
    } } while(0)

#define TEST_ASSERT_EQ(a, b)                                           \
    do { if ((a) != (b)) {                                             \
        char _a_buf[64], _b_buf[64];                                   \
        snprintf(_a_buf, sizeof(_a_buf), "%lld", (long long)(a));      \
        snprintf(_b_buf, sizeof(_b_buf), "%lld", (long long)(b));      \
        cdo_ut_record_failure(__FILE__, __LINE__, #a " == " #b,        \
                              _a_buf, _b_buf);                         \
        return 1;                                                      \
    } } while(0)

#define TEST_ASSERT_NEQ(a, b)                                          \
    do { if ((a) == (b)) {                                             \
        char _a_buf[64];                                               \
        snprintf(_a_buf, sizeof(_a_buf), "%lld", (long long)(a));      \
        cdo_ut_record_failure(__FILE__, __LINE__, #a " != " #b,        \
                              _a_buf, _a_buf);                         \
        return 1;                                                      \
    } } while(0)

#define TEST_ASSERT_STR_EQ(a, b)                                       \
    do { if (strcmp((a), (b)) != 0) {                                   \
        cdo_ut_record_failure(__FILE__, __LINE__, #a " streq " #b,     \
                              (a), (b));                                \
        return 1;                                                      \
    } } while(0)

#define TEST_ASSERT_NULL(ptr)                                           \
    do { if ((ptr) != NULL) {                                           \
        cdo_ut_record_failure(__FILE__, __LINE__, #ptr " == NULL",      \
                              "(non-null)", "NULL");                    \
        return 1;                                                      \
    } } while(0)

#ifdef __cplusplus
}
#endif

#endif /* CDO_UT_H */
