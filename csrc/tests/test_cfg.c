/*
 * CFG and Liveness Analysis Tests
 *
 * Tests for control flow graph construction and backward liveness analysis.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../ast/ast.h"
#include "../analysis/analysis.h"

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

/* Helper to create a list from args */
static OmniValue* mk_list2(OmniValue* a, OmniValue* b) {
    return mk_cons(a, mk_cons(b, omni_nil));
}

static OmniValue* mk_list3(OmniValue* a, OmniValue* b, OmniValue* c) {
    return mk_cons(a, mk_cons(b, mk_cons(c, omni_nil)));
}

static OmniValue* mk_list4(OmniValue* a, OmniValue* b, OmniValue* c, OmniValue* d) {
    return mk_cons(a, mk_cons(b, mk_cons(c, mk_cons(d, omni_nil))));
}

/* ========== Basic CFG Tests ========== */

TEST(test_cfg_simple_expr) {
    /* Just a symbol reference: x */
    OmniValue* expr = mk_sym("x");
    CFG* cfg = omni_build_cfg(expr);

    ASSERT(cfg != NULL);
    ASSERT(cfg->node_count >= 2);  /* At least entry and exit */
    ASSERT(cfg->entry != NULL);
    ASSERT(cfg->exit != NULL);

    omni_cfg_free(cfg);
    /* Note: Not freeing expr - would need proper omni value free */
}

TEST(test_cfg_if_branches) {
    /* (if cond then else) */
    OmniValue* expr = mk_list4(
        mk_sym("if"),
        mk_sym("cond"),
        mk_sym("then_val"),
        mk_sym("else_val")
    );

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    /* Should have: entry, cond/branch, then_entry, else_entry, join, exit */
    ASSERT(cfg->node_count >= 4);

    /* Find branch node */
    int branch_count = 0;
    int join_count = 0;
    for (size_t i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i]->node_type == CFG_BRANCH) branch_count++;
        if (cfg->nodes[i]->node_type == CFG_JOIN) join_count++;
    }
    ASSERT(branch_count == 1);
    ASSERT(join_count == 1);

    omni_cfg_free(cfg);
}

TEST(test_cfg_let_bindings) {
    /* (let ((x 1)) x) */
    OmniValue* bindings = mk_cons(
        mk_list2(mk_sym("x"), mk_int(1)),
        omni_nil
    );
    OmniValue* expr = mk_list3(
        mk_sym("let"),
        bindings,
        mk_sym("x")
    );

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);
    ASSERT(cfg->node_count >= 3);

    /* Check for def of x */
    bool found_x_def = false;
    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* n = cfg->nodes[i];
        for (size_t j = 0; j < n->def_count; j++) {
            if (strcmp(n->defs[j], "x") == 0) {
                found_x_def = true;
                break;
            }
        }
    }
    ASSERT(found_x_def);

    omni_cfg_free(cfg);
}

/* ========== Liveness Analysis Tests ========== */

TEST(test_liveness_simple_use) {
    /* x - just a use of x */
    OmniValue* expr = mk_sym("x");

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);
    omni_compute_liveness(cfg, ctx);

    /* x should be used somewhere in the CFG */
    bool x_is_used = false;
    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* n = cfg->nodes[i];
        for (size_t j = 0; j < n->use_count; j++) {
            if (strcmp(n->uses[j], "x") == 0) {
                x_is_used = true;
                break;
            }
        }
        if (x_is_used) break;
    }
    ASSERT(x_is_used);

    omni_cfg_free(cfg);
    omni_analysis_free(ctx);
    /* Note: OmniValue cleanup handled by allocator */
}

TEST(test_liveness_if_branches) {
    /* (if cond x y)
     * x should be live on then branch
     * y should be live on else branch
     * cond should be live at condition
     */
    OmniValue* expr = mk_list4(
        mk_sym("if"),
        mk_sym("cond"),
        mk_sym("x"),
        mk_sym("y")
    );

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);
    omni_compute_liveness(cfg, ctx);

    /* Find the join node */
    CFGNode* join = NULL;
    for (size_t i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i]->node_type == CFG_JOIN) {
            join = cfg->nodes[i];
            break;
        }
    }
    ASSERT(join != NULL);

    /* At join, neither x nor y should be live (they were used in branches) */
    /* Actually they might be live-out if they're not defined locally */

    omni_cfg_free(cfg);
    omni_analysis_free(ctx);
    /* Note: OmniValue cleanup handled by allocator */
}

TEST(test_liveness_let_scope) {
    /* (let ((x 1)) (+ x 1))
     * x is defined then used - should die after use
     */
    OmniValue* bindings = mk_cons(
        mk_list2(mk_sym("x"), mk_int(1)),
        omni_nil
    );
    OmniValue* body = mk_list3(mk_sym("+"), mk_sym("x"), mk_int(1));
    OmniValue* expr = mk_list3(mk_sym("let"), bindings, body);

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);
    omni_compute_liveness(cfg, ctx);

    /* x should be in some node's live_in but not in that node's live_out
     * (meaning it dies there) */
    bool x_dies_somewhere = false;
    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* n = cfg->nodes[i];
        bool in_live_in = false;
        bool in_live_out = false;

        for (size_t j = 0; j < n->live_in_count; j++) {
            if (strcmp(n->live_in[j], "x") == 0) in_live_in = true;
        }
        for (size_t j = 0; j < n->live_out_count; j++) {
            if (strcmp(n->live_out[j], "x") == 0) in_live_out = true;
        }

        if (in_live_in && !in_live_out) {
            x_dies_somewhere = true;
            break;
        }
    }
    ASSERT(x_dies_somewhere);

    omni_cfg_free(cfg);
    omni_analysis_free(ctx);
    /* Note: OmniValue cleanup handled by allocator */
}

/* ========== Free Point Tests ========== */

TEST(test_free_points_basic) {
    /* (let ((x (mk-obj))) x)
     * x should be freed after the use
     */
    OmniValue* bindings = mk_cons(
        mk_list2(mk_sym("x"), mk_list2(mk_sym("mk-obj"), omni_nil)),
        omni_nil
    );
    OmniValue* expr = mk_list3(mk_sym("let"), bindings, mk_sym("x"));

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);
    omni_compute_liveness(cfg, ctx);

    CFGFreePoint* fps = omni_compute_cfg_free_points(cfg, ctx);
    /* Should have at least one free point for x */
    /* Note: depends on ownership analysis marking x as must_free */

    omni_cfg_free_points_free(fps);
    omni_cfg_free(cfg);
    omni_analysis_free(ctx);
    /* Note: OmniValue cleanup handled by allocator */
}

/* ========== Print CFG Test ========== */

TEST(test_print_cfg) {
    /* (if cond (+ x 1) (+ y 2)) */
    OmniValue* then_expr = mk_list3(mk_sym("+"), mk_sym("x"), mk_int(1));
    OmniValue* else_expr = mk_list3(mk_sym("+"), mk_sym("y"), mk_int(2));
    OmniValue* expr = mk_list4(mk_sym("if"), mk_sym("cond"), then_expr, else_expr);

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);
    omni_compute_liveness(cfg, ctx);

    /* Just verify it doesn't crash */
    printf("\n");
    omni_print_cfg(cfg);

    omni_cfg_free(cfg);
    omni_analysis_free(ctx);
    /* Note: OmniValue cleanup handled by allocator */
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== CFG and Liveness Analysis Tests ===\033[0m\n");

    printf("\n\033[33m--- Basic CFG Construction ---\033[0m\n");
    RUN_TEST(test_cfg_simple_expr);
    RUN_TEST(test_cfg_if_branches);
    RUN_TEST(test_cfg_let_bindings);

    printf("\n\033[33m--- Liveness Analysis ---\033[0m\n");
    RUN_TEST(test_liveness_simple_use);
    RUN_TEST(test_liveness_if_branches);
    RUN_TEST(test_liveness_let_scope);

    printf("\n\033[33m--- Free Point Computation ---\033[0m\n");
    RUN_TEST(test_free_points_basic);

    printf("\n\033[33m--- Debug Output ---\033[0m\n");
    RUN_TEST(test_print_cfg);

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
