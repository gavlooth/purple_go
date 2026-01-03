/*
 * Slot Pool Tests - Sound Generational References
 *
 * Tests for the stable slot pool that provides sound use-after-free detection.
 * The key property: generation validation never reads freed memory.
 */

/* Must define before including pthread.h */
#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Include runtime for Obj and BorrowRef definitions */
#include "../src/runtime.c"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %s: ", #name); \
    name(); \
    tests_run++; \
    tests_passed++; \
    printf("\033[32mPASS\033[0m\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\033[31mFAIL\033[0m (line %d: %s)\n", __LINE__, #cond); \
        tests_run++; \
        return; \
    } \
} while(0)

/* ========== Slot Pool Basic Tests ========== */

TEST(test_slot_pool_create) {
    SlotPool* pool = slot_pool_create(sizeof(Obj), 16, 64);
    ASSERT(pool != NULL);
    ASSERT(pool->blocks != NULL);
    ASSERT(pool->block_count == 1);
    slot_pool_destroy(pool);
}

TEST(test_slot_pool_alloc) {
    SlotPool* pool = slot_pool_create(sizeof(Obj), 16, 64);
    Slot* slot = slot_pool_alloc(pool);
    ASSERT(slot != NULL);
    ASSERT(slot->flags == SLOT_IN_USE);
    ASSERT(slot->generation != 0);
    slot_pool_destroy(pool);
}

TEST(test_slot_pool_free) {
    SlotPool* pool = slot_pool_create(sizeof(Obj), 16, 64);
    Slot* slot = slot_pool_alloc(pool);
    uint32_t old_gen = slot->generation;

    slot_pool_free(pool, slot);

    /* Generation should have evolved */
    ASSERT(slot->generation != old_gen);
    ASSERT(slot->flags == SLOT_FREE);
    /* Key property: generation is still readable! */
    ASSERT(slot->generation != 0);

    slot_pool_destroy(pool);
}

TEST(test_slot_pool_reuse) {
    SlotPool* pool = slot_pool_create(sizeof(Obj), 16, 64);
    Slot* slot1 = slot_pool_alloc(pool);
    void* ptr1 = slot1;

    slot_pool_free(pool, slot1);

    Slot* slot2 = slot_pool_alloc(pool);
    /* Should reuse the same slot */
    ASSERT(slot2 == ptr1);
    /* But with a new generation */
    ASSERT(slot2->flags == SLOT_IN_USE);

    slot_pool_destroy(pool);
}

TEST(test_slot_pool_growth) {
    /* Create small pool that will need to grow */
    SlotPool* pool = slot_pool_create(sizeof(Obj), 16, 4);

    /* Allocate more than initial capacity */
    Slot* slots[10];
    for (int i = 0; i < 10; i++) {
        slots[i] = slot_pool_alloc(pool);
        ASSERT(slots[i] != NULL);
    }

    /* Should have grown */
    ASSERT(pool->block_count >= 2);

    /* Free all */
    for (int i = 0; i < 10; i++) {
        slot_pool_free(pool, slots[i]);
    }

    slot_pool_destroy(pool);
}

/* ========== Handle Tests ========== */

TEST(test_handle_create) {
    SlotPool* pool = slot_pool_create(sizeof(Obj), 16, 64);
    Slot* slot = slot_pool_alloc(pool);

    Handle h = slot_pool_make_handle(pool, slot);
    ASSERT(h != HANDLE_INVALID);

    /* Handle should contain slot pointer */
    Slot* recovered = handle_to_slot(h);
    ASSERT(recovered == slot);

    slot_pool_destroy(pool);
}

TEST(test_handle_valid) {
    SlotPool* pool = slot_pool_create(sizeof(Obj), 16, 64);
    Slot* slot = slot_pool_alloc(pool);
    Handle h = slot_pool_make_handle(pool, slot);

    /* Handle should be valid for in-use slot */
    ASSERT(handle_is_valid(h));

    slot_pool_destroy(pool);
}

TEST(test_handle_invalid_after_free) {
    SlotPool* pool = slot_pool_create(sizeof(Obj), 16, 64);
    Slot* slot = slot_pool_alloc(pool);
    Handle h = slot_pool_make_handle(pool, slot);

    /* Free the slot */
    slot_pool_free(pool, slot);

    /* Handle should now be INVALID - this is the key soundness property! */
    ASSERT(!handle_is_valid(h));

    /* But reading the slot is still safe (no UB) */
    ASSERT(slot->flags == SLOT_FREE);
    ASSERT(slot->generation != 0);

    slot_pool_destroy(pool);
}

TEST(test_handle_invalid_after_reuse) {
    SlotPool* pool = slot_pool_create(sizeof(Obj), 16, 64);
    Slot* slot = slot_pool_alloc(pool);
    Handle h1 = slot_pool_make_handle(pool, slot);

    /* Free and reallocate */
    slot_pool_free(pool, slot);
    Slot* slot2 = slot_pool_alloc(pool);
    Handle h2 = slot_pool_make_handle(pool, slot2);

    /* Old handle invalid, new handle valid */
    ASSERT(!handle_is_valid(h1));
    ASSERT(handle_is_valid(h2));

    slot_pool_destroy(pool);
}

/* ========== Handle-Based Obj Allocation Tests ========== */

TEST(test_alloc_obj_basic) {
    Obj* obj = alloc_obj();
    ASSERT(obj != NULL);
    ASSERT(is_pool_obj(obj));
    free_obj_pool(obj);
}

TEST(test_alloc_obj_handle) {
    Obj* obj = alloc_obj();
    Handle h = handle_from_obj(obj);

    /* Should get valid handle for pool object */
    ASSERT(h != HANDLE_INVALID);
    ASSERT(handle_is_valid(h));

    /* Dereference should return same object */
    Obj* deref = handle_deref_obj(h);
    ASSERT(deref == obj);

    free_obj_pool(obj);
}

TEST(test_alloc_obj_handle_invalid_after_free) {
    Obj* obj = alloc_obj();
    Handle h = handle_from_obj(obj);

    free_obj_pool(obj);

    /* Handle should be invalid after free */
    ASSERT(!handle_is_valid(h));

    /* Deref should return NULL */
    Obj* deref = handle_deref_obj(h);
    ASSERT(deref == NULL);
}

/* ========== BorrowRef with Handle Tests ========== */

TEST(test_borrow_create_pool_obj) {
    Obj* obj = alloc_obj();
    obj->tag = TAG_INT;
    obj->i = 42;

    BorrowRef* ref = borrow_create(obj, "test");
    ASSERT(ref != NULL);
    ASSERT(ref->handle != HANDLE_INVALID);

    /* Should be valid */
    ASSERT(borrow_is_valid(ref));

    borrow_release(ref);
    free_obj_pool(obj);
}

TEST(test_borrow_invalid_after_free_pool) {
    Obj* obj = alloc_obj();
    obj->tag = TAG_INT;
    obj->i = 42;

    BorrowRef* ref = borrow_create(obj, "test");
    ASSERT(borrow_is_valid(ref));

    /* Free the object */
    free_obj_pool(obj);

    /* BorrowRef should now be INVALID - sound detection! */
    ASSERT(!borrow_is_valid(ref));

    borrow_release(ref);
}

TEST(test_borrow_multiple_refs_pool) {
    Obj* obj = alloc_obj();
    obj->tag = TAG_INT;
    obj->i = 42;

    /* Create multiple borrows */
    BorrowRef* ref1 = borrow_create(obj, "ref1");
    BorrowRef* ref2 = borrow_create(obj, "ref2");
    BorrowRef* ref3 = borrow_create(obj, "ref3");

    ASSERT(borrow_is_valid(ref1));
    ASSERT(borrow_is_valid(ref2));
    ASSERT(borrow_is_valid(ref3));

    /* Free object - all refs should become invalid */
    free_obj_pool(obj);

    ASSERT(!borrow_is_valid(ref1));
    ASSERT(!borrow_is_valid(ref2));
    ASSERT(!borrow_is_valid(ref3));

    borrow_release(ref1);
    borrow_release(ref2);
    borrow_release(ref3);
}

/* ========== Soundness Stress Tests ========== */

TEST(test_soundness_alloc_free_cycle) {
    /* Repeatedly allocate, borrow, free - handles should track correctly */
    for (int i = 0; i < 100; i++) {
        Obj* obj = alloc_obj();
        obj->tag = TAG_INT;
        obj->i = i;

        BorrowRef* ref = borrow_create(obj, "cycle_test");
        ASSERT(borrow_is_valid(ref));

        free_obj_pool(obj);
        ASSERT(!borrow_is_valid(ref));

        borrow_release(ref);
    }
}

TEST(test_soundness_many_outstanding_borrows) {
    #define NUM_OBJS 50
    Obj* objs[NUM_OBJS];
    BorrowRef* refs[NUM_OBJS];

    /* Allocate all */
    for (int i = 0; i < NUM_OBJS; i++) {
        objs[i] = alloc_obj();
        objs[i]->tag = TAG_INT;
        objs[i]->i = i;
        refs[i] = borrow_create(objs[i], "many_test");
    }

    /* All should be valid */
    for (int i = 0; i < NUM_OBJS; i++) {
        ASSERT(borrow_is_valid(refs[i]));
    }

    /* Free half */
    for (int i = 0; i < NUM_OBJS / 2; i++) {
        free_obj_pool(objs[i]);
    }

    /* First half invalid, second half valid */
    for (int i = 0; i < NUM_OBJS / 2; i++) {
        ASSERT(!borrow_is_valid(refs[i]));
    }
    for (int i = NUM_OBJS / 2; i < NUM_OBJS; i++) {
        ASSERT(borrow_is_valid(refs[i]));
    }

    /* Cleanup */
    for (int i = NUM_OBJS / 2; i < NUM_OBJS; i++) {
        free_obj_pool(objs[i]);
    }
    for (int i = 0; i < NUM_OBJS; i++) {
        borrow_release(refs[i]);
    }
    #undef NUM_OBJS
}

TEST(test_soundness_realloc_detection) {
    /* Allocate, create borrow, free, reallocate same slot, old borrow invalid */
    Obj* obj1 = alloc_obj();
    obj1->tag = TAG_INT;
    obj1->i = 1;

    BorrowRef* ref1 = borrow_create(obj1, "realloc_test");
    ASSERT(borrow_is_valid(ref1));

    /* Free and reallocate */
    free_obj_pool(obj1);
    Obj* obj2 = alloc_obj();  /* May reuse same slot */
    obj2->tag = TAG_INT;
    obj2->i = 2;

    /* Old ref should be invalid even if memory reused */
    ASSERT(!borrow_is_valid(ref1));

    /* New borrow on new object should be valid */
    BorrowRef* ref2 = borrow_create(obj2, "realloc_test2");
    ASSERT(borrow_is_valid(ref2));

    borrow_release(ref1);
    borrow_release(ref2);
    free_obj_pool(obj2);
}

/* ========== Statistics Tests ========== */

TEST(test_pool_stats) {
    SlotPool* pool = slot_pool_create(sizeof(Obj), 16, 64);
    SlotPoolStats stats;

    slot_pool_get_stats(pool, &stats);
    ASSERT(stats.total_slots == 64);
    ASSERT(stats.in_use_slots == 0);
    ASSERT(stats.free_slots == 64);

    /* Allocate some */
    Slot* s1 = slot_pool_alloc(pool);
    Slot* s2 = slot_pool_alloc(pool);
    Slot* s3 = slot_pool_alloc(pool);

    slot_pool_get_stats(pool, &stats);
    ASSERT(stats.in_use_slots == 3);
    ASSERT(stats.total_allocs == 3);

    /* Free one */
    slot_pool_free(pool, s2);

    slot_pool_get_stats(pool, &stats);
    ASSERT(stats.in_use_slots == 2);
    ASSERT(stats.total_frees == 1);

    slot_pool_free(pool, s1);
    slot_pool_free(pool, s3);
    slot_pool_destroy(pool);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Slot Pool Soundness Tests ===\033[0m\n");

    printf("\n\033[33m--- Basic Slot Pool ---\033[0m\n");
    RUN_TEST(test_slot_pool_create);
    RUN_TEST(test_slot_pool_alloc);
    RUN_TEST(test_slot_pool_free);
    RUN_TEST(test_slot_pool_reuse);
    RUN_TEST(test_slot_pool_growth);

    printf("\n\033[33m--- Handle Operations ---\033[0m\n");
    RUN_TEST(test_handle_create);
    RUN_TEST(test_handle_valid);
    RUN_TEST(test_handle_invalid_after_free);
    RUN_TEST(test_handle_invalid_after_reuse);

    printf("\n\033[33m--- Handle-Based Obj Allocation ---\033[0m\n");
    RUN_TEST(test_alloc_obj_basic);
    RUN_TEST(test_alloc_obj_handle);
    RUN_TEST(test_alloc_obj_handle_invalid_after_free);

    printf("\n\033[33m--- BorrowRef with Handles ---\033[0m\n");
    RUN_TEST(test_borrow_create_pool_obj);
    RUN_TEST(test_borrow_invalid_after_free_pool);
    RUN_TEST(test_borrow_multiple_refs_pool);

    printf("\n\033[33m--- Soundness Stress Tests ---\033[0m\n");
    RUN_TEST(test_soundness_alloc_free_cycle);
    RUN_TEST(test_soundness_many_outstanding_borrows);
    RUN_TEST(test_soundness_realloc_detection);

    printf("\n\033[33m--- Statistics ---\033[0m\n");
    RUN_TEST(test_pool_stats);

    printf("\n\033[33m=== Summary ===\033[0m\n");
    printf("  Total:  %d\n", tests_run);
    if (tests_passed == tests_run) {
        printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
    } else {
        printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
        printf("  \033[31mFailed: %d\033[0m\n", tests_run - tests_passed);
    }
    printf("  Failed: %d\n", tests_run - tests_passed);

    return (tests_passed == tests_run) ? 0 : 1;
}
