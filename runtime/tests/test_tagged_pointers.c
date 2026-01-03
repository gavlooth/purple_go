/* test_tagged_pointers.c - Comprehensive tagged pointer tests */
#include "test_framework.h"

/* ========== Immediate Integer Tests ========== */

void test_imm_int_positive(void) {
    Obj* x = mk_int_unboxed(42);
    ASSERT(IS_IMMEDIATE_INT(x));
    ASSERT_EQ(INT_IMM_VALUE(x), 42);
    PASS();
}

void test_imm_int_negative(void) {
    Obj* x = mk_int_unboxed(-100);
    ASSERT(IS_IMMEDIATE_INT(x));
    ASSERT_EQ(INT_IMM_VALUE(x), -100);
    PASS();
}

void test_imm_int_zero(void) {
    Obj* x = mk_int_unboxed(0);
    ASSERT(IS_IMMEDIATE_INT(x));
    ASSERT_EQ(INT_IMM_VALUE(x), 0);
    PASS();
}

void test_imm_int_large_positive(void) {
    long large = (1L << 60) - 1;  /* Large but fits in 61 bits */
    Obj* x = mk_int_unboxed(large);
    ASSERT(IS_IMMEDIATE_INT(x));
    ASSERT_EQ(INT_IMM_VALUE(x), large);
    PASS();
}

void test_imm_int_large_negative(void) {
    long large_neg = -(1L << 60) + 1;
    Obj* x = mk_int_unboxed(large_neg);
    ASSERT(IS_IMMEDIATE_INT(x));
    ASSERT_EQ(INT_IMM_VALUE(x), large_neg);
    PASS();
}

void test_imm_int_range_stress(void) {
    /* Test various values across the range */
    long values[] = {-1000000, -1, 0, 1, 1000000, 12345678};
    for (int i = 0; i < 6; i++) {
        Obj* x = mk_int_unboxed(values[i]);
        ASSERT(IS_IMMEDIATE_INT(x));
        ASSERT_EQ(INT_IMM_VALUE(x), values[i]);
    }
    PASS();
}

/* ========== Immediate Boolean Tests ========== */

void test_imm_bool_true(void) {
    ASSERT(IS_IMMEDIATE_BOOL(PURPLE_TRUE));
    ASSERT(obj_to_bool(PURPLE_TRUE) == 1);
    PASS();
}

void test_imm_bool_false(void) {
    ASSERT(IS_IMMEDIATE_BOOL(PURPLE_FALSE));
    ASSERT(obj_to_bool(PURPLE_FALSE) == 0);
    PASS();
}

void test_imm_bool_distinct(void) {
    ASSERT(PURPLE_TRUE != PURPLE_FALSE);
    PASS();
}

void test_mk_bool(void) {
    ASSERT(mk_bool(1) == PURPLE_TRUE);
    ASSERT(mk_bool(0) == PURPLE_FALSE);
    ASSERT(mk_bool(100) == PURPLE_TRUE);
    ASSERT(mk_bool(-1) == PURPLE_TRUE);
    PASS();
}

/* ========== Immediate Character Tests ========== */

void test_imm_char_ascii(void) {
    Obj* c = mk_char_unboxed('A');
    ASSERT(IS_IMMEDIATE_CHAR(c));
    ASSERT_EQ(CHAR_IMM_VALUE(c), 'A');
    PASS();
}

void test_imm_char_zero(void) {
    Obj* c = mk_char_unboxed(0);
    ASSERT(IS_IMMEDIATE_CHAR(c));
    ASSERT_EQ(CHAR_IMM_VALUE(c), 0);
    PASS();
}

void test_imm_char_unicode_basic(void) {
    Obj* c = mk_char_unboxed(0x03B1);  /* Greek alpha */
    ASSERT(IS_IMMEDIATE_CHAR(c));
    ASSERT_EQ(CHAR_IMM_VALUE(c), 0x03B1);
    PASS();
}

void test_imm_char_unicode_emoji(void) {
    Obj* c = mk_char_unboxed(0x1F600);  /* Emoji */
    ASSERT(IS_IMMEDIATE_CHAR(c));
    ASSERT_EQ(CHAR_IMM_VALUE(c), 0x1F600);
    PASS();
}

void test_imm_char_max_unicode(void) {
    Obj* c = mk_char_unboxed(0x10FFFF);  /* Max unicode */
    ASSERT(IS_IMMEDIATE_CHAR(c));
    ASSERT_EQ(CHAR_IMM_VALUE(c), 0x10FFFF);
    PASS();
}

/* ========== Tag Detection Tests ========== */

void test_tag_detection_int(void) {
    Obj* x = mk_int_unboxed(42);
    ASSERT(IS_IMMEDIATE(x));
    ASSERT(IS_IMMEDIATE_INT(x));
    ASSERT(!IS_IMMEDIATE_CHAR(x));
    ASSERT(!IS_IMMEDIATE_BOOL(x));
    ASSERT(!IS_BOXED(x));
    PASS();
}

void test_tag_detection_char(void) {
    Obj* x = mk_char_unboxed('Z');
    ASSERT(IS_IMMEDIATE(x));
    ASSERT(IS_IMMEDIATE_CHAR(x));
    ASSERT(!IS_IMMEDIATE_INT(x));
    ASSERT(!IS_IMMEDIATE_BOOL(x));
    ASSERT(!IS_BOXED(x));
    PASS();
}

void test_tag_detection_bool(void) {
    ASSERT(IS_IMMEDIATE(PURPLE_TRUE));
    ASSERT(IS_IMMEDIATE_BOOL(PURPLE_TRUE));
    ASSERT(!IS_IMMEDIATE_INT(PURPLE_TRUE));
    ASSERT(!IS_IMMEDIATE_CHAR(PURPLE_TRUE));
    ASSERT(!IS_BOXED(PURPLE_TRUE));
    PASS();
}

void test_tag_detection_boxed(void) {
    Obj* x = mk_int(42);
    ASSERT(!IS_IMMEDIATE(x));
    ASSERT(IS_BOXED(x));
    ASSERT(!IS_IMMEDIATE_INT(x));
    dec_ref(x);
    PASS();
}

void test_tag_detection_null(void) {
    ASSERT(!IS_IMMEDIATE(NULL));
    ASSERT(!IS_BOXED(NULL));
    PASS();
}

/* ========== obj_to_int Tests ========== */

void test_obj_to_int_immediate(void) {
    Obj* x = mk_int_unboxed(999);
    ASSERT_EQ(obj_to_int(x), 999);
    PASS();
}

void test_obj_to_int_boxed(void) {
    Obj* x = mk_int(888);
    ASSERT_EQ(obj_to_int(x), 888);
    dec_ref(x);
    PASS();
}

void test_obj_to_int_bool_true(void) {
    ASSERT_EQ(obj_to_int(PURPLE_TRUE), 1);
    PASS();
}

void test_obj_to_int_bool_false(void) {
    ASSERT_EQ(obj_to_int(PURPLE_FALSE), 0);
    PASS();
}

void test_obj_to_int_char(void) {
    Obj* c = mk_char_unboxed('X');
    ASSERT_EQ(obj_to_int(c), 'X');
    PASS();
}

void test_obj_to_int_null(void) {
    ASSERT_EQ(obj_to_int(NULL), 0);
    PASS();
}

/* ========== obj_to_bool Tests ========== */

void test_obj_to_bool_true(void) {
    ASSERT_EQ(obj_to_bool(PURPLE_TRUE), 1);
    PASS();
}

void test_obj_to_bool_false(void) {
    ASSERT_EQ(obj_to_bool(PURPLE_FALSE), 0);
    PASS();
}

void test_obj_to_bool_int_nonzero(void) {
    Obj* x = mk_int_unboxed(42);
    ASSERT_EQ(obj_to_bool(x), 1);
    PASS();
}

void test_obj_to_bool_int_zero(void) {
    Obj* x = mk_int_unboxed(0);
    ASSERT_EQ(obj_to_bool(x), 0);
    PASS();
}

void test_obj_to_bool_null(void) {
    ASSERT_EQ(obj_to_bool(NULL), 0);
    PASS();
}

void test_obj_to_bool_boxed(void) {
    Obj* x = mk_int(1);
    ASSERT_EQ(obj_to_bool(x), 1);
    dec_ref(x);
    PASS();
}

/* ========== obj_tag Tests ========== */

void test_obj_tag_null(void) {
    ASSERT_EQ(obj_tag(NULL), 0);
    PASS();
}

void test_obj_tag_imm_int(void) {
    Obj* x = mk_int_unboxed(5);
    ASSERT_EQ(obj_tag(x), TAG_INT);
    PASS();
}

void test_obj_tag_imm_char(void) {
    Obj* x = mk_char_unboxed('Q');
    ASSERT_EQ(obj_tag(x), TAG_CHAR);
    PASS();
}

void test_obj_tag_imm_bool(void) {
    ASSERT_EQ(obj_tag(PURPLE_TRUE), TAG_INT);  /* Bools treated as int-like */
    PASS();
}

void test_obj_tag_boxed_int(void) {
    Obj* x = mk_int(7);
    ASSERT_EQ(obj_tag(x), TAG_INT);
    dec_ref(x);
    PASS();
}

void test_obj_tag_boxed_float(void) {
    Obj* x = mk_float(3.14);
    ASSERT_EQ(obj_tag(x), TAG_FLOAT);
    dec_ref(x);
    PASS();
}

void test_obj_tag_pair(void) {
    Obj* p = mk_pair(mk_int_unboxed(1), mk_int_unboxed(2));
    ASSERT_EQ(obj_tag(p), TAG_PAIR);
    dec_ref(p);
    PASS();
}

/* ========== is_int Tests ========== */

void test_is_int_immediate(void) {
    Obj* x = mk_int_unboxed(42);
    ASSERT(is_int(x));
    PASS();
}

void test_is_int_boxed(void) {
    Obj* x = mk_int(42);
    ASSERT(is_int(x));
    dec_ref(x);
    PASS();
}

void test_is_int_bool(void) {
    ASSERT(is_int(PURPLE_TRUE));
    ASSERT(is_int(PURPLE_FALSE));
    PASS();
}

void test_is_int_char(void) {
    Obj* c = mk_char_unboxed('A');
    ASSERT(!is_int(c));
    PASS();
}

void test_is_int_float(void) {
    Obj* f = mk_float(3.14);
    ASSERT(!is_int(f));
    dec_ref(f);
    PASS();
}

void test_is_int_null(void) {
    ASSERT(!is_int(NULL));
    PASS();
}

/* ========== is_char Tests ========== */

void test_is_char_immediate(void) {
    Obj* c = mk_char_unboxed('A');
    ASSERT(is_char(c));
    PASS();
}

void test_is_char_int(void) {
    Obj* x = mk_int_unboxed(65);
    ASSERT(!is_char(x));
    PASS();
}

void test_is_char_null(void) {
    ASSERT(!is_char(NULL));
    PASS();
}

/* ========== Arithmetic with Immediates ========== */

void test_add_immediates(void) {
    Obj* a = mk_int_unboxed(10);
    Obj* b = mk_int_unboxed(20);
    Obj* result = add(a, b);
    ASSERT(IS_IMMEDIATE_INT(result));
    ASSERT_EQ(obj_to_int(result), 30);
    PASS();
}

void test_sub_immediates(void) {
    Obj* a = mk_int_unboxed(50);
    Obj* b = mk_int_unboxed(30);
    Obj* result = sub(a, b);
    ASSERT(IS_IMMEDIATE_INT(result));
    ASSERT_EQ(obj_to_int(result), 20);
    PASS();
}

void test_mul_immediates(void) {
    Obj* a = mk_int_unboxed(7);
    Obj* b = mk_int_unboxed(6);
    Obj* result = mul(a, b);
    ASSERT(IS_IMMEDIATE_INT(result));
    ASSERT_EQ(obj_to_int(result), 42);
    PASS();
}

void test_div_immediates(void) {
    Obj* a = mk_int_unboxed(100);
    Obj* b = mk_int_unboxed(10);
    Obj* result = div_op(a, b);
    ASSERT(IS_IMMEDIATE_INT(result));
    ASSERT_EQ(obj_to_int(result), 10);
    PASS();
}

void test_mod_immediates(void) {
    Obj* a = mk_int_unboxed(17);
    Obj* b = mk_int_unboxed(5);
    Obj* result = mod_op(a, b);
    ASSERT(IS_IMMEDIATE_INT(result));
    ASSERT_EQ(obj_to_int(result), 2);
    PASS();
}

void test_add_mixed_imm_boxed(void) {
    Obj* a = mk_int_unboxed(10);
    Obj* b = mk_int(20);
    Obj* result = add(a, b);
    ASSERT(IS_IMMEDIATE_INT(result));
    ASSERT_EQ(obj_to_int(result), 30);
    dec_ref(b);
    PASS();
}

/* ========== Comparison with Immediates ========== */

void test_lt_immediates(void) {
    Obj* a = mk_int_unboxed(5);
    Obj* b = mk_int_unboxed(10);
    Obj* result = lt_op(a, b);
    ASSERT(obj_to_bool(result) == 1);
    PASS();
}

void test_gt_immediates(void) {
    Obj* a = mk_int_unboxed(10);
    Obj* b = mk_int_unboxed(5);
    Obj* result = gt_op(a, b);
    ASSERT(obj_to_bool(result) == 1);
    PASS();
}

void test_eq_immediates(void) {
    Obj* a = mk_int_unboxed(42);
    Obj* b = mk_int_unboxed(42);
    Obj* result = eq_op(a, b);
    ASSERT(obj_to_bool(result) == 1);
    PASS();
}

void test_le_immediates(void) {
    Obj* a = mk_int_unboxed(5);
    Obj* b = mk_int_unboxed(5);
    Obj* result = le_op(a, b);
    ASSERT(obj_to_bool(result) == 1);
    PASS();
}

void test_ge_immediates(void) {
    Obj* a = mk_int_unboxed(10);
    Obj* b = mk_int_unboxed(5);
    Obj* result = ge_op(a, b);
    ASSERT(obj_to_bool(result) == 1);
    PASS();
}

/* ========== Reference Counting with Immediates ========== */

void test_inc_ref_immediate(void) {
    Obj* x = mk_int_unboxed(42);
    inc_ref(x);  /* Should be no-op */
    ASSERT_EQ(INT_IMM_VALUE(x), 42);  /* Still valid */
    PASS();
}

void test_dec_ref_immediate(void) {
    Obj* x = mk_int_unboxed(42);
    dec_ref(x);  /* Should be no-op */
    ASSERT_EQ(INT_IMM_VALUE(x), 42);  /* Still valid */
    PASS();
}

void test_free_obj_immediate(void) {
    Obj* x = mk_int_unboxed(42);
    free_obj(x);  /* Should be no-op */
    ASSERT_EQ(INT_IMM_VALUE(x), 42);  /* Still valid */
    PASS();
}

void test_free_unique_immediate(void) {
    Obj* x = mk_int_unboxed(42);
    free_unique(x);  /* Should be no-op */
    ASSERT_EQ(INT_IMM_VALUE(x), 42);  /* Still valid */
    PASS();
}

/* ========== Stress Tests ========== */

void test_tagged_stress_many_ints(void) {
    for (int i = 0; i < 10000; i++) {
        Obj* x = mk_int_unboxed(i);
        ASSERT(IS_IMMEDIATE_INT(x));
        ASSERT_EQ(INT_IMM_VALUE(x), i);
    }
    PASS();
}

void test_tagged_stress_mixed_ops(void) {
    for (int i = 0; i < 1000; i++) {
        Obj* a = mk_int_unboxed(i);
        Obj* b = mk_int_unboxed(i + 1);
        Obj* sum = add(a, b);
        ASSERT_EQ(obj_to_int(sum), 2 * i + 1);
    }
    PASS();
}

void test_tagged_stress_chars(void) {
    for (int i = 0; i < 128; i++) {
        Obj* c = mk_char_unboxed(i);
        ASSERT(IS_IMMEDIATE_CHAR(c));
        ASSERT_EQ(CHAR_IMM_VALUE(c), i);
    }
    PASS();
}

/* ========== Run All Tagged Pointer Tests ========== */

void run_tagged_pointer_tests(void) {
    TEST_SUITE("Tagged Pointers");

    TEST_SECTION("Immediate Integers");
    RUN_TEST(test_imm_int_positive);
    RUN_TEST(test_imm_int_negative);
    RUN_TEST(test_imm_int_zero);
    RUN_TEST(test_imm_int_large_positive);
    RUN_TEST(test_imm_int_large_negative);
    RUN_TEST(test_imm_int_range_stress);

    TEST_SECTION("Immediate Booleans");
    RUN_TEST(test_imm_bool_true);
    RUN_TEST(test_imm_bool_false);
    RUN_TEST(test_imm_bool_distinct);
    RUN_TEST(test_mk_bool);

    TEST_SECTION("Immediate Characters");
    RUN_TEST(test_imm_char_ascii);
    RUN_TEST(test_imm_char_zero);
    RUN_TEST(test_imm_char_unicode_basic);
    RUN_TEST(test_imm_char_unicode_emoji);
    RUN_TEST(test_imm_char_max_unicode);

    TEST_SECTION("Tag Detection");
    RUN_TEST(test_tag_detection_int);
    RUN_TEST(test_tag_detection_char);
    RUN_TEST(test_tag_detection_bool);
    RUN_TEST(test_tag_detection_boxed);
    RUN_TEST(test_tag_detection_null);

    TEST_SECTION("obj_to_int");
    RUN_TEST(test_obj_to_int_immediate);
    RUN_TEST(test_obj_to_int_boxed);
    RUN_TEST(test_obj_to_int_bool_true);
    RUN_TEST(test_obj_to_int_bool_false);
    RUN_TEST(test_obj_to_int_char);
    RUN_TEST(test_obj_to_int_null);

    TEST_SECTION("obj_to_bool");
    RUN_TEST(test_obj_to_bool_true);
    RUN_TEST(test_obj_to_bool_false);
    RUN_TEST(test_obj_to_bool_int_nonzero);
    RUN_TEST(test_obj_to_bool_int_zero);
    RUN_TEST(test_obj_to_bool_null);
    RUN_TEST(test_obj_to_bool_boxed);

    TEST_SECTION("obj_tag");
    RUN_TEST(test_obj_tag_null);
    RUN_TEST(test_obj_tag_imm_int);
    RUN_TEST(test_obj_tag_imm_char);
    RUN_TEST(test_obj_tag_imm_bool);
    RUN_TEST(test_obj_tag_boxed_int);
    RUN_TEST(test_obj_tag_boxed_float);
    RUN_TEST(test_obj_tag_pair);

    TEST_SECTION("is_int");
    RUN_TEST(test_is_int_immediate);
    RUN_TEST(test_is_int_boxed);
    RUN_TEST(test_is_int_bool);
    RUN_TEST(test_is_int_char);
    RUN_TEST(test_is_int_float);
    RUN_TEST(test_is_int_null);

    TEST_SECTION("is_char");
    RUN_TEST(test_is_char_immediate);
    RUN_TEST(test_is_char_int);
    RUN_TEST(test_is_char_null);

    TEST_SECTION("Arithmetic with Immediates");
    RUN_TEST(test_add_immediates);
    RUN_TEST(test_sub_immediates);
    RUN_TEST(test_mul_immediates);
    RUN_TEST(test_div_immediates);
    RUN_TEST(test_mod_immediates);
    RUN_TEST(test_add_mixed_imm_boxed);

    TEST_SECTION("Comparison with Immediates");
    RUN_TEST(test_lt_immediates);
    RUN_TEST(test_gt_immediates);
    RUN_TEST(test_eq_immediates);
    RUN_TEST(test_le_immediates);
    RUN_TEST(test_ge_immediates);

    TEST_SECTION("Reference Counting with Immediates");
    RUN_TEST(test_inc_ref_immediate);
    RUN_TEST(test_dec_ref_immediate);
    RUN_TEST(test_free_obj_immediate);
    RUN_TEST(test_free_unique_immediate);

    TEST_SECTION("Stress Tests");
    RUN_TEST(test_tagged_stress_many_ints);
    RUN_TEST(test_tagged_stress_mixed_ops);
    RUN_TEST(test_tagged_stress_chars);
}
