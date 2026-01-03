/*
 * Shape Analysis + Weak Back-Edge Routing Tests
 *
 * Tests for shape analysis (Tree/DAG/Cyclic) and back-edge detection.
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

/* ========== Back-Edge Pattern Tests ========== */

TEST(test_back_edge_patterns) {
    /* Test that common back-edge field names are recognized */
    ASSERT(omni_is_back_edge_pattern("parent") == true);
    ASSERT(omni_is_back_edge_pattern("prev") == true);
    ASSERT(omni_is_back_edge_pattern("previous") == true);
    ASSERT(omni_is_back_edge_pattern("back") == true);
    ASSERT(omni_is_back_edge_pattern("up") == true);
    ASSERT(omni_is_back_edge_pattern("owner") == true);

    /* Non-back-edge names */
    ASSERT(omni_is_back_edge_pattern("next") == false);
    ASSERT(omni_is_back_edge_pattern("child") == false);
    ASSERT(omni_is_back_edge_pattern("data") == false);
    ASSERT(omni_is_back_edge_pattern("value") == false);
}

TEST(test_back_edge_pattern_substrings) {
    /* Test that patterns work as substrings */
    ASSERT(omni_is_back_edge_pattern("parent_node") == true);
    ASSERT(omni_is_back_edge_pattern("my_parent") == true);
    ASSERT(omni_is_back_edge_pattern("prev_sibling") == true);
    ASSERT(omni_is_back_edge_pattern("backup") == true);  /* Contains "back" */
}

/* ========== Shape Analysis Tests ========== */

TEST(test_shape_tree_no_self_ref) {
    /* (defstruct Point (x Int) (y Int))
     * No self-references → Tree shape
     */
    OmniValue* type_def = mk_list4(
        mk_sym("defstruct"),
        mk_sym("Point"),
        mk_list2(mk_sym("x"), mk_sym("Int")),
        mk_list2(mk_sym("y"), mk_sym("Int"))
    );

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_shape(ctx, type_def);

    ShapeClass shape = omni_get_type_shape(ctx, "Point");
    ASSERT(shape == SHAPE_TREE);
    ASSERT(omni_is_cyclic_type(ctx, "Point") == false);

    omni_analysis_free(ctx);
}

TEST(test_shape_dag_self_ref) {
    /* (defstruct Node (data Int) (next Node))
     * Self-reference without back-edge pattern → DAG
     */
    OmniValue* type_def = mk_list4(
        mk_sym("defstruct"),
        mk_sym("Node"),
        mk_list2(mk_sym("data"), mk_sym("Int")),
        mk_list2(mk_sym("next"), mk_sym("Node"))
    );

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_shape(ctx, type_def);

    ShapeClass shape = omni_get_type_shape(ctx, "Node");
    ASSERT(shape == SHAPE_DAG);
    ASSERT(omni_is_cyclic_type(ctx, "Node") == false);

    size_t count;
    char** back_edges = omni_get_back_edge_fields(ctx, "Node", &count);
    ASSERT(count == 0);
    (void)back_edges;

    omni_analysis_free(ctx);
}

TEST(test_shape_cyclic_back_edge) {
    /* (defstruct DLNode (data Int) (next DLNode) (prev DLNode))
     * Self-reference with back-edge pattern "prev" → Cyclic with weak back-edge
     */
    OmniValue* type_def = mk_cons(
        mk_sym("defstruct"),
        mk_cons(mk_sym("DLNode"),
            mk_cons(mk_list2(mk_sym("data"), mk_sym("Int")),
                mk_cons(mk_list2(mk_sym("next"), mk_sym("DLNode")),
                    mk_cons(mk_list2(mk_sym("prev"), mk_sym("DLNode")), omni_nil)))));

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_shape(ctx, type_def);

    ShapeClass shape = omni_get_type_shape(ctx, "DLNode");
    ASSERT(shape == SHAPE_CYCLIC);
    ASSERT(omni_is_cyclic_type(ctx, "DLNode") == true);

    /* Check that prev is detected as back-edge */
    size_t count;
    char** back_edges = omni_get_back_edge_fields(ctx, "DLNode", &count);
    ASSERT(count >= 1);
    bool found_prev = false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(back_edges[i], "prev") == 0) found_prev = true;
    }
    ASSERT(found_prev);

    omni_analysis_free(ctx);
}

TEST(test_shape_tree_with_parent) {
    /* (defstruct TreeNode (data Int) (left TreeNode) (right TreeNode) (parent TreeNode))
     * Has "parent" field → detected as back-edge → Cyclic shape
     */
    OmniValue* type_def = mk_cons(
        mk_sym("defstruct"),
        mk_cons(mk_sym("TreeNode"),
            mk_cons(mk_list2(mk_sym("data"), mk_sym("Int")),
                mk_cons(mk_list2(mk_sym("left"), mk_sym("TreeNode")),
                    mk_cons(mk_list2(mk_sym("right"), mk_sym("TreeNode")),
                        mk_cons(mk_list2(mk_sym("parent"), mk_sym("TreeNode")), omni_nil))))));

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_shape(ctx, type_def);

    ShapeClass shape = omni_get_type_shape(ctx, "TreeNode");
    ASSERT(shape == SHAPE_CYCLIC);

    /* parent should be a back-edge */
    ASSERT(omni_is_back_edge_field(ctx, "TreeNode", "parent") == true);
    ASSERT(omni_is_back_edge_field(ctx, "TreeNode", "left") == false);
    ASSERT(omni_is_back_edge_field(ctx, "TreeNode", "right") == false);

    omni_analysis_free(ctx);
}

/* ========== Codegen Tests ========== */

TEST(test_codegen_has_weak_ref_support) {
    /* Test that generated code includes weak reference infrastructure */
    OmniValue* expr = mk_sym("x");

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Should have weak reference types and functions */
    ASSERT(strstr(output, "WeakRef") != NULL);
    ASSERT(strstr(output, "weak_ref_register") != NULL);
    ASSERT(strstr(output, "weak_refs_nullify") != NULL);
    ASSERT(strstr(output, "SET_WEAK") != NULL);
    ASSERT(strstr(output, "GET_WEAK") != NULL);

    free(output);
    omni_codegen_free(cg);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Shape Analysis + Weak Back-Edge Tests ===\033[0m\n");

    printf("\n\033[33m--- Back-Edge Pattern Detection ---\033[0m\n");
    RUN_TEST(test_back_edge_patterns);
    RUN_TEST(test_back_edge_pattern_substrings);

    printf("\n\033[33m--- Shape Analysis ---\033[0m\n");
    RUN_TEST(test_shape_tree_no_self_ref);
    RUN_TEST(test_shape_dag_self_ref);
    RUN_TEST(test_shape_cyclic_back_edge);
    RUN_TEST(test_shape_tree_with_parent);

    printf("\n\033[33m--- Code Generation ---\033[0m\n");
    RUN_TEST(test_codegen_has_weak_ref_support);

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
