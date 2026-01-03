/* Test Memory Management */
#include "test_framework.h"

/* === inc_ref / dec_ref tests === */

void test_inc_ref_normal(void) {
    Obj* x = mk_int(42);
    ASSERT_EQ(x->mark, 1);
    inc_ref(x);
    ASSERT_EQ(x->mark, 2);
    dec_ref(x);
    ASSERT_EQ(x->mark, 1);
    dec_ref(x);  /* Should free */
    PASS();
}

void test_inc_ref_null(void) {
    inc_ref(NULL);  /* Should not crash */
    PASS();
}

void test_dec_ref_null(void) {
    dec_ref(NULL);  /* Should not crash */
    PASS();
}

void test_dec_ref_to_zero(void) {
    Obj* x = mk_int(42);
    ASSERT_EQ(x->mark, 1);
    dec_ref(x);  /* mark becomes 0, object freed */
    /* x is now invalid, can't check */
    PASS();
}

void test_inc_ref_multiple(void) {
    Obj* x = mk_int(42);
    for (int i = 0; i < 100; i++) {
        inc_ref(x);
    }
    ASSERT_EQ(x->mark, 101);
    for (int i = 0; i < 101; i++) {
        dec_ref(x);
    }
    PASS();
}

void test_ref_stack_obj(void) {
    int old_ptr = STACK_PTR;
    Obj* x = mk_int_stack(42);
    int initial_mark = x->mark;

    /* inc_ref and dec_ref should be no-ops for stack objects */
    inc_ref(x);
    ASSERT_EQ(x->mark, initial_mark);  /* Unchanged */
    dec_ref(x);
    ASSERT_EQ(x->mark, initial_mark);  /* Unchanged */

    STACK_PTR = old_ptr;
    PASS();
}

/* === is_stack_obj tests === */

void test_is_stack_obj_true(void) {
    int old_ptr = STACK_PTR;
    Obj* x = mk_int_stack(42);
    ASSERT(is_stack_obj(x));
    STACK_PTR = old_ptr;
    PASS();
}

void test_is_stack_obj_false(void) {
    Obj* x = mk_int(42);
    ASSERT(!is_stack_obj(x));
    dec_ref(x);
    PASS();
}

void test_is_stack_obj_null(void) {
    ASSERT(!is_stack_obj(NULL));
    PASS();
}

/* === is_nil tests === */

void test_is_nil_null(void) {
    ASSERT(is_nil(NULL));
    PASS();
}

void test_is_nil_non_null(void) {
    Obj* x = mk_int(42);
    ASSERT(!is_nil(x));
    dec_ref(x);
    PASS();
}

/* === free_obj / free_tree tests === */

void test_free_obj_normal(void) {
    Obj* x = mk_int(42);
    free_obj(x);
    /* Object is in free list, will be freed on flush */
    flush_freelist();
    PASS();
}

void test_free_obj_null(void) {
    free_obj(NULL);  /* Should not crash */
    PASS();
}

void test_free_tree_single(void) {
    Obj* x = mk_int(42);
    free_tree(x);
    PASS();
}

void test_free_tree_pair(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* p = mk_pair(a, b);
    free_tree(p);
    PASS();
}

void test_free_tree_nested(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* c = mk_int(3);
    Obj* d = mk_int(4);
    Obj* p1 = mk_pair(a, b);
    Obj* p2 = mk_pair(c, d);
    Obj* outer = mk_pair(p1, p2);
    free_tree(outer);
    PASS();
}

void test_free_tree_null(void) {
    free_tree(NULL);  /* Should not crash */
    PASS();
}

void test_free_tree_deep(void) {
    /* Create a deeply nested list */
    Obj* list = NULL;
    for (int i = 0; i < 100; i++) {
        Obj* elem = mk_int(i);
        list = mk_pair(elem, list);
    }
    free_tree(list);
    PASS();
}

/* === free_unique tests === */

void test_free_unique_normal(void) {
    Obj* x = mk_int(42);
    free_unique(x);
    PASS();
}

void test_free_unique_null(void) {
    free_unique(NULL);
    PASS();
}

void test_free_unique_pair(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* p = mk_pair(a, b);
    free_unique(p);
    PASS();
}

/* === flush_freelist tests === */

void test_flush_freelist_empty(void) {
    /* Ensure free list is empty */
    flush_freelist();
    /* Flush again - should be no-op */
    flush_freelist();
    PASS();
}

void test_flush_freelist_single(void) {
    Obj* x = mk_int(42);
    free_obj(x);
    flush_freelist();
    PASS();
}

void test_flush_freelist_multiple(void) {
    for (int i = 0; i < 10; i++) {
        Obj* x = mk_int(i);
        free_obj(x);
    }
    flush_freelist();
    PASS();
}

/* === Deferred reference counting tests === */

void test_defer_decrement_normal(void) {
    Obj* x = mk_int(42);
    inc_ref(x);  /* refcount = 2 */
    defer_decrement(x);
    /* x still alive until flush */
    ASSERT_EQ(x->mark, 2);
    flush_deferred();
    ASSERT_EQ(x->mark, 1);
    dec_ref(x);
    PASS();
}

void test_defer_decrement_coalesce(void) {
    Obj* x = mk_int(42);
    inc_ref(x);
    inc_ref(x);
    inc_ref(x);  /* refcount = 4 */

    defer_decrement(x);
    defer_decrement(x);  /* Should coalesce */

    flush_deferred();
    ASSERT_EQ(x->mark, 2);
    dec_ref(x);
    dec_ref(x);
    PASS();
}

void test_safe_point_below_threshold(void) {
    /* Safe point should be fast when nothing pending */
    safe_point();
    PASS();
}

void test_safe_point_above_threshold(void) {
    /* Create many deferred decrements */
    Obj* objs[100];
    for (int i = 0; i < 100; i++) {
        objs[i] = mk_int(i);
        inc_ref(objs[i]);  /* Ensure they survive */
        defer_decrement(objs[i]);
    }

    /* Safe point should process some */
    safe_point();

    /* Cleanup */
    flush_deferred();
    for (int i = 0; i < 100; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

/* === Box operations tests === */

void test_box_get_normal(void) {
    Obj* v = mk_int(42);
    Obj* b = mk_box(v);
    Obj* got = box_get(b);
    ASSERT_EQ(got, v);
    dec_ref(b);
    dec_ref(v);
    PASS();
}

void test_box_get_empty(void) {
    Obj* b = mk_box(NULL);
    Obj* got = box_get(b);
    ASSERT_NULL(got);
    dec_ref(b);
    PASS();
}

void test_box_get_null_box(void) {
    Obj* got = box_get(NULL);
    ASSERT_NULL(got);
    PASS();
}

void test_box_get_wrong_tag(void) {
    Obj* x = mk_int(42);
    Obj* got = box_get(x);  /* x is not a box */
    ASSERT_NULL(got);
    dec_ref(x);
    PASS();
}

void test_box_set_normal(void) {
    Obj* v1 = mk_int(1);
    Obj* v2 = mk_int(2);
    Obj* b = mk_box(v1);

    box_set(b, v2);
    ASSERT_EQ(box_get(b), v2);

    dec_ref(b);
    dec_ref(v1);
    dec_ref(v2);
    PASS();
}

void test_box_set_null_value(void) {
    Obj* v = mk_int(42);
    Obj* b = mk_box(v);

    box_set(b, NULL);
    ASSERT_NULL(box_get(b));

    dec_ref(b);
    dec_ref(v);
    PASS();
}

void test_box_set_null_box(void) {
    Obj* v = mk_int(42);
    box_set(NULL, v);  /* Should not crash */
    dec_ref(v);
    PASS();
}

/* === Pair access tests === */

void test_obj_car_normal(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* p = mk_pair(a, b);
    Obj* car = obj_car(p);
    ASSERT_EQ(car, a);
    dec_ref(car);
    /* free_tree recursively frees the pair and its children */
    free_tree(p);
    PASS();
}

void test_obj_cdr_normal(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* p = mk_pair(a, b);
    Obj* cdr = obj_cdr(p);
    ASSERT_EQ(cdr, b);
    dec_ref(cdr);
    /* free_tree recursively frees the pair and its children */
    free_tree(p);
    PASS();
}

void test_obj_car_null(void) {
    Obj* result = obj_car(NULL);
    ASSERT_NULL(result);
    PASS();
}

void test_obj_cdr_null(void) {
    Obj* result = obj_cdr(NULL);
    ASSERT_NULL(result);
    PASS();
}

void test_obj_car_non_pair(void) {
    Obj* x = mk_int(42);
    Obj* result = obj_car(x);
    ASSERT_NULL(result);
    dec_ref(x);
    PASS();
}

void test_obj_cdr_non_pair(void) {
    Obj* x = mk_int(42);
    Obj* result = obj_cdr(x);
    ASSERT_NULL(result);
    dec_ref(x);
    PASS();
}

/* === release_children tests === */

void test_release_children_pair(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    inc_ref(a);
    inc_ref(b);
    Obj* p = mk_pair(a, b);

    release_children(p);
    ASSERT_EQ(a->mark, 1);
    ASSERT_EQ(b->mark, 1);

    p->a = NULL;
    p->b = NULL;
    dec_ref(p);
    dec_ref(a);
    dec_ref(b);
    PASS();
}

void test_release_children_box(void) {
    Obj* v = mk_int(9);
    Obj* b = mk_box(v);

    release_children(b);
    ASSERT_EQ(v->mark, 1);

    b->ptr = NULL;
    dec_ref(b);
    dec_ref(v);
    PASS();
}

static Obj* mem_return_1(Obj** caps, Obj** args, int nargs) {
    (void)caps; (void)args; (void)nargs;
    return mk_int(1);
}

void test_release_children_closure(void) {
    Obj* cap = mk_int(7);
    Obj* caps[1] = {cap};
    Obj* clos = mk_closure(mem_return_1, caps, NULL, 1, 0);

    release_children(clos);
    ASSERT_EQ(cap->mark, 1);

    clos->ptr = NULL;
    dec_ref(clos);
    dec_ref(cap);
    PASS();
}

void test_release_children_sym_error(void) {
    Obj* s = mk_sym("sym");
    Obj* e = mk_error("err");

    release_children(s);
    release_children(e);

    s->ptr = NULL;
    e->ptr = NULL;
    dec_ref(s);
    dec_ref(e);
    PASS();
}

void test_release_children_channel(void) {
    Obj* ch = make_channel(1);
    release_children(ch);
    ASSERT_NULL(ch->ptr);
    dec_ref(ch);
    PASS();
}

void test_release_children_atom(void) {
    Obj* init = mk_int(3);
    Obj* a = make_atom(init);
    dec_ref(init);
    release_children(a);
    ASSERT_NULL(a->ptr);
    dec_ref(a);
    PASS();
}

static Obj* mem_thread_fn(Obj** caps, Obj** args, int nargs) {
    (void)caps; (void)args; (void)nargs;
    return mk_int(1);
}

void test_release_children_thread(void) {
    Obj* clos = mk_closure(mem_thread_fn, NULL, NULL, 0, 0);
    Obj* th = spawn_thread(clos);
    release_children(th);
    ASSERT_NULL(th->ptr);
    dec_ref(th);
    dec_ref(clos);
    PASS();
}

void test_free_tree_immediate(void) {
    Obj* imm = mk_int_unboxed(42);
    free_tree(imm);  /* Should be no-op */
    ASSERT_EQ(INT_IMM_VALUE(imm), 42);
    PASS();
}

/* === Run all memory tests === */

void run_memory_tests(void) {
    TEST_SUITE("Memory Management");

    /* inc_ref / dec_ref */
    RUN_TEST(test_inc_ref_normal);
    RUN_TEST(test_inc_ref_null);
    RUN_TEST(test_dec_ref_null);
    RUN_TEST(test_dec_ref_to_zero);
    RUN_TEST(test_inc_ref_multiple);
    RUN_TEST(test_ref_stack_obj);

    /* is_stack_obj */
    RUN_TEST(test_is_stack_obj_true);
    RUN_TEST(test_is_stack_obj_false);
    RUN_TEST(test_is_stack_obj_null);

    /* is_nil */
    RUN_TEST(test_is_nil_null);
    RUN_TEST(test_is_nil_non_null);

    /* free_obj / free_tree */
    RUN_TEST(test_free_obj_normal);
    RUN_TEST(test_free_obj_null);
    RUN_TEST(test_free_tree_single);
    RUN_TEST(test_free_tree_pair);
    RUN_TEST(test_free_tree_nested);
    RUN_TEST(test_free_tree_null);
    RUN_TEST(test_free_tree_deep);

    /* free_unique */
    RUN_TEST(test_free_unique_normal);
    RUN_TEST(test_free_unique_null);
    RUN_TEST(test_free_unique_pair);

    /* flush_freelist */
    RUN_TEST(test_flush_freelist_empty);
    RUN_TEST(test_flush_freelist_single);
    RUN_TEST(test_flush_freelist_multiple);

    /* Deferred RC */
    RUN_TEST(test_defer_decrement_normal);
    RUN_TEST(test_defer_decrement_coalesce);
    RUN_TEST(test_safe_point_below_threshold);
    RUN_TEST(test_safe_point_above_threshold);

    /* Box operations */
    RUN_TEST(test_box_get_normal);
    RUN_TEST(test_box_get_empty);
    RUN_TEST(test_box_get_null_box);
    RUN_TEST(test_box_get_wrong_tag);
    RUN_TEST(test_box_set_normal);
    RUN_TEST(test_box_set_null_value);
    RUN_TEST(test_box_set_null_box);

    /* Pair access */
    RUN_TEST(test_obj_car_normal);
    RUN_TEST(test_obj_cdr_normal);
    RUN_TEST(test_obj_car_null);
    RUN_TEST(test_obj_cdr_null);
    RUN_TEST(test_obj_car_non_pair);
    RUN_TEST(test_obj_cdr_non_pair);

    TEST_SECTION("release_children");
    RUN_TEST(test_release_children_pair);
    RUN_TEST(test_release_children_box);
    RUN_TEST(test_release_children_closure);
    RUN_TEST(test_release_children_sym_error);
    RUN_TEST(test_release_children_channel);
    RUN_TEST(test_release_children_atom);
    RUN_TEST(test_release_children_thread);
    RUN_TEST(test_free_tree_immediate);
}
