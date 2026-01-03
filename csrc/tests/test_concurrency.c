/*
 * Concurrency Ownership Inference Tests
 *
 * Tests that thread locality, channel ownership transfer, and
 * atomic reference counting are correctly inferred.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../ast/ast.h"
#include "../analysis/analysis.h"
#include "../codegen/codegen.h"

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

/* Helper to create a simple symbol */
static OmniValue* mk_sym(const char* name) {
    return omni_new_sym(name);
}

/* Helper to create a cons cell */
static OmniValue* mk_cons(OmniValue* car, OmniValue* cdr) {
    return omni_new_cell(car, cdr);
}

/* Helper to create a list */
static OmniValue* mk_list2(OmniValue* a, OmniValue* b) {
    return mk_cons(a, mk_cons(b, omni_nil));
}

static OmniValue* mk_list3(OmniValue* a, OmniValue* b, OmniValue* c) {
    return mk_cons(a, mk_cons(b, mk_cons(c, omni_nil)));
}

/* ========== Thread Locality Names ========== */

TEST(test_thread_locality_names) {
    ASSERT(strcmp(omni_thread_locality_name(THREAD_LOCAL), "local") == 0);
    ASSERT(strcmp(omni_thread_locality_name(THREAD_SHARED), "shared") == 0);
    ASSERT(strcmp(omni_thread_locality_name(THREAD_TRANSFER), "transfer") == 0);
    ASSERT(strcmp(omni_thread_locality_name(THREAD_IMMUTABLE), "immutable") == 0);
}

TEST(test_channel_op_names) {
    ASSERT(strcmp(omni_channel_op_name(CHAN_SEND), "send") == 0);
    ASSERT(strcmp(omni_channel_op_name(CHAN_RECV), "recv") == 0);
    ASSERT(strcmp(omni_channel_op_name(CHAN_CLOSE), "close") == 0);
}

/* ========== Thread Locality Marking ========== */

TEST(test_mark_thread_local) {
    AnalysisContext* ctx = omni_analysis_new();

    omni_mark_thread_local(ctx, "my_var", 0);

    ThreadLocality loc = omni_get_thread_locality(ctx, "my_var");
    ASSERT(loc == THREAD_LOCAL);
    ASSERT(omni_needs_atomic_rc(ctx, "my_var") == false);

    omni_analysis_free(ctx);
}

TEST(test_mark_thread_shared) {
    AnalysisContext* ctx = omni_analysis_new();

    omni_mark_thread_shared(ctx, "shared_data");

    ThreadLocality loc = omni_get_thread_locality(ctx, "shared_data");
    ASSERT(loc == THREAD_SHARED);
    ASSERT(omni_needs_atomic_rc(ctx, "shared_data") == true);

    omni_analysis_free(ctx);
}

TEST(test_default_locality) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Unmarked variable should be thread-local by default */
    ThreadLocality loc = omni_get_thread_locality(ctx, "unknown_var");
    ASSERT(loc == THREAD_LOCAL);
    ASSERT(omni_needs_atomic_rc(ctx, "unknown_var") == false);

    omni_analysis_free(ctx);
}

/* ========== Channel Operations ========== */

TEST(test_channel_send) {
    AnalysisContext* ctx = omni_analysis_new();

    omni_record_channel_send(ctx, "ch", "msg", true);

    ASSERT(omni_is_channel_transferred(ctx, "msg") == true);
    ASSERT(omni_get_thread_locality(ctx, "msg") == THREAD_TRANSFER);

    omni_analysis_free(ctx);
}

TEST(test_channel_recv) {
    AnalysisContext* ctx = omni_analysis_new();

    omni_record_channel_recv(ctx, "ch", "received");

    ASSERT(omni_is_channel_transferred(ctx, "received") == true);
    ASSERT(omni_get_thread_locality(ctx, "received") == THREAD_LOCAL);

    omni_analysis_free(ctx);
}

TEST(test_should_free_after_send) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Send with ownership transfer - should NOT free after */
    omni_record_channel_send(ctx, "ch", "val", true);
    ASSERT(omni_should_free_after_send(ctx, "ch", "val") == false);

    omni_analysis_free(ctx);
}

/* ========== Thread Spawn ========== */

TEST(test_thread_spawn) {
    AnalysisContext* ctx = omni_analysis_new();

    char* captured[] = {"data", "shared_ref"};
    omni_record_thread_spawn(ctx, "worker_0", captured, 2);

    /* Captured variables become shared */
    ASSERT(omni_get_thread_locality(ctx, "data") == THREAD_SHARED);
    ASSERT(omni_needs_atomic_rc(ctx, "data") == true);
    ASSERT(omni_get_thread_locality(ctx, "shared_ref") == THREAD_SHARED);

    omni_analysis_free(ctx);
}

TEST(test_threads_capturing_variable) {
    AnalysisContext* ctx = omni_analysis_new();

    char* captured1[] = {"data"};
    char* captured2[] = {"data", "other"};
    omni_record_thread_spawn(ctx, "thread_1", captured1, 1);
    omni_record_thread_spawn(ctx, "thread_2", captured2, 2);

    size_t count = 0;
    ThreadSpawnInfo** threads = omni_get_threads_capturing(ctx, "data", &count);
    ASSERT(count == 2);
    free(threads);

    omni_analysis_free(ctx);
}

/* ========== Concurrency Analysis ========== */

TEST(test_analyze_send_expr) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (send! ch value) */
    OmniValue* expr = mk_list3(
        mk_sym("send!"),
        mk_sym("ch"),
        mk_sym("value")
    );

    omni_analyze_concurrency(ctx, expr);

    ASSERT(omni_is_channel_transferred(ctx, "value") == true);

    omni_analysis_free(ctx);
}

TEST(test_analyze_spawn_expr) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Manually record a spawn to test that captured vars become shared */
    char* captured[] = {"x"};
    omni_record_thread_spawn(ctx, "test_thread", captured, 1);

    /* x should be marked as shared (captured by spawn) */
    ASSERT(omni_get_thread_locality(ctx, "x") == THREAD_SHARED);

    omni_analysis_free(ctx);
}

TEST(test_analyze_atom_expr) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (atom data) */
    OmniValue* expr = mk_list2(
        mk_sym("atom"),
        mk_sym("data")
    );

    omni_analyze_concurrency(ctx, expr);

    /* data should be marked as shared (stored in atom) */
    ASSERT(omni_get_thread_locality(ctx, "data") == THREAD_SHARED);

    omni_analysis_free(ctx);
}

/* ========== Codegen Tests ========== */

TEST(test_codegen_has_concurrency_macros) {
    OmniValue* expr = omni_new_int(42);

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Should have concurrency macros */
    ASSERT(strstr(output, "ATOMIC_INC_REF") != NULL);
    ASSERT(strstr(output, "ATOMIC_DEC_REF") != NULL);
    ASSERT(strstr(output, "THREAD_LOCAL_VAR") != NULL);
    ASSERT(strstr(output, "THREAD_SHARED_VAR") != NULL);
    ASSERT(strstr(output, "Channel") != NULL);
    ASSERT(strstr(output, "channel_send") != NULL);
    ASSERT(strstr(output, "channel_recv") != NULL);
    ASSERT(strstr(output, "SEND_OWNERSHIP") != NULL);
    ASSERT(strstr(output, "RECV_OWNERSHIP") != NULL);
    ASSERT(strstr(output, "SPAWN_THREAD") != NULL);
    ASSERT(strstr(output, "INC_REF_FOR_THREAD") != NULL);
    ASSERT(strstr(output, "DEC_REF_FOR_THREAD") != NULL);

    free(output);
    omni_codegen_free(cg);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Concurrency Ownership Inference Tests ===\033[0m\n");

    printf("\n\033[33m--- Thread Locality Names ---\033[0m\n");
    RUN_TEST(test_thread_locality_names);
    RUN_TEST(test_channel_op_names);

    printf("\n\033[33m--- Thread Locality Marking ---\033[0m\n");
    RUN_TEST(test_mark_thread_local);
    RUN_TEST(test_mark_thread_shared);
    RUN_TEST(test_default_locality);

    printf("\n\033[33m--- Channel Operations ---\033[0m\n");
    RUN_TEST(test_channel_send);
    RUN_TEST(test_channel_recv);
    RUN_TEST(test_should_free_after_send);

    printf("\n\033[33m--- Thread Spawn ---\033[0m\n");
    RUN_TEST(test_thread_spawn);
    RUN_TEST(test_threads_capturing_variable);

    printf("\n\033[33m--- Concurrency Analysis ---\033[0m\n");
    RUN_TEST(test_analyze_send_expr);
    RUN_TEST(test_analyze_spawn_expr);
    RUN_TEST(test_analyze_atom_expr);

    printf("\n\033[33m--- Code Generation ---\033[0m\n");
    RUN_TEST(test_codegen_has_concurrency_macros);

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
