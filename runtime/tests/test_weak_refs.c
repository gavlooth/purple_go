/* test_weak_refs.c - Weak reference tests */
#include "test_framework.h"

/* ========== Weak Reference Creation Tests ========== */

void test_mk_weak_ref(void) {
    int dummy = 42;
    _InternalWeakRef* w = _mk_weak_ref(&dummy);
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(w->target, &dummy);
    ASSERT_EQ(w->alive, 1);
    PASS();
}

void test_mk_weak_ref_null_target(void) {
    _InternalWeakRef* w = _mk_weak_ref(NULL);
    ASSERT_NOT_NULL(w);
    ASSERT_NULL(w->target);
    ASSERT_EQ(w->alive, 1);
    PASS();
}

/* ========== Weak Reference Deref Tests ========== */

void test_deref_weak_valid(void) {
    int dummy = 42;
    _InternalWeakRef* w = _mk_weak_ref(&dummy);
    void* deref = _deref_weak(w);
    ASSERT_EQ(deref, &dummy);
    PASS();
}

void test_deref_weak_null(void) {
    void* deref = _deref_weak(NULL);
    ASSERT_NULL(deref);
    PASS();
}

void test_deref_weak_invalidated(void) {
    int dummy = 42;
    _InternalWeakRef* w = _mk_weak_ref(&dummy);
    _invalidate_weak(w);
    void* deref = _deref_weak(w);
    ASSERT_NULL(deref);
    PASS();
}

/* ========== Weak Reference Invalidation Tests ========== */

void test_invalidate_weak(void) {
    int dummy = 42;
    _InternalWeakRef* w = _mk_weak_ref(&dummy);
    ASSERT_EQ(w->alive, 1);
    _invalidate_weak(w);
    ASSERT_EQ(w->alive, 0);
    PASS();
}

void test_invalidate_weak_null(void) {
    _invalidate_weak(NULL);  /* Should not crash */
    PASS();
}

void test_invalidate_weak_twice(void) {
    int dummy = 42;
    _InternalWeakRef* w = _mk_weak_ref(&dummy);
    _invalidate_weak(w);
    _invalidate_weak(w);  /* Should be idempotent */
    ASSERT_EQ(w->alive, 0);
    PASS();
}

/* ========== invalidate_weak_refs_for Tests ========== */

void test_invalidate_weak_refs_for_single(void) {
    int dummy = 42;
    _InternalWeakRef* w = _mk_weak_ref(&dummy);
    ASSERT_EQ(w->alive, 1);
    invalidate_weak_refs_for(&dummy);
    ASSERT_EQ(w->alive, 0);
    PASS();
}

void test_invalidate_weak_refs_for_multiple(void) {
    int dummy = 42;
    _InternalWeakRef* w1 = _mk_weak_ref(&dummy);
    _InternalWeakRef* w2 = _mk_weak_ref(&dummy);
    _InternalWeakRef* w3 = _mk_weak_ref(&dummy);

    invalidate_weak_refs_for(&dummy);

    ASSERT_EQ(w1->alive, 0);
    ASSERT_EQ(w2->alive, 0);
    ASSERT_EQ(w3->alive, 0);
    PASS();
}

void test_invalidate_weak_refs_for_selective(void) {
    int d1 = 1, d2 = 2;
    _InternalWeakRef* w1 = _mk_weak_ref(&d1);
    _InternalWeakRef* w2 = _mk_weak_ref(&d2);

    invalidate_weak_refs_for(&d1);

    ASSERT_EQ(w1->alive, 0);  /* d1 refs invalidated */
    ASSERT_EQ(w2->alive, 1);  /* d2 refs still alive */
    PASS();
}

void test_invalidate_weak_refs_for_null(void) {
    invalidate_weak_refs_for(NULL);  /* Should not crash */
    PASS();
}

/* ========== Weak References with Obj ========== */

void test_weak_ref_to_obj(void) {
    Obj* obj = mk_int(42);
    _InternalWeakRef* w = _mk_weak_ref(obj);
    ASSERT_EQ(w->target, obj);
    ASSERT_EQ(w->alive, 1);

    /* Deref should return the Obj */
    Obj* deref = (Obj*)_deref_weak(w);
    ASSERT_EQ(deref, obj);

    dec_ref(obj);
    ASSERT_EQ(w->alive, 0);
    ASSERT_NULL(_deref_weak(w));
    PASS();
}

void test_weak_ref_invalidated_on_free(void) {
    Obj* obj = mk_int(42);
    _InternalWeakRef* w = _mk_weak_ref(obj);

    /* free_obj defers to free list; flush to trigger invalidation */
    free_obj(obj);
    flush_freelist();

    ASSERT_EQ(w->alive, 0);
    void* deref = _deref_weak(w);
    ASSERT_NULL(deref);
    PASS();
}

void test_weak_ref_invalidated_on_dec_ref(void) {
    Obj* obj = mk_int(42);
    _InternalWeakRef* w = _mk_weak_ref(obj);

    /* dec_ref to zero should invalidate weak refs */
    dec_ref(obj);

    ASSERT_EQ(w->alive, 0);
    PASS();
}

/* ========== Weak References with Pairs ========== */

void test_weak_ref_to_pair(void) {
    Obj* pair = mk_pair(mk_int(1), mk_int(2));
    _InternalWeakRef* w = _mk_weak_ref(pair);

    ASSERT_EQ(w->alive, 1);
    Obj* deref = (Obj*)_deref_weak(w);
    ASSERT_EQ(deref, pair);
    ASSERT_EQ(deref->tag, TAG_PAIR);

    dec_ref(pair);
    PASS();
}

void test_weak_ref_to_nested_pair(void) {
    Obj* inner = mk_pair(mk_int(1), mk_int(2));
    Obj* outer = mk_pair(inner, mk_int(3));
    _InternalWeakRef* w_outer = _mk_weak_ref(outer);
    _InternalWeakRef* w_inner = _mk_weak_ref(inner);

    ASSERT_EQ(w_outer->alive, 1);
    ASSERT_EQ(w_inner->alive, 1);

    dec_ref(outer);

    /* Both should be invalidated */
    ASSERT_EQ(w_outer->alive, 0);
    ASSERT_EQ(w_inner->alive, 0);
    PASS();
}

/* ========== Weak Reference Registry Tests ========== */

void test_weak_ref_registry_grows(void) {
    _InternalWeakRefNode* old_head = _WEAK_REF_HEAD;
    int dummies[10];

    for (int i = 0; i < 10; i++) {
        dummies[i] = i;
        _mk_weak_ref(&dummies[i]);
    }

    /* Registry should have grown */
    ASSERT(_WEAK_REF_HEAD != old_head);
    PASS();
}

/* ========== Stress Tests ========== */

void test_weak_ref_stress_many_refs(void) {
    int dummies[100];
    _InternalWeakRef* refs[100];

    for (int i = 0; i < 100; i++) {
        dummies[i] = i;
        refs[i] = _mk_weak_ref(&dummies[i]);
        ASSERT_NOT_NULL(refs[i]);
        ASSERT_EQ(refs[i]->alive, 1);
    }

    /* Invalidate all */
    for (int i = 0; i < 100; i++) {
        invalidate_weak_refs_for(&dummies[i]);
        ASSERT_EQ(refs[i]->alive, 0);
    }
    PASS();
}

void test_weak_ref_stress_many_to_same_target(void) {
    int dummy = 42;
    _InternalWeakRef* refs[50];

    for (int i = 0; i < 50; i++) {
        refs[i] = _mk_weak_ref(&dummy);
    }

    /* All should be alive */
    for (int i = 0; i < 50; i++) {
        ASSERT_EQ(refs[i]->alive, 1);
    }

    /* Invalidate should affect all */
    invalidate_weak_refs_for(&dummy);

    for (int i = 0; i < 50; i++) {
        ASSERT_EQ(refs[i]->alive, 0);
    }
    PASS();
}

void test_weak_ref_stress_with_objs(void) {
    Obj* objs[100];
    _InternalWeakRef* refs[100];

    for (int i = 0; i < 100; i++) {
        objs[i] = mk_int(i);
        refs[i] = _mk_weak_ref(objs[i]);
    }

    /* Free half - deferred, so flush afterwards */
    for (int i = 0; i < 50; i++) {
        free_obj(objs[i]);
    }
    flush_freelist();

    /* First half should be invalidated */
    for (int i = 0; i < 50; i++) {
        ASSERT_EQ(refs[i]->alive, 0);
    }

    /* Second half should still be alive */
    for (int i = 50; i < 100; i++) {
        ASSERT_EQ(refs[i]->alive, 1);
        dec_ref(objs[i]);
    }
    PASS();
}

/* ========== Edge Cases ========== */

void test_weak_ref_deref_after_multiple_invalidations(void) {
    int dummy = 42;
    _InternalWeakRef* w = _mk_weak_ref(&dummy);

    _invalidate_weak(w);
    _invalidate_weak(w);
    _invalidate_weak(w);

    void* deref = _deref_weak(w);
    ASSERT_NULL(deref);
    PASS();
}

void test_weak_ref_to_stack_pool_obj(void) {
    int old_ptr = STACK_PTR;
    Obj* obj = mk_int_stack(42);
    _InternalWeakRef* w = _mk_weak_ref(obj);

    ASSERT_EQ(w->target, obj);
    ASSERT_EQ(w->alive, 1);

    /* Stack objects shouldn't be freed normally */
    STACK_PTR = old_ptr;
    PASS();
}

void test_weak_ref_to_immediate(void) {
    Obj* imm = mk_int_unboxed(42);
    _InternalWeakRef* w = _mk_weak_ref(imm);

    /* Immediates don't have identity but can still be pointed to */
    ASSERT_EQ(w->target, imm);
    ASSERT_EQ(w->alive, 1);
    PASS();
}

/* ========== Run All Weak Reference Tests ========== */

void run_weak_refs_tests(void) {
    TEST_SUITE("Weak References");

    TEST_SECTION("Creation");
    RUN_TEST(test_mk_weak_ref);
    RUN_TEST(test_mk_weak_ref_null_target);

    TEST_SECTION("Deref");
    RUN_TEST(test_deref_weak_valid);
    RUN_TEST(test_deref_weak_null);
    RUN_TEST(test_deref_weak_invalidated);

    TEST_SECTION("Invalidation");
    RUN_TEST(test_invalidate_weak);
    RUN_TEST(test_invalidate_weak_null);
    RUN_TEST(test_invalidate_weak_twice);

    TEST_SECTION("invalidate_weak_refs_for");
    RUN_TEST(test_invalidate_weak_refs_for_single);
    RUN_TEST(test_invalidate_weak_refs_for_multiple);
    RUN_TEST(test_invalidate_weak_refs_for_selective);
    RUN_TEST(test_invalidate_weak_refs_for_null);

    TEST_SECTION("With Obj");
    RUN_TEST(test_weak_ref_to_obj);
    RUN_TEST(test_weak_ref_invalidated_on_free);
    RUN_TEST(test_weak_ref_invalidated_on_dec_ref);

    TEST_SECTION("With Pairs");
    RUN_TEST(test_weak_ref_to_pair);
    RUN_TEST(test_weak_ref_to_nested_pair);

    TEST_SECTION("Registry");
    RUN_TEST(test_weak_ref_registry_grows);

    TEST_SECTION("Stress Tests");
    RUN_TEST(test_weak_ref_stress_many_refs);
    RUN_TEST(test_weak_ref_stress_many_to_same_target);
    RUN_TEST(test_weak_ref_stress_with_objs);

    TEST_SECTION("Edge Cases");
    RUN_TEST(test_weak_ref_deref_after_multiple_invalidations);
    RUN_TEST(test_weak_ref_to_stack_pool_obj);
    RUN_TEST(test_weak_ref_to_immediate);
}
