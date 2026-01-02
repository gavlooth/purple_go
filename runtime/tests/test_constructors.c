/* Test Object Constructors */
#include "../include/purple.h"
#include "test_framework.h"
#include <limits.h>
#include <float.h>

/* === mk_int tests === */

void test_mk_int_positive(void) {
    Obj* x = mk_int(42);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->tag, TAG_INT);
    ASSERT_EQ(x->i, 42);
    ASSERT_EQ(x->mark, 1);
    ASSERT_EQ(x->is_pair, 0);
    dec_ref(x);
    PASS();
}

void test_mk_int_negative(void) {
    Obj* x = mk_int(-100);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->tag, TAG_INT);
    ASSERT_EQ(x->i, -100);
    dec_ref(x);
    PASS();
}

void test_mk_int_zero(void) {
    Obj* x = mk_int(0);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->i, 0);
    dec_ref(x);
    PASS();
}

void test_mk_int_max(void) {
    Obj* x = mk_int(LONG_MAX);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->i, LONG_MAX);
    dec_ref(x);
    PASS();
}

void test_mk_int_min(void) {
    Obj* x = mk_int(LONG_MIN);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->i, LONG_MIN);
    dec_ref(x);
    PASS();
}

/* === mk_float tests === */

void test_mk_float_positive(void) {
    Obj* x = mk_float(3.14);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->tag, TAG_FLOAT);
    ASSERT_EQ_FLOAT(x->f, 3.14, 0.0001);
    dec_ref(x);
    PASS();
}

void test_mk_float_negative(void) {
    Obj* x = mk_float(-2.718);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ_FLOAT(x->f, -2.718, 0.0001);
    dec_ref(x);
    PASS();
}

void test_mk_float_zero(void) {
    Obj* x = mk_float(0.0);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ_FLOAT(x->f, 0.0, 0.0001);
    dec_ref(x);
    PASS();
}

void test_mk_float_large(void) {
    Obj* x = mk_float(DBL_MAX);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ_FLOAT(x->f, DBL_MAX, DBL_MAX * 0.0001);
    dec_ref(x);
    PASS();
}

void test_mk_float_small(void) {
    Obj* x = mk_float(DBL_MIN);
    ASSERT_NOT_NULL(x);
    ASSERT(x->f > 0);
    dec_ref(x);
    PASS();
}

/* === mk_char tests === */

void test_mk_char_ascii(void) {
    Obj* x = mk_char('A');
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->tag, TAG_CHAR);
    ASSERT_EQ(x->i, 'A');
    dec_ref(x);
    PASS();
}

void test_mk_char_zero(void) {
    Obj* x = mk_char(0);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->i, 0);
    dec_ref(x);
    PASS();
}

void test_mk_char_extended(void) {
    Obj* x = mk_char(255);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->i, 255);
    dec_ref(x);
    PASS();
}

/* === mk_pair tests === */

void test_mk_pair_normal(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* p = mk_pair(a, b);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->tag, TAG_PAIR);
    ASSERT_EQ(p->is_pair, 1);
    ASSERT_EQ(p->a, a);
    ASSERT_EQ(p->b, b);
    dec_ref(p);
    PASS();
}

void test_mk_pair_null_car(void) {
    Obj* b = mk_int(2);
    Obj* p = mk_pair(NULL, b);
    ASSERT_NOT_NULL(p);
    ASSERT_NULL(p->a);
    ASSERT_EQ(p->b, b);
    dec_ref(p);
    PASS();
}

void test_mk_pair_null_cdr(void) {
    Obj* a = mk_int(1);
    Obj* p = mk_pair(a, NULL);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->a, a);
    ASSERT_NULL(p->b);
    dec_ref(p);
    PASS();
}

void test_mk_pair_both_null(void) {
    Obj* p = mk_pair(NULL, NULL);
    ASSERT_NOT_NULL(p);
    ASSERT_NULL(p->a);
    ASSERT_NULL(p->b);
    dec_ref(p);
    PASS();
}

void test_mk_pair_nested(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* c = mk_int(3);
    Obj* inner = mk_pair(b, c);
    Obj* outer = mk_pair(a, inner);
    ASSERT_NOT_NULL(outer);
    ASSERT_EQ(outer->a, a);
    ASSERT_EQ(outer->b, inner);
    dec_ref(outer);
    PASS();
}

/* === mk_sym tests === */

void test_mk_sym_normal(void) {
    Obj* x = mk_sym("hello");
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->tag, TAG_SYM);
    ASSERT_STR_EQ((char*)x->ptr, "hello");
    dec_ref(x);
    PASS();
}

void test_mk_sym_empty(void) {
    Obj* x = mk_sym("");
    ASSERT_NOT_NULL(x);
    ASSERT_STR_EQ((char*)x->ptr, "");
    dec_ref(x);
    PASS();
}

void test_mk_sym_null(void) {
    Obj* x = mk_sym(NULL);
    ASSERT_NOT_NULL(x);
    ASSERT_NULL(x->ptr);
    dec_ref(x);
    PASS();
}

void test_mk_sym_long(void) {
    char buf[1024];
    memset(buf, 'x', 1023);
    buf[1023] = '\0';
    Obj* x = mk_sym(buf);
    ASSERT_NOT_NULL(x);
    ASSERT_STR_EQ((char*)x->ptr, buf);
    dec_ref(x);
    PASS();
}

/* === mk_box tests === */

void test_mk_box_normal(void) {
    Obj* v = mk_int(42);
    Obj* b = mk_box(v);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ(b->tag, TAG_BOX);
    ASSERT_EQ(box_get(b), v);
    dec_ref(b);
    dec_ref(v);  /* box incremented ref */
    PASS();
}

void test_mk_box_null(void) {
    Obj* b = mk_box(NULL);
    ASSERT_NOT_NULL(b);
    ASSERT_NULL(box_get(b));
    dec_ref(b);
    PASS();
}

/* === mk_error tests === */

void test_mk_error_normal(void) {
    Obj* x = mk_error("something failed");
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->tag, TAG_ERROR);
    ASSERT_STR_EQ((char*)x->ptr, "something failed");
    dec_ref(x);
    PASS();
}

void test_mk_error_empty(void) {
    Obj* x = mk_error("");
    ASSERT_NOT_NULL(x);
    ASSERT_STR_EQ((char*)x->ptr, "");
    dec_ref(x);
    PASS();
}

void test_mk_error_null(void) {
    Obj* x = mk_error(NULL);
    ASSERT_NOT_NULL(x);
    ASSERT_NULL(x->ptr);
    dec_ref(x);
    PASS();
}

/* === mk_int_stack tests === */

void test_mk_int_stack_normal(void) {
    int old_ptr = STACK_PTR;
    Obj* x = mk_int_stack(99);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->tag, TAG_INT);
    ASSERT_EQ(x->i, 99);
    ASSERT_EQ(x->mark, 0);  /* Stack objects have mark=0 */
    STACK_PTR = old_ptr;  /* Reset for other tests */
    PASS();
}

void test_mk_int_stack_fallback(void) {
    /* Fill up the stack pool */
    int old_ptr = STACK_PTR;
    STACK_PTR = STACK_POOL_SIZE;

    Obj* x = mk_int_stack(123);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->i, 123);
    ASSERT_EQ(x->mark, 1);  /* Heap allocated */

    dec_ref(x);
    STACK_PTR = old_ptr;
    PASS();
}

/* === Run all constructor tests === */

void run_constructor_tests(void) {
    TEST_SUITE("Object Constructors");

    /* mk_int */
    RUN_TEST(test_mk_int_positive);
    RUN_TEST(test_mk_int_negative);
    RUN_TEST(test_mk_int_zero);
    RUN_TEST(test_mk_int_max);
    RUN_TEST(test_mk_int_min);

    /* mk_float */
    RUN_TEST(test_mk_float_positive);
    RUN_TEST(test_mk_float_negative);
    RUN_TEST(test_mk_float_zero);
    RUN_TEST(test_mk_float_large);
    RUN_TEST(test_mk_float_small);

    /* mk_char */
    RUN_TEST(test_mk_char_ascii);
    RUN_TEST(test_mk_char_zero);
    RUN_TEST(test_mk_char_extended);

    /* mk_pair */
    RUN_TEST(test_mk_pair_normal);
    RUN_TEST(test_mk_pair_null_car);
    RUN_TEST(test_mk_pair_null_cdr);
    RUN_TEST(test_mk_pair_both_null);
    RUN_TEST(test_mk_pair_nested);

    /* mk_sym */
    RUN_TEST(test_mk_sym_normal);
    RUN_TEST(test_mk_sym_empty);
    RUN_TEST(test_mk_sym_null);
    RUN_TEST(test_mk_sym_long);

    /* mk_box */
    RUN_TEST(test_mk_box_normal);
    RUN_TEST(test_mk_box_null);

    /* mk_error */
    RUN_TEST(test_mk_error_normal);
    RUN_TEST(test_mk_error_empty);
    RUN_TEST(test_mk_error_null);

    /* mk_int_stack */
    RUN_TEST(test_mk_int_stack_normal);
    RUN_TEST(test_mk_int_stack_fallback);
}
