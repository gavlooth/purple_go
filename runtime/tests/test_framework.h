/* Purple Runtime Test Framework */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Test counters - defined in test_main.c */
extern int tests_run;
extern int tests_passed;
extern int tests_failed;
extern const char* current_suite;

/* Colors for output */
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define RESET   "\033[0m"

/* Test macros */
#define TEST_SUITE(name) \
    do { \
        current_suite = name; \
        printf("\n" YELLOW "=== %s ===" RESET "\n", name); \
    } while(0)

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %s: ", name); \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        printf(GREEN "PASS" RESET "\n"); \
    } while(0)

#define FAIL(msg) \
    do { \
        tests_failed++; \
        printf(RED "FAIL" RESET " - %s\n", msg); \
    } while(0)

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            FAIL(#cond); \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            char _msg[256]; \
            snprintf(_msg, sizeof(_msg), "%s != %s (got %ld, expected %ld)", #a, #b, (long)(a), (long)(b)); \
            FAIL(_msg); \
            return; \
        } \
    } while(0)

#define ASSERT_EQ_FLOAT(a, b, eps) \
    do { \
        if (fabs((a) - (b)) > (eps)) { \
            char _msg[256]; \
            snprintf(_msg, sizeof(_msg), "%s != %s (got %g, expected %g)", #a, #b, (double)(a), (double)(b)); \
            FAIL(_msg); \
            return; \
        } \
    } while(0)

#define ASSERT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            FAIL(#ptr " should be NULL"); \
            return; \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            FAIL(#ptr " should not be NULL"); \
            return; \
        } \
    } while(0)

#define ASSERT_STR_EQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            char _msg[256]; \
            snprintf(_msg, sizeof(_msg), "'%s' != '%s'", (a), (b)); \
            FAIL(_msg); \
            return; \
        } \
    } while(0)

#define RUN_TEST(fn) \
    do { \
        TEST(#fn); \
        fn(); \
        if (tests_passed == tests_run) { \
            /* Already passed */ \
        } \
    } while(0)

/* Summary */
#define TEST_SUMMARY() \
    do { \
        printf("\n" YELLOW "=== Summary ===" RESET "\n"); \
        printf("  Total:  %d\n", tests_run); \
        printf("  " GREEN "Passed: %d" RESET "\n", tests_passed); \
        if (tests_failed > 0) { \
            printf("  " RED "Failed: %d" RESET "\n", tests_failed); \
        } else { \
            printf("  Failed: %d\n", tests_failed); \
        } \
        printf("\n"); \
    } while(0)

#define TEST_EXIT() \
    do { \
        TEST_SUMMARY(); \
        return tests_failed > 0 ? 1 : 0; \
    } while(0)

#endif /* TEST_FRAMEWORK_H */
