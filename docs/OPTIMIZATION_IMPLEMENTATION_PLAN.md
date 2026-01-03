# Detailed Optimization Implementation Plan

This document provides step-by-step implementation instructions for each of the
11 remaining optimizations from `UNIFIED_OPTIMIZATION_PLAN.md`.

**Current State Summary:**
- Analysis module (`csrc/analysis/`) has infrastructure for VarUsage, EscapeInfo, OwnerInfo, ShapeInfo, ReuseCandidate
- Codegen (`csrc/codegen/`) emits basic C with `free_obj` at scope end, but doesn't use analysis data fully
- Runtime (`runtime/src/runtime.c`) has Arena, Region, SCC, Symmetric RC, BorrowRef implementations
- Compiler driver works end-to-end but optimizations are not wired in

---

## 1. Escape-Aware Stack Allocation

**Goal:** Route local-only allocations to stack/pool instead of heap.

### 1.1 Analysis Changes (csrc/analysis/analysis.c)

**File:** `csrc/analysis/analysis.c`

```c
/* Add to analyze_expr for allocation tracking */
typedef struct AllocSite {
    int position;
    char* var_name;
    char* alloc_type;        /* "int", "pair", "lambda", etc. */
    size_t estimated_size;
    EscapeClass escape;
    bool can_stack_alloc;
    struct AllocSite* next;
} AllocSite;

/* Add to AnalysisContext */
AllocSite* alloc_sites;

/* New function to register allocation */
static void register_alloc_site(AnalysisContext* ctx, const char* var_name,
                                 const char* type, size_t size) {
    AllocSite* site = malloc(sizeof(AllocSite));
    site->position = ctx->position;
    site->var_name = strdup(var_name);
    site->alloc_type = strdup(type);
    site->estimated_size = size;
    site->escape = ESCAPE_NONE;  /* Will be updated by escape analysis */
    site->can_stack_alloc = false;
    site->next = ctx->alloc_sites;
    ctx->alloc_sites = site;
}

/* Post-analysis pass to determine stack eligibility */
void omni_compute_stack_eligibility(AnalysisContext* ctx) {
    for (AllocSite* site = ctx->alloc_sites; site; site = site->next) {
        EscapeClass ec = omni_get_escape_class(ctx, site->var_name);
        site->escape = ec;

        /* Can stack allocate if:
         * 1. Does not escape (ESCAPE_NONE)
         * 2. Not captured by closure
         * 3. Size is reasonable (<= 256 bytes typical)
         */
        VarUsage* u = omni_get_var_usage(ctx, site->var_name);
        site->can_stack_alloc = (ec == ESCAPE_NONE) &&
                                 !(u && (u->flags & VAR_USAGE_CAPTURED)) &&
                                 (site->estimated_size <= 256);
    }
}

/* Query function */
bool omni_can_stack_alloc(AnalysisContext* ctx, const char* var_name) {
    for (AllocSite* site = ctx->alloc_sites; site; site = site->next) {
        if (strcmp(site->var_name, var_name) == 0) {
            return site->can_stack_alloc;
        }
    }
    return false;
}
```

### 1.2 Runtime Additions (runtime/src/runtime.c)

```c
/* Stack allocation macros - use alloca or compound literals */
#define STACK_OBJ_SIZE (sizeof(Obj) + 16)  /* Padding for alignment */

/* Stack-allocated object marker */
#define MARK_STACK -99

static inline Obj* mk_int_stack(int64_t i) {
    /* Use compound literal for "stack" allocation
     * This is actually static duration but serves the purpose */
    static __thread Obj stack_objs[64];
    static __thread int stack_idx = 0;

    Obj* x = &stack_objs[stack_idx++ & 63];
    x->generation = _next_generation();
    x->mark = MARK_STACK;
    x->tag = TAG_INT;
    x->is_pair = 0;
    x->scc_id = -1;
    x->i = i;
    return x;
}

static inline Obj* mk_pair_stack(Obj* a, Obj* b) {
    static __thread Obj stack_objs[64];
    static __thread int stack_idx = 0;

    Obj* x = &stack_objs[stack_idx++ & 63];
    x->generation = _next_generation();
    x->mark = MARK_STACK;
    x->tag = TAG_PAIR;
    x->is_pair = 1;
    x->scc_id = -1;
    x->a = a;
    x->b = b;
    return x;
}

/* Modified free_obj to skip stack-allocated */
static inline void free_obj_safe(Obj* x) {
    if (!x) return;
    if (x->mark == MARK_STACK) return;  /* Stack allocated - no free */
    free_obj(x);
}
```

### 1.3 Codegen Changes (csrc/codegen/codegen.c)

```c
/* Modify allocation emission */
static void codegen_alloc(CodeGenContext* ctx, const char* var_name,
                          const char* type, OmniValue* init_expr) {
    bool use_stack = false;

    if (ctx->analysis) {
        use_stack = omni_can_stack_alloc(ctx->analysis, var_name);
    }

    char* c_name = omni_codegen_mangle(var_name);

    if (use_stack && strcmp(type, "int") == 0) {
        omni_codegen_emit(ctx, "Obj* %s = mk_int_stack(", c_name);
        codegen_expr(ctx, init_expr);
        omni_codegen_emit_raw(ctx, ");\n");
    } else if (use_stack && strcmp(type, "pair") == 0) {
        /* Similar for pairs */
        omni_codegen_emit(ctx, "Obj* %s = mk_pair_stack(", c_name);
        /* ... emit car, cdr ... */
        omni_codegen_emit_raw(ctx, ");\n");
    } else {
        /* Default heap allocation */
        omni_codegen_emit(ctx, "Obj* %s = ", c_name);
        codegen_expr(ctx, init_expr);
        omni_codegen_emit_raw(ctx, ";\n");
    }

    register_symbol(ctx, var_name, c_name);
    free(c_name);
}
```

### 1.4 Testing

Add tests in `runtime/tests/test_stack_alloc.c`:
- Test that ESCAPE_NONE variables use stack allocation
- Test that ESCAPE_ARG/RETURN/CLOSURE use heap
- Test that stack objects are not freed
- Verify no memory leaks with Valgrind

### 1.5 Verification Checklist
- [ ] AllocSite tracking added to analysis
- [ ] Stack eligibility computed after escape analysis
- [ ] mk_*_stack functions added to runtime
- [ ] free_obj_safe skips stack-marked objects
- [ ] Codegen routes to stack alloc when eligible
- [ ] Tests pass under Valgrind

---

## 2. Full Liveness-Driven Free Insertion

**Goal:** Free at last-use point, not scope end.

### 2.1 Analysis Enhancement

**Current State:** `omni_analyze_ownership` sets `free_pos = u->last_use` but this
is only the position number, not control-flow aware.

**Required Changes:**

```c
/* Add to analysis.h */
typedef struct CFGNode {
    int id;
    int position_start;
    int position_end;
    struct CFGNode** successors;
    size_t succ_count;
    struct CFGNode** predecessors;
    size_t pred_count;

    /* Live variables at entry/exit */
    char** live_in;
    size_t live_in_count;
    char** live_out;
    size_t live_out_count;
} CFGNode;

typedef struct CFG {
    CFGNode** nodes;
    size_t node_count;
    CFGNode* entry;
    CFGNode* exit;
} CFG;

/* Build CFG from expression */
CFG* omni_build_cfg(OmniValue* expr);

/* Compute liveness using dataflow */
void omni_compute_liveness(CFG* cfg, AnalysisContext* ctx);

/* Get free points for a specific CFG node */
char** omni_get_frees_for_node(CFG* cfg, CFGNode* node, size_t* out_count);
```

**Implementation:**

```c
/* In analysis.c */

static CFGNode* cfg_node_new(int id) {
    CFGNode* n = calloc(1, sizeof(CFGNode));
    n->id = id;
    return n;
}

static void cfg_add_edge(CFGNode* from, CFGNode* to) {
    from->successors = realloc(from->successors,
                               (from->succ_count + 1) * sizeof(CFGNode*));
    from->successors[from->succ_count++] = to;

    to->predecessors = realloc(to->predecessors,
                               (to->pred_count + 1) * sizeof(CFGNode*));
    to->predecessors[to->pred_count++] = from;
}

CFG* omni_build_cfg(OmniValue* expr) {
    CFG* cfg = calloc(1, sizeof(CFG));
    cfg->entry = cfg_node_new(0);
    cfg->exit = cfg_node_new(1);

    /* Build nodes for each basic block */
    build_cfg_expr(cfg, expr, cfg->entry, cfg->exit);

    return cfg;
}

static CFGNode* build_cfg_expr(CFG* cfg, OmniValue* expr,
                                CFGNode* entry, CFGNode* exit) {
    if (!expr || omni_is_nil(expr)) {
        cfg_add_edge(entry, exit);
        return entry;
    }

    if (omni_is_cell(expr)) {
        OmniValue* head = omni_car(expr);
        if (omni_is_sym(head)) {
            const char* name = head->str_val;

            if (strcmp(name, "if") == 0) {
                /* Create branch structure:
                 *      entry
                 *        |
                 *      cond
                 *      /   \
                 *   then   else
                 *      \   /
                 *       join
                 *        |
                 *      exit
                 */
                CFGNode* cond_node = cfg_node_new(cfg->node_count++);
                CFGNode* then_node = cfg_node_new(cfg->node_count++);
                CFGNode* else_node = cfg_node_new(cfg->node_count++);
                CFGNode* join_node = cfg_node_new(cfg->node_count++);

                cfg_add_edge(entry, cond_node);
                cfg_add_edge(cond_node, then_node);
                cfg_add_edge(cond_node, else_node);

                OmniValue* args = omni_cdr(expr);
                OmniValue* then_expr = omni_car(omni_cdr(args));
                OmniValue* else_expr = omni_car(omni_cdr(omni_cdr(args)));

                build_cfg_expr(cfg, then_expr, then_node, join_node);
                build_cfg_expr(cfg, else_expr, else_node, join_node);

                cfg_add_edge(join_node, exit);
                return cond_node;
            }

            if (strcmp(name, "let") == 0) {
                /* Sequential binding + body */
                /* ... similar pattern ... */
            }
        }
    }

    /* Default: straight-line code */
    CFGNode* stmt = cfg_node_new(cfg->node_count++);
    cfg_add_edge(entry, stmt);
    cfg_add_edge(stmt, exit);
    return stmt;
}

/* Standard backward dataflow for liveness */
void omni_compute_liveness(CFG* cfg, AnalysisContext* ctx) {
    bool changed = true;

    while (changed) {
        changed = false;

        /* Process nodes in reverse postorder */
        for (size_t i = cfg->node_count; i > 0; i--) {
            CFGNode* n = cfg->nodes[i - 1];

            /* live_out = union of successors' live_in */
            for (size_t s = 0; s < n->succ_count; s++) {
                CFGNode* succ = n->successors[s];
                for (size_t v = 0; v < succ->live_in_count; v++) {
                    if (!array_contains(n->live_out, n->live_out_count,
                                       succ->live_in[v])) {
                        n->live_out = realloc(n->live_out,
                                              (n->live_out_count + 1) * sizeof(char*));
                        n->live_out[n->live_out_count++] = succ->live_in[v];
                        changed = true;
                    }
                }
            }

            /* live_in = use(n) ∪ (live_out - def(n)) */
            /* ... compute based on node's uses/defs ... */
        }
    }
}

/* Determine what to free at end of each node */
char** omni_get_frees_for_node(CFG* cfg, CFGNode* node, size_t* out_count) {
    /* Variables that are:
     * 1. In live_in but not in live_out
     * 2. Owned locally (must_free = true)
     * These are "last use" points
     */
    size_t count = 0;
    char** to_free = NULL;

    for (size_t i = 0; i < node->live_in_count; i++) {
        char* var = node->live_in[i];
        if (!array_contains(node->live_out, node->live_out_count, var)) {
            /* Variable dies here - check if we own it */
            OwnerInfo* o = omni_get_owner_info(ctx, var);
            if (o && o->must_free) {
                to_free = realloc(to_free, (count + 1) * sizeof(char*));
                to_free[count++] = var;
            }
        }
    }

    *out_count = count;
    return to_free;
}
```

### 2.2 Codegen Integration

```c
/* Modify codegen to use CFG-based free points */
void omni_codegen_with_cfg(CodeGenContext* ctx, OmniValue* expr) {
    /* Build CFG for this expression */
    CFG* cfg = omni_build_cfg(expr);

    /* Compute liveness */
    omni_compute_liveness(cfg, ctx->analysis);

    /* Generate code with proper free placement */
    codegen_with_cfg(ctx, expr, cfg, cfg->entry);

    /* Cleanup */
    omni_cfg_free(cfg);
}

static void codegen_with_cfg(CodeGenContext* ctx, OmniValue* expr,
                              CFG* cfg, CFGNode* current) {
    /* Generate expression code */
    codegen_expr(ctx, expr);

    /* Emit frees for variables that die at this node */
    size_t free_count;
    char** to_free = omni_get_frees_for_node(cfg, current, &free_count);
    for (size_t i = 0; i < free_count; i++) {
        const char* c_name = lookup_symbol(ctx, to_free[i]);
        if (c_name) {
            omni_codegen_emit(ctx, "free_obj(%s);\n", c_name);
        }
    }
    free(to_free);
}
```

### 2.3 Testing
- Test that variables are freed at last use, not scope end
- Test if-branch handling (free in correct branch)
- Test loop handling (no double-free)
- Verify with Valgrind and ASan

### 2.4 Verification Checklist
- [ ] CFG construction implemented
- [ ] Backward liveness dataflow working
- [ ] Free points computed from liveness
- [ ] Codegen uses CFG-based free points
- [ ] All control paths have correct frees
- [ ] Tests pass under Valgrind/ASan

---

## 3. Ownership-Driven Codegen

**Goal:** Route based on ownership: borrowed = no RC, consumed = transfer, owned = normal.

### 3.1 Ownership Classification Enhancement

```c
/* Add to analysis.h */
typedef enum {
    PARAM_BORROWED,    /* Read-only access, no RC */
    PARAM_CONSUMED,    /* Ownership transfers to callee */
    PARAM_OWNED,       /* Callee gets new ownership */
} ParamOwnership;

typedef struct FuncSummary {
    char* name;
    ParamOwnership* param_modes;  /* Array of param ownerships */
    size_t param_count;
    bool returns_fresh;           /* Does it return newly allocated? */
    bool returns_borrowed;        /* Does it return a borrow? */
    struct FuncSummary* next;
} FuncSummary;

/* Add to AnalysisContext */
FuncSummary* func_summaries;
```

### 3.2 Inference Rules

```c
/* Infer ownership mode for a parameter */
static ParamOwnership infer_param_ownership(AnalysisContext* ctx,
                                             const char* func_name,
                                             const char* param_name,
                                             OmniValue* body) {
    VarUsage* u = omni_get_var_usage(ctx, param_name);
    if (!u) return PARAM_BORROWED;

    /* Check for mutations */
    bool is_mutated = (u->flags & VAR_USAGE_WRITE);

    /* Check for storage in data structures */
    bool is_stored = check_if_stored(body, param_name);

    /* Check for return */
    bool is_returned = check_if_returned(body, param_name);

    if (is_stored || is_returned) {
        return PARAM_CONSUMED;  /* Ownership transfers */
    } else if (is_mutated) {
        return PARAM_OWNED;     /* Needs owned copy */
    } else {
        return PARAM_BORROWED;  /* Read-only */
    }
}

static bool check_if_stored(OmniValue* expr, const char* var_name) {
    /* Check for (cons var ...) or (set! field var) patterns */
    if (!expr || omni_is_nil(expr)) return false;

    if (omni_is_cell(expr)) {
        OmniValue* head = omni_car(expr);
        if (omni_is_sym(head)) {
            const char* op = head->str_val;
            if (strcmp(op, "cons") == 0 || strcmp(op, "list") == 0 ||
                strcmp(op, "vector") == 0 || strcmp(op, "set!") == 0) {
                /* Check if var appears in arguments */
                OmniValue* args = omni_cdr(expr);
                while (!omni_is_nil(args)) {
                    if (omni_is_sym(omni_car(args)) &&
                        strcmp(omni_car(args)->str_val, var_name) == 0) {
                        return true;
                    }
                    args = omni_cdr(args);
                }
            }
        }
        /* Recurse into subexpressions */
        return check_if_stored(omni_car(expr), var_name) ||
               check_if_stored(omni_cdr(expr), var_name);
    }
    return false;
}

static bool check_if_returned(OmniValue* body, const char* var_name) {
    /* Check if var is in tail position of body */
    /* Get last expression in body */
    OmniValue* last = body;
    while (!omni_is_nil(body) && omni_is_cell(body)) {
        last = omni_car(body);
        body = omni_cdr(body);
    }

    if (omni_is_sym(last) && strcmp(last->str_val, var_name) == 0) {
        return true;
    }

    /* Check for (if ... var ...) patterns */
    if (omni_is_cell(last)) {
        OmniValue* head = omni_car(last);
        if (omni_is_sym(head) && strcmp(head->str_val, "if") == 0) {
            OmniValue* args = omni_cdr(last);
            OmniValue* then_expr = omni_car(omni_cdr(args));
            OmniValue* else_expr = omni_car(omni_cdr(omni_cdr(args)));
            return check_if_returned(omni_list1(then_expr), var_name) ||
                   check_if_returned(omni_list1(else_expr), var_name);
        }
    }

    return false;
}
```

### 3.3 Codegen Based on Ownership

```c
/* Generate call with ownership-aware RC */
static void codegen_call_with_ownership(CodeGenContext* ctx,
                                         const char* func_name,
                                         OmniValue* args) {
    FuncSummary* summary = get_func_summary(ctx->analysis, func_name);

    omni_codegen_emit_raw(ctx, "%s(", func_name);

    int i = 0;
    bool first = true;
    while (!omni_is_nil(args) && omni_is_cell(args)) {
        if (!first) omni_codegen_emit_raw(ctx, ", ");
        first = false;

        OmniValue* arg = omni_car(args);
        ParamOwnership mode = PARAM_BORROWED;
        if (summary && i < summary->param_count) {
            mode = summary->param_modes[i];
        }

        switch (mode) {
        case PARAM_BORROWED:
            /* No RC change - just pass */
            codegen_expr(ctx, arg);
            break;

        case PARAM_CONSUMED:
            /* Transfer ownership - no inc_ref, caller doesn't free */
            codegen_expr(ctx, arg);
            if (omni_is_sym(arg)) {
                /* Mark as transferred so we don't free later */
                mark_ownership_transferred(ctx, arg->str_val);
            }
            break;

        case PARAM_OWNED:
            /* Callee needs owned copy - inc_ref before passing */
            omni_codegen_emit_raw(ctx, "(inc_ref(");
            codegen_expr(ctx, arg);
            omni_codegen_emit_raw(ctx, "), ");
            codegen_expr(ctx, arg);
            omni_codegen_emit_raw(ctx, ")");
            break;
        }

        args = omni_cdr(args);
        i++;
    }

    omni_codegen_emit_raw(ctx, ")");
}

static void mark_ownership_transferred(CodeGenContext* ctx, const char* var_name) {
    /* Update owner_info to mark as transferred */
    OwnerInfo* o = omni_get_owner_info(ctx->analysis, var_name);
    if (o) {
        o->ownership = OWNER_TRANSFERRED;
        o->must_free = false;
    }
}
```

### 3.4 Testing
- Test borrowed params: no inc/dec generated
- Test consumed params: no free at caller
- Test owned params: inc_ref before call
- Test return values: correct ownership

### 3.5 Verification Checklist
- [ ] FuncSummary struct added
- [ ] Parameter ownership inference implemented
- [ ] Codegen routes by ownership mode
- [ ] Transferred variables not freed
- [ ] Tests validate RC operation count

---

## 4. Shape Analysis + Weak Back-Edge Routing

**Goal:** Detect Tree/DAG/Cyclic shapes and auto-weaken back-edges.

### 4.1 Type Graph Construction

```c
/* Add to analysis.h */
typedef struct TypeNode {
    char* name;
    char** field_names;
    char** field_types;
    size_t field_count;
    bool is_recursive;         /* Has self-reference */
    bool is_mutually_recursive; /* Part of mutually recursive group */
    struct TypeNode* next;
} TypeNode;

typedef struct TypeGraph {
    TypeNode* nodes;
    /* Adjacency info for SCC detection */
    int** adj;      /* adj[i] = list of indices node i points to */
    size_t* adj_counts;
    size_t node_count;
} TypeGraph;

/* Build type graph from definitions */
TypeGraph* omni_build_type_graph(OmniValue** type_defs, size_t count);

/* Detect back-edges using DFS */
void omni_detect_back_edges(TypeGraph* graph, AnalysisContext* ctx);

/* Check if a field is a back-edge */
bool omni_is_back_edge_field(AnalysisContext* ctx,
                              const char* type_name,
                              const char* field_name);
```

### 4.2 Back-Edge Detection Heuristics

```c
/* Names that strongly suggest back-edges */
static const char* BACK_EDGE_HINTS[] = {
    "parent", "prev", "back", "owner", "container",
    "up", "predecessor", "left_sibling", "backward",
    NULL
};

static bool is_back_edge_name_hint(const char* field_name) {
    for (int i = 0; BACK_EDGE_HINTS[i]; i++) {
        if (strstr(field_name, BACK_EDGE_HINTS[i])) {
            return true;
        }
    }
    return false;
}

/* DFS-based back-edge detection */
typedef struct {
    int* color;      /* 0=white, 1=gray, 2=black */
    int* parent;
    bool* is_back_edge;  /* For each edge */
    size_t edge_count;
} DFSState;

static void dfs_visit(TypeGraph* g, int u, DFSState* state) {
    state->color[u] = 1;  /* Gray - in progress */

    for (size_t i = 0; i < g->adj_counts[u]; i++) {
        int v = g->adj[u][i];
        if (state->color[v] == 1) {
            /* Back edge: points to gray (ancestor in DFS tree) */
            /* Mark this edge as back-edge */
            mark_back_edge(g, u, i, state);
        } else if (state->color[v] == 0) {
            state->parent[v] = u;
            dfs_visit(g, v, state);
        }
    }

    state->color[u] = 2;  /* Black - finished */
}

void omni_detect_back_edges(TypeGraph* graph, AnalysisContext* ctx) {
    DFSState state = {0};
    state.color = calloc(graph->node_count, sizeof(int));
    state.parent = malloc(graph->node_count * sizeof(int));
    memset(state.parent, -1, graph->node_count * sizeof(int));

    for (size_t i = 0; i < graph->node_count; i++) {
        if (state.color[i] == 0) {
            dfs_visit(graph, i, &state);
        }
    }

    /* Also apply name heuristics */
    for (size_t i = 0; i < graph->node_count; i++) {
        TypeNode* t = get_type_node(graph, i);
        for (size_t f = 0; f < t->field_count; f++) {
            if (is_back_edge_name_hint(t->field_names[f])) {
                /* Add to back-edge list even if DFS didn't catch it */
                add_back_edge_field(ctx, t->name, t->field_names[f]);
            }
        }
    }

    free(state.color);
    free(state.parent);
}
```

### 4.3 Shape Classification

```c
void omni_analyze_shape_full(AnalysisContext* ctx, OmniValue* type_def) {
    if (!omni_is_cell(type_def)) return;

    OmniValue* head = omni_car(type_def);
    if (!omni_is_sym(head)) return;

    const char* name = head->str_val;
    if (strcmp(name, "struct") != 0 && strcmp(name, "defstruct") != 0) return;

    /* Get type name */
    OmniValue* type_name_val = omni_car(omni_cdr(type_def));
    if (!omni_is_sym(type_name_val)) return;
    const char* type_name = type_name_val->str_val;

    /* Analyze fields */
    OmniValue* fields = omni_cdr(omni_cdr(type_def));
    bool has_self_ref = false;
    bool has_back_edge = false;
    size_t ref_count = 0;

    while (!omni_is_nil(fields) && omni_is_cell(fields)) {
        OmniValue* field = omni_car(fields);
        /* Parse field: (field-name type) */
        if (omni_is_cell(field)) {
            OmniValue* fname = omni_car(field);
            OmniValue* ftype = omni_car(omni_cdr(field));
            if (omni_is_sym(ftype)) {
                const char* ftype_str = ftype->str_val;
                if (strcmp(ftype_str, type_name) == 0) {
                    has_self_ref = true;
                    ref_count++;
                }
                if (omni_is_back_edge_field(ctx, type_name, fname->str_val)) {
                    has_back_edge = true;
                }
            }
        }
        fields = omni_cdr(fields);
    }

    /* Classify shape */
    ShapeInfo* info = malloc(sizeof(ShapeInfo));
    info->type_name = strdup(type_name);
    info->back_edge_fields = NULL;
    info->back_edge_count = 0;

    if (!has_self_ref) {
        info->shape = SHAPE_SCALAR;
    } else if (ref_count == 1 && !has_back_edge) {
        info->shape = SHAPE_TREE;
    } else if (!has_back_edge) {
        info->shape = SHAPE_DAG;
    } else {
        info->shape = SHAPE_CYCLIC;
    }

    info->next = ctx->shape_info;
    ctx->shape_info = info;
}
```

### 4.4 Codegen Routing Based on Shape

```c
/* Choose reclamation strategy based on shape */
static const char* get_reclaim_strategy(AnalysisContext* ctx,
                                         const char* type_name) {
    ShapeInfo* info = get_shape_info(ctx, type_name);
    if (!info) return "rc";  /* Default to RC */

    switch (info->shape) {
    case SHAPE_SCALAR:
    case SHAPE_TREE:
        return "asap";  /* Direct free */
    case SHAPE_DAG:
        return "rc";    /* Reference counting */
    case SHAPE_CYCLIC:
        /* Check if back-edges are weakened */
        if (info->back_edge_count > 0) {
            return "rc";  /* Cycles broken by weak refs */
        }
        return "scc";   /* Need SCC-based RC */
    default:
        return "rc";
    }
}

static void codegen_free_by_shape(CodeGenContext* ctx,
                                   const char* var_name,
                                   const char* type_name) {
    const char* strategy = get_reclaim_strategy(ctx->analysis, type_name);

    if (strcmp(strategy, "asap") == 0) {
        omni_codegen_emit(ctx, "free_obj(%s);\n", var_name);
    } else if (strcmp(strategy, "rc") == 0) {
        omni_codegen_emit(ctx, "dec_ref(%s);\n", var_name);
    } else if (strcmp(strategy, "scc") == 0) {
        omni_codegen_emit(ctx, "scc_release(%s);\n", var_name);
    }
}
```

### 4.5 Testing
- Test Tree detection: linked list, binary tree
- Test DAG detection: shared nodes
- Test Cyclic detection: doubly-linked list
- Test back-edge heuristics: "parent", "prev" fields
- Verify correct reclamation strategy chosen

### 4.6 Verification Checklist
- [ ] TypeGraph construction working
- [ ] DFS back-edge detection implemented
- [ ] Name heuristics for back-edges
- [ ] ShapeInfo populated correctly
- [ ] Codegen routes to correct strategy
- [ ] Weak ref fields generated

---

## 5. Perceus Reuse Analysis

**Goal:** Pair free+alloc of same size into in-place reuse.

### 5.1 Reuse Candidate Detection

```c
/* Add to analysis.h */
typedef struct ReusePair {
    char* dying_var;       /* Variable being freed */
    int free_pos;          /* Position of free */
    char* new_var;         /* New variable being allocated */
    int alloc_pos;         /* Position of allocation */
    size_t size;           /* Size match */
    bool is_valid;         /* Reuse is safe */
    struct ReusePair* next;
} ReusePair;

/* Find reuse opportunities */
ReusePair* omni_find_reuse_pairs(AnalysisContext* ctx, OmniValue* expr);
```

### 5.2 Implementation

```c
/* Size mapping for OmniLisp types */
static size_t get_alloc_size(const char* type) {
    if (strcmp(type, "int") == 0) return sizeof(Obj);
    if (strcmp(type, "pair") == 0) return sizeof(Obj);
    if (strcmp(type, "symbol") == 0) return sizeof(Obj);
    /* Add more types as needed */
    return sizeof(Obj);  /* Default */
}

void omni_analyze_reuse(AnalysisContext* ctx, OmniValue* expr) {
    /* Build list of free points and alloc points */
    typedef struct {
        char* var;
        int pos;
        size_t size;
        bool is_free;  /* true = free, false = alloc */
    } MemOp;

    MemOp* ops = NULL;
    size_t op_count = 0;

    /* Collect memory operations in order */
    collect_mem_ops(expr, &ops, &op_count, ctx);

    /* Find matching pairs: free followed by alloc of same size */
    for (size_t i = 0; i + 1 < op_count; i++) {
        if (!ops[i].is_free) continue;

        /* Look for next alloc of same size */
        for (size_t j = i + 1; j < op_count; j++) {
            if (ops[j].is_free) continue;
            if (ops[j].size != ops[i].size) continue;

            /* Check that dying var is not used between free and alloc */
            bool intervening_use = check_intervening_use(ctx,
                                                          ops[i].var,
                                                          ops[i].pos,
                                                          ops[j].pos);
            if (intervening_use) continue;

            /* Valid reuse pair! */
            ReuseCandidate* rc = malloc(sizeof(ReuseCandidate));
            rc->alloc_pos = ops[j].pos;
            rc->free_pos = ops[i].pos;
            rc->type_name = strdup(ops[j].var);  /* Type info */
            rc->size = ops[j].size;
            rc->can_reuse = true;
            rc->next = ctx->reuse_candidates;
            ctx->reuse_candidates = rc;

            break;  /* Found match for this free */
        }
    }

    free(ops);
}

static bool check_intervening_use(AnalysisContext* ctx,
                                   const char* var,
                                   int start_pos,
                                   int end_pos) {
    VarUsage* u = omni_get_var_usage(ctx, var);
    if (!u) return false;

    /* Check if any use of var falls in (start_pos, end_pos) */
    /* This requires more detailed use tracking - simplify for now */
    return u->last_use > start_pos && u->last_use < end_pos;
}
```

### 5.3 Runtime Reuse Functions

```c
/* Add to runtime.c */

/* Reuse object memory for new int */
static Obj* reuse_as_int(Obj* old, int64_t i) {
    if (!old || old->mark == MARK_STACK) {
        /* Can't reuse - allocate new */
        return mk_int(i);
    }

    /* Clear old contents if needed */
    if (old->tag == TAG_SYM && old->s) {
        free(old->s);
    }

    /* Reinitialize as int */
    old->generation = _next_generation();
    old->mark = 0;
    old->tag = TAG_INT;
    old->is_pair = 0;
    old->i = i;
    return old;
}

/* Reuse for pair */
static Obj* reuse_as_pair(Obj* old, Obj* a, Obj* b) {
    if (!old || old->mark == MARK_STACK) {
        return mk_pair(a, b);
    }

    /* Clear old contents */
    if (old->tag == TAG_SYM && old->s) {
        free(old->s);
    } else if (old->tag == TAG_PAIR) {
        /* Note: don't free children - they may still be referenced */
    }

    old->generation = _next_generation();
    old->mark = 0;
    old->tag = TAG_PAIR;
    old->is_pair = 1;
    old->a = a;
    old->b = b;
    return old;
}

/* Check if object can be reused (unique ref, right size) */
static bool can_reuse(Obj* obj) {
    if (!obj) return false;
    if (obj->mark == MARK_STACK) return false;
    if (obj->mark < 0) return false;  /* Special marks */
    /* Check refcount if using RC */
    /* For ASAP, assume unique if we're about to free */
    return true;
}
```

### 5.4 Codegen for Reuse

```c
static void codegen_alloc_with_reuse(CodeGenContext* ctx,
                                      const char* var_name,
                                      const char* type,
                                      OmniValue* args) {
    /* Check if there's a reuse candidate for this position */
    ReuseCandidate* rc = find_reuse_at_alloc(ctx->analysis, ctx->position);

    if (rc && rc->can_reuse) {
        /* Emit reuse instead of free+alloc */
        const char* dying_var = lookup_symbol(ctx, rc->dying_var);
        char* c_name = omni_codegen_mangle(var_name);

        if (strcmp(type, "int") == 0) {
            omni_codegen_emit(ctx, "Obj* %s = reuse_as_int(%s, ",
                              c_name, dying_var);
            codegen_expr(ctx, omni_car(args));
            omni_codegen_emit_raw(ctx, ");\n");
        } else if (strcmp(type, "pair") == 0) {
            omni_codegen_emit(ctx, "Obj* %s = reuse_as_pair(%s, ",
                              c_name, dying_var);
            codegen_expr(ctx, omni_car(args));
            omni_codegen_emit_raw(ctx, ", ");
            codegen_expr(ctx, omni_car(omni_cdr(args)));
            omni_codegen_emit_raw(ctx, ");\n");
        }

        /* Don't emit free for the dying var - memory reused */
        mark_reused(ctx, rc->dying_var);

        register_symbol(ctx, var_name, c_name);
        free(c_name);
    } else {
        /* Normal allocation */
        codegen_alloc(ctx, var_name, type, args);
    }
}
```

### 5.5 Testing
- Test reuse of same-size allocations
- Test that intervening uses prevent reuse
- Test reuse across control flow
- Verify memory correctness with Valgrind
- Benchmark allocation count reduction

### 5.6 Verification Checklist
- [ ] ReusePair detection implemented
- [ ] Size matching working
- [ ] Intervening use check working
- [ ] reuse_as_* functions in runtime
- [ ] Codegen emits reuse calls
- [ ] Dying vars not double-freed

---

## 6. Region-Aware RC Elision

**Goal:** Skip inc/dec for region-local borrows.

### 6.1 Region Tracking in Analysis

```c
/* Add to analysis.h */
typedef struct RegionScope {
    int id;
    int parent_id;
    int start_pos;
    int end_pos;
    char** local_vars;     /* Variables allocated in this region */
    size_t local_count;
    char** borrowed_vars;  /* Variables borrowed into this region */
    size_t borrowed_count;
    bool all_local_immutable;  /* Can elide all RC */
    struct RegionScope* next;
} RegionScope;

/* Track region for each allocation */
void omni_analyze_regions(AnalysisContext* ctx, OmniValue* expr);

/* Check if variable is region-local */
bool omni_is_region_local(AnalysisContext* ctx, const char* var_name, int region_id);

/* Check if RC can be elided for access */
bool omni_can_elide_rc(AnalysisContext* ctx, const char* var_name, int current_region);
```

### 6.2 Implementation

```c
static int g_region_counter = 0;

void omni_analyze_regions(AnalysisContext* ctx, OmniValue* expr) {
    analyze_region_expr(ctx, expr, 0 /* root region */);
}

static void analyze_region_expr(AnalysisContext* ctx, OmniValue* expr, int region_id) {
    if (!expr || omni_is_nil(expr)) return;

    if (omni_is_cell(expr)) {
        OmniValue* head = omni_car(expr);
        if (omni_is_sym(head)) {
            const char* name = head->str_val;

            if (strcmp(name, "let") == 0 || strcmp(name, "let*") == 0) {
                /* Create new region scope */
                int new_region = ++g_region_counter;
                RegionScope* scope = malloc(sizeof(RegionScope));
                scope->id = new_region;
                scope->parent_id = region_id;
                scope->start_pos = ctx->position;
                scope->local_vars = NULL;
                scope->local_count = 0;
                scope->borrowed_vars = NULL;
                scope->borrowed_count = 0;
                scope->all_local_immutable = true;
                scope->next = ctx->region_scopes;
                ctx->region_scopes = scope;

                /* Analyze bindings - each creates a region-local var */
                OmniValue* bindings = omni_car(omni_cdr(expr));
                analyze_bindings_for_region(ctx, bindings, scope);

                /* Analyze body in new region */
                OmniValue* body = omni_cdr(omni_cdr(expr));
                while (!omni_is_nil(body) && omni_is_cell(body)) {
                    analyze_region_expr(ctx, omni_car(body), new_region);
                    body = omni_cdr(body);
                }

                scope->end_pos = ctx->position;

                /* Check if all locals are immutable */
                for (size_t i = 0; i < scope->local_count; i++) {
                    VarUsage* u = omni_get_var_usage(ctx, scope->local_vars[i]);
                    if (u && (u->flags & VAR_USAGE_WRITE)) {
                        scope->all_local_immutable = false;
                    }
                }

                return;
            }

            if (strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0) {
                /* Lambda creates a new region with captured variables borrowed */
                int new_region = ++g_region_counter;
                RegionScope* scope = malloc(sizeof(RegionScope));
                scope->id = new_region;
                scope->parent_id = region_id;
                /* ... similar setup ... */

                /* Captured variables are borrowed into this region */
                collect_captured_vars(ctx, expr, scope);

                /* Analyze lambda body */
                OmniValue* body = omni_cdr(omni_cdr(expr));
                analyze_region_expr(ctx, body, new_region);

                return;
            }
        }
    }

    /* Recurse into subexpressions */
    if (omni_is_cell(expr)) {
        analyze_region_expr(ctx, omni_car(expr), region_id);
        analyze_region_expr(ctx, omni_cdr(expr), region_id);
    }
}

bool omni_can_elide_rc(AnalysisContext* ctx, const char* var_name, int current_region) {
    /* Can elide RC if:
     * 1. Variable is local to current region or ancestor
     * 2. Variable is immutable (or borrowed)
     * 3. Variable doesn't escape the region
     */
    RegionScope* scope = find_region(ctx, current_region);
    if (!scope) return false;

    /* Check if local to this region */
    for (size_t i = 0; i < scope->local_count; i++) {
        if (strcmp(scope->local_vars[i], var_name) == 0) {
            /* Local - check immutability and escape */
            VarUsage* u = omni_get_var_usage(ctx, var_name);
            EscapeClass ec = omni_get_escape_class(ctx, var_name);
            if (u && !(u->flags & VAR_USAGE_WRITE) && ec == ESCAPE_NONE) {
                return true;  /* Can elide! */
            }
        }
    }

    /* Check if borrowed into this region */
    for (size_t i = 0; i < scope->borrowed_count; i++) {
        if (strcmp(scope->borrowed_vars[i], var_name) == 0) {
            return true;  /* Borrowed - no RC needed */
        }
    }

    /* Check parent regions */
    if (scope->parent_id >= 0) {
        return omni_can_elide_rc(ctx, var_name, scope->parent_id);
    }

    return false;
}
```

### 6.3 Codegen with RC Elision

```c
/* Modified access generation */
static void codegen_var_access(CodeGenContext* ctx, const char* var_name) {
    int current_region = ctx->current_region_id;

    if (omni_can_elide_rc(ctx->analysis, var_name, current_region)) {
        /* No RC needed - direct access */
        const char* c_name = lookup_symbol(ctx, var_name);
        omni_codegen_emit_raw(ctx, "%s", c_name);
    } else {
        /* Need RC - emit inc_ref if needed */
        const char* c_name = lookup_symbol(ctx, var_name);
        /* Depending on context, may need inc_ref */
        if (ctx->need_inc_ref) {
            omni_codegen_emit_raw(ctx, "(inc_ref(%s), %s)", c_name, c_name);
        } else {
            omni_codegen_emit_raw(ctx, "%s", c_name);
        }
    }
}
```

### 6.4 Testing
- Test that region-local immutable vars have no RC
- Test that escaped vars still have RC
- Test nested regions
- Count RC operations and verify reduction
- Benchmark RC-heavy code

### 6.5 Verification Checklist
- [ ] RegionScope tracking implemented
- [ ] Region analysis runs correctly
- [ ] RC elision query working
- [ ] Codegen skips RC for eligible vars
- [ ] No memory errors (Valgrind/ASan)
- [ ] Measurable RC reduction in benchmarks

---

## 7. Per-Region External Refcount

**Goal:** Track escaping refs per region for O(1) bulk free.

### 7.1 Runtime Additions

```c
/* Extend Region struct */
typedef struct Region {
    uint64_t id;
    struct Region* parent;
    struct Region** children;
    size_t child_count;
    size_t child_capacity;

    /* Object tracking */
    RegionObj** objects;
    size_t obj_count;
    size_t obj_capacity;

    /* NEW: External reference counting */
    int external_rc;      /* Count of refs from outside this region */
    bool can_bulk_free;   /* True if external_rc == 0 */
} Region;

/* Increment external RC when object escapes */
void region_register_escape(Region* r, RegionObj* obj) {
    r->external_rc++;
    r->can_bulk_free = false;
}

/* Decrement external RC when external ref dies */
void region_unregister_escape(Region* r, RegionObj* obj) {
    r->external_rc--;
    if (r->external_rc == 0) {
        r->can_bulk_free = true;
        /* Could trigger eager cleanup here */
    }
}

/* Check if region can be bulk-freed */
bool region_is_orphaned(Region* r) {
    return r->external_rc == 0;
}

/* Bulk free all objects in region (O(1) deallocation) */
void region_bulk_free(Region* r) {
    if (!region_is_orphaned(r)) {
        /* Still has external refs - can't bulk free */
        return;
    }

    /* Free all children first (post-order) */
    for (size_t i = 0; i < r->child_count; i++) {
        region_bulk_free(r->children[i]);
    }

    /* Free all objects in this region */
    for (size_t i = 0; i < r->obj_count; i++) {
        RegionObj* obj = r->objects[i];
        if (obj->destructor) {
            obj->destructor(obj->data);
        }
        free(obj);
    }

    /* Clear tracking */
    free(r->objects);
    r->objects = NULL;
    r->obj_count = 0;
    r->obj_capacity = 0;
}
```

### 7.2 Codegen for External RC

```c
/* When a value escapes its region */
static void codegen_region_escape(CodeGenContext* ctx,
                                   const char* var_name,
                                   int from_region,
                                   int to_region) {
    if (from_region == to_region) return;

    const char* c_name = lookup_symbol(ctx, var_name);

    /* Increment external RC on source region */
    omni_codegen_emit(ctx, "region_register_escape(get_region(%s), %s);\n",
                      c_name, c_name);
}

/* When an external ref dies */
static void codegen_region_unescape(CodeGenContext* ctx, const char* var_name) {
    const char* c_name = lookup_symbol(ctx, var_name);
    omni_codegen_emit(ctx, "region_unregister_escape(get_region(%s), %s);\n",
                      c_name, c_name);
}

/* At region exit */
static void codegen_region_exit(CodeGenContext* ctx, int region_id) {
    RegionScope* scope = find_region(ctx->analysis, region_id);
    if (!scope) return;

    /* Check if can bulk free */
    omni_codegen_emit(ctx, "if (region_is_orphaned(region_%d)) {\n", region_id);
    omni_codegen_indent(ctx);
    omni_codegen_emit(ctx, "region_bulk_free(region_%d);\n", region_id);
    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "} else {\n");
    omni_codegen_indent(ctx);
    /* Fall back to individual frees */
    for (size_t i = 0; i < scope->local_count; i++) {
        const char* c_name = lookup_symbol(ctx, scope->local_vars[i]);
        if (c_name) {
            omni_codegen_emit(ctx, "dec_ref(%s);\n", c_name);
        }
    }
    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "}\n");
}
```

### 7.3 Testing
- Test bulk free when no escapes
- Test proper RC tracking when escapes occur
- Test nested regions
- Verify no dangling pointers
- Benchmark bulk free vs individual free

### 7.4 Verification Checklist
- [ ] Region.external_rc tracking added
- [ ] region_register_escape implemented
- [ ] region_bulk_free implemented
- [ ] Codegen emits escape tracking
- [ ] Bulk free triggers correctly
- [ ] No memory corruption

---

## 8. Borrow/Tether Loop Insertion

**Goal:** Single validity check at loop entry, fast access inside.

### 8.1 Analysis for Tether Points

```c
/* Add to analysis.h */
typedef struct TetherPoint {
    char* var_name;
    int loop_entry_pos;
    int loop_exit_pos;
    bool is_borrowed;      /* Borrowed into loop */
    bool needs_tether;     /* Requires tethering */
    struct TetherPoint* next;
} TetherPoint;

/* Analyze loops for tether opportunities */
void omni_analyze_tethering(AnalysisContext* ctx, OmniValue* expr);

/* Check if variable should be tethered at this loop */
bool omni_should_tether(AnalysisContext* ctx, const char* var, int loop_id);
```

### 8.2 Loop Analysis

```c
void omni_analyze_tethering(AnalysisContext* ctx, OmniValue* expr) {
    analyze_tether_expr(ctx, expr, -1 /* no loop */);
}

static void analyze_tether_expr(AnalysisContext* ctx, OmniValue* expr, int loop_id) {
    if (!expr || omni_is_nil(expr)) return;

    if (omni_is_cell(expr)) {
        OmniValue* head = omni_car(expr);
        if (omni_is_sym(head)) {
            const char* name = head->str_val;

            /* Check for loop constructs */
            if (strcmp(name, "for") == 0 || strcmp(name, "loop") == 0 ||
                strcmp(name, "while") == 0 || strcmp(name, "do") == 0) {

                int new_loop_id = ctx->loop_counter++;

                /* Collect variables used in loop body */
                OmniValue* body = get_loop_body(expr, name);
                char** used_vars = NULL;
                size_t used_count = 0;
                collect_used_vars(body, &used_vars, &used_count);

                /* For each variable: should it be tethered? */
                for (size_t i = 0; i < used_count; i++) {
                    char* var = used_vars[i];
                    VarUsage* u = omni_get_var_usage(ctx, var);

                    /* Tether if:
                     * 1. Variable is from outer scope
                     * 2. Used multiple times in loop
                     * 3. Not mutated in loop
                     */
                    bool from_outer = u && u->def_pos < ctx->position;
                    int use_count = count_uses_in_loop(body, var);
                    bool not_mutated = !(u && (u->flags & VAR_USAGE_WRITE));

                    if (from_outer && use_count > 1 && not_mutated) {
                        TetherPoint* tp = malloc(sizeof(TetherPoint));
                        tp->var_name = strdup(var);
                        tp->loop_entry_pos = ctx->position;
                        tp->is_borrowed = true;
                        tp->needs_tether = true;
                        tp->next = ctx->tether_points;
                        ctx->tether_points = tp;
                    }
                }

                /* Analyze body with loop context */
                analyze_tether_expr(ctx, body, new_loop_id);

                for (size_t i = 0; i < used_count; i++) free(used_vars[i]);
                free(used_vars);
                return;
            }
        }
    }

    if (omni_is_cell(expr)) {
        analyze_tether_expr(ctx, omni_car(expr), loop_id);
        analyze_tether_expr(ctx, omni_cdr(expr), loop_id);
    }
}
```

### 8.3 Runtime Tether Operations

```c
/* Already mostly in runtime - ensure these are available */

/* Tether: validate once, mark as stable */
static inline Obj* tether_borrowed(BorrowRef* ref) {
    if (!borrow_is_valid(ref)) {
        return NULL;  /* Invalid - fail early */
    }
    Obj* obj = ref->target;
    tether(obj);  /* Mark as tethered */
    return obj;
}

/* Fast path deref - skip validation if tethered */
static inline Obj* deref_tethered_fast(Obj* obj) {
    /* No validation - trust tether */
    return obj;
}

/* Untether at loop exit */
static inline void untether_borrowed(Obj* obj) {
    untether(obj);
}
```

### 8.4 Codegen for Loops

```c
static void codegen_loop_with_tethering(CodeGenContext* ctx,
                                         OmniValue* expr,
                                         const char* loop_type) {
    int loop_id = ctx->current_loop_id++;

    /* Emit tethers at loop entry */
    TetherPoint* tp = ctx->analysis->tether_points;
    while (tp) {
        if (tp->loop_id == loop_id && tp->needs_tether) {
            const char* c_name = lookup_symbol(ctx, tp->var_name);
            omni_codegen_emit(ctx, "Obj* _tethered_%s = tether_borrowed(&%s_ref);\n",
                              c_name, c_name);
            omni_codegen_emit(ctx, "if (!_tethered_%s) goto loop_exit_%d;\n",
                              c_name, loop_id);
        }
        tp = tp->next;
    }

    /* Generate loop body */
    /* Inside loop, use _tethered_* versions with fast access */
    ctx->in_tethered_loop = true;
    codegen_loop_body(ctx, expr, loop_type);
    ctx->in_tethered_loop = false;

    /* Emit untethers at loop exit */
    omni_codegen_emit(ctx, "loop_exit_%d:;\n", loop_id);
    tp = ctx->analysis->tether_points;
    while (tp) {
        if (tp->loop_id == loop_id && tp->needs_tether) {
            const char* c_name = lookup_symbol(ctx, tp->var_name);
            omni_codegen_emit(ctx, "untether_borrowed(_tethered_%s);\n", c_name);
        }
        tp = tp->next;
    }
}
```

### 8.5 Testing
- Test that tethered vars skip per-iteration validation
- Test early exit on invalid borrow
- Test nested loops
- Benchmark loop performance with/without tethering

### 8.6 Verification Checklist
- [ ] TetherPoint analysis implemented
- [ ] Loop detection working
- [ ] tether_borrowed in runtime
- [ ] Codegen emits tether/untether
- [ ] Fast path used inside loops
- [ ] Measurable performance improvement

---

## 9. Interprocedural Summaries

**Goal:** Propagate ownership/escape info across function calls.

### 9.1 Summary Data Structure

```c
/* Add to analysis.h */
typedef struct FuncSummary {
    char* name;

    /* Parameter info */
    ParamOwnership* param_modes;
    EscapeClass* param_escapes;
    size_t param_count;

    /* Return info */
    bool returns_fresh;        /* Allocates new object */
    bool returns_param;        /* Returns one of params */
    int returned_param_idx;    /* Which param if returns_param */
    EscapeClass return_escape;

    /* Side effects */
    bool is_pure;             /* No side effects */
    bool modifies_globals;
    bool allocates;
    bool frees;

    /* Callee info */
    char** callees;           /* Functions this calls */
    size_t callee_count;

    struct FuncSummary* next;
} FuncSummary;

/* Compute summary for a function */
FuncSummary* omni_summarize_function(AnalysisContext* ctx, OmniValue* func_def);

/* Apply summary at call site */
void omni_apply_call_summary(AnalysisContext* ctx,
                              FuncSummary* callee,
                              OmniValue* call_expr);
```

### 9.2 Summary Computation

```c
FuncSummary* omni_summarize_function(AnalysisContext* ctx, OmniValue* func_def) {
    FuncSummary* summary = calloc(1, sizeof(FuncSummary));

    /* Parse function definition */
    OmniValue* args = omni_cdr(func_def);
    OmniValue* name_or_sig = omni_car(args);
    OmniValue* body = omni_cdr(args);

    if (omni_is_cell(name_or_sig)) {
        /* (define (fname params...) body) */
        summary->name = strdup(omni_car(name_or_sig)->str_val);

        /* Count parameters */
        OmniValue* params = omni_cdr(name_or_sig);
        while (!omni_is_nil(params)) {
            summary->param_count++;
            params = omni_cdr(params);
        }

        summary->param_modes = calloc(summary->param_count, sizeof(ParamOwnership));
        summary->param_escapes = calloc(summary->param_count, sizeof(EscapeClass));
    }

    /* Create sub-context for function analysis */
    AnalysisContext* func_ctx = omni_analysis_new();

    /* Analyze function body */
    omni_analyze(func_ctx, body);
    omni_analyze_ownership(func_ctx, body);

    /* Extract parameter info */
    params = omni_cdr(name_or_sig);
    int i = 0;
    while (!omni_is_nil(params)) {
        OmniValue* param = omni_car(params);
        if (omni_is_sym(param)) {
            const char* pname = param->str_val;

            /* Ownership mode */
            summary->param_modes[i] = infer_param_ownership(func_ctx, summary->name,
                                                             pname, body);

            /* Escape class */
            summary->param_escapes[i] = omni_get_escape_class(func_ctx, pname);
        }
        params = omni_cdr(params);
        i++;
    }

    /* Return value analysis */
    OmniValue* tail = get_tail_expr(body);
    if (omni_is_sym(tail)) {
        /* Returns a variable - check if it's a param */
        for (int j = 0; j < summary->param_count; j++) {
            OmniValue* pj = get_param_at(name_or_sig, j);
            if (pj && strcmp(tail->str_val, pj->str_val) == 0) {
                summary->returns_param = true;
                summary->returned_param_idx = j;
            }
        }
    }

    /* Check for allocations in body */
    summary->allocates = contains_allocation(body);

    /* Returns fresh if returns allocation and not param */
    summary->returns_fresh = summary->allocates && !summary->returns_param;

    /* Purity check */
    summary->is_pure = is_pure_body(body);

    /* Collect callees */
    collect_callees(body, &summary->callees, &summary->callee_count);

    omni_analysis_free(func_ctx);

    return summary;
}
```

### 9.3 Applying Summaries at Call Sites

```c
void omni_apply_call_summary(AnalysisContext* ctx,
                              FuncSummary* callee,
                              OmniValue* call_expr) {
    if (!callee) return;

    OmniValue* args = omni_cdr(call_expr);
    int i = 0;

    while (!omni_is_nil(args) && i < callee->param_count) {
        OmniValue* arg = omni_car(args);

        if (omni_is_sym(arg)) {
            const char* arg_name = arg->str_val;

            /* Update escape based on callee's param escape */
            EscapeClass callee_escape = callee->param_escapes[i];
            if (callee_escape > ESCAPE_NONE) {
                set_escape_class(ctx, arg_name, ESCAPE_ARG);
            }

            /* Update ownership based on consumption */
            if (callee->param_modes[i] == PARAM_CONSUMED) {
                OwnerInfo* o = find_or_create_owner_info(ctx, arg_name);
                o->ownership = OWNER_TRANSFERRED;
                o->must_free = false;
            }
        }

        args = omni_cdr(args);
        i++;
    }

    /* Handle return value */
    if (callee->returns_fresh) {
        /* Call returns fresh allocation - caller must free */
        /* This info is used by the caller's free point computation */
    }
}
```

### 9.4 Whole-Program Summary Computation

```c
void omni_compute_all_summaries(AnalysisContext* ctx,
                                 OmniValue** program,
                                 size_t count) {
    /* First pass: compute local summaries */
    for (size_t i = 0; i < count; i++) {
        if (is_function_def(program[i])) {
            FuncSummary* s = omni_summarize_function(ctx, program[i]);
            s->next = ctx->func_summaries;
            ctx->func_summaries = s;
        }
    }

    /* Second pass: propagate through call graph (fixpoint) */
    bool changed = true;
    while (changed) {
        changed = false;
        for (FuncSummary* s = ctx->func_summaries; s; s = s->next) {
            /* Update based on callees */
            for (size_t c = 0; c < s->callee_count; c++) {
                FuncSummary* callee = find_summary(ctx, s->callees[c]);
                if (callee) {
                    /* Propagate allocates/frees/purity */
                    if (callee->allocates && !s->allocates) {
                        s->allocates = true;
                        changed = true;
                    }
                    if (!callee->is_pure && s->is_pure) {
                        s->is_pure = false;
                        changed = true;
                    }
                }
            }
        }
    }
}
```

### 9.5 Testing
- Test summary computation for simple functions
- Test ownership propagation through calls
- Test escape propagation
- Test fixpoint convergence for recursive functions
- Verify correct RC at call sites

### 9.6 Verification Checklist
- [ ] FuncSummary struct implemented
- [ ] Local summary computation working
- [ ] Whole-program fixpoint working
- [ ] Call site summary application working
- [ ] RC correctly adjusted at calls
- [ ] Tests for various call patterns

---

## 10. Concurrency Ownership Inference

**Goal:** Infer ownership transfer at message-passing boundaries.

### 10.1 Channel Analysis

```c
/* Add to analysis.h */
typedef enum {
    CHAN_SEND,       /* Value sent on channel */
    CHAN_RECV,       /* Value received from channel */
    CHAN_CLOSE,      /* Channel closed */
} ChanOpKind;

typedef struct ChanOp {
    ChanOpKind kind;
    char* channel_var;
    char* value_var;   /* For send/recv */
    int position;
    struct ChanOp* next;
} ChanOp;

typedef struct SpawnInfo {
    int spawn_pos;
    OmniValue* body;
    char** captured_vars;
    size_t captured_count;
    struct SpawnInfo* next;
} SpawnInfo;

/* Analyze concurrency constructs */
void omni_analyze_concurrency(AnalysisContext* ctx, OmniValue* expr);

/* Check if variable is transferred via channel */
bool omni_is_chan_transferred(AnalysisContext* ctx, const char* var);
```

### 10.2 Implementation

```c
void omni_analyze_concurrency(AnalysisContext* ctx, OmniValue* expr) {
    analyze_concurrent_expr(ctx, expr);
}

static void analyze_concurrent_expr(AnalysisContext* ctx, OmniValue* expr) {
    if (!expr || omni_is_nil(expr)) return;

    if (omni_is_cell(expr)) {
        OmniValue* head = omni_car(expr);
        if (omni_is_sym(head)) {
            const char* name = head->str_val;

            if (strcmp(name, "chan-send!") == 0 || strcmp(name, "send!") == 0) {
                /* (chan-send! chan value) */
                OmniValue* args = omni_cdr(expr);
                OmniValue* chan = omni_car(args);
                OmniValue* value = omni_car(omni_cdr(args));

                ChanOp* op = malloc(sizeof(ChanOp));
                op->kind = CHAN_SEND;
                op->channel_var = omni_is_sym(chan) ? strdup(chan->str_val) : NULL;
                op->value_var = omni_is_sym(value) ? strdup(value->str_val) : NULL;
                op->position = ctx->position;
                op->next = ctx->chan_ops;
                ctx->chan_ops = op;

                /* Value transfers ownership to receiver */
                if (op->value_var) {
                    OwnerInfo* o = find_or_create_owner_info(ctx, op->value_var);
                    o->ownership = OWNER_TRANSFERRED;
                    o->must_free = false;  /* Sender doesn't free */
                }
                return;
            }

            if (strcmp(name, "chan-recv!") == 0 || strcmp(name, "recv!") == 0) {
                /* Result of recv takes ownership */
                ChanOp* op = malloc(sizeof(ChanOp));
                op->kind = CHAN_RECV;
                op->position = ctx->position;
                op->next = ctx->chan_ops;
                ctx->chan_ops = op;
                return;
            }

            if (strcmp(name, "spawn") == 0 || strcmp(name, "go") == 0) {
                /* Analyze spawned body for captures */
                OmniValue* body = omni_cdr(expr);

                SpawnInfo* si = malloc(sizeof(SpawnInfo));
                si->spawn_pos = ctx->position;
                si->body = body;
                si->captured_vars = NULL;
                si->captured_count = 0;

                /* Find captured variables */
                collect_free_vars(body, &si->captured_vars, &si->captured_count, ctx);

                /* Captured vars transfer to spawned thread */
                for (size_t i = 0; i < si->captured_count; i++) {
                    OwnerInfo* o = find_or_create_owner_info(ctx, si->captured_vars[i]);
                    o->ownership = OWNER_TRANSFERRED;
                    o->must_free = false;
                }

                si->next = ctx->spawn_info;
                ctx->spawn_info = si;

                /* Analyze body in spawned context */
                ctx->in_spawned_context = true;
                analyze_concurrent_expr(ctx, body);
                ctx->in_spawned_context = false;
                return;
            }
        }
    }

    if (omni_is_cell(expr)) {
        analyze_concurrent_expr(ctx, omni_car(expr));
        analyze_concurrent_expr(ctx, omni_cdr(expr));
    }
}

bool omni_is_chan_transferred(AnalysisContext* ctx, const char* var) {
    for (ChanOp* op = ctx->chan_ops; op; op = op->next) {
        if (op->kind == CHAN_SEND && op->value_var &&
            strcmp(op->value_var, var) == 0) {
            return true;
        }
    }
    return false;
}
```

### 10.3 Codegen for Concurrent Ownership

```c
static void codegen_chan_send(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    OmniValue* chan = omni_car(args);
    OmniValue* value = omni_car(omni_cdr(args));

    omni_codegen_emit(ctx, "channel_send(");
    codegen_expr(ctx, chan);
    omni_codegen_emit_raw(ctx, ", ");
    codegen_expr(ctx, value);
    omni_codegen_emit_raw(ctx, ");\n");

    /* Mark value as transferred - don't free in sender */
    if (omni_is_sym(value)) {
        mark_ownership_transferred(ctx, value->str_val);
    }
}

static void codegen_spawn(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* body = omni_cdr(expr);

    /* Generate closure for spawned thread */
    int closure_id = ctx->closure_counter++;

    /* Emit closure struct with captured vars */
    SpawnInfo* si = find_spawn_info(ctx->analysis, ctx->position);
    if (si) {
        omni_codegen_emit(ctx, "struct _spawn_closure_%d {\n", closure_id);
        omni_codegen_indent(ctx);
        for (size_t i = 0; i < si->captured_count; i++) {
            omni_codegen_emit(ctx, "Obj* %s;\n", si->captured_vars[i]);
        }
        omni_codegen_dedent(ctx);
        omni_codegen_emit(ctx, "};\n");

        /* Emit thread function */
        omni_codegen_emit(ctx, "static void* _spawn_fn_%d(void* arg) {\n", closure_id);
        omni_codegen_indent(ctx);
        omni_codegen_emit(ctx, "struct _spawn_closure_%d* c = arg;\n", closure_id);

        /* Unpack captures */
        for (size_t i = 0; i < si->captured_count; i++) {
            omni_codegen_emit(ctx, "Obj* %s = c->%s;\n",
                              si->captured_vars[i], si->captured_vars[i]);
        }

        /* Emit body */
        codegen_expr(ctx, body);

        /* Free captures at end (spawned thread owns them) */
        for (size_t i = 0; i < si->captured_count; i++) {
            omni_codegen_emit(ctx, "dec_ref(%s);\n", si->captured_vars[i]);
        }

        omni_codegen_emit(ctx, "free(c);\n");
        omni_codegen_emit(ctx, "return NULL;\n");
        omni_codegen_dedent(ctx);
        omni_codegen_emit(ctx, "}\n\n");

        /* Emit spawn call */
        omni_codegen_emit(ctx, "{\n");
        omni_codegen_indent(ctx);
        omni_codegen_emit(ctx, "struct _spawn_closure_%d* c = malloc(sizeof(*c));\n",
                          closure_id);
        for (size_t i = 0; i < si->captured_count; i++) {
            const char* c_name = lookup_symbol(ctx, si->captured_vars[i]);
            omni_codegen_emit(ctx, "c->%s = %s; inc_ref(%s);\n",
                              si->captured_vars[i], c_name, c_name);
            /* Note: we inc_ref for the transfer, but then don't dec_ref locally */
        }
        omni_codegen_emit(ctx, "pthread_t t; pthread_create(&t, NULL, _spawn_fn_%d, c);\n",
                          closure_id);
        omni_codegen_emit(ctx, "pthread_detach(t);\n");
        omni_codegen_dedent(ctx);
        omni_codegen_emit(ctx, "}\n");
    }
}
```

### 10.4 Testing
- Test ownership transfer on channel send
- Test spawn capture transfers
- Test that sender doesn't double-free
- Test that receiver frees
- Thread sanitizer validation

### 10.5 Verification Checklist
- [ ] ChanOp tracking implemented
- [ ] SpawnInfo tracking implemented
- [ ] Ownership transfer on send
- [ ] Captured vars transferred to spawned thread
- [ ] Codegen generates correct RC
- [ ] No races or leaks (TSan/Valgrind)

---

## 11. GenRef/IPGE Soundness Fix

**Goal:** Make generational validation sound by using stable slots.

### 11.1 Slot Pool Implementation

```c
/* Add to runtime.c */

#define SLOT_POOL_SIZE 4096
#define SLOT_FREE 0
#define SLOT_IN_USE 1

typedef struct Slot {
    uint32_t generation;
    uint8_t flags;
    uint8_t tag8;        /* Cached validation tag */
    uint16_t pad;
    Obj obj;             /* Embedded object */
} Slot;

typedef struct SlotPool {
    Slot* slots;
    size_t capacity;
    uint32_t* freelist;
    size_t freelist_top;
    uint64_t secret;     /* For tag computation */
} SlotPool;

/* Global slot pool */
static SlotPool* g_slot_pool = NULL;

/* Initialize slot pool */
void slot_pool_init(void) {
    if (g_slot_pool) return;

    g_slot_pool = malloc(sizeof(SlotPool));
    g_slot_pool->capacity = SLOT_POOL_SIZE;
    g_slot_pool->slots = calloc(SLOT_POOL_SIZE, sizeof(Slot));
    g_slot_pool->freelist = malloc(SLOT_POOL_SIZE * sizeof(uint32_t));
    g_slot_pool->freelist_top = SLOT_POOL_SIZE;

    /* Initialize freelist and random secret */
    g_slot_pool->secret = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)g_slot_pool;
    g_slot_pool->secret *= 0x5851f42d4c957f2dULL;

    for (size_t i = 0; i < SLOT_POOL_SIZE; i++) {
        g_slot_pool->slots[i].generation = 1;
        g_slot_pool->slots[i].flags = SLOT_FREE;
        g_slot_pool->freelist[i] = (uint32_t)i;
    }
}

/* Allocate from slot pool */
Slot* slot_alloc(void) {
    if (!g_slot_pool) slot_pool_init();
    if (g_slot_pool->freelist_top == 0) {
        /* Pool exhausted - grow or fall back to malloc */
        /* For now, return NULL */
        return NULL;
    }

    uint32_t idx = g_slot_pool->freelist[--g_slot_pool->freelist_top];
    Slot* s = &g_slot_pool->slots[idx];

    s->generation++;
    if (s->generation == 0) s->generation = 1;  /* Skip 0 */
    s->flags = SLOT_IN_USE;
    s->tag8 = compute_tag8(s);

    return s;
}

/* Free to slot pool */
void slot_free(Slot* s) {
    if (!s || !g_slot_pool) return;
    if (s->flags == SLOT_FREE) return;

    s->generation++;
    if (s->generation == 0) s->generation = 1;
    s->flags = SLOT_FREE;
    s->tag8 = compute_tag8(s);  /* Update for next use */

    /* Return to freelist - slot memory stays allocated! */
    size_t idx = s - g_slot_pool->slots;
    g_slot_pool->freelist[g_slot_pool->freelist_top++] = (uint32_t)idx;
}

/* Tag computation */
static inline uint64_t mix64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static inline uint8_t compute_tag8(Slot* s) {
    uint64_t x = (uint64_t)(uintptr_t)s;
    x ^= (uint64_t)s->generation * 0x9e3779b97f4a7c15ULL;
    x ^= g_slot_pool->secret;
    return (uint8_t)(mix64(x) >> 56);
}
```

### 11.2 Handle Type

```c
typedef uintptr_t Handle;
#define HANDLE_INVALID 0

/* Platform-specific encoding */
#if defined(__aarch64__)
  #define HANDLE_USE_TOP_BYTE 1
  #define TAG_SHIFT 56
  #define TAG_MASK  ((uintptr_t)0xFFu << TAG_SHIFT)
  #define PTR_MASK  (~TAG_MASK)
#else
  #define HANDLE_USE_TOP_BYTE 0
  #define TAG_BITS 4
  #define TAG_MASK ((uintptr_t)((1u << TAG_BITS) - 1u))
  #define PTR_MASK (~TAG_MASK)
#endif

static inline Handle handle_create(Slot* s) {
    if (!s) return HANDLE_INVALID;
#if HANDLE_USE_TOP_BYTE
    return ((uintptr_t)s & PTR_MASK) | ((uintptr_t)s->tag8 << TAG_SHIFT);
#else
    return ((uintptr_t)s & PTR_MASK) | ((uintptr_t)(s->tag8 & TAG_MASK));
#endif
}

static inline Slot* handle_slot(Handle h) {
    return (Slot*)(h & PTR_MASK);
}

static inline uint8_t handle_tag(Handle h) {
#if HANDLE_USE_TOP_BYTE
    return (uint8_t)(h >> TAG_SHIFT);
#else
    return (uint8_t)(h & TAG_MASK);
#endif
}

/* SOUND validation - reads from stable slot, never freed memory */
static inline bool handle_is_valid(Handle h) {
    if (h == HANDLE_INVALID) return false;

    Slot* s = handle_slot(h);
    if (!s) return false;

    /* SAFE: slot memory never freed, always readable */
#if HANDLE_USE_TOP_BYTE
    if (handle_tag(h) != s->tag8) return false;
#else
    if ((handle_tag(h) & TAG_MASK) != (s->tag8 & TAG_MASK)) return false;
#endif

    if (s->flags != SLOT_IN_USE) return false;

    return true;
}

static inline Obj* handle_deref(Handle h) {
    if (!handle_is_valid(h)) return NULL;
    return &handle_slot(h)->obj;
}
```

### 11.3 Modified BorrowRef

```c
/* Replace old BorrowRef with handle-based version */
typedef struct BorrowRef {
    Handle handle;
    const char* source_desc;
} BorrowRef;

BorrowRef borrow_from_slot(Slot* s, const char* desc) {
    return (BorrowRef){
        .handle = handle_create(s),
        .source_desc = desc
    };
}

bool borrow_is_valid(BorrowRef* ref) {
    return handle_is_valid(ref->handle);
}

Obj* borrow_deref(BorrowRef* ref) {
    return handle_deref(ref->handle);
}
```

### 11.4 Integration with Existing Code

```c
/* Modified mk_* functions to use slot pool for IPGE-managed objects */
Obj* mk_int_safe(int64_t i) {
    Slot* s = slot_alloc();
    if (!s) return mk_int(i);  /* Fall back to malloc */

    Obj* x = &s->obj;
    x->generation = s->generation;
    x->mark = 0;
    x->tag = TAG_INT;
    x->is_pair = 0;
    x->scc_id = -1;
    x->i = i;
    return x;
}

void free_obj_safe(Obj* x) {
    if (!x) return;

    /* Check if from slot pool */
    if (g_slot_pool) {
        Slot* first = g_slot_pool->slots;
        Slot* last = first + g_slot_pool->capacity;
        Slot* s = (Slot*)((char*)x - offsetof(Slot, obj));
        if (s >= first && s < last) {
            /* From slot pool - use slot_free */
            slot_free(s);
            return;
        }
    }

    /* Not from slot pool - regular free */
    free_obj(x);
}
```

### 11.5 Testing
- Test that validation never reads freed memory
- Test handle validity after slot reuse
- Test tag collision detection
- Valgrind: no invalid reads
- ASan: no use-after-free

### 11.6 Verification Checklist
- [ ] SlotPool implemented
- [ ] Slot memory never freed (stays in pool)
- [ ] Handle encoding/decoding working
- [ ] Validation reads from stable slots
- [ ] BorrowRef uses handles
- [ ] No UB in validation (Valgrind/ASan clean)
- [ ] Performance acceptable

---

## Implementation Priority and Dependencies

```
Phase 1 (Foundation):
  11. GenRef/IPGE Soundness Fix ← CRITICAL for correctness
  2.  Full Liveness Analysis   ← Enables most other opts

Phase 2 (Core Optimizations):
  3.  Ownership-Driven Codegen ← Uses liveness, enables reuse
  1.  Escape-Aware Stack Alloc ← Independent
  4.  Shape Analysis           ← Independent

Phase 3 (Advanced):
  5.  Perceus Reuse            ← Needs ownership + liveness
  6.  Region-Aware RC Elision  ← Needs ownership
  7.  Per-Region External RC   ← Extends #6

Phase 4 (Performance):
  8.  Borrow/Tether Loops      ← Needs liveness
  9.  Interprocedural Summary  ← Enhances #3
  10. Concurrency Ownership    ← Extends #3
```

## Estimated Effort

| Item | Complexity | LOC Estimate | Days |
|------|------------|--------------|------|
| 1. Stack Alloc | Medium | 300 | 2 |
| 2. Liveness CFG | High | 600 | 5 |
| 3. Ownership Codegen | Medium | 400 | 3 |
| 4. Shape Analysis | High | 500 | 4 |
| 5. Perceus Reuse | Medium | 400 | 3 |
| 6. Region RC Elision | Medium | 350 | 3 |
| 7. Region External RC | Low | 200 | 2 |
| 8. Loop Tethering | Medium | 300 | 2 |
| 9. Interproc Summary | High | 500 | 4 |
| 10. Concurrency | High | 450 | 4 |
| 11. IPGE Soundness | Medium | 400 | 3 |

**Total: ~4400 LOC, ~35 development days**

## Testing Strategy

Each optimization should include:
1. Unit tests for analysis pass
2. Integration tests for codegen
3. Memory safety validation (Valgrind, ASan, TSan)
4. Performance benchmarks
5. Regression tests against existing suite
