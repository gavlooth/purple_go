/*
 * Interprocedural Summary Tests
 *
 * Tests that function ownership summaries are correctly inferred
 * and can be queried for interprocedural ASAP analysis.
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

/* ========== Ownership Name Tests ========== */

TEST(test_param_ownership_names) {
    ASSERT(strcmp(omni_param_ownership_name(PARAM_BORROWED), "borrowed") == 0);
    ASSERT(strcmp(omni_param_ownership_name(PARAM_CONSUMED), "consumed") == 0);
    ASSERT(strcmp(omni_param_ownership_name(PARAM_PASSTHROUGH), "passthrough") == 0);
    ASSERT(strcmp(omni_param_ownership_name(PARAM_CAPTURED), "captured") == 0);
}

TEST(test_return_ownership_names) {
    ASSERT(strcmp(omni_return_ownership_name(RETURN_FRESH), "fresh") == 0);
    ASSERT(strcmp(omni_return_ownership_name(RETURN_PASSTHROUGH), "passthrough") == 0);
    ASSERT(strcmp(omni_return_ownership_name(RETURN_BORROWED), "borrowed") == 0);
    ASSERT(strcmp(omni_return_ownership_name(RETURN_NONE), "none") == 0);
}

/* ========== Function Summary Analysis Tests ========== */

TEST(test_simple_function_define) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (define (identity x) x) */
    OmniValue* func = mk_list3(
        mk_sym("define"),
        mk_list2(mk_sym("identity"), mk_sym("x")),
        mk_sym("x")
    );

    omni_analyze_function_summary(ctx, func);

    FunctionSummary* summary = omni_get_function_summary(ctx, "identity");
    ASSERT(summary != NULL);
    ASSERT(strcmp(summary->name, "identity") == 0);
    ASSERT(summary->param_count == 1);
    ASSERT(summary->return_ownership == RETURN_PASSTHROUGH);
    ASSERT(summary->return_param_index == 0);

    omni_analysis_free(ctx);
}

TEST(test_function_returns_fresh) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (define (make-pair a b) (cons a b)) */
    OmniValue* func = mk_list3(
        mk_sym("define"),
        mk_list3(mk_sym("make-pair"), mk_sym("a"), mk_sym("b")),
        mk_list3(mk_sym("cons"), mk_sym("a"), mk_sym("b"))
    );

    omni_analyze_function_summary(ctx, func);

    FunctionSummary* summary = omni_get_function_summary(ctx, "make-pair");
    ASSERT(summary != NULL);
    ASSERT(summary->param_count == 2);
    ASSERT(summary->return_ownership == RETURN_FRESH);
    ASSERT(summary->allocates == true);

    omni_analysis_free(ctx);
}

TEST(test_function_with_side_effects) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (define (print-and-return x) (print x) x) */
    OmniValue* func = mk_list4(
        mk_sym("define"),
        mk_list2(mk_sym("print-and-return"), mk_sym("x")),
        mk_list2(mk_sym("print"), mk_sym("x")),
        mk_sym("x")
    );

    omni_analyze_function_summary(ctx, func);

    FunctionSummary* summary = omni_get_function_summary(ctx, "print-and-return");
    ASSERT(summary != NULL);
    ASSERT(summary->has_side_effects == true);

    omni_analysis_free(ctx);
}

TEST(test_param_ownership_query) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (define (identity x) x) - x is passthrough */
    OmniValue* func = mk_list3(
        mk_sym("define"),
        mk_list2(mk_sym("identity"), mk_sym("x")),
        mk_sym("x")
    );

    omni_analyze_function_summary(ctx, func);

    ParamOwnership ownership = omni_get_param_ownership(ctx, "identity", "x");
    ASSERT(ownership == PARAM_PASSTHROUGH);

    omni_analysis_free(ctx);
}

TEST(test_caller_should_free_arg) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (define (identity x) x) - passthrough means caller doesn't free */
    OmniValue* func = mk_list3(
        mk_sym("define"),
        mk_list2(mk_sym("id"), mk_sym("x")),
        mk_sym("x")
    );

    omni_analyze_function_summary(ctx, func);

    bool should_free = omni_caller_should_free_arg(ctx, "id", 0);
    ASSERT(should_free == false);  /* Passthrough - caller gets it back */

    omni_analysis_free(ctx);
}

TEST(test_function_consumes_param) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (define (consume-and-return x) (free x)) */
    OmniValue* func = mk_list3(
        mk_sym("define"),
        mk_list2(mk_sym("consume"), mk_sym("x")),
        mk_list2(mk_sym("free"), mk_sym("x"))
    );

    omni_analyze_function_summary(ctx, func);

    bool consumes = omni_function_consumes_param(ctx, "consume", "x");
    ASSERT(consumes == true);

    omni_analysis_free(ctx);
}

TEST(test_default_return_ownership) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Query non-existent function - should return default */
    ReturnOwnership ret = omni_get_return_ownership(ctx, "nonexistent");
    ASSERT(ret == RETURN_FRESH);  /* Default: fresh allocation */

    omni_analysis_free(ctx);
}

TEST(test_lambda_style_define) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (define add1 (lambda (x) (+ x 1))) */
    OmniValue* func = mk_list3(
        mk_sym("define"),
        mk_sym("add1"),
        mk_list3(
            mk_sym("lambda"),
            mk_cons(mk_sym("x"), omni_nil),
            mk_list3(mk_sym("+"), mk_sym("x"), omni_new_int(1))
        )
    );

    omni_analyze_function_summary(ctx, func);

    FunctionSummary* summary = omni_get_function_summary(ctx, "add1");
    ASSERT(summary != NULL);
    ASSERT(summary->param_count == 1);

    omni_analysis_free(ctx);
}

/* ========== Codegen Tests ========== */

TEST(test_codegen_has_interprocedural_macros) {
    OmniValue* expr = omni_new_int(42);

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Should have interprocedural macros */
    ASSERT(strstr(output, "PARAM_BORROWED") != NULL);
    ASSERT(strstr(output, "PARAM_CONSUMED") != NULL);
    ASSERT(strstr(output, "PARAM_PASSTHROUGH") != NULL);
    ASSERT(strstr(output, "PARAM_CAPTURED") != NULL);
    ASSERT(strstr(output, "RETURN_FRESH") != NULL);
    ASSERT(strstr(output, "RETURN_PASSTHROUGH") != NULL);
    ASSERT(strstr(output, "FUNC_SUMMARY") != NULL);
    ASSERT(strstr(output, "ASSERT_OWNED") != NULL);

    free(output);
    omni_codegen_free(cg);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Interprocedural Summary Tests ===\033[0m\n");

    printf("\n\033[33m--- Ownership Name Tests ---\033[0m\n");
    RUN_TEST(test_param_ownership_names);
    RUN_TEST(test_return_ownership_names);

    printf("\n\033[33m--- Function Summary Analysis ---\033[0m\n");
    RUN_TEST(test_simple_function_define);
    RUN_TEST(test_function_returns_fresh);
    RUN_TEST(test_function_with_side_effects);
    RUN_TEST(test_param_ownership_query);
    RUN_TEST(test_caller_should_free_arg);
    RUN_TEST(test_function_consumes_param);
    RUN_TEST(test_default_return_ownership);
    RUN_TEST(test_lambda_style_define);

    printf("\n\033[33m--- Code Generation ---\033[0m\n");
    RUN_TEST(test_codegen_has_interprocedural_macros);

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
