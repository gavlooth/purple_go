/*
 * Perceus Reuse Analysis Tests
 *
 * Tests that reuse candidates are correctly identified when a free
 * immediately precedes an allocation of the same size class.
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

/* Helper to create a simple integer */
static OmniValue* mk_int(long val) {
    return omni_new_int(val);
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

/* ========== Type Size Tests ========== */

TEST(test_type_sizes) {
    /* Verify size class assignments */
    ASSERT(omni_type_size("Int") == 24);
    ASSERT(omni_type_size("int") == 24);
    ASSERT(omni_type_size("Float") == 24);
    ASSERT(omni_type_size("Cell") == 32);
    ASSERT(omni_type_size("Cons") == 32);
    ASSERT(omni_type_size("String") == 32);
    ASSERT(omni_type_size("unknown") == 32);  /* Default */
}

TEST(test_type_size_matching) {
    /* Int and Float have same size class (24) */
    ASSERT(omni_type_size("Int") == omni_type_size("Float"));

    /* Cell and String have same size class (32) */
    ASSERT(omni_type_size("Cell") == omni_type_size("String"));

    /* Int and Cell have different size classes */
    ASSERT(omni_type_size("Int") != omni_type_size("Cell"));
}

/* ========== Reuse Candidate Tests ========== */

TEST(test_add_reuse_candidate) {
    /* Set up context with ownership info */
    AnalysisContext* ctx = omni_analysis_new();

    /* Create owner info for a variable */
    OwnerInfo* o = malloc(sizeof(OwnerInfo));
    o->name = strdup("old_cell");
    o->ownership = OWNER_LOCAL;
    o->must_free = true;
    o->free_pos = 5;
    o->is_unique = true;
    o->shape = SHAPE_TREE;
    o->alloc_strategy = ALLOC_HEAP;
    o->next = ctx->owner_info;
    ctx->owner_info = o;

    /* Add reuse candidate */
    omni_add_reuse_candidate(ctx, "old_cell", "Cell", 6);

    /* Should have one reuse candidate */
    ASSERT(ctx->reuse_candidates != NULL);
    ASSERT(strcmp(ctx->reuse_candidates->freed_var, "old_cell") == 0);
    ASSERT(strcmp(ctx->reuse_candidates->type_name, "Cell") == 0);
    ASSERT(ctx->reuse_candidates->alloc_pos == 6);
    ASSERT(ctx->reuse_candidates->free_pos == 5);
    ASSERT(ctx->reuse_candidates->can_reuse == true);
    ASSERT(ctx->reuse_candidates->is_consumed == false);

    omni_analysis_free(ctx);
}

TEST(test_get_reuse_at) {
    /* Set up context with a reuse candidate */
    AnalysisContext* ctx = omni_analysis_new();

    /* Create owner info */
    OwnerInfo* o = malloc(sizeof(OwnerInfo));
    o->name = strdup("reusable");
    o->ownership = OWNER_LOCAL;
    o->must_free = true;
    o->free_pos = 10;
    o->is_unique = true;
    o->shape = SHAPE_TREE;
    o->alloc_strategy = ALLOC_HEAP;
    o->next = ctx->owner_info;
    ctx->owner_info = o;

    /* Add reuse candidate at position 11 */
    omni_add_reuse_candidate(ctx, "reusable", "Cell", 11);

    /* Get reuse at correct position */
    ReuseCandidate* rc = omni_get_reuse_at(ctx, 11);
    ASSERT(rc != NULL);
    ASSERT(strcmp(rc->freed_var, "reusable") == 0);

    /* Get reuse at wrong position should return NULL */
    ReuseCandidate* rc2 = omni_get_reuse_at(ctx, 12);
    ASSERT(rc2 == NULL);

    omni_analysis_free(ctx);
}

TEST(test_reuse_not_for_borrowed) {
    /* Borrowed variables should not be reused */
    AnalysisContext* ctx = omni_analysis_new();

    /* Create borrowed owner info */
    OwnerInfo* o = malloc(sizeof(OwnerInfo));
    o->name = strdup("borrowed");
    o->ownership = OWNER_BORROWED;
    o->must_free = false;
    o->free_pos = 0;
    o->is_unique = false;
    o->shape = SHAPE_TREE;
    o->alloc_strategy = ALLOC_HEAP;
    o->next = ctx->owner_info;
    ctx->owner_info = o;

    /* Should not be able to reuse */
    ASSERT(omni_can_reuse_for(ctx, "borrowed", "Cell") == false);

    omni_analysis_free(ctx);
}

TEST(test_reuse_not_for_shared) {
    /* Non-unique shared variables should not be reused */
    AnalysisContext* ctx = omni_analysis_new();

    /* Create shared owner info */
    OwnerInfo* o = malloc(sizeof(OwnerInfo));
    o->name = strdup("shared");
    o->ownership = OWNER_SHARED;
    o->must_free = true;
    o->free_pos = 5;
    o->is_unique = false;  /* Not unique - can't reuse */
    o->shape = SHAPE_DAG;
    o->alloc_strategy = ALLOC_HEAP;
    o->next = ctx->owner_info;
    ctx->owner_info = o;

    /* Should not be able to reuse */
    ASSERT(omni_can_reuse_for(ctx, "shared", "Cell") == false);

    omni_analysis_free(ctx);
}

/* ========== Codegen Tests ========== */

TEST(test_codegen_has_reuse_functions) {
    /* Test that generated code includes reuse infrastructure */
    OmniValue* expr = mk_int(42);

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Should have reuse functions */
    ASSERT(strstr(output, "reuse_as_int") != NULL);
    ASSERT(strstr(output, "reuse_as_cell") != NULL);
    ASSERT(strstr(output, "reuse_as_float") != NULL);

    /* Should have CAN_REUSE macro */
    ASSERT(strstr(output, "CAN_REUSE") != NULL);

    /* Should have conditional reuse macros */
    ASSERT(strstr(output, "REUSE_OR_NEW_INT") != NULL);
    ASSERT(strstr(output, "REUSE_OR_NEW_CELL") != NULL);

    free(output);
    omni_codegen_free(cg);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Perceus Reuse Analysis Tests ===\033[0m\n");

    printf("\n\033[33m--- Type Size Classes ---\033[0m\n");
    RUN_TEST(test_type_sizes);
    RUN_TEST(test_type_size_matching);

    printf("\n\033[33m--- Reuse Candidate Tracking ---\033[0m\n");
    RUN_TEST(test_add_reuse_candidate);
    RUN_TEST(test_get_reuse_at);
    RUN_TEST(test_reuse_not_for_borrowed);
    RUN_TEST(test_reuse_not_for_shared);

    printf("\n\033[33m--- Code Generation ---\033[0m\n");
    RUN_TEST(test_codegen_has_reuse_functions);

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
