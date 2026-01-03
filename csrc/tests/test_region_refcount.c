/*
 * Per-Region External Refcount Tests
 *
 * Tests for tracking external references to regions rather than
 * individual objects.
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

/* ========== External Refcount Tests ========== */

TEST(test_region_external_init) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Create a region */
    RegionInfo* r = omni_region_new(ctx, "test");
    ASSERT(r != NULL);
    ASSERT(r->external_refcount == 0);
    ASSERT(r->has_escaping_refs == false);

    omni_analysis_free(ctx);
}

TEST(test_region_inc_dec_external) {
    AnalysisContext* ctx = omni_analysis_new();

    RegionInfo* r = omni_region_new(ctx, "test");

    /* Increment external refcount */
    omni_region_inc_external(ctx, r->region_id);
    ASSERT(omni_region_get_external(ctx, r->region_id) == 1);

    omni_region_inc_external(ctx, r->region_id);
    ASSERT(omni_region_get_external(ctx, r->region_id) == 2);

    /* Decrement */
    omni_region_dec_external(ctx, r->region_id);
    ASSERT(omni_region_get_external(ctx, r->region_id) == 1);

    omni_region_dec_external(ctx, r->region_id);
    ASSERT(omni_region_get_external(ctx, r->region_id) == 0);

    omni_analysis_free(ctx);
}

TEST(test_region_can_bulk_free) {
    AnalysisContext* ctx = omni_analysis_new();

    RegionInfo* r = omni_region_new(ctx, "test");

    /* Fresh region can be bulk freed */
    ASSERT(omni_region_can_bulk_free(ctx, r->region_id) == true);

    /* With external ref, cannot bulk free */
    omni_region_inc_external(ctx, r->region_id);
    ASSERT(omni_region_can_bulk_free(ctx, r->region_id) == false);

    /* After dec, can bulk free again */
    omni_region_dec_external(ctx, r->region_id);
    ASSERT(omni_region_can_bulk_free(ctx, r->region_id) == true);

    omni_analysis_free(ctx);
}

TEST(test_region_escaping_refs) {
    AnalysisContext* ctx = omni_analysis_new();

    RegionInfo* r = omni_region_new(ctx, "test");

    /* Mark as having escaping refs */
    omni_region_mark_escaping(ctx, r->region_id);
    ASSERT(r->has_escaping_refs == true);

    /* Cannot bulk free if has escaping refs */
    ASSERT(omni_region_can_bulk_free(ctx, r->region_id) == false);

    omni_analysis_free(ctx);
}

TEST(test_cross_region_ref) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Create two regions with variables */
    omni_region_new(ctx, "r1");
    omni_region_add_var(ctx, "a");
    omni_region_end(ctx);

    omni_region_new(ctx, "r2");
    omni_region_add_var(ctx, "b");
    omni_region_end(ctx);

    /* a and b are in different regions */
    ASSERT(omni_is_cross_region_ref(ctx, "a", "b") == true);

    /* Same region should be false */
    omni_region_new(ctx, "r3");
    omni_region_add_var(ctx, "x");
    omni_region_add_var(ctx, "y");
    omni_region_end(ctx);

    ASSERT(omni_is_cross_region_ref(ctx, "x", "y") == false);

    omni_analysis_free(ctx);
}

TEST(test_get_region_by_id) {
    AnalysisContext* ctx = omni_analysis_new();

    RegionInfo* r1 = omni_region_new(ctx, "first");
    omni_region_end(ctx);

    RegionInfo* r2 = omni_region_new(ctx, "second");
    omni_region_end(ctx);

    /* Get by ID */
    RegionInfo* found1 = omni_get_region_by_id(ctx, r1->region_id);
    ASSERT(found1 == r1);

    RegionInfo* found2 = omni_get_region_by_id(ctx, r2->region_id);
    ASSERT(found2 == r2);

    /* Invalid ID returns NULL */
    RegionInfo* notfound = omni_get_region_by_id(ctx, 999);
    ASSERT(notfound == NULL);

    omni_analysis_free(ctx);
}

/* ========== Codegen Tests ========== */

TEST(test_codegen_has_region_refcount_macros) {
    /* Test that generated code includes region refcount infrastructure */
    OmniValue* expr = omni_new_int(42);

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Should have region structure */
    ASSERT(strstr(output, "typedef struct Region") != NULL);

    /* Should have region functions */
    ASSERT(strstr(output, "region_new") != NULL);
    ASSERT(strstr(output, "region_end") != NULL);

    /* Should have region macros */
    ASSERT(strstr(output, "REGION_INC_EXTERNAL") != NULL);
    ASSERT(strstr(output, "REGION_DEC_EXTERNAL") != NULL);
    ASSERT(strstr(output, "REGION_CAN_BULK_FREE") != NULL);

    free(output);
    omni_codegen_free(cg);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Per-Region External Refcount Tests ===\033[0m\n");

    printf("\n\033[33m--- External Refcount Management ---\033[0m\n");
    RUN_TEST(test_region_external_init);
    RUN_TEST(test_region_inc_dec_external);
    RUN_TEST(test_region_can_bulk_free);
    RUN_TEST(test_region_escaping_refs);
    RUN_TEST(test_cross_region_ref);
    RUN_TEST(test_get_region_by_id);

    printf("\n\033[33m--- Code Generation ---\033[0m\n");
    RUN_TEST(test_codegen_has_region_refcount_macros);

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
