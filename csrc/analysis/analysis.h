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

/* ============== Shape Analysis (forward for OwnerInfo) ============== */

typedef enum {
    SHAPE_UNKNOWN = 0,
    SHAPE_SCALAR,        /* Simple value (int, char, etc.) */
    SHAPE_TREE,          /* Tree structure (no cycles) */
    SHAPE_DAG,           /* DAG (no cycles, shared refs) */
    SHAPE_CYCLIC,        /* Potentially cyclic */
} ShapeClass;

/* ============== Ownership ============== */

typedef enum {
    OWNER_LOCAL = 0,     /* Owned locally, must be freed */
    OWNER_BORROWED,      /* Borrowed reference, don't free */
    OWNER_TRANSFERRED,   /* Ownership transferred to callee */
    OWNER_SHARED,        /* Shared ownership (refcounted) */
} OwnershipKind;

/* Free strategy - determined by ownership + shape analysis */
typedef enum {
    FREE_STRATEGY_NONE = 0,      /* Don't free (borrowed/transferred) */
    FREE_STRATEGY_UNIQUE,        /* free_unique: known single ref, no RC check */
    FREE_STRATEGY_TREE,          /* free_tree: tree-shaped, recursive free */
    FREE_STRATEGY_RC,            /* dec_ref: shared/DAG, RC decrement */
    FREE_STRATEGY_RC_TREE,       /* dec_ref with recursive free on 0 */
} FreeStrategy;

/* Allocation strategy - determined by escape analysis */
typedef enum {
    ALLOC_HEAP = 0,              /* malloc - value may escape */
    ALLOC_STACK,                 /* alloca or local struct - value stays local */
    ALLOC_POOL,                  /* Pool allocation - short-lived, group free */
    ALLOC_ARENA,                 /* Arena allocation - bulk free at scope end */
} AllocStrategy;

typedef struct OwnerInfo {
    char* name;
    OwnershipKind ownership;
    bool must_free;      /* Must free when scope ends */
    int free_pos;        /* Position where free should occur */
    bool is_unique;      /* Known to be the only reference */
    ShapeClass shape;    /* Shape of the data structure */
    AllocStrategy alloc_strategy;  /* Where to allocate */
    struct OwnerInfo* next;
} OwnerInfo;

/* ============== Shape Analysis (continued) ============== */

typedef struct ShapeInfo {
    char* type_name;
    ShapeClass shape;
    char** back_edge_fields;  /* Fields that form back-edges */
    size_t back_edge_count;
    struct ShapeInfo* next;
} ShapeInfo;

/* ============== Reuse Analysis ============== */

typedef struct ReuseCandidate {
    int alloc_pos;       /* Position of new allocation */
    int free_pos;        /* Position of corresponding free */
    char* freed_var;     /* Name of variable being freed */
    char* type_name;     /* Type being allocated */
    size_t size;         /* Size of allocation */
    bool can_reuse;      /* Can be reused for subsequent alloc */
    bool is_consumed;    /* Has this reuse opportunity been used */
    struct ReuseCandidate* next;
} ReuseCandidate;

/* ============== Region Analysis ============== */

typedef struct RegionInfo {
    int region_id;           /* Unique region identifier */
    char* name;              /* Optional region name */
    int scope_depth;         /* Nesting level of this region */
    int start_pos;           /* First position in region */
    int end_pos;             /* Last position in region */
    char** variables;        /* Variables allocated in this region */
    size_t var_count;
    size_t var_capacity;
    int external_refcount;   /* Count of refs from outside this region */
    bool has_escaping_refs;  /* True if any ref escapes to outer scope */
    struct RegionInfo* parent;   /* Enclosing region */
    struct RegionInfo* next;
} RegionInfo;

/* ============== RC Elision Analysis ============== */

typedef enum {
    RC_REQUIRED = 0,         /* Must use inc_ref/dec_ref */
    RC_ELIDE_INC,            /* Can skip inc_ref only */
    RC_ELIDE_DEC,            /* Can skip dec_ref only */
    RC_ELIDE_BOTH,           /* Can skip both inc and dec */
} RCElisionClass;

typedef struct RCElisionInfo {
    char* var_name;
    RCElisionClass elision;
    int region_id;           /* Which region this var belongs to */
    bool same_region_refs;   /* All refs are within same region */
    struct RCElisionInfo* next;
} RCElisionInfo;

/* ============== Borrow/Tether Analysis ============== */

typedef enum {
    BORROW_NONE = 0,
    BORROW_SHARED,           /* Multiple readers, no writers */
    BORROW_EXCLUSIVE,        /* Single reader/writer */
    BORROW_LOOP,             /* Borrowed for loop iteration */
} BorrowKind;

typedef struct BorrowInfo {
    char* borrowed_var;      /* Variable being borrowed */
    char* borrow_holder;     /* Who holds the borrow (loop var, closure, etc.) */
    BorrowKind kind;
    int start_pos;           /* Where borrow starts */
    int end_pos;             /* Where borrow ends */
    bool needs_tether;       /* Must keep alive during borrow */
    struct BorrowInfo* next;
} BorrowInfo;

typedef struct TetherPoint {
    int position;            /* Program position */
    char* tethered_var;      /* Variable being kept alive */
    bool is_entry;           /* true = tether start, false = tether end */
    struct TetherPoint* next;
} TetherPoint;

/* ============== Interprocedural Summaries ============== */

typedef enum {
    PARAM_BORROWED = 0,      /* Parameter is borrowed, caller keeps ownership */
    PARAM_CONSUMED,          /* Parameter is consumed, callee frees it */
    PARAM_PASSTHROUGH,       /* Parameter passes through to return value */
    PARAM_CAPTURED,          /* Parameter is captured in closure/data structure */
} ParamOwnership;

typedef enum {
    RETURN_FRESH = 0,        /* Returns freshly allocated value */
    RETURN_PASSTHROUGH,      /* Returns one of the parameters */
    RETURN_BORROWED,         /* Returns borrowed reference (don't free) */
    RETURN_NONE,             /* Returns nil/void */
} ReturnOwnership;

typedef struct ParamSummary {
    char* name;
    ParamOwnership ownership;
    int passthrough_index;   /* If PARAM_PASSTHROUGH, which param passes through */
    struct ParamSummary* next;
} ParamSummary;

typedef struct FunctionSummary {
    char* name;              /* Function name */
    ParamSummary* params;    /* Parameter summaries */
    size_t param_count;
    ReturnOwnership return_ownership;
    int return_param_index;  /* If RETURN_PASSTHROUGH, which param is returned */
    bool allocates;          /* Does this function allocate? */
    bool has_side_effects;   /* Does this function have side effects? */
    struct FunctionSummary* next;
} FunctionSummary;

/* Forward declarations for concurrency types */
typedef struct ThreadLocalityInfo ThreadLocalityInfo;
typedef struct ThreadSpawnInfo ThreadSpawnInfo;
typedef struct ChannelOpInfo ChannelOpInfo;

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

    /* Region info */
    RegionInfo* regions;
    int next_region_id;
    RegionInfo* current_region;

    /* RC elision info */
    RCElisionInfo* rc_elision;

    /* Borrow tracking */
    BorrowInfo* borrows;

    /* Tether points */
    TetherPoint* tethers;

    /* Function summaries */
    FunctionSummary* function_summaries;

    /* Concurrency tracking */
    ThreadLocalityInfo* thread_locality;
    ThreadSpawnInfo* thread_spawns;
    ChannelOpInfo* channel_ops;
    int current_thread_id;   /* -1 = main thread, >= 0 = spawned */

    /* Current position counter */
    int position;

    /* Function being analyzed */
    OmniValue* current_function;

    /* Analysis flags */
    bool in_lambda;
    bool in_return_position;
    bool in_loop;
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

/* Check if a specific field is a back-edge (should be weak reference) */
bool omni_is_back_edge_field(AnalysisContext* ctx, const char* type_name, const char* field_name);

/* Get shape classification for a type */
ShapeClass omni_get_type_shape(AnalysisContext* ctx, const char* type_name);

/* Check if a field name looks like a back-edge by naming convention */
bool omni_is_back_edge_pattern(const char* field_name);

/* Get the free strategy for a variable (combines ownership + shape) */
FreeStrategy omni_get_free_strategy(AnalysisContext* ctx, const char* name);

/* Get free strategy name string for codegen comments */
const char* omni_free_strategy_name(FreeStrategy strategy);

/* Get allocation strategy for a variable (based on escape analysis) */
AllocStrategy omni_get_alloc_strategy(AnalysisContext* ctx, const char* name);

/* Get allocation strategy name string for codegen comments */
const char* omni_alloc_strategy_name(AllocStrategy strategy);

/* Check if a variable can be stack allocated */
bool omni_can_stack_alloc(AnalysisContext* ctx, const char* name);

/* ============== Reuse Analysis Query Functions ============== */

/* Add a reuse candidate (allocation paired with prior free) */
void omni_add_reuse_candidate(AnalysisContext* ctx, const char* freed_var,
                              const char* alloc_type, int alloc_pos);

/* Get reuse candidate for an allocation position */
ReuseCandidate* omni_get_reuse_at(AnalysisContext* ctx, int alloc_pos);

/* Check if a freed variable's memory can be reused for a new type */
bool omni_can_reuse_for(AnalysisContext* ctx, const char* freed_var,
                        const char* new_type);

/* Get the size of a type in bytes (for reuse matching) */
size_t omni_type_size(const char* type_name);

/* ============== Region Analysis Functions ============== */

/* Create a new region */
RegionInfo* omni_region_new(AnalysisContext* ctx, const char* name);

/* End current region and return to parent */
void omni_region_end(AnalysisContext* ctx);

/* Add a variable to the current region */
void omni_region_add_var(AnalysisContext* ctx, const char* var_name);

/* Get the region a variable belongs to */
RegionInfo* omni_get_var_region(AnalysisContext* ctx, const char* var_name);

/* Check if two variables are in the same region */
bool omni_same_region(AnalysisContext* ctx, const char* var1, const char* var2);

/* ============== Per-Region External Refcount Functions ============== */

/* Increment external refcount for a region */
void omni_region_inc_external(AnalysisContext* ctx, int region_id);

/* Decrement external refcount for a region */
void omni_region_dec_external(AnalysisContext* ctx, int region_id);

/* Get the external refcount for a region */
int omni_region_get_external(AnalysisContext* ctx, int region_id);

/* Check if a reference crosses region boundaries */
bool omni_is_cross_region_ref(AnalysisContext* ctx, const char* src_var, const char* dst_var);

/* Mark a region as having escaping references */
void omni_region_mark_escaping(AnalysisContext* ctx, int region_id);

/* Check if a region can be bulk-freed (no external refs) */
bool omni_region_can_bulk_free(AnalysisContext* ctx, int region_id);

/* Get region by ID */
RegionInfo* omni_get_region_by_id(AnalysisContext* ctx, int region_id);

/* ============== RC Elision Functions ============== */

/* Analyze RC elision opportunities for an expression */
void omni_analyze_rc_elision(AnalysisContext* ctx, OmniValue* expr);

/* Get RC elision class for a variable */
RCElisionClass omni_get_rc_elision(AnalysisContext* ctx, const char* var_name);

/* Get RC elision class name for debugging */
const char* omni_rc_elision_name(RCElisionClass elision);

/* Check if inc_ref can be elided for a variable */
bool omni_can_elide_inc_ref(AnalysisContext* ctx, const char* var_name);

/* Check if dec_ref can be elided for a variable */
bool omni_can_elide_dec_ref(AnalysisContext* ctx, const char* var_name);

/* ============== Borrow/Tether Functions ============== */

/* Analyze borrow patterns in an expression */
void omni_analyze_borrows(AnalysisContext* ctx, OmniValue* expr);

/* Start a borrow (e.g., beginning of loop over collection) */
void omni_borrow_start(AnalysisContext* ctx, const char* borrowed_var,
                       const char* holder, BorrowKind kind);

/* End a borrow */
void omni_borrow_end(AnalysisContext* ctx, const char* borrowed_var);

/* Check if a variable is currently borrowed */
bool omni_is_borrowed(AnalysisContext* ctx, const char* var_name);

/* Get the borrow info for a variable */
BorrowInfo* omni_get_borrow_info(AnalysisContext* ctx, const char* var_name);

/* Add a tether point (keep-alive) */
void omni_add_tether(AnalysisContext* ctx, const char* var_name, bool is_entry);

/* Get tether points at a position */
TetherPoint** omni_get_tethers_at(AnalysisContext* ctx, int position, size_t* count);

/* Check if a variable needs tethering at a position */
bool omni_needs_tether(AnalysisContext* ctx, const char* var_name, int position);

/* Get borrow kind name for debugging */
const char* omni_borrow_kind_name(BorrowKind kind);

/* ============== Interprocedural Summary Functions ============== */

/* Analyze a function definition and create its summary */
void omni_analyze_function_summary(AnalysisContext* ctx, OmniValue* func_def);

/* Get the summary for a function by name */
FunctionSummary* omni_get_function_summary(AnalysisContext* ctx, const char* func_name);

/* Get parameter ownership for a function */
ParamOwnership omni_get_param_ownership(AnalysisContext* ctx, const char* func_name,
                                        const char* param_name);

/* Get return ownership for a function */
ReturnOwnership omni_get_return_ownership(AnalysisContext* ctx, const char* func_name);

/* Check if a function consumes a parameter */
bool omni_function_consumes_param(AnalysisContext* ctx, const char* func_name,
                                  const char* param_name);

/* Check if caller should free after call */
bool omni_caller_should_free_arg(AnalysisContext* ctx, const char* func_name,
                                 int arg_index);

/* Get param ownership name for debugging */
const char* omni_param_ownership_name(ParamOwnership ownership);

/* Get return ownership name for debugging */
const char* omni_return_ownership_name(ReturnOwnership ownership);

/* ============== Control Flow Graph ============== */

typedef struct CFGNode {
    int id;
    int position_start;      /* First position in this basic block */
    int position_end;        /* Last position in this basic block */

    /* Control flow edges */
    struct CFGNode** successors;
    size_t succ_count;
    size_t succ_capacity;
    struct CFGNode** predecessors;
    size_t pred_count;
    size_t pred_capacity;

    /* Variables used/defined in this node */
    char** uses;             /* Variables read in this block */
    size_t use_count;
    char** defs;             /* Variables defined in this block */
    size_t def_count;

    /* Liveness sets (computed by dataflow) */
    char** live_in;          /* Live at entry to this node */
    size_t live_in_count;
    char** live_out;         /* Live at exit from this node */
    size_t live_out_count;

    /* Node type for structured control flow */
    enum {
        CFG_BASIC,           /* Basic block */
        CFG_BRANCH,          /* If condition */
        CFG_JOIN,            /* Merge point after if */
        CFG_LOOP_HEAD,       /* Loop header */
        CFG_LOOP_EXIT,       /* Loop exit point */
        CFG_ENTRY,           /* Function entry */
        CFG_EXIT,            /* Function exit */
    } node_type;
} CFGNode;

typedef struct CFG {
    CFGNode** nodes;
    size_t node_count;
    size_t node_capacity;
    CFGNode* entry;
    CFGNode* exit;
} CFG;

/* Build CFG from expression */
CFG* omni_build_cfg(OmniValue* expr);

/* Free CFG */
void omni_cfg_free(CFG* cfg);

/* Compute liveness using backward dataflow */
void omni_compute_liveness(CFG* cfg, AnalysisContext* ctx);

/* Get variables that should be freed at end of a CFG node */
char** omni_get_frees_for_node(CFG* cfg, CFGNode* node,
                                AnalysisContext* ctx, size_t* out_count);

/* Print CFG for debugging */
void omni_print_cfg(CFG* cfg);

/* ============== CFG-Based Free Points ============== */

typedef struct CFGFreePoint {
    CFGNode* node;           /* The CFG node */
    char** vars;             /* Variables to free after this node */
    size_t var_count;
    struct CFGFreePoint* next;
} CFGFreePoint;

/* Compute CFG-aware free points */
CFGFreePoint* omni_compute_cfg_free_points(CFG* cfg, AnalysisContext* ctx);

/* Free CFG free points list */
void omni_cfg_free_points_free(CFGFreePoint* points);

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

/* ============== Concurrency Ownership Inference ============== */

/* Thread locality classification */
typedef enum {
    THREAD_LOCAL = 0,        /* Data stays in one thread */
    THREAD_SHARED,           /* Data accessed by multiple threads */
    THREAD_TRANSFER,         /* Data transferred via message passing */
    THREAD_IMMUTABLE,        /* Immutable data - can be freely shared */
} ThreadLocality;

/* Channel operation type */
typedef enum {
    CHAN_SEND = 0,           /* Send ownership to channel */
    CHAN_RECV,               /* Receive ownership from channel */
    CHAN_CLOSE,              /* Close channel */
} ChannelOp;

/* Thread spawn info */
typedef struct ThreadSpawnInfo {
    int spawn_pos;           /* Position of spawn */
    char* thread_id;         /* Thread identifier */
    char** captured_vars;    /* Variables captured by thread */
    size_t captured_count;
    ThreadLocality* capture_locality;  /* Locality of each capture */
    struct ThreadSpawnInfo* next;
} ThreadSpawnInfo;

/* Channel operation tracking */
typedef struct ChannelOpInfo {
    int position;            /* Position of operation */
    ChannelOp op;            /* Type of operation */
    char* channel_name;      /* Name of channel variable */
    char* value_var;         /* Variable being sent/received */
    bool transfers_ownership; /* Does this transfer ownership? */
    struct ChannelOpInfo* next;
} ChannelOpInfo;

/* Thread locality info for variables */
typedef struct ThreadLocalityInfo {
    char* var_name;
    ThreadLocality locality;
    int thread_id;           /* -1 for shared, >= 0 for specific thread */
    bool needs_atomic_rc;    /* True if needs atomic refcount operations */
    bool is_message;         /* True if sent via channel */
    struct ThreadLocalityInfo* next;
} ThreadLocalityInfo;

/* ============== Concurrency Analysis Functions ============== */

/* Analyze concurrency patterns in an expression */
void omni_analyze_concurrency(AnalysisContext* ctx, OmniValue* expr);

/* Get thread locality for a variable */
ThreadLocality omni_get_thread_locality(AnalysisContext* ctx, const char* var_name);

/* Check if a variable needs atomic refcount operations */
bool omni_needs_atomic_rc(AnalysisContext* ctx, const char* var_name);

/* Check if a variable is sent via channel (ownership transfer) */
bool omni_is_channel_transferred(AnalysisContext* ctx, const char* var_name);

/* Mark a variable as thread-local */
void omni_mark_thread_local(AnalysisContext* ctx, const char* var_name, int thread_id);

/* Mark a variable as shared between threads */
void omni_mark_thread_shared(AnalysisContext* ctx, const char* var_name);

/* Record a channel send operation */
void omni_record_channel_send(AnalysisContext* ctx, const char* channel,
                              const char* value_var, bool transfers_ownership);

/* Record a channel receive operation */
void omni_record_channel_recv(AnalysisContext* ctx, const char* channel,
                              const char* value_var);

/* Record a thread spawn */
void omni_record_thread_spawn(AnalysisContext* ctx, const char* thread_id,
                              char** captured_vars, size_t count);

/* Get thread locality name for debugging */
const char* omni_thread_locality_name(ThreadLocality locality);

/* Get channel op name for debugging */
const char* omni_channel_op_name(ChannelOp op);

/* Check if caller should free after send (usually false - ownership transfers) */
bool omni_should_free_after_send(AnalysisContext* ctx, const char* channel,
                                 const char* var_name);

/* Get the spawned threads that capture a variable */
ThreadSpawnInfo** omni_get_threads_capturing(AnalysisContext* ctx, const char* var_name,
                                             size_t* count);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_ANALYSIS_H */
