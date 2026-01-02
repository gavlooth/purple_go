/* Purple Runtime Test Suite */
#include "../include/purple.h"
#include "test_framework.h"

/* Test counter definitions */
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;
const char* current_suite = NULL;

/* External test suites */
extern void run_constructor_tests(void);
extern void run_memory_tests(void);
extern void run_primitive_tests(void);

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("Purple Runtime Test Suite\n");
    printf("==========================\n");

    run_constructor_tests();
    run_memory_tests();
    run_primitive_tests();

    TEST_EXIT();
}
