/*
 * Borrow/Tether Loop Insertion Tests
 *
 * Tests that borrowed references are kept alive during loop iteration
 * through tethering.
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

static OmniValue* mk_list4(OmniValue* a, OmniValue* b, OmniValue* c, OmniValue* d) {
    return mk_cons(a, mk_cons(b, mk_cons(c, mk_cons(d, omni_nil))));
}

/* ========== Borrow Kind Names ========== */

TEST(test_borrow_kind_names) {
    ASSERT(strcmp(omni_borrow_kind_name(BORROW_NONE), "none") == 0);
    ASSERT(strcmp(omni_borrow_kind_name(BORROW_SHARED), "shared") == 0);
    ASSERT(strcmp(omni_borrow_kind_name(BORROW_EXCLUSIVE), "exclusive") == 0);
    ASSERT(strcmp(omni_borrow_kind_name(BORROW_LOOP), "loop") == 0);
}

/* ========== Borrow Start/End ========== */

TEST(test_borrow_start_end) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Start a borrow */
    omni_borrow_start(ctx, "my_list", "iter", BORROW_LOOP);

    /* Should be borrowed */
    ASSERT(omni_is_borrowed(ctx, "my_list") == true);

    /* Get borrow info */
    BorrowInfo* b = omni_get_borrow_info(ctx, "my_list");
    ASSERT(b != NULL);
    ASSERT(strcmp(b->borrowed_var, "my_list") == 0);
    ASSERT(strcmp(b->borrow_holder, "iter") == 0);
    ASSERT(b->kind == BORROW_LOOP);
    ASSERT(b->needs_tether == true);  /* Loop borrows need tethering */

    /* End borrow */
    omni_borrow_end(ctx, "my_list");

    /* Should no longer be actively borrowed */
    ASSERT(omni_is_borrowed(ctx, "my_list") == false);

    omni_analysis_free(ctx);
}

TEST(test_borrow_no_tether_for_shared) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Shared borrow doesn't need tethering */
    omni_borrow_start(ctx, "data", NULL, BORROW_SHARED);

    BorrowInfo* b = omni_get_borrow_info(ctx, "data");
    ASSERT(b != NULL);
    ASSERT(b->needs_tether == false);

    omni_analysis_free(ctx);
}

/* ========== Tether Points ========== */

TEST(test_add_tether) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Add tether entry */
    omni_add_tether(ctx, "coll", true);
    ctx->position++;

    /* Add tether exit */
    omni_add_tether(ctx, "coll", false);

    /* Should have tethers */
    ASSERT(ctx->tethers != NULL);

    omni_analysis_free(ctx);
}

TEST(test_needs_tether) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Start a loop borrow */
    int start_pos = ctx->position;
    omni_borrow_start(ctx, "items", "x", BORROW_LOOP);
    ctx->position += 5;  /* Simulate loop body */

    /* Should need tether during the borrow */
    ASSERT(omni_needs_tether(ctx, "items", start_pos) == true);
    ASSERT(omni_needs_tether(ctx, "items", start_pos + 3) == true);

    /* End borrow */
    omni_borrow_end(ctx, "items");

    /* After borrow ends, should still show needs_tether for positions during borrow */
    ASSERT(omni_needs_tether(ctx, "items", start_pos + 2) == true);

    omni_analysis_free(ctx);
}

/* ========== Borrow Analysis ========== */

TEST(test_analyze_foreach) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (for-each x items (print x)) */
    OmniValue* expr = mk_list4(
        mk_sym("for-each"),
        mk_sym("x"),
        mk_sym("items"),
        mk_list2(mk_sym("print"), mk_sym("x"))
    );

    omni_analyze_borrows(ctx, expr);

    /* Should have recorded a borrow for 'items' */
    BorrowInfo* b = omni_get_borrow_info(ctx, "items");
    ASSERT(b != NULL);
    ASSERT(b->kind == BORROW_LOOP);

    omni_analysis_free(ctx);
}

TEST(test_analyze_map) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (map f xs) */
    OmniValue* expr = mk_list3(
        mk_sym("map"),
        mk_sym("f"),
        mk_sym("xs")
    );

    omni_analyze_borrows(ctx, expr);

    /* 'xs' should be borrowed */
    BorrowInfo* b = omni_get_borrow_info(ctx, "xs");
    ASSERT(b != NULL);
    ASSERT(b->kind == BORROW_LOOP);

    omni_analysis_free(ctx);
}

/* ========== Codegen Tests ========== */

TEST(test_codegen_has_tether_macros) {
    OmniValue* expr = omni_new_int(42);

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Should have tether macros */
    ASSERT(strstr(output, "#define TETHER") != NULL);
    ASSERT(strstr(output, "#define UNTETHER") != NULL);
    ASSERT(strstr(output, "BORROW_FOR_LOOP") != NULL);
    ASSERT(strstr(output, "END_LOOP_BORROW") != NULL);
    ASSERT(strstr(output, "SCOPED_TETHER") != NULL);

    free(output);
    omni_codegen_free(cg);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Borrow/Tether Loop Insertion Tests ===\033[0m\n");

    printf("\n\033[33m--- Borrow Kind Names ---\033[0m\n");
    RUN_TEST(test_borrow_kind_names);

    printf("\n\033[33m--- Borrow Start/End ---\033[0m\n");
    RUN_TEST(test_borrow_start_end);
    RUN_TEST(test_borrow_no_tether_for_shared);

    printf("\n\033[33m--- Tether Points ---\033[0m\n");
    RUN_TEST(test_add_tether);
    RUN_TEST(test_needs_tether);

    printf("\n\033[33m--- Borrow Analysis ---\033[0m\n");
    RUN_TEST(test_analyze_foreach);
    RUN_TEST(test_analyze_map);

    printf("\n\033[33m--- Code Generation ---\033[0m\n");
    RUN_TEST(test_codegen_has_tether_macros);

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
