/* test_closures.c - Comprehensive closure operation tests */
#include "test_framework.h"

/* Test closure functions */
static Obj* return_42(Obj** captures, Obj** args, int nargs) {
    (void)captures; (void)args; (void)nargs;
    return mk_int(42);
}

static Obj* return_first_arg(Obj** captures, Obj** args, int nargs) {
    (void)captures;
    if (nargs < 1 || !args[0]) return NULL;
    inc_ref(args[0]);
    return args[0];
}

static Obj* add_args(Obj** captures, Obj** args, int nargs) {
    (void)captures;
    if (nargs < 2) return mk_int(0);
    long a = args[0] ? obj_to_int(args[0]) : 0;
    long b = args[1] ? obj_to_int(args[1]) : 0;
    return mk_int(a + b);
}

static Obj* return_captured(Obj** captures, Obj** args, int nargs) {
    (void)args; (void)nargs;
    if (!captures || !captures[0]) return NULL;
    inc_ref(captures[0]);
    return captures[0];
}

static Obj* add_captured_to_arg(Obj** captures, Obj** args, int nargs) {
    if (!captures || !captures[0]) return mk_int(0);
    if (nargs < 1 || !args[0]) return mk_int(0);
    long cap = obj_to_int(captures[0]);
    long arg = obj_to_int(args[0]);
    return mk_int(cap + arg);
}

static Obj* sum_three_captures(Obj** captures, Obj** args, int nargs) {
    (void)args; (void)nargs;
    if (!captures) return mk_int(0);
    long sum = 0;
    for (int i = 0; i < 3; i++) {
        if (captures[i]) sum += obj_to_int(captures[i]);
    }
    return mk_int(sum);
}

static Obj* multiply_all_args(Obj** captures, Obj** args, int nargs) {
    (void)captures;
    long product = 1;
    for (int i = 0; i < nargs; i++) {
        if (args[i]) product *= obj_to_int(args[i]);
    }
    return mk_int(product);
}

static Obj* return_arg_count(Obj** captures, Obj** args, int nargs) {
    (void)captures; (void)args;
    return mk_int(nargs);
}

/* ========== mk_closure tests ========== */

void test_mk_closure_no_captures(void) {
    Obj* closure = mk_closure(return_42, NULL, NULL, 0, 0);
    ASSERT_NOT_NULL(closure);
    ASSERT_EQ(closure->tag, TAG_CLOSURE);
    dec_ref(closure);
}

void test_mk_closure_with_one_capture(void) {
    Obj* cap = mk_int(100);
    Obj* caps[1] = {cap};
    Obj* closure = mk_closure(return_captured, caps, NULL, 1, 0);
    ASSERT_NOT_NULL(closure);
    ASSERT_EQ(closure->tag, TAG_CLOSURE);
    dec_ref(closure);
    dec_ref(cap);
}

void test_mk_closure_with_multiple_captures(void) {
    Obj* cap1 = mk_int(10);
    Obj* cap2 = mk_int(20);
    Obj* cap3 = mk_int(30);
    Obj* caps[3] = {cap1, cap2, cap3};
    Obj* closure = mk_closure(sum_three_captures, caps, NULL, 3, 0);
    ASSERT_NOT_NULL(closure);
    dec_ref(closure);
    dec_ref(cap1);
    dec_ref(cap2);
    dec_ref(cap3);
}

void test_mk_closure_arity_zero(void) {
    Obj* closure = mk_closure(return_42, NULL, NULL, 0, 0);
    ASSERT_NOT_NULL(closure);
    dec_ref(closure);
}

void test_mk_closure_arity_one(void) {
    Obj* closure = mk_closure(return_first_arg, NULL, NULL, 0, 1);
    ASSERT_NOT_NULL(closure);
    dec_ref(closure);
}

void test_mk_closure_arity_two(void) {
    Obj* closure = mk_closure(add_args, NULL, NULL, 0, 2);
    ASSERT_NOT_NULL(closure);
    dec_ref(closure);
}

void test_mk_closure_variadic(void) {
    Obj* closure = mk_closure(multiply_all_args, NULL, NULL, 0, -1);
    ASSERT_NOT_NULL(closure);
    dec_ref(closure);
}

/* ========== call_closure tests ========== */

void test_call_closure_no_args(void) {
    Obj* closure = mk_closure(return_42, NULL, NULL, 0, 0);
    Obj* result = call_closure(closure, NULL, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 42);
    dec_ref(result);
    dec_ref(closure);
}

void test_call_closure_one_arg(void) {
    Obj* closure = mk_closure(return_first_arg, NULL, NULL, 0, 1);
    Obj* arg = mk_int(99);
    Obj* args[1] = {arg};
    Obj* result = call_closure(closure, args, 1);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 99);
    dec_ref(result);
    dec_ref(arg);
    dec_ref(closure);
}

void test_call_closure_two_args(void) {
    Obj* closure = mk_closure(add_args, NULL, NULL, 0, 2);
    Obj* arg1 = mk_int(30);
    Obj* arg2 = mk_int(12);
    Obj* args[2] = {arg1, arg2};
    Obj* result = call_closure(closure, args, 2);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 42);
    dec_ref(result);
    dec_ref(arg1);
    dec_ref(arg2);
    dec_ref(closure);
}

void test_call_closure_with_capture(void) {
    Obj* cap = mk_int(100);
    Obj* caps[1] = {cap};
    Obj* closure = mk_closure(return_captured, caps, NULL, 1, 0);
    Obj* result = call_closure(closure, NULL, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 100);
    dec_ref(result);
    dec_ref(closure);
    dec_ref(cap);
}

void test_call_closure_capture_plus_arg(void) {
    Obj* cap = mk_int(50);
    Obj* caps[1] = {cap};
    Obj* closure = mk_closure(add_captured_to_arg, caps, NULL, 1, 1);
    Obj* arg = mk_int(25);
    Obj* args[1] = {arg};
    Obj* result = call_closure(closure, args, 1);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 75);
    dec_ref(result);
    dec_ref(arg);
    dec_ref(closure);
    dec_ref(cap);
}

void test_call_closure_multiple_captures(void) {
    Obj* cap1 = mk_int(10);
    Obj* cap2 = mk_int(20);
    Obj* cap3 = mk_int(30);
    Obj* caps[3] = {cap1, cap2, cap3};
    Obj* closure = mk_closure(sum_three_captures, caps, NULL, 3, 0);
    Obj* result = call_closure(closure, NULL, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 60);
    dec_ref(result);
    dec_ref(closure);
    dec_ref(cap1);
    dec_ref(cap2);
    dec_ref(cap3);
}

void test_call_closure_variadic_zero_args(void) {
    Obj* closure = mk_closure(multiply_all_args, NULL, NULL, 0, -1);
    Obj* result = call_closure(closure, NULL, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 1); /* Empty product */
    dec_ref(result);
    dec_ref(closure);
}

void test_call_closure_variadic_many_args(void) {
    Obj* closure = mk_closure(multiply_all_args, NULL, NULL, 0, -1);
    Obj* arg1 = mk_int(2);
    Obj* arg2 = mk_int(3);
    Obj* arg3 = mk_int(4);
    Obj* args[3] = {arg1, arg2, arg3};
    Obj* result = call_closure(closure, args, 3);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 24);
    dec_ref(result);
    dec_ref(arg1);
    dec_ref(arg2);
    dec_ref(arg3);
    dec_ref(closure);
}

void test_call_closure_null_closure(void) {
    Obj* result = call_closure(NULL, NULL, 0);
    ASSERT_NULL(result);
}

void test_call_closure_wrong_tag(void) {
    Obj* not_closure = mk_int(1);
    Obj* result = call_closure(not_closure, NULL, 0);
    ASSERT_NULL(result);
    dec_ref(not_closure);
}

void test_call_closure_wrong_arity(void) {
    Obj* closure = mk_closure(return_first_arg, NULL, NULL, 0, 1);
    Obj* result = call_closure(closure, NULL, 0);
    ASSERT_NULL(result);
    dec_ref(closure);
}

void test_call_closure_arg_count(void) {
    Obj* closure = mk_closure(return_arg_count, NULL, NULL, 0, -1);
    Obj* arg1 = mk_int(1);
    Obj* arg2 = mk_int(2);
    Obj* arg3 = mk_int(3);
    Obj* arg4 = mk_int(4);
    Obj* arg5 = mk_int(5);
    Obj* args[5] = {arg1, arg2, arg3, arg4, arg5};
    Obj* result = call_closure(closure, args, 5);
    ASSERT_EQ(obj_to_int(result), 5);
    dec_ref(result);
    for (int i = 0; i < 5; i++) dec_ref(args[i]);
    dec_ref(closure);
}

/* ========== Closure with different captured types ========== */

static Obj* return_captured_pair_car(Obj** captures, Obj** args, int nargs) {
    (void)args; (void)nargs;
    if (!captures || !captures[0]) return NULL;
    return obj_car(captures[0]);
}

void test_closure_capture_pair(void) {
    Obj* pair = mk_pair(mk_int(42), mk_int(99));
    Obj* caps[1] = {pair};
    Obj* closure = mk_closure(return_captured_pair_car, caps, NULL, 1, 0);
    Obj* result = call_closure(closure, NULL, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 42);
    dec_ref(result);
    dec_ref(closure);
    dec_ref(pair);
}

/* Helper to count list length as int */
static int closure_count_list_length(Obj* xs) {
    int len = 0;
    while (xs && xs->tag == TAG_PAIR) {
        len++;
        xs = xs->b;
    }
    return len;
}

static Obj* return_captured_list_length(Obj** captures, Obj** args, int nargs) {
    (void)args; (void)nargs;
    if (!captures || !captures[0]) return mk_int(0);
    return mk_int(closure_count_list_length(captures[0]));
}

void test_closure_capture_list(void) {
    Obj* list = mk_pair(mk_int(1), mk_pair(mk_int(2), mk_pair(mk_int(3), NULL)));
    Obj* caps[1] = {list};
    Obj* closure = mk_closure(return_captured_list_length, caps, NULL, 1, 0);
    Obj* result = call_closure(closure, NULL, 0);
    ASSERT_EQ(obj_to_int(result), 3);
    dec_ref(result);
    dec_ref(closure);
    dec_ref(list);
}

/* ========== Closure chaining ========== */

static Obj* make_adder_inner(Obj** captures, Obj** args, int nargs) {
    if (!captures || !captures[0]) return mk_int(0);
    if (nargs < 1 || !args[0]) return mk_int(0);
    return mk_int(obj_to_int(captures[0]) + obj_to_int(args[0]));
}

void test_closure_returned_from_closure(void) {
    /* Simulate (make-adder 10) returning a closure that adds 10 */
    Obj* ten = mk_int(10);
    Obj* caps[1] = {ten};
    Obj* adder = mk_closure(make_adder_inner, caps, NULL, 1, 1);

    /* Call (adder 5) => 15 */
    Obj* five = mk_int(5);
    Obj* args[1] = {five};
    Obj* result = call_closure(adder, args, 1);
    ASSERT_EQ(obj_to_int(result), 15);

    dec_ref(result);
    dec_ref(five);
    dec_ref(adder);
    dec_ref(ten);
}

/* ========== Stress tests ========== */

void test_closure_many_calls(void) {
    Obj* closure = mk_closure(return_42, NULL, NULL, 0, 0);
    for (int i = 0; i < 1000; i++) {
        Obj* result = call_closure(closure, NULL, 0);
        ASSERT_EQ(obj_to_int(result), 42);
        dec_ref(result);
    }
    dec_ref(closure);
}

#define TEST_MANY_CAPS 50
static Obj* sum_all_caps_fn(Obj** captures, Obj** args, int nargs) {
    (void)args; (void)nargs;
    long sum = 0;
    for (int i = 0; i < TEST_MANY_CAPS; i++) {
        if (captures && captures[i]) sum += obj_to_int(captures[i]);
    }
    return mk_int(sum);
}

void test_closure_many_captures(void) {
    Obj* caps[TEST_MANY_CAPS];
    long expected_sum = 0;
    for (int i = 0; i < TEST_MANY_CAPS; i++) {
        caps[i] = mk_int(i);
        expected_sum += i;
    }

    Obj* closure = mk_closure(sum_all_caps_fn, caps, NULL, TEST_MANY_CAPS, 0);
    Obj* result = call_closure(closure, NULL, 0);
    ASSERT_EQ(obj_to_int(result), expected_sum);

    dec_ref(result);
    dec_ref(closure);
    for (int i = 0; i < TEST_MANY_CAPS; i++) dec_ref(caps[i]);
}

void test_closure_create_destroy_cycle(void) {
    for (int i = 0; i < 1000; i++) {
        Obj* cap = mk_int(i);
        Obj* caps[1] = {cap};
        Obj* closure = mk_closure(return_captured, caps, NULL, 1, 0);
        Obj* result = call_closure(closure, NULL, 0);
        ASSERT_EQ(obj_to_int(result), i);
        dec_ref(result);
        dec_ref(closure);
        dec_ref(cap);
    }
}

/* ========== Run all closure tests ========== */

void run_closure_tests(void) {
    TEST_SECTION("Closure Creation");
    RUN_TEST(test_mk_closure_no_captures);
    RUN_TEST(test_mk_closure_with_one_capture);
    RUN_TEST(test_mk_closure_with_multiple_captures);
    RUN_TEST(test_mk_closure_arity_zero);
    RUN_TEST(test_mk_closure_arity_one);
    RUN_TEST(test_mk_closure_arity_two);
    RUN_TEST(test_mk_closure_variadic);

    TEST_SECTION("Closure Calls");
    RUN_TEST(test_call_closure_no_args);
    RUN_TEST(test_call_closure_one_arg);
    RUN_TEST(test_call_closure_two_args);
    RUN_TEST(test_call_closure_with_capture);
    RUN_TEST(test_call_closure_capture_plus_arg);
    RUN_TEST(test_call_closure_multiple_captures);
    RUN_TEST(test_call_closure_variadic_zero_args);
    RUN_TEST(test_call_closure_variadic_many_args);
    RUN_TEST(test_call_closure_null_closure);
    RUN_TEST(test_call_closure_wrong_tag);
    RUN_TEST(test_call_closure_wrong_arity);
    RUN_TEST(test_call_closure_arg_count);

    TEST_SECTION("Closure with Complex Captures");
    RUN_TEST(test_closure_capture_pair);
    RUN_TEST(test_closure_capture_list);
    RUN_TEST(test_closure_returned_from_closure);

    TEST_SECTION("Closure Stress Tests");
    RUN_TEST(test_closure_many_calls);
    RUN_TEST(test_closure_many_captures);
    RUN_TEST(test_closure_create_destroy_cycle);
}
