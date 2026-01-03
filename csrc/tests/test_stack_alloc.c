/*
 * Escape-Aware Stack Allocation Tests
 *
 * Tests that allocation strategy is correctly determined based on escape analysis.
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

/* ========== Allocation Strategy Tests ========== */

TEST(test_alloc_strategy_names) {
    /* Verify strategy name strings */
    ASSERT(strcmp(omni_alloc_strategy_name(ALLOC_HEAP), "heap") == 0);
    ASSERT(strcmp(omni_alloc_strategy_name(ALLOC_STACK), "stack") == 0);
    ASSERT(strcmp(omni_alloc_strategy_name(ALLOC_POOL), "pool") == 0);
    ASSERT(strcmp(omni_alloc_strategy_name(ALLOC_ARENA), "arena") == 0);
}

TEST(test_local_unique_can_stack) {
    /* (let ((x 1)) x)
     * x is local and unique - should be able to stack allocate
     */
    OmniValue* bindings = mk_cons(
        mk_list2(mk_sym("x"), mk_int(1)),
        omni_nil
    );
    OmniValue* expr = mk_list3(mk_sym("let"), bindings, mk_sym("x"));

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);

    /* Check ownership info for x */
    OwnerInfo* o = omni_get_owner_info(ctx, "x");
    ASSERT(o != NULL);
    ASSERT(o->ownership == OWNER_LOCAL);
    ASSERT(o->is_unique == true);

    /* For local unique with no escape info, should use stack */
    AllocStrategy strategy = omni_get_alloc_strategy(ctx, "x");
    ASSERT(strategy == ALLOC_STACK);

    omni_analysis_free(ctx);
}

TEST(test_param_not_our_allocation) {
    /* Function parameters are not our allocation */
    AnalysisContext* ctx = omni_analysis_new();

    /* Manually set up a parameter */
    VarUsage* u = malloc(sizeof(VarUsage));
    u->name = strdup("param");
    u->flags = VAR_USAGE_READ;
    u->first_use = 0;
    u->last_use = 1;
    u->def_pos = 0;
    u->is_param = true;
    u->next = ctx->var_usages;
    ctx->var_usages = u;

    /* Run ownership pass */
    OmniValue* body = mk_sym("param");
    omni_analyze_ownership(ctx, body);

    /* Parameters are heap (not our allocation to decide) */
    AllocStrategy strategy = omni_get_alloc_strategy(ctx, "param");
    ASSERT(strategy == ALLOC_HEAP);

    omni_analysis_free(ctx);
}

TEST(test_escape_return_uses_heap) {
    /* Variables that escape via return must use heap */
    AnalysisContext* ctx = omni_analysis_new();

    /* Set up escape info for return */
    EscapeInfo* e = malloc(sizeof(EscapeInfo));
    e->name = strdup("result");
    e->escape_class = ESCAPE_RETURN;
    e->is_unique = true;
    e->next = ctx->escape_info;
    ctx->escape_info = e;

    /* Set up var usage */
    VarUsage* u = malloc(sizeof(VarUsage));
    u->name = strdup("result");
    u->flags = VAR_USAGE_READ | VAR_USAGE_WRITE;
    u->first_use = 0;
    u->last_use = 1;
    u->def_pos = 0;
    u->is_param = false;
    u->next = ctx->var_usages;
    ctx->var_usages = u;

    /* Run ownership analysis */
    OmniValue* body = mk_sym("result");
    omni_analyze_ownership(ctx, body);

    /* Escapes via return - must use heap */
    AllocStrategy strategy = omni_get_alloc_strategy(ctx, "result");
    ASSERT(strategy == ALLOC_HEAP);

    omni_analysis_free(ctx);
}

TEST(test_escape_none_uses_stack) {
    /* Variables that don't escape can use stack */
    AnalysisContext* ctx = omni_analysis_new();

    /* Set up escape info as NONE */
    EscapeInfo* e = malloc(sizeof(EscapeInfo));
    e->name = strdup("local");
    e->escape_class = ESCAPE_NONE;
    e->is_unique = true;
    e->next = ctx->escape_info;
    ctx->escape_info = e;

    /* Set up var usage */
    VarUsage* u = malloc(sizeof(VarUsage));
    u->name = strdup("local");
    u->flags = VAR_USAGE_READ | VAR_USAGE_WRITE;
    u->first_use = 0;
    u->last_use = 1;
    u->def_pos = 0;
    u->is_param = false;
    u->next = ctx->var_usages;
    ctx->var_usages = u;

    /* Run ownership analysis */
    OmniValue* body = mk_sym("local");
    omni_analyze_ownership(ctx, body);

    /* Does not escape - can use stack */
    AllocStrategy strategy = omni_get_alloc_strategy(ctx, "local");
    ASSERT(strategy == ALLOC_STACK);

    omni_analysis_free(ctx);
}

TEST(test_can_stack_alloc_helper) {
    /* Test the omni_can_stack_alloc helper function */
    AnalysisContext* ctx = omni_analysis_new();

    /* Set up ESCAPE_NONE variable */
    EscapeInfo* e = malloc(sizeof(EscapeInfo));
    e->name = strdup("stackable");
    e->escape_class = ESCAPE_NONE;
    e->is_unique = true;
    e->next = ctx->escape_info;
    ctx->escape_info = e;

    /* Set up var usage */
    VarUsage* u = malloc(sizeof(VarUsage));
    u->name = strdup("stackable");
    u->flags = VAR_USAGE_READ;
    u->first_use = 0;
    u->last_use = 1;
    u->def_pos = 0;
    u->is_param = false;
    u->next = ctx->var_usages;
    ctx->var_usages = u;

    omni_analyze_ownership(ctx, mk_sym("stackable"));

    ASSERT(omni_can_stack_alloc(ctx, "stackable") == true);

    omni_analysis_free(ctx);
}

TEST(test_codegen_has_stack_macros) {
    /* Test that generated code includes stack allocation macros */
    OmniValue* expr = mk_int(42);

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();
    omni_analyze_ownership(cg->analysis, expr);

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Should have stack allocation macros */
    ASSERT(strstr(output, "STACK_INT") != NULL);
    ASSERT(strstr(output, "STACK_CELL") != NULL);
    ASSERT(strstr(output, "init_int") != NULL);
    ASSERT(strstr(output, "init_cell") != NULL);

    free(output);
    omni_codegen_free(cg);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Escape-Aware Stack Allocation Tests ===\033[0m\n");

    printf("\n\033[33m--- Allocation Strategy Selection ---\033[0m\n");
    RUN_TEST(test_alloc_strategy_names);
    RUN_TEST(test_local_unique_can_stack);
    RUN_TEST(test_param_not_our_allocation);
    RUN_TEST(test_escape_return_uses_heap);
    RUN_TEST(test_escape_none_uses_stack);
    RUN_TEST(test_can_stack_alloc_helper);

    printf("\n\033[33m--- Code Generation ---\033[0m\n");
    RUN_TEST(test_codegen_has_stack_macros);

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
