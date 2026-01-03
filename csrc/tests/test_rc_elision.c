/*
 * Region-Aware RC Elision Tests
 *
 * Tests that reference counting operations are correctly elided when
 * analysis proves they are unnecessary.
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

/* ========== RC Elision Class Names ========== */

TEST(test_rc_elision_names) {
    /* Verify elision class name strings */
    ASSERT(strcmp(omni_rc_elision_name(RC_REQUIRED), "required") == 0);
    ASSERT(strcmp(omni_rc_elision_name(RC_ELIDE_INC), "elide_inc") == 0);
    ASSERT(strcmp(omni_rc_elision_name(RC_ELIDE_DEC), "elide_dec") == 0);
    ASSERT(strcmp(omni_rc_elision_name(RC_ELIDE_BOTH), "elide_both") == 0);
}

/* ========== Region Tests ========== */

TEST(test_region_new) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Create a region */
    RegionInfo* r = omni_region_new(ctx, "test_region");
    ASSERT(r != NULL);
    ASSERT(r->region_id == 0);  /* First region */
    ASSERT(strcmp(r->name, "test_region") == 0);
    ASSERT(r->parent == NULL);  /* No parent */

    omni_analysis_free(ctx);
}

TEST(test_region_nesting) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Create nested regions */
    RegionInfo* outer = omni_region_new(ctx, "outer");
    ASSERT(outer->region_id == 0);

    RegionInfo* inner = omni_region_new(ctx, "inner");
    ASSERT(inner->region_id == 1);
    ASSERT(inner->parent == outer);  /* Parent is outer */

    omni_region_end(ctx);  /* End inner */
    ASSERT(ctx->current_region == outer);

    omni_region_end(ctx);  /* End outer */
    ASSERT(ctx->current_region == NULL);

    omni_analysis_free(ctx);
}

TEST(test_region_add_var) {
    AnalysisContext* ctx = omni_analysis_new();

    omni_region_new(ctx, "test");
    omni_region_add_var(ctx, "x");
    omni_region_add_var(ctx, "y");

    RegionInfo* r = ctx->current_region;
    ASSERT(r->var_count == 2);
    ASSERT(strcmp(r->variables[0], "x") == 0);
    ASSERT(strcmp(r->variables[1], "y") == 0);

    omni_analysis_free(ctx);
}

TEST(test_same_region) {
    AnalysisContext* ctx = omni_analysis_new();

    omni_region_new(ctx, "r1");
    omni_region_add_var(ctx, "a");
    omni_region_add_var(ctx, "b");
    omni_region_end(ctx);

    omni_region_new(ctx, "r2");
    omni_region_add_var(ctx, "c");
    omni_region_end(ctx);

    /* a and b are in same region */
    ASSERT(omni_same_region(ctx, "a", "b") == true);

    /* a and c are in different regions */
    ASSERT(omni_same_region(ctx, "a", "c") == false);

    omni_analysis_free(ctx);
}

/* ========== RC Elision Tests ========== */

TEST(test_elide_unique) {
    /* Unique references should elide both inc and dec */
    AnalysisContext* ctx = omni_analysis_new();

    /* Set up unique owner info */
    OwnerInfo* o = malloc(sizeof(OwnerInfo));
    o->name = strdup("unique_var");
    o->ownership = OWNER_LOCAL;
    o->must_free = true;
    o->free_pos = 5;
    o->is_unique = true;  /* KEY: unique */
    o->shape = SHAPE_TREE;
    o->alloc_strategy = ALLOC_HEAP;
    o->next = ctx->owner_info;
    ctx->owner_info = o;

    /* Run RC elision on a symbol */
    omni_analyze_rc_elision(ctx, mk_sym("unique_var"));

    /* Should elide both */
    RCElisionClass elision = omni_get_rc_elision(ctx, "unique_var");
    ASSERT(elision == RC_ELIDE_BOTH);
    ASSERT(omni_can_elide_inc_ref(ctx, "unique_var") == true);
    ASSERT(omni_can_elide_dec_ref(ctx, "unique_var") == true);

    omni_analysis_free(ctx);
}

TEST(test_elide_stack) {
    /* Stack-allocated should elide both */
    AnalysisContext* ctx = omni_analysis_new();

    /* Set up stack-allocated owner info */
    OwnerInfo* o = malloc(sizeof(OwnerInfo));
    o->name = strdup("stack_var");
    o->ownership = OWNER_LOCAL;
    o->must_free = false;  /* No heap free needed */
    o->free_pos = -1;
    o->is_unique = false;
    o->shape = SHAPE_SCALAR;
    o->alloc_strategy = ALLOC_STACK;  /* KEY: stack allocated */
    o->next = ctx->owner_info;
    ctx->owner_info = o;

    omni_analyze_rc_elision(ctx, mk_sym("stack_var"));

    RCElisionClass elision = omni_get_rc_elision(ctx, "stack_var");
    ASSERT(elision == RC_ELIDE_BOTH);

    omni_analysis_free(ctx);
}

TEST(test_elide_arena) {
    /* Arena/pool-allocated should elide dec only (bulk free) */
    AnalysisContext* ctx = omni_analysis_new();

    /* Set up arena-allocated owner info */
    OwnerInfo* o = malloc(sizeof(OwnerInfo));
    o->name = strdup("arena_var");
    o->ownership = OWNER_LOCAL;
    o->must_free = false;  /* Arena handles it */
    o->free_pos = -1;
    o->is_unique = false;
    o->shape = SHAPE_TREE;
    o->alloc_strategy = ALLOC_ARENA;  /* KEY: arena allocated */
    o->next = ctx->owner_info;
    ctx->owner_info = o;

    omni_analyze_rc_elision(ctx, mk_sym("arena_var"));

    RCElisionClass elision = omni_get_rc_elision(ctx, "arena_var");
    ASSERT(elision == RC_ELIDE_DEC);
    ASSERT(omni_can_elide_inc_ref(ctx, "arena_var") == false);
    ASSERT(omni_can_elide_dec_ref(ctx, "arena_var") == true);

    omni_analysis_free(ctx);
}

TEST(test_elide_borrowed) {
    /* Borrowed references should elide inc only */
    AnalysisContext* ctx = omni_analysis_new();

    /* Set up borrowed owner info */
    OwnerInfo* o = malloc(sizeof(OwnerInfo));
    o->name = strdup("borrowed_var");
    o->ownership = OWNER_BORROWED;  /* KEY: borrowed */
    o->must_free = false;
    o->free_pos = -1;
    o->is_unique = false;
    o->shape = SHAPE_TREE;
    o->alloc_strategy = ALLOC_HEAP;
    o->next = ctx->owner_info;
    ctx->owner_info = o;

    omni_analyze_rc_elision(ctx, mk_sym("borrowed_var"));

    RCElisionClass elision = omni_get_rc_elision(ctx, "borrowed_var");
    ASSERT(elision == RC_ELIDE_INC);
    ASSERT(omni_can_elide_inc_ref(ctx, "borrowed_var") == true);
    ASSERT(omni_can_elide_dec_ref(ctx, "borrowed_var") == false);

    omni_analysis_free(ctx);
}

TEST(test_rc_required) {
    /* Shared heap variables should require RC */
    AnalysisContext* ctx = omni_analysis_new();

    /* Set up shared owner info */
    OwnerInfo* o = malloc(sizeof(OwnerInfo));
    o->name = strdup("shared_var");
    o->ownership = OWNER_SHARED;
    o->must_free = true;
    o->free_pos = 10;
    o->is_unique = false;  /* Not unique */
    o->shape = SHAPE_DAG;
    o->alloc_strategy = ALLOC_HEAP;
    o->next = ctx->owner_info;
    ctx->owner_info = o;

    omni_analyze_rc_elision(ctx, mk_sym("shared_var"));

    RCElisionClass elision = omni_get_rc_elision(ctx, "shared_var");
    ASSERT(elision == RC_REQUIRED);
    ASSERT(omni_can_elide_inc_ref(ctx, "shared_var") == false);
    ASSERT(omni_can_elide_dec_ref(ctx, "shared_var") == false);

    omni_analysis_free(ctx);
}

/* ========== Codegen Tests ========== */

TEST(test_codegen_has_rc_elision_macros) {
    /* Test that generated code includes RC elision infrastructure */
    OmniValue* expr = mk_int(42);

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Should have RC elision macros */
    ASSERT(strstr(output, "INC_REF_IF_NEEDED") != NULL);
    ASSERT(strstr(output, "DEC_REF_IF_NEEDED") != NULL);
    ASSERT(strstr(output, "RC_ELIDED") != NULL);
    ASSERT(strstr(output, "REGION_LOCAL_REF") != NULL);

    free(output);
    omni_codegen_free(cg);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Region-Aware RC Elision Tests ===\033[0m\n");

    printf("\n\033[33m--- RC Elision Class Names ---\033[0m\n");
    RUN_TEST(test_rc_elision_names);

    printf("\n\033[33m--- Region Management ---\033[0m\n");
    RUN_TEST(test_region_new);
    RUN_TEST(test_region_nesting);
    RUN_TEST(test_region_add_var);
    RUN_TEST(test_same_region);

    printf("\n\033[33m--- RC Elision Detection ---\033[0m\n");
    RUN_TEST(test_elide_unique);
    RUN_TEST(test_elide_stack);
    RUN_TEST(test_elide_arena);
    RUN_TEST(test_elide_borrowed);
    RUN_TEST(test_rc_required);

    printf("\n\033[33m--- Code Generation ---\033[0m\n");
    RUN_TEST(test_codegen_has_rc_elision_macros);

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
