/*
 * OmniLisp Analysis Module
 *
 * Static analysis passes for ASAP memory management:
 * - Liveness analysis: determine when variables are last used
 * - Escape analysis: determine if values escape their scope
 * - Ownership analysis: track value ownership for safe deallocation
 * - Shape analysis: determine if data structures are Tree/DAG/Cyclic
 * - Reuse analysis: identify opportunities for in-place mutation
 */

#ifndef OMNILISP_ANALYSIS_H
#define OMNILISP_ANALYSIS_H

#include "../ast/ast.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Variable Usage ============== */

typedef enum {
    VAR_USAGE_NONE = 0,
    VAR_USAGE_READ = 1,
    VAR_USAGE_WRITE = 2,
    VAR_USAGE_CAPTURED = 4,    /* Captured by closure */
    VAR_USAGE_ESCAPED = 8,     /* Escapes current scope */
    VAR_USAGE_RETURNED = 16,   /* Returned from function */
} VarUsageFlags;

typedef struct VarUsage {
    char* name;
    int flags;
    int first_use;       /* Position of first use */
    int last_use;        /* Position of last use */
    int def_pos;         /* Position where defined */
    bool is_param;       /* Is this a function parameter */
    struct VarUsage* next;
} VarUsage;

/* ============== Escape Classification ============== */

typedef enum {
    ESCAPE_NONE = 0,     /* Value stays local, can stack-allocate */
    ESCAPE_ARG,          /* Escapes via function argument */
    ESCAPE_RETURN,       /* Escapes via return value */
    ESCAPE_CLOSURE,      /* Escapes via closure capture */
    ESCAPE_GLOBAL,       /* Escapes to global/module scope */
} EscapeClass;

typedef struct EscapeInfo {
    char* name;
    EscapeClass escape_class;
    bool is_unique;      /* Is this the only reference */
    struct EscapeInfo* next;
} EscapeInfo;

/* ============== Ownership ============== */

typedef enum {
    OWNER_LOCAL = 0,     /* Owned locally, must be freed */
    OWNER_BORROWED,      /* Borrowed reference, don't free */
    OWNER_TRANSFERRED,   /* Ownership transferred to callee */
    OWNER_SHARED,        /* Shared ownership (refcounted) */
} OwnershipKind;

typedef struct OwnerInfo {
    char* name;
    OwnershipKind ownership;
    bool must_free;      /* Must free when scope ends */
    int free_pos;        /* Position where free should occur */
    struct OwnerInfo* next;
} OwnerInfo;

/* ============== Shape Analysis ============== */

typedef enum {
    SHAPE_UNKNOWN = 0,
    SHAPE_SCALAR,        /* Simple value (int, char, etc.) */
    SHAPE_TREE,          /* Tree structure (no cycles) */
    SHAPE_DAG,           /* DAG (no cycles, shared refs) */
    SHAPE_CYCLIC,        /* Potentially cyclic */
} ShapeClass;

typedef struct ShapeInfo {
    char* type_name;
    ShapeClass shape;
    char** back_edge_fields;  /* Fields that form back-edges */
    size_t back_edge_count;
    struct ShapeInfo* next;
} ShapeInfo;

/* ============== Reuse Analysis ============== */

typedef struct ReuseCandidate {
    int alloc_pos;       /* Position of allocation */
    int free_pos;        /* Position of corresponding free */
    char* type_name;     /* Type being allocated */
    size_t size;         /* Size of allocation */
    bool can_reuse;      /* Can be reused for subsequent alloc */
    struct ReuseCandidate* next;
} ReuseCandidate;

/* ============== Analysis Context ============== */

typedef struct AnalysisContext {
    /* Variable usage info */
    VarUsage* var_usages;

    /* Escape info */
    EscapeInfo* escape_info;

    /* Ownership info */
    OwnerInfo* owner_info;

    /* Shape info */
    ShapeInfo* shape_info;

    /* Reuse candidates */
    ReuseCandidate* reuse_candidates;

    /* Current position counter */
    int position;

    /* Function being analyzed */
    OmniValue* current_function;

    /* Analysis flags */
    bool in_lambda;
    bool in_return_position;
    int scope_depth;
} AnalysisContext;

/* ============== Analysis API ============== */

/* Create a new analysis context */
AnalysisContext* omni_analysis_new(void);

/* Free analysis context */
void omni_analysis_free(AnalysisContext* ctx);

/* Run all analyses on an expression */
void omni_analyze(AnalysisContext* ctx, OmniValue* expr);

/* Run all analyses on a program (list of expressions) */
void omni_analyze_program(AnalysisContext* ctx, OmniValue** exprs, size_t count);

/* ============== Individual Analysis Passes ============== */

/* Liveness analysis - compute last-use positions */
void omni_analyze_liveness(AnalysisContext* ctx, OmniValue* expr);

/* Escape analysis - compute escape classifications */
void omni_analyze_escape(AnalysisContext* ctx, OmniValue* expr);

/* Ownership analysis - compute ownership and free points */
void omni_analyze_ownership(AnalysisContext* ctx, OmniValue* expr);

/* Shape analysis - compute data structure shapes */
void omni_analyze_shape(AnalysisContext* ctx, OmniValue* type_def);

/* Reuse analysis - find reuse opportunities */
void omni_analyze_reuse(AnalysisContext* ctx, OmniValue* expr);

/* ============== Query Functions ============== */

/* Get variable usage info */
VarUsage* omni_get_var_usage(AnalysisContext* ctx, const char* name);

/* Get escape classification for a variable */
EscapeClass omni_get_escape_class(AnalysisContext* ctx, const char* name);

/* Get ownership info for a variable */
OwnerInfo* omni_get_owner_info(AnalysisContext* ctx, const char* name);

/* Check if a variable should be freed at given position */
bool omni_should_free_at(AnalysisContext* ctx, const char* name, int position);

/* Get all variables that should be freed at given position */
char** omni_get_frees_at(AnalysisContext* ctx, int position, size_t* out_count);

/* Check if a type has cyclic references */
bool omni_is_cyclic_type(AnalysisContext* ctx, const char* type_name);

/* Get back-edge fields for a type */
char** omni_get_back_edge_fields(AnalysisContext* ctx, const char* type_name, size_t* out_count);

/* ============== ASAP Free Injection ============== */

typedef struct FreePoint {
    int position;
    char** vars;
    size_t var_count;
    struct FreePoint* next;
} FreePoint;

/* Compute all free injection points for a function */
FreePoint* omni_compute_free_points(AnalysisContext* ctx, OmniValue* func);

/* Free the free points list */
void omni_free_points_free(FreePoint* points);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_ANALYSIS_H */
