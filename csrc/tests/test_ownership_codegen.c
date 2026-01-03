/*
 * Ownership-Driven Codegen Tests
 *
 * Tests that codegen emits the correct free strategy based on ownership analysis.
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

/* ========== Free Strategy Tests ========== */

TEST(test_local_unique_strategy) {
    /* (let ((x 1)) x)
     * x is local, unique - should use free_unique
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
    ASSERT(o->is_unique == true);  /* Local, not aliased */

    /* Check free strategy */
    FreeStrategy strategy = omni_get_free_strategy(ctx, "x");
    ASSERT(strategy == FREE_STRATEGY_UNIQUE || strategy == FREE_STRATEGY_TREE);

    omni_analysis_free(ctx);
}

TEST(test_param_borrowed_strategy) {
    /* Function parameter - should be borrowed, no free */
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

    /* Check free strategy */
    FreeStrategy strategy = omni_get_free_strategy(ctx, "param");
    ASSERT(strategy == FREE_STRATEGY_NONE);  /* Borrowed - no free */

    omni_analysis_free(ctx);
}

TEST(test_free_strategy_names) {
    /* Verify strategy name strings */
    ASSERT(strcmp(omni_free_strategy_name(FREE_STRATEGY_NONE), "none") == 0);
    ASSERT(strcmp(omni_free_strategy_name(FREE_STRATEGY_UNIQUE), "unique") == 0);
    ASSERT(strcmp(omni_free_strategy_name(FREE_STRATEGY_TREE), "tree") == 0);
    ASSERT(strcmp(omni_free_strategy_name(FREE_STRATEGY_RC), "rc") == 0);
    ASSERT(strcmp(omni_free_strategy_name(FREE_STRATEGY_RC_TREE), "rc_tree") == 0);
}

TEST(test_codegen_emits_free_unique) {
    /* Test that codegen generates free_unique for unique locals */
    OmniValue* bindings = mk_cons(
        mk_list2(mk_sym("x"), mk_int(42)),
        omni_nil
    );
    OmniValue* expr = mk_list3(mk_sym("let"), bindings, mk_sym("x"));

    CodeGenContext* cg = omni_codegen_new_buffer();

    /* Set up analysis */
    cg->analysis = omni_analysis_new();
    omni_analyze_ownership(cg->analysis, expr);

    /* Generate code */
    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Check that free_unique is defined in the runtime */
    ASSERT(strstr(output, "free_unique") != NULL);

    /* Check that free_tree is defined */
    ASSERT(strstr(output, "free_tree") != NULL);

    free(output);
    omni_codegen_free(cg);
}

TEST(test_codegen_has_ownership_comments) {
    /* Test that generated code includes ownership strategy comments */
    OmniValue* bindings = mk_cons(
        mk_list2(mk_sym("x"), mk_int(1)),
        omni_nil
    );
    OmniValue* body = mk_list3(mk_sym("+"), mk_sym("x"), mk_int(1));
    OmniValue* expr = mk_list3(mk_sym("let"), bindings, body);

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();
    omni_analyze_ownership(cg->analysis, expr);

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Should have ownership-aware free functions */
    ASSERT(strstr(output, "static void free_unique") != NULL);
    ASSERT(strstr(output, "static void free_tree") != NULL);

    free(output);
    omni_codegen_free(cg);
}

TEST(test_shape_defaults_to_tree) {
    /* New local variables should default to tree shape */
    OmniValue* bindings = mk_cons(
        mk_list2(mk_sym("lst"), mk_list2(mk_sym("cons"), mk_int(1))),
        omni_nil
    );
    OmniValue* expr = mk_list3(mk_sym("let"), bindings, mk_sym("lst"));

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);

    OwnerInfo* o = omni_get_owner_info(ctx, "lst");
    ASSERT(o != NULL);
    ASSERT(o->shape == SHAPE_TREE);  /* Default for Lisp data */

    omni_analysis_free(ctx);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Ownership-Driven Codegen Tests ===\033[0m\n");

    printf("\n\033[33m--- Free Strategy Selection ---\033[0m\n");
    RUN_TEST(test_local_unique_strategy);
    RUN_TEST(test_param_borrowed_strategy);
    RUN_TEST(test_free_strategy_names);
    RUN_TEST(test_shape_defaults_to_tree);

    printf("\n\033[33m--- Code Generation ---\033[0m\n");
    RUN_TEST(test_codegen_emits_free_unique);
    RUN_TEST(test_codegen_has_ownership_comments);

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
