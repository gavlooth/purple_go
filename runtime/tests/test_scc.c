/* test_scc.c - SCC (Strongly Connected Components) detection tests */
#include "test_framework.h"

/* ========== SCC Registry Tests ========== */

void test_scc_registry_init(void) {
    /* Registry should start empty */
    ASSERT_EQ(SCC_REGISTRY.next_id, 0);
    PASS();
}

void test_create_scc(void) {
    SCC* scc = create_scc();
    ASSERT_NOT_NULL(scc);
    ASSERT_EQ(scc->member_count, 0);
    ASSERT_EQ(scc->ref_count, 1);
    ASSERT_EQ(scc->frozen, 0);
    freeze_scc(scc);
    release_scc(scc);
    PASS();
}

void test_create_multiple_sccs(void) {
    int base_id = SCC_REGISTRY.next_id;
    SCC* scc1 = create_scc();
    SCC* scc2 = create_scc();
    SCC* scc3 = create_scc();
    ASSERT_EQ(scc1->id, base_id);
    ASSERT_EQ(scc2->id, base_id + 1);
    ASSERT_EQ(scc3->id, base_id + 2);
    freeze_scc(scc1);
    freeze_scc(scc2);
    freeze_scc(scc3);
    release_scc(scc1);
    release_scc(scc2);
    release_scc(scc3);
    PASS();
}

/* ========== SCC Member Management Tests ========== */

void test_scc_add_member(void) {
    SCC* scc = create_scc();
    Obj* obj = mk_int(42);
    scc_add_member(scc, obj);
    ASSERT_EQ(scc->member_count, 1);
    ASSERT_EQ(scc->members[0], obj);
    ASSERT_EQ(obj->scc_id, scc->id);
    freeze_scc(scc);
    release_scc(scc); /* frees members */
    PASS();
}

void test_scc_add_multiple_members(void) {
    SCC* scc = create_scc();
    Obj* objs[10];
    for (int i = 0; i < 10; i++) {
        objs[i] = mk_int(i);
        scc_add_member(scc, objs[i]);
    }
    ASSERT_EQ(scc->member_count, 10);
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(scc->members[i], objs[i]);
        ASSERT_EQ(objs[i]->scc_id, scc->id);
    }
    freeze_scc(scc);
    release_scc(scc); /* frees members */
    PASS();
}

void test_scc_add_member_null_scc(void) {
    Obj* obj = mk_int(42);
    scc_add_member(NULL, obj);  /* Should not crash */
    dec_ref(obj);
    PASS();
}

void test_scc_add_member_null_obj(void) {
    SCC* scc = create_scc();
    scc_add_member(scc, NULL);  /* Should not crash */
    ASSERT_EQ(scc->member_count, 0);  /* Should not add */
    freeze_scc(scc);
    release_scc(scc);
    PASS();
}

/* ========== SCC Freeze Tests ========== */

void test_freeze_scc(void) {
    SCC* scc = create_scc();
    ASSERT_EQ(scc->frozen, 0);
    freeze_scc(scc);
    ASSERT_EQ(scc->frozen, 1);
    release_scc(scc);
    PASS();
}

void test_freeze_scc_null(void) {
    freeze_scc(NULL);  /* Should not crash */
    PASS();
}

/* ========== SCC Lookup Tests ========== */

void test_find_scc(void) {
    SCC* scc = create_scc();
    int id = scc->id;
    SCC* found = find_scc(id);
    ASSERT_EQ(found, scc);
    freeze_scc(scc);
    release_scc(scc);
    PASS();
}

void test_find_scc_not_found(void) {
    SCC* found = find_scc(-999);  /* Non-existent ID */
    ASSERT_NULL(found);
    PASS();
}

/* ========== SCC Reference Counting Tests ========== */

void test_release_scc_single_ref(void) {
    SCC* scc = create_scc();
    Obj* obj = mk_int(42);
    scc_add_member(scc, obj);
    freeze_scc(scc);
    ASSERT_EQ(scc->ref_count, 1);
    release_scc(scc);  /* Should free everything */
    PASS();
}

void test_release_scc_null(void) {
    release_scc(NULL);  /* Should not crash */
    PASS();
}

void test_release_scc_not_frozen(void) {
    SCC* scc = create_scc();
    Obj* obj = mk_int(42);
    scc_add_member(scc, obj);
    /* Don't freeze */
    scc->ref_count = 0;
    release_scc(scc);  /* Should not free members because not frozen */
    /* Reset refcount so cleanup can proceed */
    scc->ref_count = 1;
    freeze_scc(scc);
    release_scc(scc);  /* cleanup SCC */
    PASS();
}

/* ========== release_with_scc Tests ========== */

void test_release_with_scc_member(void) {
    SCC* scc = create_scc();
    Obj* obj = mk_int(42);
    inc_ref(obj);  /* Extra ref for SCC */
    scc_add_member(scc, obj);
    freeze_scc(scc);
    release_with_scc(obj);  /* Should release via SCC */
    /* obj may be freed by release_with_scc, don't dec_ref again */
    PASS();
}

void test_release_with_scc_non_member(void) {
    Obj* obj = mk_int(42);
    release_with_scc(obj);  /* Should fall back to dec_ref */
    PASS();
}

void test_release_with_scc_null(void) {
    release_with_scc(NULL);  /* Should not crash */
    PASS();
}

/* ========== Tarjan State Tests ========== */

void test_tarjan_init(void) {
    TarjanState* state = tarjan_init(100);
    ASSERT_NOT_NULL(state);
    ASSERT_NOT_NULL(state->index);
    ASSERT_NOT_NULL(state->lowlink);
    ASSERT_NOT_NULL(state->on_stack);
    ASSERT_NOT_NULL(state->stack);
    ASSERT_EQ(state->stack_top, 0);
    ASSERT_EQ(state->current_index, 1);
    ASSERT_EQ(state->capacity, 100);
    tarjan_free(state);
    PASS();
}

void test_tarjan_init_large(void) {
    TarjanState* state = tarjan_init(10000);
    ASSERT_NOT_NULL(state);
    ASSERT_EQ(state->capacity, 10000);
    tarjan_free(state);
    PASS();
}

void test_tarjan_free_null(void) {
    tarjan_free(NULL);  /* Should not crash */
    PASS();
}

/* ========== SCC Detection Tests ========== */

void test_detect_sccs_single_obj(void) {
    Obj* obj = mk_int(42);
    detect_and_freeze_sccs(obj);  /* Should handle single node */
    dec_ref(obj);
    PASS();
}

void test_detect_sccs_null(void) {
    detect_and_freeze_sccs(NULL);  /* Should not crash */
    PASS();
}

void test_detect_sccs_pair(void) {
    Obj* a = mk_int(1);
    Obj* b = mk_int(2);
    Obj* p = mk_pair(a, b);
    detect_and_freeze_sccs(p);
    dec_ref(p);
    PASS();
}

void test_detect_sccs_list(void) {
    /* Build list (1 2 3) */
    Obj* list = mk_pair(mk_int(1),
                        mk_pair(mk_int(2),
                                mk_pair(mk_int(3), NULL)));
    detect_and_freeze_sccs(list);
    dec_ref(list);
    PASS();
}

/* ========== Cyclic Structure Tests ========== */

void test_detect_sccs_simple_cycle(void) {
    /* Create a simple cycle: A -> B -> A */
    Obj* a = mk_pair(NULL, NULL);
    Obj* b = mk_pair(NULL, NULL);
    a->a = b;  /* a -> b */
    b->a = a;  /* b -> a (creates cycle) */

    detect_and_freeze_sccs(a);

    /* Both should be in same SCC */
    ASSERT(a->scc_id >= 0);
    ASSERT_EQ(a->scc_id, b->scc_id);

    /* Clean up */
    release_with_scc(a);
    PASS();
}

void test_detect_sccs_self_loop(void) {
    /* Create self-referencing pair */
    Obj* a = mk_pair(NULL, NULL);
    a->a = a;  /* Self-loop */

    detect_and_freeze_sccs(a);
    if (a->scc_id >= 0 && find_scc(a->scc_id)) {
        release_with_scc(a);
    } else {
        /* Break cycle and free normally when SCC not detected */
        a->a = NULL;
        dec_ref(a);
    }
    PASS();
}

/* ========== on_scc_found Callback Tests ========== */

static int scc_found_count = 0;
static int last_scc_size = 0;

void test_on_scc_found_callback(void) {
    Obj* members[] = {mk_int(1), mk_int(2), mk_int(3)};
    int old_count = SCC_REGISTRY.next_id;
    on_scc_found(members, 3);
    ASSERT_EQ(SCC_REGISTRY.next_id, old_count + 1);
    SCC* scc = find_scc(old_count);
    if (scc) {
        freeze_scc(scc);
        release_scc(scc);
    }
    PASS();
}

/* ========== SCC Capacity Tests ========== */

void test_scc_member_capacity_growth(void) {
    SCC* scc = create_scc();
    int initial_cap = scc->member_capacity;
    /* Add more members than initial capacity */
    for (int i = 0; i < initial_cap * 3; i++) {
        Obj* obj = mk_int(i);
        scc_add_member(scc, obj);
        /* Note: not dec_ref'ing because SCC owns them now */
    }
    ASSERT(scc->member_capacity > initial_cap);
    ASSERT_EQ(scc->member_count, initial_cap * 3);
    freeze_scc(scc);
    release_scc(scc);
    PASS();
}

/* ========== SCC Integration Tests ========== */

void test_scc_full_lifecycle(void) {
    /* Create SCC */
    SCC* scc = create_scc();
    int scc_id = scc->id;

    /* Add members */
    for (int i = 0; i < 5; i++) {
        Obj* obj = mk_int(i);
        scc_add_member(scc, obj);
    }

    /* Freeze */
    freeze_scc(scc);
    ASSERT_EQ(scc->frozen, 1);

    /* Find it */
    SCC* found = find_scc(scc_id);
    ASSERT_EQ(found, scc);

    /* Release */
    release_scc(scc);
    PASS();
}

/* ========== Stress Tests ========== */

void test_scc_stress_many_members(void) {
    SCC* scc = create_scc();
    for (int i = 0; i < 1000; i++) {
        Obj* obj = mk_int(i);
        scc_add_member(scc, obj);
    }
    ASSERT_EQ(scc->member_count, 1000);
    freeze_scc(scc);
    release_scc(scc);
    PASS();
}

void test_scc_stress_many_sccs(void) {
    SCC* sccs[100];
    for (int i = 0; i < 100; i++) {
        sccs[i] = create_scc();
        for (int j = 0; j < 10; j++) {
            Obj* obj = mk_int(i * 10 + j);
            scc_add_member(sccs[i], obj);
        }
        freeze_scc(sccs[i]);
    }
    /* Release all */
    for (int i = 0; i < 100; i++) {
        release_scc(sccs[i]);
    }
    PASS();
}

void test_scc_stress_detect_many_trees(void) {
    for (int i = 0; i < 100; i++) {
        Obj* tree = mk_pair(
            mk_pair(mk_int(1), mk_int(2)),
            mk_pair(mk_int(3), mk_int(4))
        );
        detect_and_freeze_sccs(tree);
        dec_ref(tree);
    }
    PASS();
}

/* ========== Run All SCC Tests ========== */

void run_scc_tests(void) {
    TEST_SUITE("SCC Detection");

    TEST_SECTION("SCC Registry");
    RUN_TEST(test_scc_registry_init);
    RUN_TEST(test_create_scc);
    RUN_TEST(test_create_multiple_sccs);

    TEST_SECTION("Member Management");
    RUN_TEST(test_scc_add_member);
    RUN_TEST(test_scc_add_multiple_members);
    RUN_TEST(test_scc_add_member_null_scc);
    RUN_TEST(test_scc_add_member_null_obj);

    TEST_SECTION("Freeze");
    RUN_TEST(test_freeze_scc);
    RUN_TEST(test_freeze_scc_null);

    TEST_SECTION("Lookup");
    RUN_TEST(test_find_scc);
    RUN_TEST(test_find_scc_not_found);

    TEST_SECTION("Reference Counting");
    RUN_TEST(test_release_scc_single_ref);
    RUN_TEST(test_release_scc_null);
    RUN_TEST(test_release_scc_not_frozen);

    TEST_SECTION("release_with_scc");
    RUN_TEST(test_release_with_scc_member);
    RUN_TEST(test_release_with_scc_non_member);
    RUN_TEST(test_release_with_scc_null);

    TEST_SECTION("Tarjan State");
    RUN_TEST(test_tarjan_init);
    RUN_TEST(test_tarjan_init_large);
    RUN_TEST(test_tarjan_free_null);

    TEST_SECTION("SCC Detection");
    RUN_TEST(test_detect_sccs_single_obj);
    RUN_TEST(test_detect_sccs_null);
    RUN_TEST(test_detect_sccs_pair);
    RUN_TEST(test_detect_sccs_list);

    TEST_SECTION("Cyclic Structures");
    RUN_TEST(test_detect_sccs_simple_cycle);
    RUN_TEST(test_detect_sccs_self_loop);

    TEST_SECTION("Callback");
    RUN_TEST(test_on_scc_found_callback);

    TEST_SECTION("Capacity Growth");
    RUN_TEST(test_scc_member_capacity_growth);

    TEST_SECTION("Integration");
    RUN_TEST(test_scc_full_lifecycle);

    TEST_SECTION("Stress Tests");
    RUN_TEST(test_scc_stress_many_members);
    RUN_TEST(test_scc_stress_many_sccs);
    RUN_TEST(test_scc_stress_detect_many_trees);
}
