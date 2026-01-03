/* test_borrowref.c - BorrowRef and IPGE tests */
#include "test_framework.h"

/* ========== IPGE Evolution Tests ========== */
/*
 * Note: Generation type is uint16_t in compact mode, uint64_t in robust mode.
 * These tests work for both modes.
 */

void test_ipge_evolve_changes(void) {
    Generation gen1 = 12345;
    Generation gen2 = ipge_evolve(gen1);
    ASSERT(gen1 != gen2);
    PASS();
}

void test_ipge_evolve_deterministic(void) {
    Generation gen = 12345;
    Generation evolved1 = ipge_evolve(gen);
    Generation evolved2 = ipge_evolve(gen);
    ASSERT_EQ(evolved1, evolved2);
    PASS();
}

void test_ipge_evolve_chain(void) {
    Generation gen = 1;
    Generation prev = gen;
    for (int i = 0; i < 100; i++) {
        gen = ipge_evolve(gen);
        ASSERT(gen != prev);
        prev = gen;
    }
    PASS();
}

/* Test 64-bit evolution (for seed generation) */
void test_ipge_evolve64_chain(void) {
    uint64_t gen = 1;
    uint64_t prev = gen;
    for (int i = 0; i < 100; i++) {
        gen = ipge_evolve64(gen);
        ASSERT(gen != prev);
        prev = gen;
    }
    PASS();
}

/* ========== BorrowRef Creation Tests ========== */

void test_borrow_create(void) {
    Obj* obj = mk_int(42);
    BorrowRef* ref = borrow_create(obj, "test");
    ASSERT_NOT_NULL(ref);
    ASSERT_EQ(ref->ipge_target, obj);
    ASSERT_EQ(ref->remembered_gen, obj->generation);
    ASSERT_STR_EQ(ref->source_desc, "test");
    borrow_release(ref);
    dec_ref(obj);
    PASS();
}

void test_borrow_create_null_obj(void) {
    BorrowRef* ref = borrow_create(NULL, "test");
    ASSERT_NULL(ref);
    PASS();
}

void test_borrow_create_immediate(void) {
    Obj* imm = mk_int_unboxed(42);
    BorrowRef* ref = borrow_create(imm, "test");
    ASSERT_NULL(ref);  /* Immediates cannot have borrowed refs */
    PASS();
}

void test_borrow_create_null_desc(void) {
    Obj* obj = mk_int(42);
    BorrowRef* ref = borrow_create(obj, NULL);
    ASSERT_NOT_NULL(ref);
    ASSERT_NULL(ref->source_desc);
    borrow_release(ref);
    dec_ref(obj);
    PASS();
}

/* ========== BorrowRef Validity Tests ========== */

void test_borrow_is_valid_fresh(void) {
    Obj* obj = mk_int(42);
    BorrowRef* ref = borrow_create(obj, "test");
    ASSERT(borrow_is_valid(ref));
    borrow_release(ref);
    dec_ref(obj);
    PASS();
}

void test_borrow_is_valid_null(void) {
    ASSERT(!borrow_is_valid(NULL));
    PASS();
}

void test_borrow_is_valid_after_evolve(void) {
    Obj* obj = mk_int(42);
    BorrowRef* ref = borrow_create(obj, "test");

    /* Manually evolve generation (simulating free) */
    obj->generation = ipge_evolve(obj->generation);

    ASSERT(!borrow_is_valid(ref));
    borrow_release(ref);
    PASS();
}

void test_borrow_is_valid_after_invalidate(void) {
    Obj* obj = mk_int(42);
    BorrowRef* ref = borrow_create(obj, "test");

    borrow_invalidate_obj(obj);

    ASSERT(!borrow_is_valid(ref));
    borrow_release(ref);
    PASS();
}

void test_borrow_is_valid_zero_gen(void) {
    Obj* obj = mk_int(42);
    BorrowRef* ref = borrow_create(obj, "test");

    obj->generation = 0;  /* Mark as freed */

    ASSERT(!borrow_is_valid(ref));
    borrow_release(ref);
    PASS();
}

/* ========== borrow_invalidate_obj Tests ========== */

void test_borrow_invalidate_obj(void) {
    Obj* obj = mk_int(42);
    Generation old_gen = obj->generation;
    borrow_invalidate_obj(obj);
    ASSERT(obj->generation != old_gen);
    dec_ref(obj);
    PASS();
}

/* ========== Generation Type Tests ========== */

void test_generation_size(void) {
    /* Generation type is uint16_t in compact mode, uint64_t in robust mode */
#if IPGE_ROBUST_MODE
    ASSERT_EQ(sizeof(Generation), 8);
#else
    ASSERT_EQ(sizeof(Generation), 2);
#endif
    PASS();
}

void test_borrowref_size(void) {
    /* BorrowRef is heap-allocated struct */
    ASSERT(sizeof(BorrowRef) >= sizeof(void*) + sizeof(Generation));
    PASS();
}

void test_borrow_invalidate_obj_null(void) {
    borrow_invalidate_obj(NULL);  /* Should not crash */
    PASS();
}

void test_borrow_invalidate_obj_immediate(void) {
    Obj* imm = mk_int_unboxed(42);
    borrow_invalidate_obj(imm);  /* Should be no-op */
    PASS();
}

/* ========== BorrowRef Release Tests ========== */

void test_borrow_release(void) {
    Obj* obj = mk_int(42);
    BorrowRef* ref = borrow_create(obj, "test");
    borrow_release(ref);  /* Should free the ref */
    dec_ref(obj);
    PASS();
}

void test_borrow_release_null(void) {
    borrow_release(NULL);  /* Should not crash */
    PASS();
}

/* ========== Multiple BorrowRefs Tests ========== */

void test_multiple_borrow_refs_same_obj(void) {
    Obj* obj = mk_int(42);
    BorrowRef* ref1 = borrow_create(obj, "ref1");
    BorrowRef* ref2 = borrow_create(obj, "ref2");
    BorrowRef* ref3 = borrow_create(obj, "ref3");

    ASSERT(borrow_is_valid(ref1));
    ASSERT(borrow_is_valid(ref2));
    ASSERT(borrow_is_valid(ref3));

    borrow_invalidate_obj(obj);

    ASSERT(!borrow_is_valid(ref1));
    ASSERT(!borrow_is_valid(ref2));
    ASSERT(!borrow_is_valid(ref3));

    borrow_release(ref1);
    borrow_release(ref2);
    borrow_release(ref3);
    PASS();
}

void test_borrow_refs_different_objs(void) {
    Obj* obj1 = mk_int(1);
    Obj* obj2 = mk_int(2);
    BorrowRef* ref1 = borrow_create(obj1, "ref1");
    BorrowRef* ref2 = borrow_create(obj2, "ref2");

    ASSERT(borrow_is_valid(ref1));
    ASSERT(borrow_is_valid(ref2));

    borrow_invalidate_obj(obj1);

    ASSERT(!borrow_is_valid(ref1));
    ASSERT(borrow_is_valid(ref2));  /* Still valid */

    borrow_release(ref1);
    borrow_release(ref2);
    dec_ref(obj2);
    PASS();
}

/* ========== BorrowRef with Closures Tests ========== */

static Obj* return_captured_borrow(Obj** captures, Obj** args, int nargs) {
    (void)args; (void)nargs;
    if (!captures || !captures[0]) return NULL;
    inc_ref(captures[0]);
    return captures[0];
}

void test_borrow_in_closure(void) {
    Obj* cap = mk_int(42);
    BorrowRef* ref = borrow_create(cap, "closure_capture");
    Obj* caps[1] = {cap};
    BorrowRef* refs[1] = {ref};

    Obj* closure = mk_closure(return_captured_borrow, caps, refs, 1, 0);
    ASSERT_NOT_NULL(closure);

    /* Closure should validate captures before calling */
    ASSERT(closure_validate((Closure*)closure->ptr));

    Obj* result = call_closure(closure, NULL, 0);
    ASSERT_EQ(obj_to_int(result), 42);

    dec_ref(result);
    dec_ref(closure);
    dec_ref(cap);
    PASS();
}

void test_borrow_invalid_in_closure(void) {
    Obj* cap = mk_int(42);
    BorrowRef* ref = borrow_create(cap, "closure_capture");
    Obj* caps[1] = {cap};
    BorrowRef* refs[1] = {ref};

    Obj* closure = mk_closure(return_captured_borrow, caps, refs, 1, 0);

    /* Invalidate the capture */
    borrow_invalidate_obj(cap);

    /* Closure validation should fail */
    ASSERT(!closure_validate((Closure*)closure->ptr));

    dec_ref(closure);
    dec_ref(cap);
    PASS();
}

/* ========== IPGE Integration Tests ========== */

void test_ipge_on_free_obj(void) {
    Obj* obj = mk_int(42);
    BorrowRef* ref = borrow_create(obj, "test");

    ASSERT(borrow_is_valid(ref));

    free_obj(obj);

    /* After free_obj, generation should have evolved */
    ASSERT(!borrow_is_valid(ref));
    borrow_release(ref);
    PASS();
}

void test_ipge_on_dec_ref_to_zero(void) {
    Obj* obj = mk_int(42);
    BorrowRef* ref = borrow_create(obj, "test");

    ASSERT(borrow_is_valid(ref));

    dec_ref(obj);

    /* After dec_ref to zero, generation should have evolved */
    ASSERT(!borrow_is_valid(ref));
    borrow_release(ref);
    PASS();
}

void test_ipge_on_free_tree(void) {
    Obj* tree = mk_pair(mk_int(1), mk_pair(mk_int(2), mk_int(3)));
    BorrowRef* ref = borrow_create(tree, "tree");

    ASSERT(borrow_is_valid(ref));

    free_tree(tree);

    ASSERT(!borrow_is_valid(ref));
    borrow_release(ref);
    PASS();
}

void test_ipge_on_free_unique(void) {
    Obj* obj = mk_int(42);
    BorrowRef* ref = borrow_create(obj, "test");

    ASSERT(borrow_is_valid(ref));

    free_unique(obj);

    ASSERT(!borrow_is_valid(ref));
    borrow_release(ref);
    PASS();
}

/* ========== Generation Uniqueness Tests ========== */

void test_generations_unique(void) {
    Obj* objs[100];
    for (int i = 0; i < 100; i++) {
        objs[i] = mk_int(i);
    }

    /* Check all generations are unique */
    for (int i = 0; i < 100; i++) {
        for (int j = i + 1; j < 100; j++) {
            ASSERT(objs[i]->generation != objs[j]->generation);
        }
    }

    for (int i = 0; i < 100; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

void test_generation_never_zero(void) {
    for (int i = 0; i < 100; i++) {
        Obj* obj = mk_int(i);
        ASSERT(obj->generation != 0);
        dec_ref(obj);
    }
    PASS();
}

/* ========== Stress Tests ========== */

void test_borrow_stress_many_refs(void) {
    Obj* obj = mk_int(42);
    BorrowRef* refs[100];

    for (int i = 0; i < 100; i++) {
        refs[i] = borrow_create(obj, "stress");
        ASSERT_NOT_NULL(refs[i]);
        ASSERT(borrow_is_valid(refs[i]));
    }

    borrow_invalidate_obj(obj);

    for (int i = 0; i < 100; i++) {
        ASSERT(!borrow_is_valid(refs[i]));
        borrow_release(refs[i]);
    }
    PASS();
}

void test_borrow_stress_create_release_cycle(void) {
    for (int i = 0; i < 1000; i++) {
        Obj* obj = mk_int(i);
        BorrowRef* ref = borrow_create(obj, "cycle");
        ASSERT(borrow_is_valid(ref));
        borrow_release(ref);
        dec_ref(obj);
    }
    PASS();
}

void test_ipge_stress_many_evolutions(void) {
    uint64_t gen = 1;
    for (int i = 0; i < 10000; i++) {
        uint64_t next = ipge_evolve(gen);
        ASSERT(gen != next);
        gen = next;
    }
    PASS();
}

/* ========== Edge Cases ========== */

void test_borrow_ref_after_realloc(void) {
    /* Test that borrow ref correctly detects reused memory */
    Obj* obj1 = mk_int(42);
    BorrowRef* ref = borrow_create(obj1, "test");
    uint64_t old_gen = ref->remembered_gen;

    free_obj(obj1);

    /* ref is now invalid */
    ASSERT(!borrow_is_valid(ref));

    /* Allocate new object - might get same memory */
    Obj* obj2 = mk_int(99);

    /* Even if same memory, generation is different */
    if (ref->ipge_target == obj2) {
        /* Same memory reused, but generation evolved */
        ASSERT(obj2->generation != old_gen);
        ASSERT(!borrow_is_valid(ref));  /* Still invalid */
    }

    borrow_release(ref);
    dec_ref(obj2);
    PASS();
}

void test_borrow_pair_components(void) {
    Obj* car = mk_int(1);
    Obj* cdr = mk_int(2);
    Obj* pair = mk_pair(car, cdr);

    BorrowRef* ref_pair = borrow_create(pair, "pair");

    ASSERT(borrow_is_valid(ref_pair));

    /* Free just the pair (car/cdr refs transferred) */
    borrow_invalidate_obj(pair);
    ASSERT(!borrow_is_valid(ref_pair));

    borrow_release(ref_pair);
    dec_ref(pair);
    PASS();
}

/* ========== Run All BorrowRef Tests ========== */

void run_borrowref_tests(void) {
    TEST_SUITE("BorrowRef and IPGE");

    TEST_SECTION("IPGE Evolution");
    RUN_TEST(test_ipge_evolve_changes);
    RUN_TEST(test_ipge_evolve_deterministic);
    RUN_TEST(test_ipge_evolve_chain);
    RUN_TEST(test_ipge_evolve64_chain);

    TEST_SECTION("BorrowRef Creation");
    RUN_TEST(test_borrow_create);
    RUN_TEST(test_borrow_create_null_obj);
    RUN_TEST(test_borrow_create_immediate);
    RUN_TEST(test_borrow_create_null_desc);

    TEST_SECTION("BorrowRef Validity");
    RUN_TEST(test_borrow_is_valid_fresh);
    RUN_TEST(test_borrow_is_valid_null);
    RUN_TEST(test_borrow_is_valid_after_evolve);
    RUN_TEST(test_borrow_is_valid_after_invalidate);
    RUN_TEST(test_borrow_is_valid_zero_gen);

    TEST_SECTION("borrow_invalidate_obj");
    RUN_TEST(test_borrow_invalidate_obj);
    RUN_TEST(test_borrow_invalidate_obj_null);
    RUN_TEST(test_borrow_invalidate_obj_immediate);

    TEST_SECTION("Generation and BorrowRef Types");
    RUN_TEST(test_generation_size);
    RUN_TEST(test_borrowref_size);

    TEST_SECTION("BorrowRef Release");
    RUN_TEST(test_borrow_release);
    RUN_TEST(test_borrow_release_null);

    TEST_SECTION("Multiple BorrowRefs");
    RUN_TEST(test_multiple_borrow_refs_same_obj);
    RUN_TEST(test_borrow_refs_different_objs);

    TEST_SECTION("BorrowRef with Closures");
    RUN_TEST(test_borrow_in_closure);
    RUN_TEST(test_borrow_invalid_in_closure);

    TEST_SECTION("IPGE Integration");
    RUN_TEST(test_ipge_on_free_obj);
    RUN_TEST(test_ipge_on_dec_ref_to_zero);
    RUN_TEST(test_ipge_on_free_tree);
    RUN_TEST(test_ipge_on_free_unique);

    TEST_SECTION("Generation Uniqueness");
    RUN_TEST(test_generations_unique);
    RUN_TEST(test_generation_never_zero);

    TEST_SECTION("Stress Tests");
    RUN_TEST(test_borrow_stress_many_refs);
    RUN_TEST(test_borrow_stress_create_release_cycle);
    RUN_TEST(test_ipge_stress_many_evolutions);

    TEST_SECTION("Edge Cases");
    RUN_TEST(test_borrow_ref_after_realloc);
    RUN_TEST(test_borrow_pair_components);
}
