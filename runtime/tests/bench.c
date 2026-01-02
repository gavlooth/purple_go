/* Simple benchmarks comparing Purple runtime vs raw C */
#include "../include/purple.h"
#include <time.h>
#include <stdio.h>

#define ITERATIONS 1000000
#define LIST_SIZE 10000  /* Smaller to avoid stack overflow in free_tree */

/* Prevent dead code elimination */
volatile long sink;

/* Raw C fibonacci */
long fib_c(long n) {
    if (n <= 1) return n;
    long a = 0, b = 1;
    for (long i = 2; i <= n; i++) {
        long tmp = a + b;
        a = b;
        b = tmp;
    }
    return b;
}

/* Purple runtime fibonacci (boxed integers) */
Obj* fib_purple(long n) {
    if (n <= 1) return mk_int(n);
    Obj* a = mk_int(0);
    Obj* b = mk_int(1);
    for (long i = 2; i <= n; i++) {
        Obj* tmp = prim_add(a, b);
        dec_ref(a);
        a = b;
        b = tmp;
    }
    dec_ref(a);
    return b;
}

/* Purple runtime fibonacci with UNBOXED integers (tagged pointers) */
Obj* fib_unboxed(long n) {
    if (n <= 1) return mk_int_unboxed(n);
    Obj* a = mk_int_unboxed(0);
    Obj* b = mk_int_unboxed(1);
    for (long i = 2; i <= n; i++) {
        Obj* tmp = prim_add(a, b);  /* prim_add now returns unboxed! */
        /* No dec_ref needed - unboxed integers don't need RC */
        a = b;
        b = tmp;
    }
    return b;
}

/* Raw C sum - use volatile to prevent optimization */
long sum_c(long n) {
    volatile long total = 0;
    for (long i = 0; i < n; i++) {
        total += i;
    }
    return total;
}

/* Purple runtime sum */
Obj* sum_purple(long n) {
    Obj* total = mk_int(0);
    for (long i = 0; i < n; i++) {
        Obj* val = mk_int(i);
        Obj* new_total = prim_add(total, val);
        dec_ref(total);
        dec_ref(val);
        total = new_total;
    }
    return total;
}

/* Purple runtime sum with UNBOXED integers */
Obj* sum_unboxed(long n) {
    Obj* total = mk_int_unboxed(0);
    for (long i = 0; i < n; i++) {
        Obj* val = mk_int_unboxed(i);
        Obj* new_total = prim_add(total, val);
        /* No dec_ref needed for unboxed! */
        total = new_total;
    }
    return total;
}

/* Raw C list creation - allocate n separate objects like Purple does */
void list_c(long n) {
    typedef struct Node { long val; struct Node* next; } Node;
    Node* list = NULL;
    for (long i = 0; i < n; i++) {
        Node* node = malloc(sizeof(Node));
        node->val = i;
        node->next = list;
        list = node;
    }
    /* Free iteratively */
    while (list) {
        Node* next = list->next;
        free(list);
        list = next;
    }
}

/* Purple runtime list creation - just create, let it leak for benchmark */
void list_purple(long n) {
    Obj* list = NULL;
    for (long i = 0; i < n; i++) {
        Obj* val = mk_int(i);
        Obj* new_list = mk_pair(val, list);
        list = new_list;
    }
    /* Note: In a real program we'd properly free this
     * For benchmark purposes, we're measuring allocation speed */
    (void)list;
}

double time_us(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000000.0 +
           (end.tv_nsec - start.tv_nsec) / 1000.0;
}

int main(void) {
    struct timespec start, end;

    printf("Purple Runtime Benchmarks\n");
    printf("=========================\n\n");

    /* Fibonacci benchmark */
    printf("Fibonacci(40):\n");

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 10000; i++) {
        sink = fib_c(40);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double c_fib = time_us(start, end);
    printf("  Raw C:        %.1f us (10000 iterations)\n", c_fib);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 10000; i++) {
        Obj* r = fib_purple(40);
        sink = obj_to_int(r);  /* Safe extraction for both boxed/unboxed */
        if (IS_BOXED(r)) dec_ref(r);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double purple_fib = time_us(start, end);
    printf("  Purple boxed: %.1f us (10000 iterations)\n", purple_fib);
    printf("  Ratio:        %.1fx slower\n", purple_fib / c_fib);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 10000; i++) {
        Obj* r = fib_unboxed(40);
        sink = obj_to_int(r);  /* Use safe extraction */
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double unboxed_fib = time_us(start, end);
    printf("  Purple UNBOX: %.1f us (10000 iterations)\n", unboxed_fib);
    printf("  Ratio:        %.1fx slower (%.1fx speedup!)\n\n", unboxed_fib / c_fib, purple_fib / unboxed_fib);

    /* Sum benchmark */
    printf("Sum(0..100000):\n");

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 100; i++) sink = sum_c(100000);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double c_sum = time_us(start, end);
    printf("  Raw C:   %.1f us (100 iterations)\n", c_sum);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 100; i++) {
        Obj* r = sum_purple(100000);
        sink = obj_to_int(r);  /* Safe extraction for both boxed/unboxed */
        if (IS_BOXED(r)) dec_ref(r);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double purple_sum = time_us(start, end);
    printf("  Purple boxed:  %.1f us (100 iterations)\n", purple_sum);
    printf("  Ratio:         %.1fx slower\n", purple_sum / c_sum);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 100; i++) {
        Obj* r = sum_unboxed(100000);
        sink = obj_to_int(r);  /* Use safe extraction */
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double unboxed_sum = time_us(start, end);
    printf("  Purple UNBOX:  %.1f us (100 iterations)\n", unboxed_sum);
    printf("  Ratio:         %.1fx slower (%.1fx speedup!)\n\n", unboxed_sum / c_sum, purple_sum / unboxed_sum);

    /* List creation benchmark */
    printf("Create list of %d elements:\n", LIST_SIZE);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 1000; i++) list_c(LIST_SIZE);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double c_list = time_us(start, end);
    printf("  Raw C (malloc+free):  %.1f us (1000 iterations)\n", c_list);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 1000; i++) list_purple(LIST_SIZE);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double purple_list = time_us(start, end);
    printf("  Purple (alloc only):  %.1f us (1000 iterations)\n", purple_list);
    printf("  Ratio:   %.1fx slower\n\n", purple_list / c_list);

    printf("Memory per integer:\n");
    printf("  Raw C:          %zu bytes\n", sizeof(long));
    printf("  Purple boxed:   %zu bytes\n", sizeof(Obj));
    printf("  Purple UNBOX:   %zu bytes (tagged pointer)\n", sizeof(Obj*));

    return 0;
}
