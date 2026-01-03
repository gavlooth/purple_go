#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/purple.h"

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %s: ", name); } while (0)
#define PASS() do { printf("PASS\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL - %s\n", msg); return; } while (0)

#define ASSERT(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { FAIL("ASSERT_EQ failed"); } } while (0)
#define ASSERT_NOT_NULL(p) do { if ((p) == NULL) { FAIL("NULL"); } } while (0)

static Obj* api_return_7(Obj** captures, Obj** args, int nargs) {
    (void)captures; (void)args; (void)nargs;
    return mk_int(7);
}

static void test_api_constructors(void) {
    TEST("constructors");
    Obj* i = mk_int(42);
    Obj* f = mk_float(3.5);
    Obj* c = mk_char('A');
    Obj* s = mk_sym("ok");
    Obj* e = mk_error("err");
    Obj* p = mk_pair(mk_int(1), mk_int(2));
    Obj* v = mk_int(9);
    Obj* b = mk_box(v);
    dec_ref(v);

    ASSERT_NOT_NULL(i);
    ASSERT_NOT_NULL(f);
    ASSERT_NOT_NULL(c);
    ASSERT_NOT_NULL(s);
    ASSERT_NOT_NULL(e);
    ASSERT_NOT_NULL(p);
    ASSERT_NOT_NULL(b);

    dec_ref(i);
    dec_ref(f);
    dec_ref(c);
    dec_ref(s);
    dec_ref(e);
    dec_ref(p);
    dec_ref(b);
    PASS();
}

static void test_api_primitives(void) {
    TEST("primitives");
    Obj* a = mk_int_unboxed(2);
    Obj* b = mk_int_unboxed(3);
    Obj* sum = prim_add(a, b);
    ASSERT_EQ(obj_to_int(sum), 5);

    Obj* eq = prim_eq(mk_int_unboxed(5), mk_int_unboxed(5));
    ASSERT(obj_to_int(eq) != 0);

    dec_ref(sum);
    dec_ref(eq);
    PASS();
}

static void test_api_list_ops(void) {
    TEST("list ops");
    Obj* list = mk_pair(mk_int(1), mk_pair(mk_int(2), NULL));
    Obj* len = list_length(list);
    ASSERT_EQ(obj_to_int(len), 2);

    Obj* rev = list_reverse(list);
    Obj* first = obj_car(rev);
    ASSERT_EQ(obj_to_int(first), 2);
    dec_ref(first);

    dec_ref(len);
    dec_ref(rev);
    dec_ref(list);
    PASS();
}

static void test_api_closure(void) {
    TEST("closure");
    Obj* clos = mk_closure(api_return_7, NULL, NULL, 0, 0);
    Obj* res = call_closure(clos, NULL, 0);
    ASSERT_EQ(obj_to_int(res), 7);
    dec_ref(res);
    dec_ref(clos);
    PASS();
}

static void test_api_borrow_get(void) {
    TEST("borrow_get");
    Obj* obj = mk_int(11);
    BorrowRef* ref = borrow_create(obj, "api");
    Obj* got = borrow_get(ref);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(obj_to_int(got), 11);
    dec_ref(got);
    borrow_release(ref);
    dec_ref(obj);
    PASS();
}

static void test_api_arena(void) {
    TEST("arena");
    Arena* a = arena_create();
    ASSERT_NOT_NULL(a);
    Obj* x = arena_mk_int(a, 42);
    Obj* p = arena_mk_pair(a, x, NULL);
    ASSERT_NOT_NULL(p);
    arena_reset(a);
    arena_destroy(a);
    PASS();
}

static void test_api_channel(void) {
    TEST("channel");
    Obj* ch = channel_create(1);
    Obj* val = mk_int(55);
    int sent = channel_send(ch, val);
    ASSERT(sent);
    Obj* recv = channel_recv(ch);
    ASSERT_NOT_NULL(recv);
    ASSERT_EQ(obj_to_int(recv), 55);
    dec_ref(recv);
    channel_close(ch);
    dec_ref(ch);
    PASS();
}

static void test_api_atom(void) {
    TEST("atom");
    Obj* init = mk_int(1);
    Obj* atom = atom_create(init);
    dec_ref(init);
    Obj* v1 = atom_deref(atom);
    ASSERT_EQ(obj_to_int(v1), 1);

    Obj* newv = mk_int(2);
    Obj* v2 = atom_reset(atom, newv);
    dec_ref(newv);
    ASSERT_EQ(obj_to_int(v2), 2);

    Obj* ok = atom_compare_and_set(atom, v2, mk_int(3));
    ASSERT(obj_to_int(ok) != 0);

    dec_ref(v1);
    dec_ref(v2);
    dec_ref(ok);
    dec_ref(atom);
    PASS();
}

static void test_api_thread(void) {
    TEST("thread");
    Obj* clos = mk_closure(api_return_7, NULL, NULL, 0, 0);
    Obj* th = thread_create(clos);
    Obj* res = thread_join(th);
    ASSERT_EQ(obj_to_int(res), 7);
    dec_ref(res);
    dec_ref(th);
    dec_ref(clos);
    PASS();
}

int main(void) {
    printf("Runtime API Test Suite\n");
    printf("========================\n");

    test_api_constructors();
    test_api_primitives();
    test_api_list_ops();
    test_api_closure();
    test_api_borrow_get();
    test_api_arena();
    test_api_channel();
    test_api_atom();
    test_api_thread();

    if (tests_failed == 0) {
        printf("\nAll %d tests passed.\n", tests_run);
        return 0;
    }

    printf("\n%d/%d tests failed.\n", tests_failed, tests_run);
    return 1;
}
