/* Test Primitive Operations */
#include "test_framework.h"
#include <limits.h>

/* === Arithmetic tests === */

void test_prim_add_ints(void) {
    Obj* a = mk_int(2);
    Obj* b = mk_int(3);
    Obj* r = prim_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_tag(r), TAG_INT);
    ASSERT_EQ(obj_to_int(r), 5);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_add_immediates(void) {
    Obj* a = mk_int_unboxed(2);
    Obj* b = mk_int_unboxed(3);
    Obj* r = prim_add(a, b);
    ASSERT(IS_IMMEDIATE(r));
    ASSERT_EQ(INT_IMM_VALUE(r), 5);
    PASS();
}

void test_prim_add_floats(void) {
    Obj* a = mk_float(2.5);
    Obj* b = mk_float(3.5);
    Obj* r = prim_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_tag(r), TAG_FLOAT);
    ASSERT_EQ_FLOAT(num_to_double(r), 6.0, 0.0001);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_add_int_float(void) {
    Obj* a = mk_int(2);
    Obj* b = mk_float(3.5);
    Obj* r = prim_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_tag(r), TAG_FLOAT);
    ASSERT_EQ_FLOAT(num_to_double(r), 5.5, 0.0001);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_add_null(void) {
    Obj* a = mk_int(2);
    Obj* r = prim_add(a, NULL);
    /* add(a, NULL) returns a directly */
    ASSERT_EQ(r, a);
    dec_ref(a);  /* r and a are same object */
    PASS();
}

void test_prim_sub_ints(void) {
    Obj* a = mk_int(10);
    Obj* b = mk_int(3);
    Obj* r = prim_sub(a, b);
    ASSERT_EQ(obj_to_int(r), 7);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_sub_immediates(void) {
    Obj* a = mk_int_unboxed(10);
    Obj* b = mk_int_unboxed(3);
    Obj* r = prim_sub(a, b);
    ASSERT(IS_IMMEDIATE(r));
    ASSERT_EQ(INT_IMM_VALUE(r), 7);
    PASS();
}

void test_prim_sub_negative(void) {
    Obj* a = mk_int(3);
    Obj* b = mk_int(10);
    Obj* r = prim_sub(a, b);
    ASSERT_EQ(obj_to_int(r), -7);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_sub_null_left(void) {
    Obj* b = mk_int(5);
    Obj* r = prim_sub(NULL, b);
    ASSERT(IS_IMMEDIATE(r));
    ASSERT_EQ(INT_IMM_VALUE(r), -5);
    dec_ref(b);
    PASS();
}

void test_prim_sub_null_right(void) {
    Obj* a = mk_int(5);
    Obj* r = prim_sub(a, NULL);
    ASSERT_EQ(r, a);
    dec_ref(a);
    PASS();
}

void test_prim_mul_ints(void) {
    Obj* a = mk_int(6);
    Obj* b = mk_int(7);
    Obj* r = prim_mul(a, b);
    ASSERT_EQ(obj_to_int(r), 42);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_mul_immediates(void) {
    Obj* a = mk_int_unboxed(6);
    Obj* b = mk_int_unboxed(7);
    Obj* r = prim_mul(a, b);
    ASSERT(IS_IMMEDIATE(r));
    ASSERT_EQ(INT_IMM_VALUE(r), 42);
    PASS();
}

void test_prim_mul_zero(void) {
    Obj* a = mk_int(100);
    Obj* b = mk_int(0);
    Obj* r = prim_mul(a, b);
    ASSERT_EQ(obj_to_int(r), 0);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_div_ints(void) {
    Obj* a = mk_int(10);
    Obj* b = mk_int(3);
    Obj* r = prim_div(a, b);
    ASSERT_EQ(obj_to_int(r), 3);  /* Integer division */
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_div_immediates(void) {
    Obj* a = mk_int_unboxed(10);
    Obj* b = mk_int_unboxed(2);
    Obj* r = prim_div(a, b);
    ASSERT(IS_IMMEDIATE(r));
    ASSERT_EQ(INT_IMM_VALUE(r), 5);
    PASS();
}

void test_prim_div_floats(void) {
    Obj* a = mk_float(10.0);
    Obj* b = mk_float(4.0);
    Obj* r = prim_div(a, b);
    ASSERT_EQ_FLOAT(num_to_double(r), 2.5, 0.0001);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_div_by_zero(void) {
    Obj* a = mk_int(10);
    Obj* b = mk_int(0);
    Obj* r = prim_div(a, b);
    /* Should return NULL or error, not crash */
    dec_ref(a); dec_ref(b);
    if (r) dec_ref(r);
    PASS();
}

void test_prim_mod_normal(void) {
    Obj* a = mk_int(10);
    Obj* b = mk_int(3);
    Obj* r = prim_mod(a, b);
    ASSERT_EQ(obj_to_int(r), 1);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_mod_immediates(void) {
    Obj* a = mk_int_unboxed(10);
    Obj* b = mk_int_unboxed(3);
    Obj* r = prim_mod(a, b);
    ASSERT(IS_IMMEDIATE(r));
    ASSERT_EQ(INT_IMM_VALUE(r), 1);
    PASS();
}

void test_prim_mod_negative(void) {
    Obj* a = mk_int(-10);
    Obj* b = mk_int(3);
    Obj* r = prim_mod(a, b);
    /* C modulo behavior with negative numbers */
    ASSERT_NOT_NULL(r);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_abs_positive(void) {
    Obj* a = mk_int(42);
    Obj* r = prim_abs(a);
    ASSERT_EQ(obj_to_int(r), 42);
    dec_ref(a); dec_ref(r);
    PASS();
}

void test_prim_abs_negative(void) {
    Obj* a = mk_int(-42);
    Obj* r = prim_abs(a);
    ASSERT_EQ(obj_to_int(r), 42);
    dec_ref(a); dec_ref(r);
    PASS();
}

void test_prim_abs_zero(void) {
    Obj* a = mk_int(0);
    Obj* r = prim_abs(a);
    ASSERT_EQ(obj_to_int(r), 0);
    dec_ref(a); dec_ref(r);
    PASS();
}

/* === Comparison tests === */

void test_prim_lt_true(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* r = prim_lt(a, b);
    ASSERT(obj_to_int(r) != 0);  /* truthy */
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_lt_false(void) {
    Obj* a = mk_int(2);
    Obj* b = mk_int(1);
    Obj* r = prim_lt(a, b);
    ASSERT(obj_to_int(r) == 0);  /* falsy */
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_lt_equal(void) {
    Obj* a = mk_int(2);
    Obj* b = mk_int(2);
    Obj* r = prim_lt(a, b);
    ASSERT(obj_to_int(r) == 0);  /* equal is not less than */
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_gt_true(void) {
    Obj* a = mk_int(2);
    Obj* b = mk_int(1);
    Obj* r = prim_gt(a, b);
    ASSERT(obj_to_int(r) != 0);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_gt_false(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* r = prim_gt(a, b);
    ASSERT(obj_to_int(r) == 0);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_le_true(void) {
    Obj* a = mk_int(2);
    Obj* b = mk_int(2);
    Obj* r = prim_le(a, b);
    ASSERT(obj_to_int(r) != 0);  /* equal satisfies <= */
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_le_false(void) {
    Obj* a = mk_int(3);
    Obj* b = mk_int(2);
    Obj* r = prim_le(a, b);
    ASSERT(obj_to_int(r) == 0);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_ge_true(void) {
    Obj* a = mk_int(2);
    Obj* b = mk_int(2);
    Obj* r = prim_ge(a, b);
    ASSERT(obj_to_int(r) != 0);  /* equal satisfies >= */
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_ge_false(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* r = prim_ge(a, b);
    ASSERT(obj_to_int(r) == 0);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_eq_same(void) {
    Obj* a = mk_int(42);
    Obj* b = mk_int(42);
    Obj* r = prim_eq(a, b);
    ASSERT(obj_to_int(r) != 0);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_eq_float_same(void) {
    Obj* a = mk_float(1.5);
    Obj* b = mk_float(1.5);
    Obj* r = prim_eq(a, b);
    ASSERT(obj_to_int(r) != 0);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_eq_different(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* r = prim_eq(a, b);
    ASSERT(obj_to_int(r) == 0);
    dec_ref(a); dec_ref(b); dec_ref(r);
    PASS();
}

void test_prim_not_truthy(void) {
    Obj* a = mk_int(1);
    Obj* r = prim_not(a);
    ASSERT(obj_to_int(r) == 0);  /* not of truthy is falsy */
    dec_ref(a); dec_ref(r);
    PASS();
}

void test_prim_not_falsy(void) {
    Obj* a = mk_int(0);
    Obj* r = prim_not(a);
    ASSERT(obj_to_int(r) != 0);  /* not of falsy is truthy */
    dec_ref(a); dec_ref(r);
    PASS();
}

/* === Type predicate tests === */

void test_prim_null_true(void) {
    Obj* r = prim_null(NULL);
    ASSERT(obj_to_int(r) != 0);
    dec_ref(r);
    PASS();
}

void test_prim_null_false(void) {
    Obj* x = mk_int(42);
    Obj* r = prim_null(x);
    ASSERT(obj_to_int(r) == 0);
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_prim_pair_true(void) {
    Obj* p = mk_pair(mk_int(1), mk_int(2));
    Obj* r = prim_pair(p);
    ASSERT(obj_to_int(r) != 0);
    dec_ref(p); dec_ref(r);
    PASS();
}

void test_prim_pair_false(void) {
    Obj* x = mk_int(42);
    Obj* r = prim_pair(x);
    ASSERT(obj_to_int(r) == 0);
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_prim_int_true(void) {
    Obj* x = mk_int(42);
    Obj* r = prim_int(x);
    ASSERT(obj_to_int(r) != 0);
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_prim_int_false(void) {
    Obj* x = mk_float(3.14);
    Obj* r = prim_int(x);
    ASSERT(obj_to_int(r) == 0);
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_prim_float_true(void) {
    Obj* x = mk_float(3.14);
    Obj* r = prim_float(x);
    ASSERT(obj_to_int(r) != 0);
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_prim_float_false(void) {
    Obj* x = mk_int(3);
    Obj* r = prim_float(x);
    ASSERT(obj_to_int(r) == 0);
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_prim_char_true(void) {
    Obj* x = mk_char('A');
    Obj* r = prim_char(x);
    ASSERT(obj_to_int(r) != 0);
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_prim_char_false(void) {
    Obj* x = mk_int(65);
    Obj* r = prim_char(x);
    ASSERT(obj_to_int(r) == 0);
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_prim_sym_true(void) {
    Obj* x = mk_sym("hello");
    Obj* r = prim_sym(x);
    ASSERT(obj_to_int(r) != 0);
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_prim_sym_false(void) {
    Obj* x = mk_int(1);
    Obj* r = prim_sym(x);
    ASSERT(obj_to_int(r) == 0);
    dec_ref(x); dec_ref(r);
    PASS();
}

/* === Character/Float conversion tests === */

void test_char_to_int(void) {
    Obj* c = mk_char('A');
    Obj* r = char_to_int(c);
    ASSERT_EQ(obj_to_int(r), 65);
    dec_ref(c); dec_ref(r);
    PASS();
}

void test_char_to_int_wrong_type(void) {
    Obj* n = mk_int(65);
    Obj* r = char_to_int(n);
    ASSERT_EQ(obj_to_int(r), 0);
    dec_ref(n); dec_ref(r);
    PASS();
}

void test_int_to_char(void) {
    Obj* n = mk_int(65);
    Obj* r = int_to_char(n);
    ASSERT_EQ(obj_tag(r), TAG_CHAR);
    ASSERT_EQ(obj_to_char_val(r), 65);
    dec_ref(n); dec_ref(r);
    PASS();
}

void test_int_to_char_immediate(void) {
    Obj* n = mk_int_unboxed(66);
    Obj* r = int_to_char(n);
    ASSERT_EQ(obj_tag(r), TAG_CHAR);
    ASSERT_EQ(obj_to_char_val(r), 66);
    dec_ref(r);
    PASS();
}

void test_int_to_float(void) {
    Obj* n = mk_int(42);
    Obj* r = int_to_float(n);
    ASSERT_EQ(obj_tag(r), TAG_FLOAT);
    ASSERT_EQ_FLOAT(num_to_double(r), 42.0, 0.0001);
    dec_ref(n); dec_ref(r);
    PASS();
}

void test_float_to_int(void) {
    Obj* f = mk_float(3.7);
    Obj* r = float_to_int(f);
    ASSERT_EQ(obj_tag(r), TAG_INT);
    ASSERT_EQ(obj_to_int(r), 3);  /* truncated */
    dec_ref(f); dec_ref(r);
    PASS();
}

void test_prim_floor(void) {
    Obj* f = mk_float(3.7);
    Obj* r = prim_floor(f);
    ASSERT_EQ_FLOAT(num_to_double(r), 3.0, 0.0001);
    dec_ref(f); dec_ref(r);
    PASS();
}

void test_prim_floor_negative(void) {
    Obj* f = mk_float(-3.2);
    Obj* r = prim_floor(f);
    ASSERT_EQ_FLOAT(num_to_double(r), -4.0, 0.0001);
    dec_ref(f); dec_ref(r);
    PASS();
}

void test_prim_ceil(void) {
    Obj* f = mk_float(3.2);
    Obj* r = prim_ceil(f);
    ASSERT_EQ_FLOAT(num_to_double(r), 4.0, 0.0001);
    dec_ref(f); dec_ref(r);
    PASS();
}

void test_prim_ceil_negative(void) {
    Obj* f = mk_float(-3.7);
    Obj* r = prim_ceil(f);
    ASSERT_EQ_FLOAT(num_to_double(r), -3.0, 0.0001);
    dec_ref(f); dec_ref(r);
    PASS();
}

/* === Type introspection tests === */

void test_ctr_tag_int(void) {
    Obj* x = mk_int(42);
    Obj* r = ctr_tag(x);
    /* ctr_tag returns a symbol like "int", "float", etc. */
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_tag(r), TAG_SYM);
    ASSERT_STR_EQ((char*)r->ptr, "int");
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_ctr_tag_pair(void) {
    Obj* p = mk_pair(mk_int(1), mk_int(2));
    Obj* r = ctr_tag(p);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_tag(r), TAG_SYM);
    ASSERT_STR_EQ((char*)r->ptr, "cell");  /* pairs are called "cell" */
    dec_ref(p); dec_ref(r);
    PASS();
}

void test_ctr_tag_null(void) {
    Obj* r = ctr_tag(NULL);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_tag(r), TAG_SYM);
    ASSERT_STR_EQ((char*)r->ptr, "nil");
    dec_ref(r);
    PASS();
}

void test_ctr_arg_car(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* p = mk_pair(a, b);
    Obj* idx = mk_int(0);
    Obj* r = ctr_arg(p, idx);
    ASSERT_EQ(r, a);
    dec_ref(p); dec_ref(idx);
    PASS();
}

void test_ctr_arg_cdr(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* p = mk_pair(a, b);
    Obj* idx = mk_int(1);
    Obj* r = ctr_arg(p, idx);
    ASSERT_EQ(r, b);
    dec_ref(p); dec_ref(idx);
    PASS();
}

/* === Truthiness tests === */

void test_is_truthy_null(void) {
    ASSERT(!is_truthy(NULL));
    PASS();
}

void test_is_truthy_zero(void) {
    Obj* x = mk_int(0);
    ASSERT(!is_truthy(x));
    dec_ref(x);
    PASS();
}

void test_is_truthy_nonzero(void) {
    Obj* x = mk_int(1);
    ASSERT(is_truthy(x));
    dec_ref(x);
    PASS();
}

void test_is_truthy_pair(void) {
    Obj* p = mk_pair(mk_int(1), NULL);
    ASSERT(is_truthy(p));
    dec_ref(p);
    PASS();
}

/* === Run all primitive tests === */

void run_primitive_tests(void) {
    TEST_SUITE("Primitive Operations");

    /* Arithmetic */
    RUN_TEST(test_prim_add_ints);
    RUN_TEST(test_prim_add_immediates);
    RUN_TEST(test_prim_add_floats);
    RUN_TEST(test_prim_add_int_float);
    RUN_TEST(test_prim_add_null);
    RUN_TEST(test_prim_sub_ints);
    RUN_TEST(test_prim_sub_immediates);
    RUN_TEST(test_prim_sub_negative);
    RUN_TEST(test_prim_sub_null_left);
    RUN_TEST(test_prim_sub_null_right);
    RUN_TEST(test_prim_mul_ints);
    RUN_TEST(test_prim_mul_immediates);
    RUN_TEST(test_prim_mul_zero);
    RUN_TEST(test_prim_div_ints);
    RUN_TEST(test_prim_div_immediates);
    RUN_TEST(test_prim_div_floats);
    RUN_TEST(test_prim_div_by_zero);
    RUN_TEST(test_prim_mod_normal);
    RUN_TEST(test_prim_mod_immediates);
    RUN_TEST(test_prim_mod_negative);
    RUN_TEST(test_prim_abs_positive);
    RUN_TEST(test_prim_abs_negative);
    RUN_TEST(test_prim_abs_zero);

    /* Comparison */
    RUN_TEST(test_prim_lt_true);
    RUN_TEST(test_prim_lt_false);
    RUN_TEST(test_prim_lt_equal);
    RUN_TEST(test_prim_gt_true);
    RUN_TEST(test_prim_gt_false);
    RUN_TEST(test_prim_le_true);
    RUN_TEST(test_prim_le_false);
    RUN_TEST(test_prim_ge_true);
    RUN_TEST(test_prim_ge_false);
    RUN_TEST(test_prim_eq_same);
    RUN_TEST(test_prim_eq_float_same);
    RUN_TEST(test_prim_eq_different);
    RUN_TEST(test_prim_not_truthy);
    RUN_TEST(test_prim_not_falsy);

    /* Type predicates */
    RUN_TEST(test_prim_null_true);
    RUN_TEST(test_prim_null_false);
    RUN_TEST(test_prim_pair_true);
    RUN_TEST(test_prim_pair_false);
    RUN_TEST(test_prim_int_true);
    RUN_TEST(test_prim_int_false);
    RUN_TEST(test_prim_float_true);
    RUN_TEST(test_prim_float_false);
    RUN_TEST(test_prim_char_true);
    RUN_TEST(test_prim_char_false);
    RUN_TEST(test_prim_sym_true);
    RUN_TEST(test_prim_sym_false);

    /* Conversions */
    RUN_TEST(test_char_to_int);
    RUN_TEST(test_char_to_int_wrong_type);
    RUN_TEST(test_int_to_char);
    RUN_TEST(test_int_to_char_immediate);
    RUN_TEST(test_int_to_float);
    RUN_TEST(test_float_to_int);
    RUN_TEST(test_prim_floor);
    RUN_TEST(test_prim_floor_negative);
    RUN_TEST(test_prim_ceil);
    RUN_TEST(test_prim_ceil_negative);

    /* Type introspection */
    RUN_TEST(test_ctr_tag_int);
    RUN_TEST(test_ctr_tag_pair);
    RUN_TEST(test_ctr_tag_null);
    RUN_TEST(test_ctr_arg_car);
    RUN_TEST(test_ctr_arg_cdr);

    /* Truthiness */
    RUN_TEST(test_is_truthy_null);
    RUN_TEST(test_is_truthy_zero);
    RUN_TEST(test_is_truthy_nonzero);
    RUN_TEST(test_is_truthy_pair);
}
