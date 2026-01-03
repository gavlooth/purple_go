/* test_stress.c - Comprehensive stress tests for the runtime */
#include "test_framework.h"
#include <time.h>

/* ========== Allocation Stress Tests ========== */

void test_stress_alloc_10k_ints(void) {
    Obj* objs[10000];
    for (int i = 0; i < 10000; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < 10000; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

void test_stress_alloc_10k_pairs(void) {
    Obj* pairs[10000];
    for (int i = 0; i < 10000; i++) {
        pairs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        ASSERT_NOT_NULL(pairs[i]);
    }
    for (int i = 0; i < 10000; i++) {
        dec_ref(pairs[i]);
    }
    PASS();
}

void test_stress_alloc_mixed_types(void) {
    for (int i = 0; i < 5000; i++) {
        Obj* x;
        switch (i % 5) {
            case 0: x = mk_int(i); break;
            case 1: x = mk_float((double)i); break;
            case 2: x = mk_char(i % 128); break;
            case 3: x = mk_pair(mk_int_unboxed(i), NULL); break;
            case 4: x = mk_sym("test"); break;
            default: x = NULL;
        }
        ASSERT_NOT_NULL(x);
        dec_ref(x);
    }
    PASS();
}

void test_stress_alloc_free_cycle(void) {
    for (int i = 0; i < 10000; i++) {
        Obj* x = mk_int(i);
        ASSERT_NOT_NULL(x);
        dec_ref(x);
    }
    PASS();
}

/* ========== List Stress Tests ========== */

/* Helper to count list length (returns int) */
static int stress_count_list_length(Obj* xs) {
    int len = 0;
    while (xs && xs->tag == TAG_PAIR) {
        len++;
        xs = xs->b;
    }
    return len;
}

void test_stress_list_100k_elements(void) {
    /* Build list of 100k elements */
    Obj* list = NULL;
    for (int i = 0; i < 100000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }

    /* Count length */
    int len = stress_count_list_length(list);
    ASSERT_EQ(len, 100000);

    dec_ref(list);
    PASS();
}

void test_stress_list_deep_nesting(void) {
    /* Create deeply nested list */
    Obj* nested = mk_int_unboxed(0);
    for (int i = 1; i <= 10000; i++) {
        nested = mk_pair(mk_int_unboxed(i), nested);
    }
    ASSERT_NOT_NULL(nested);
    dec_ref(nested);
    PASS();
}

void test_stress_list_reverse_large(void) {
    /* Build list */
    Obj* list = NULL;
    for (int i = 0; i < 10000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }

    /* Reverse it */
    Obj* reversed = list_reverse(list);
    ASSERT_NOT_NULL(reversed);

    /* Verify first element */
    ASSERT_EQ(obj_to_int(reversed->a), 0);

    dec_ref(list);
    dec_ref(reversed);
    PASS();
}

/* ========== Closure Stress Tests ========== */

static Obj* stress_identity_fn(Obj** caps, Obj** args, int nargs) {
    (void)caps;
    if (nargs > 0 && args[0]) {
        inc_ref(args[0]);
        return args[0];
    }
    return NULL;
}

void test_stress_closure_10k_calls(void) {
    Obj* closure = mk_closure(stress_identity_fn, NULL, NULL, 0, 1);

    for (int i = 0; i < 10000; i++) {
        Obj* arg = mk_int_unboxed(i);
        Obj* args[] = {arg};
        Obj* result = call_closure(closure, args, 1);
        ASSERT_EQ(obj_to_int(result), i);
        dec_ref(result);
    }

    dec_ref(closure);
    PASS();
}

void test_stress_closure_create_destroy(void) {
    for (int i = 0; i < 5000; i++) {
        Obj* closure = mk_closure(stress_identity_fn, NULL, NULL, 0, 1);
        ASSERT_NOT_NULL(closure);
        dec_ref(closure);
    }
    PASS();
}

void test_stress_closure_with_captures(void) {
    for (int i = 0; i < 1000; i++) {
        Obj* caps[3] = {mk_int(i), mk_int(i + 1), mk_int(i + 2)};
        Obj* closure = mk_closure(stress_identity_fn, caps, NULL, 3, 1);
        ASSERT_NOT_NULL(closure);
        dec_ref(closure);
        for (int j = 0; j < 3; j++) {
            dec_ref(caps[j]);
        }
    }
    PASS();
}

/* ========== Arena Stress Tests ========== */

void test_stress_arena_100k_allocs(void) {
    Arena* a = arena_create();
    for (int i = 0; i < 100000; i++) {
        Obj* x = arena_mk_int(a, i);
        ASSERT_NOT_NULL(x);
    }
    arena_destroy(a);
    PASS();
}

void test_stress_arena_reset_cycles(void) {
    Arena* a = arena_create();
    for (int round = 0; round < 1000; round++) {
        for (int i = 0; i < 100; i++) {
            arena_mk_int(a, i + round * 100);
        }
        arena_reset(a);
    }
    arena_destroy(a);
    PASS();
}

void test_stress_arena_multiple_arenas(void) {
    Arena* arenas[10];
    for (int i = 0; i < 10; i++) {
        arenas[i] = arena_create();
        for (int j = 0; j < 1000; j++) {
            arena_mk_int(arenas[i], i * 1000 + j);
        }
    }
    for (int i = 0; i < 10; i++) {
        arena_destroy(arenas[i]);
    }
    PASS();
}

/* ========== Reference Counting Stress Tests ========== */

void test_stress_refcount_up_down(void) {
    Obj* obj = mk_int(42);
    for (int i = 0; i < 10000; i++) {
        inc_ref(obj);
    }
    for (int i = 0; i < 10000; i++) {
        dec_ref(obj);
    }
    dec_ref(obj);  /* Final ref */
    PASS();
}

void test_stress_refcount_shared(void) {
    Obj* shared = mk_int(42);
    Obj* containers[100];

    for (int i = 0; i < 100; i++) {
        inc_ref(shared);
        containers[i] = mk_pair(shared, NULL);
    }

    for (int i = 0; i < 100; i++) {
        dec_ref(containers[i]);
    }
    dec_ref(shared);
    PASS();
}

/* ========== Tagged Pointer Stress Tests ========== */

void test_stress_tagged_100k_ops(void) {
    for (int i = 0; i < 100000; i++) {
        Obj* a = mk_int_unboxed(i);
        Obj* b = mk_int_unboxed(i + 1);
        Obj* sum = add(a, b);
        ASSERT_EQ(obj_to_int(sum), 2 * i + 1);
    }
    PASS();
}

void test_stress_tagged_mixed_boxing(void) {
    for (int i = 0; i < 10000; i++) {
        Obj* imm = mk_int_unboxed(i);
        Obj* boxed = mk_int(i);

        ASSERT_EQ(obj_to_int(imm), obj_to_int(boxed));

        Obj* sum = add(imm, boxed);
        ASSERT_EQ(obj_to_int(sum), 2 * i);

        dec_ref(boxed);
    }
    PASS();
}

/* ========== SCC Stress Tests ========== */

void test_stress_scc_many_trees(void) {
    for (int i = 0; i < 1000; i++) {
        Obj* tree = mk_pair(
            mk_pair(mk_int(i), mk_int(i + 1)),
            mk_pair(mk_int(i + 2), mk_int(i + 3))
        );
        detect_and_freeze_sccs(tree);
        dec_ref(tree);
    }
    PASS();
}

/* ========== Weak Reference Stress Tests ========== */

void test_stress_weak_ref_many(void) {
    Obj* objs[1000];
    _InternalWeakRef* refs[1000];

    for (int i = 0; i < 1000; i++) {
        objs[i] = mk_int(i);
        refs[i] = _mk_weak_ref(objs[i]);
    }

    /* Free half, check weak refs */
    for (int i = 0; i < 500; i++) {
        dec_ref(objs[i]);
        ASSERT_EQ(refs[i]->alive, 0);
    }

    /* Other half should still be valid */
    for (int i = 500; i < 1000; i++) {
        ASSERT_EQ(refs[i]->alive, 1);
        dec_ref(objs[i]);
    }
    PASS();
}

/* ========== BorrowRef Stress Tests ========== */

void test_stress_borrow_many_refs(void) {
    Obj* obj = mk_int(42);
    BorrowRef* refs[1000];

    for (int i = 0; i < 1000; i++) {
        refs[i] = borrow_create(obj, "stress");
        ASSERT(borrow_is_valid(refs[i]));
    }

    borrow_invalidate_obj(obj);

    for (int i = 0; i < 1000; i++) {
        ASSERT(!borrow_is_valid(refs[i]));
        borrow_release(refs[i]);
    }
    PASS();
}

/* ========== Deferred RC Stress Tests ========== */

void test_stress_deferred_large_batch(void) {
    flush_deferred();
    set_deferred_batch_size(100);

    Obj* objs[10000];
    for (int i = 0; i < 10000; i++) {
        objs[i] = mk_int(i);
        inc_ref(objs[i]);
        defer_decrement(objs[i]);
    }

    /* Process all */
    while (DEFERRED_CTX.pending_count > 0) {
        process_deferred();
    }

    for (int i = 0; i < 10000; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

/* ========== Memory Pattern Stress Tests ========== */

void test_stress_fragmentation(void) {
    /* Allocate many objects, free every other one, allocate more */
    Obj* objs[1000];
    for (int i = 0; i < 1000; i++) {
        objs[i] = mk_int(i);
    }

    /* Free even indices */
    for (int i = 0; i < 1000; i += 2) {
        dec_ref(objs[i]);
        objs[i] = NULL;
    }

    /* Allocate again */
    for (int i = 0; i < 1000; i += 2) {
        objs[i] = mk_int(i + 1000);
    }

    /* Free all */
    for (int i = 0; i < 1000; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    PASS();
}

void test_stress_lifo_pattern(void) {
    /* Stack-like allocation pattern */
    Obj* stack[1000];
    int top = 0;

    for (int round = 0; round < 100; round++) {
        /* Push 10 */
        for (int i = 0; i < 10; i++) {
            stack[top++] = mk_int(round * 10 + i);
        }
        /* Pop 5 */
        for (int i = 0; i < 5; i++) {
            dec_ref(stack[--top]);
        }
    }

    /* Clean up remaining */
    while (top > 0) {
        dec_ref(stack[--top]);
    }
    PASS();
}

void test_stress_fifo_pattern(void) {
    /* Queue-like allocation pattern */
    Obj* queue[1000];
    int head = 0, tail = 0;

    for (int round = 0; round < 100; round++) {
        /* Enqueue 5 */
        for (int i = 0; i < 5; i++) {
            queue[tail++ % 1000] = mk_int(round * 5 + i);
        }
        /* Dequeue 3 */
        for (int i = 0; i < 3 && head < tail; i++) {
            dec_ref(queue[head++ % 1000]);
        }
    }

    /* Clean up remaining */
    while (head < tail) {
        dec_ref(queue[head++ % 1000]);
    }
    PASS();
}

/* ========== Concurrency Stress Tests ========== */

/* Increment closure for atom_swap - takes 1 arg (old val), returns new val */
static Obj* stress_increment_closure_fn(Obj** caps, Obj** args, int nargs) {
    (void)caps; (void)nargs;
    if (!args || !args[0]) return mk_int(1);
    long n = obj_to_int(args[0]);
    return mk_int(n + 1);
}

static Obj* stress_counter_increment(Obj** caps, Obj** args, int nargs) {
    (void)args; (void)nargs;
    Obj* atom = caps[0];
    Obj* inc_closure = caps[1];
    for (int i = 0; i < 100; i++) {
        Obj* result = atom_swap(atom, inc_closure);
        if (result) dec_ref(result);
    }
    return NULL;
}

void test_stress_concurrent_atoms(void) {
    Obj* val = mk_int(0);
    Obj* atom = make_atom(val);
    Obj* inc_closure = mk_closure(stress_increment_closure_fn, NULL, NULL, 0, 1);
    Obj* caps[] = {atom, inc_closure};

    Obj* closures[10];
    Obj* threads[10];

    /* Create worker closures */
    for (int i = 0; i < 10; i++) {
        closures[i] = mk_closure(stress_counter_increment, caps, NULL, 2, 0);
    }

    /* Spawn threads */
    for (int i = 0; i < 10; i++) {
        threads[i] = spawn_thread(closures[i]);
    }

    /* Join all */
    for (int i = 0; i < 10; i++) {
        thread_join(threads[i]);
        dec_ref(threads[i]);
        dec_ref(closures[i]);
    }

    Obj* final = atom_deref(atom);
    ASSERT_EQ(obj_to_int(final), 1000);  /* 10 threads * 100 increments */
    dec_ref(final);
    dec_ref(inc_closure);
    dec_ref(atom);
    dec_ref(val);
    PASS();
}

void test_stress_channel_throughput(void) {
    Obj* ch = make_channel(1000);

    /* Send 10000 values */
    for (int i = 0; i < 10000; i++) {
        channel_send(ch, mk_int(i));
    }

    /* Receive all */
    for (int i = 0; i < 10000; i++) {
        Obj* val = channel_recv(ch);
        ASSERT_NOT_NULL(val);
        ASSERT_EQ(obj_to_int(val), i);
        dec_ref(val);
    }

    dec_ref(ch);
    PASS();
}

/* ========== Comprehensive Integration Stress Test ========== */

void test_stress_integration(void) {
    /* Test combining multiple features */
    Arena* arena = arena_create();

    /* Create arena-allocated list */
    Obj* arena_list = NULL;
    for (int i = 0; i < 100; i++) {
        arena_list = arena_mk_pair(arena, arena_mk_int(arena, i), arena_list);
    }

    /* Create heap-allocated structures */
    Obj* heap_objs[100];
    for (int i = 0; i < 100; i++) {
        heap_objs[i] = mk_pair(mk_int(i), mk_float((double)i * 1.5));
    }

    /* Create closures */
    Obj* closures[10];
    for (int i = 0; i < 10; i++) {
        closures[i] = mk_closure(stress_identity_fn, NULL, NULL, 0, 1);
    }

    /* Use closures */
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 100; j++) {
            Obj* arg = mk_int_unboxed(j);
            Obj* args[] = {arg};
            Obj* result = call_closure(closures[i], args, 1);
            dec_ref(result);
        }
    }

    /* Cleanup */
    for (int i = 0; i < 10; i++) {
        dec_ref(closures[i]);
    }
    for (int i = 0; i < 100; i++) {
        dec_ref(heap_objs[i]);
    }
    arena_destroy(arena);
    PASS();
}

/* ========== Run All Stress Tests ========== */

void run_stress_tests(void) {
    TEST_SUITE("Stress Tests");

    TEST_SECTION("Allocation");
    RUN_TEST(test_stress_alloc_10k_ints);
    RUN_TEST(test_stress_alloc_10k_pairs);
    RUN_TEST(test_stress_alloc_mixed_types);
    RUN_TEST(test_stress_alloc_free_cycle);

    TEST_SECTION("Lists");
    RUN_TEST(test_stress_list_100k_elements);
    RUN_TEST(test_stress_list_deep_nesting);
    RUN_TEST(test_stress_list_reverse_large);

    TEST_SECTION("Closures");
    RUN_TEST(test_stress_closure_10k_calls);
    RUN_TEST(test_stress_closure_create_destroy);
    RUN_TEST(test_stress_closure_with_captures);

    TEST_SECTION("Arena");
    RUN_TEST(test_stress_arena_100k_allocs);
    RUN_TEST(test_stress_arena_reset_cycles);
    RUN_TEST(test_stress_arena_multiple_arenas);

    TEST_SECTION("Reference Counting");
    RUN_TEST(test_stress_refcount_up_down);
    RUN_TEST(test_stress_refcount_shared);

    TEST_SECTION("Tagged Pointers");
    RUN_TEST(test_stress_tagged_100k_ops);
    RUN_TEST(test_stress_tagged_mixed_boxing);

    TEST_SECTION("SCC");
    RUN_TEST(test_stress_scc_many_trees);

    TEST_SECTION("Weak References");
    RUN_TEST(test_stress_weak_ref_many);

    TEST_SECTION("BorrowRef");
    RUN_TEST(test_stress_borrow_many_refs);

    TEST_SECTION("Deferred RC");
    RUN_TEST(test_stress_deferred_large_batch);

    TEST_SECTION("Memory Patterns");
    RUN_TEST(test_stress_fragmentation);
    RUN_TEST(test_stress_lifo_pattern);
    RUN_TEST(test_stress_fifo_pattern);

    TEST_SECTION("Concurrency");
    RUN_TEST(test_stress_concurrent_atoms);
    RUN_TEST(test_stress_channel_throughput);

    TEST_SECTION("Integration");
    RUN_TEST(test_stress_integration);
}
