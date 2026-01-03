/*
 * OmniLisp Analysis Implementation
 *
 * Static analysis passes for ASAP memory management.
 */

#include "analysis.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============== Context Management ============== */

AnalysisContext* omni_analysis_new(void) {
    AnalysisContext* ctx = malloc(sizeof(AnalysisContext));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(AnalysisContext));
    return ctx;
}

static void free_var_usages(VarUsage* u) {
    while (u) {
        VarUsage* next = u->next;
        free(u->name);
        free(u);
        u = next;
    }
}

static void free_escape_info(EscapeInfo* e) {
    while (e) {
        EscapeInfo* next = e->next;
        free(e->name);
        free(e);
        e = next;
    }
}

static void free_owner_info(OwnerInfo* o) {
    while (o) {
        OwnerInfo* next = o->next;
        free(o->name);
        free(o);
        o = next;
    }
}

static void free_shape_info(ShapeInfo* s) {
    while (s) {
        ShapeInfo* next = s->next;
        free(s->type_name);
        for (size_t i = 0; i < s->back_edge_count; i++) {
            free(s->back_edge_fields[i]);
        }
        free(s->back_edge_fields);
        free(s);
        s = next;
    }
}

static void free_reuse_candidates(ReuseCandidate* r) {
    while (r) {
        ReuseCandidate* next = r->next;
        free(r->freed_var);
        free(r->type_name);
        free(r);
        r = next;
    }
}

static void free_regions(RegionInfo* r) {
    while (r) {
        RegionInfo* next = r->next;
        free(r->name);
        for (size_t i = 0; i < r->var_count; i++) {
            free(r->variables[i]);
        }
        free(r->variables);
        free(r);
        r = next;
    }
}

static void free_rc_elision(RCElisionInfo* e) {
    while (e) {
        RCElisionInfo* next = e->next;
        free(e->var_name);
        free(e);
        e = next;
    }
}

static void free_borrows(BorrowInfo* b) {
    while (b) {
        BorrowInfo* next = b->next;
        free(b->borrowed_var);
        free(b->borrow_holder);
        free(b);
        b = next;
    }
}

static void free_tethers(TetherPoint* t) {
    while (t) {
        TetherPoint* next = t->next;
        free(t->tethered_var);
        free(t);
        t = next;
    }
}

static void free_param_summaries(ParamSummary* p) {
    while (p) {
        ParamSummary* next = p->next;
        free(p->name);
        free(p);
        p = next;
    }
}

static void free_function_summaries(FunctionSummary* f) {
    while (f) {
        FunctionSummary* next = f->next;
        free(f->name);
        free_param_summaries(f->params);
        free(f);
        f = next;
    }
}

static void free_thread_locality(ThreadLocalityInfo* t) {
    while (t) {
        ThreadLocalityInfo* next = t->next;
        free(t->var_name);
        free(t);
        t = next;
    }
}

static void free_thread_spawns(ThreadSpawnInfo* s) {
    while (s) {
        ThreadSpawnInfo* next = s->next;
        free(s->thread_id);
        for (size_t i = 0; i < s->captured_count; i++) {
            free(s->captured_vars[i]);
        }
        free(s->captured_vars);
        free(s->capture_locality);
        free(s);
        s = next;
    }
}

static void free_channel_ops(ChannelOpInfo* c) {
    while (c) {
        ChannelOpInfo* next = c->next;
        free(c->channel_name);
        free(c->value_var);
        free(c);
        c = next;
    }
}

void omni_analysis_free(AnalysisContext* ctx) {
    if (!ctx) return;
    free_var_usages(ctx->var_usages);
    free_escape_info(ctx->escape_info);
    free_owner_info(ctx->owner_info);
    free_shape_info(ctx->shape_info);
    free_reuse_candidates(ctx->reuse_candidates);
    free_regions(ctx->regions);
    free_rc_elision(ctx->rc_elision);
    free_borrows(ctx->borrows);
    free_tethers(ctx->tethers);
    free_function_summaries(ctx->function_summaries);
    free_thread_locality(ctx->thread_locality);
    free_thread_spawns(ctx->thread_spawns);
    free_channel_ops(ctx->channel_ops);
    free(ctx);
}

/* ============== Variable Usage Tracking ============== */

static VarUsage* find_or_create_var_usage(AnalysisContext* ctx, const char* name) {
    for (VarUsage* u = ctx->var_usages; u; u = u->next) {
        if (strcmp(u->name, name) == 0) return u;
    }

    VarUsage* u = malloc(sizeof(VarUsage));
    u->name = strdup(name);
    u->flags = VAR_USAGE_NONE;
    u->first_use = -1;
    u->last_use = -1;
    u->def_pos = -1;
    u->is_param = false;
    u->next = ctx->var_usages;
    ctx->var_usages = u;
    return u;
}

static void mark_var_read(AnalysisContext* ctx, const char* name) {
    VarUsage* u = find_or_create_var_usage(ctx, name);
    u->flags |= VAR_USAGE_READ;
    if (u->first_use < 0) u->first_use = ctx->position;
    u->last_use = ctx->position;
}

static void mark_var_write(AnalysisContext* ctx, const char* name) {
    VarUsage* u = find_or_create_var_usage(ctx, name);
    u->flags |= VAR_USAGE_WRITE;
    if (u->def_pos < 0) u->def_pos = ctx->position;
}

static void mark_var_captured(AnalysisContext* ctx, const char* name) {
    VarUsage* u = find_or_create_var_usage(ctx, name);
    u->flags |= VAR_USAGE_CAPTURED;
}

static void mark_var_escaped(AnalysisContext* ctx, const char* name) {
    VarUsage* u = find_or_create_var_usage(ctx, name);
    u->flags |= VAR_USAGE_ESCAPED;
}

/* ============== Escape Analysis ============== */

static EscapeInfo* find_or_create_escape_info(AnalysisContext* ctx, const char* name) {
    for (EscapeInfo* e = ctx->escape_info; e; e = e->next) {
        if (strcmp(e->name, name) == 0) return e;
    }

    EscapeInfo* e = malloc(sizeof(EscapeInfo));
    e->name = strdup(name);
    e->escape_class = ESCAPE_NONE;
    e->is_unique = true;
    e->next = ctx->escape_info;
    ctx->escape_info = e;
    return e;
}

static void set_escape_class(AnalysisContext* ctx, const char* name, EscapeClass ec) {
    EscapeInfo* e = find_or_create_escape_info(ctx, name);
    /* Take the maximum escape class */
    if (ec > e->escape_class) {
        e->escape_class = ec;
    }
}

/* ============== Ownership Analysis ============== */

static OwnerInfo* find_or_create_owner_info(AnalysisContext* ctx, const char* name) {
    for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
        if (strcmp(o->name, name) == 0) return o;
    }

    OwnerInfo* o = malloc(sizeof(OwnerInfo));
    o->name = strdup(name);
    o->ownership = OWNER_LOCAL;
    o->must_free = true;
    o->free_pos = -1;
    o->is_unique = true;      /* Assume unique until proven otherwise */
    o->shape = SHAPE_UNKNOWN; /* Will be refined by shape analysis */
    o->alloc_strategy = ALLOC_HEAP; /* Default to heap, refined by escape analysis */
    o->next = ctx->owner_info;
    ctx->owner_info = o;
    return o;
}

/* ============== Expression Analysis ============== */

static void analyze_expr(AnalysisContext* ctx, OmniValue* expr);

static void analyze_symbol(AnalysisContext* ctx, OmniValue* expr) {
    const char* name = expr->str_val;
    mark_var_read(ctx, name);

    if (ctx->in_lambda) {
        /* Variable might be captured by closure */
        VarUsage* u = find_or_create_var_usage(ctx, name);
        if (u->def_pos < 0 || ctx->scope_depth > 0) {
            /* Defined in outer scope - captured */
            mark_var_captured(ctx, name);
            set_escape_class(ctx, name, ESCAPE_CLOSURE);
        }
    }

    if (ctx->in_return_position) {
        mark_var_escaped(ctx, name);
        set_escape_class(ctx, name, ESCAPE_RETURN);
    }

    ctx->position++;
}

static void analyze_define(AnalysisContext* ctx, OmniValue* expr) {
    /* (define name value) or (define (name params...) body) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    OmniValue* name_or_sig = omni_car(args);
    OmniValue* body = omni_cdr(args);

    if (omni_is_sym(name_or_sig)) {
        /* Simple define: (define x value) */
        mark_var_write(ctx, name_or_sig->str_val);
        ctx->position++;

        if (!omni_is_nil(body)) {
            analyze_expr(ctx, omni_car(body));
        }
    } else if (omni_is_cell(name_or_sig)) {
        /* Function define: (define (f x y) body) */
        OmniValue* fname = omni_car(name_or_sig);
        if (omni_is_sym(fname)) {
            mark_var_write(ctx, fname->str_val);
        }
        ctx->position++;

        /* Mark parameters */
        OmniValue* params = omni_cdr(name_or_sig);
        while (!omni_is_nil(params) && omni_is_cell(params)) {
            OmniValue* param = omni_car(params);
            if (omni_is_sym(param)) {
                VarUsage* u = find_or_create_var_usage(ctx, param->str_val);
                u->is_param = true;
                u->def_pos = ctx->position;
                ctx->position++;
            }
            params = omni_cdr(params);
        }

        /* Analyze body in return position */
        bool old_return_pos = ctx->in_return_position;
        ctx->in_return_position = true;
        ctx->scope_depth++;

        while (!omni_is_nil(body) && omni_is_cell(body)) {
            /* Last expr is in return position */
            ctx->in_return_position = omni_is_nil(omni_cdr(body));
            analyze_expr(ctx, omni_car(body));
            body = omni_cdr(body);
        }

        ctx->scope_depth--;
        ctx->in_return_position = old_return_pos;
    }
}

static void analyze_let(AnalysisContext* ctx, OmniValue* expr) {
    /* (let ((x val) (y val)) body...) or (let [x val y val] body...) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    OmniValue* bindings = omni_car(args);
    OmniValue* body = omni_cdr(args);

    ctx->scope_depth++;

    /* Handle array-style bindings [x 1 y 2] */
    if (omni_is_array(bindings)) {
        for (size_t i = 0; i + 1 < bindings->array.len; i += 2) {
            OmniValue* name = bindings->array.data[i];
            OmniValue* val = bindings->array.data[i + 1];
            if (omni_is_sym(name)) {
                mark_var_write(ctx, name->str_val);
                ctx->position++;
            }
            analyze_expr(ctx, val);
        }
    }
    /* Handle list-style bindings ((x 1) (y 2)) */
    else if (omni_is_cell(bindings)) {
        while (!omni_is_nil(bindings) && omni_is_cell(bindings)) {
            OmniValue* binding = omni_car(bindings);
            if (omni_is_cell(binding)) {
                OmniValue* name = omni_car(binding);
                OmniValue* val = omni_car(omni_cdr(binding));
                if (omni_is_sym(name)) {
                    mark_var_write(ctx, name->str_val);
                    ctx->position++;
                }
                if (val) analyze_expr(ctx, val);
            }
            bindings = omni_cdr(bindings);
        }
    }

    /* Analyze body */
    bool old_return_pos = ctx->in_return_position;
    while (!omni_is_nil(body) && omni_is_cell(body)) {
        ctx->in_return_position = old_return_pos && omni_is_nil(omni_cdr(body));
        analyze_expr(ctx, omni_car(body));
        body = omni_cdr(body);
    }
    ctx->in_return_position = old_return_pos;

    ctx->scope_depth--;
}

static void analyze_lambda(AnalysisContext* ctx, OmniValue* expr) {
    /* (lambda (params...) body...) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    OmniValue* params = omni_car(args);
    OmniValue* body = omni_cdr(args);

    bool old_in_lambda = ctx->in_lambda;
    bool old_return_pos = ctx->in_return_position;
    ctx->in_lambda = true;
    ctx->scope_depth++;

    /* Mark parameters */
    if (omni_is_cell(params)) {
        while (!omni_is_nil(params) && omni_is_cell(params)) {
            OmniValue* param = omni_car(params);
            if (omni_is_sym(param)) {
                VarUsage* u = find_or_create_var_usage(ctx, param->str_val);
                u->is_param = true;
                u->def_pos = ctx->position;
                ctx->position++;
            }
            params = omni_cdr(params);
        }
    } else if (omni_is_array(params)) {
        for (size_t i = 0; i < params->array.len; i++) {
            OmniValue* param = params->array.data[i];
            if (omni_is_sym(param)) {
                VarUsage* u = find_or_create_var_usage(ctx, param->str_val);
                u->is_param = true;
                u->def_pos = ctx->position;
                ctx->position++;
            }
        }
    }

    /* Analyze body */
    while (!omni_is_nil(body) && omni_is_cell(body)) {
        ctx->in_return_position = omni_is_nil(omni_cdr(body));
        analyze_expr(ctx, omni_car(body));
        body = omni_cdr(body);
    }

    ctx->scope_depth--;
    ctx->in_lambda = old_in_lambda;
    ctx->in_return_position = old_return_pos;
}

static void analyze_if(AnalysisContext* ctx, OmniValue* expr) {
    /* (if cond then else) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    OmniValue* cond = omni_car(args);
    args = omni_cdr(args);
    OmniValue* then_branch = omni_is_nil(args) ? NULL : omni_car(args);
    args = omni_cdr(args);
    OmniValue* else_branch = omni_is_nil(args) ? NULL : omni_car(args);

    bool old_return_pos = ctx->in_return_position;
    ctx->in_return_position = false;
    analyze_expr(ctx, cond);

    ctx->in_return_position = old_return_pos;
    if (then_branch) analyze_expr(ctx, then_branch);
    if (else_branch) analyze_expr(ctx, else_branch);
}

static void analyze_application(AnalysisContext* ctx, OmniValue* expr) {
    /* (func arg1 arg2 ...) */
    OmniValue* func = omni_car(expr);
    OmniValue* args = omni_cdr(expr);

    bool old_return_pos = ctx->in_return_position;
    ctx->in_return_position = false;

    analyze_expr(ctx, func);

    /* Arguments escape to the function */
    while (!omni_is_nil(args) && omni_is_cell(args)) {
        OmniValue* arg = omni_car(args);
        analyze_expr(ctx, arg);

        /* Mark as escaping via argument */
        if (omni_is_sym(arg)) {
            set_escape_class(ctx, arg->str_val, ESCAPE_ARG);
        }

        args = omni_cdr(args);
    }

    ctx->in_return_position = old_return_pos;
    ctx->position++;
}

static void analyze_list(AnalysisContext* ctx, OmniValue* expr) {
    if (omni_is_nil(expr)) return;

    OmniValue* head = omni_car(expr);

    /* Check for special forms */
    if (omni_is_sym(head)) {
        const char* name = head->str_val;

        if (strcmp(name, "define") == 0) {
            analyze_define(ctx, expr);
            return;
        }
        if (strcmp(name, "let") == 0 || strcmp(name, "let*") == 0 ||
            strcmp(name, "letrec") == 0) {
            analyze_let(ctx, expr);
            return;
        }
        if (strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0) {
            analyze_lambda(ctx, expr);
            return;
        }
        if (strcmp(name, "if") == 0) {
            analyze_if(ctx, expr);
            return;
        }
        if (strcmp(name, "quote") == 0) {
            /* Quoted data - no analysis needed */
            ctx->position++;
            return;
        }
        if (strcmp(name, "set!") == 0) {
            OmniValue* args = omni_cdr(expr);
            if (!omni_is_nil(args)) {
                OmniValue* target = omni_car(args);
                if (omni_is_sym(target)) {
                    mark_var_write(ctx, target->str_val);
                }
                args = omni_cdr(args);
                if (!omni_is_nil(args)) {
                    analyze_expr(ctx, omni_car(args));
                }
            }
            ctx->position++;
            return;
        }
    }

    /* Regular function application */
    analyze_application(ctx, expr);
}

static void analyze_expr(AnalysisContext* ctx, OmniValue* expr) {
    if (!expr || omni_is_nil(expr)) return;

    switch (expr->tag) {
    case OMNI_INT:
    case OMNI_FLOAT:
    case OMNI_CHAR:
    case OMNI_KEYWORD:
        ctx->position++;
        break;

    case OMNI_SYM:
        analyze_symbol(ctx, expr);
        break;

    case OMNI_CELL:
        analyze_list(ctx, expr);
        break;

    case OMNI_ARRAY:
        for (size_t i = 0; i < expr->array.len; i++) {
            analyze_expr(ctx, expr->array.data[i]);
        }
        break;

    case OMNI_DICT:
        for (size_t i = 0; i < expr->dict.len; i++) {
            analyze_expr(ctx, expr->dict.keys[i]);
            analyze_expr(ctx, expr->dict.values[i]);
        }
        break;

    default:
        ctx->position++;
        break;
    }
}

/* ============== Public API ============== */

void omni_analyze(AnalysisContext* ctx, OmniValue* expr) {
    analyze_expr(ctx, expr);
}

void omni_analyze_program(AnalysisContext* ctx, OmniValue** exprs, size_t count) {
    for (size_t i = 0; i < count; i++) {
        analyze_expr(ctx, exprs[i]);
    }
}

void omni_analyze_liveness(AnalysisContext* ctx, OmniValue* expr) {
    /* Liveness is computed during general analysis */
    analyze_expr(ctx, expr);
}

void omni_analyze_escape(AnalysisContext* ctx, OmniValue* expr) {
    /* Escape is computed during general analysis */
    analyze_expr(ctx, expr);
}

void omni_analyze_ownership(AnalysisContext* ctx, OmniValue* expr) {
    /* First pass: compute usage and escape */
    analyze_expr(ctx, expr);

    /* Second pass: determine ownership based on escape */
    for (VarUsage* u = ctx->var_usages; u; u = u->next) {
        OwnerInfo* o = find_or_create_owner_info(ctx, u->name);

        EscapeInfo* e = NULL;
        for (EscapeInfo* ei = ctx->escape_info; ei; ei = ei->next) {
            if (strcmp(ei->name, u->name) == 0) {
                e = ei;
                break;
            }
        }

        /* Determine uniqueness - non-unique if:
         * 1. Captured by closure (shared with closure env)
         * 2. Escapes via argument (may be aliased)
         * 3. Not marked as is_unique in escape analysis
         */
        o->is_unique = true;
        if (u->flags & VAR_USAGE_CAPTURED) {
            o->is_unique = false;  /* Shared with closure */
        }
        if (e && e->escape_class == ESCAPE_ARG) {
            o->is_unique = false;  /* May be aliased through function call */
        }
        if (e && !e->is_unique) {
            o->is_unique = false;  /* Escape analysis found aliasing */
        }

        /* Determine shape from escape info and type hints */
        if (e) {
            /* Simple heuristic: params tend to be tree-shaped in Lisp */
            o->shape = SHAPE_TREE;
        } else {
            /* Local allocations start as tree-shaped */
            o->shape = SHAPE_TREE;
        }

        if (u->flags & VAR_USAGE_CAPTURED) {
            /* Captured by closure - don't free in parent scope */
            o->ownership = OWNER_TRANSFERRED;
            o->must_free = false;
            o->is_unique = false;  /* Closure shares it */
        } else if (e && e->escape_class >= ESCAPE_RETURN) {
            /* Escapes via return - don't free */
            o->ownership = OWNER_TRANSFERRED;
            o->must_free = false;
        } else if (u->is_param) {
            /* Parameter - borrowed by default */
            o->ownership = OWNER_BORROWED;
            o->must_free = false;
            o->is_unique = false;  /* Caller owns it */
        } else {
            /* Local variable - owned and must free */
            o->ownership = OWNER_LOCAL;
            o->must_free = true;
            o->free_pos = u->last_use;
            /* is_unique stays as determined above */
        }

        /* Determine allocation strategy based on escape class */
        if (e) {
            switch (e->escape_class) {
                case ESCAPE_NONE:
                    /* Stays local - can stack allocate if small enough */
                    o->alloc_strategy = ALLOC_STACK;
                    break;
                case ESCAPE_ARG:
                    /* Escapes via function argument - may be stored
                     * Conservative: use heap unless callee is known to not store */
                    o->alloc_strategy = ALLOC_HEAP;
                    break;
                case ESCAPE_RETURN:
                case ESCAPE_CLOSURE:
                case ESCAPE_GLOBAL:
                    /* Definitely escapes scope - must use heap */
                    o->alloc_strategy = ALLOC_HEAP;
                    break;
            }
        } else {
            /* No escape info - check if captured by closure */
            if (u->flags & VAR_USAGE_CAPTURED) {
                o->alloc_strategy = ALLOC_HEAP;  /* Closure escapes */
            } else if (o->ownership == OWNER_LOCAL && o->is_unique) {
                o->alloc_strategy = ALLOC_STACK;  /* Local unique - can stack */
            } else {
                o->alloc_strategy = ALLOC_HEAP;  /* Conservative default */
            }
        }

        /* Parameters can't be stack-allocated (already exist) */
        if (u->is_param) {
            o->alloc_strategy = ALLOC_HEAP;  /* Not our allocation */
        }
    }
}

/* Back-edge field name patterns (heuristic detection) */
static const char* back_edge_patterns[] = {
    "parent", "prev", "previous", "back", "up", "owner",
    NULL
};

static bool is_back_edge_name(const char* name) {
    for (int i = 0; back_edge_patterns[i]; i++) {
        if (strstr(name, back_edge_patterns[i]) != NULL) {
            return true;
        }
    }
    return false;
}

static ShapeInfo* find_or_create_shape_info(AnalysisContext* ctx, const char* type_name) {
    for (ShapeInfo* s = ctx->shape_info; s; s = s->next) {
        if (strcmp(s->type_name, type_name) == 0) return s;
    }

    ShapeInfo* s = malloc(sizeof(ShapeInfo));
    s->type_name = strdup(type_name);
    s->shape = SHAPE_UNKNOWN;
    s->back_edge_fields = NULL;
    s->back_edge_count = 0;
    s->next = ctx->shape_info;
    ctx->shape_info = s;
    return s;
}

static void add_back_edge_field(ShapeInfo* s, const char* field_name) {
    /* Check if already recorded */
    for (size_t i = 0; i < s->back_edge_count; i++) {
        if (strcmp(s->back_edge_fields[i], field_name) == 0) return;
    }

    s->back_edge_fields = realloc(s->back_edge_fields,
                                  (s->back_edge_count + 1) * sizeof(char*));
    s->back_edge_fields[s->back_edge_count++] = strdup(field_name);
}

void omni_analyze_shape(AnalysisContext* ctx, OmniValue* type_def) {
    /* Analyze a type definition for cyclic references
     *
     * Expected form: (defstruct type-name (field1 type1) (field2 type2) ...)
     * or: (deftype type-name ...)
     */
    if (!omni_is_cell(type_def)) return;

    OmniValue* head = omni_car(type_def);
    if (!omni_is_sym(head)) return;

    const char* form = head->str_val;
    if (strcmp(form, "defstruct") != 0 && strcmp(form, "deftype") != 0) {
        return;  /* Not a type definition */
    }

    OmniValue* rest = omni_cdr(type_def);
    if (!omni_is_cell(rest)) return;

    /* Get type name */
    OmniValue* name_val = omni_car(rest);
    if (!omni_is_sym(name_val)) return;
    const char* type_name = name_val->str_val;

    ShapeInfo* shape = find_or_create_shape_info(ctx, type_name);
    shape->shape = SHAPE_TREE;  /* Start optimistic */

    /* Analyze fields */
    OmniValue* fields = omni_cdr(rest);
    bool has_self_ref = false;
    bool has_back_edge = false;

    while (omni_is_cell(fields)) {
        OmniValue* field_def = omni_car(fields);

        if (omni_is_cell(field_def)) {
            OmniValue* field_name_val = omni_car(field_def);
            OmniValue* field_type_val = omni_cdr(field_def);

            if (omni_is_sym(field_name_val)) {
                const char* field_name = field_name_val->str_val;

                /* Check if this is a back-edge by name pattern */
                if (is_back_edge_name(field_name)) {
                    add_back_edge_field(shape, field_name);
                    has_back_edge = true;
                }

                /* Check for self-reference (same type) */
                if (omni_is_cell(field_type_val)) {
                    OmniValue* ft = omni_car(field_type_val);
                    if (omni_is_sym(ft)) {
                        const char* field_type = ft->str_val;
                        if (strcmp(field_type, type_name) == 0) {
                            has_self_ref = true;
                            /* Self-reference with back-edge name → weak */
                            if (is_back_edge_name(field_name)) {
                                add_back_edge_field(shape, field_name);
                            }
                        }
                    }
                }
            }
        }

        fields = omni_cdr(fields);
    }

    /* Determine final shape */
    if (has_back_edge) {
        /* Has back-edge fields → potentially cyclic but we route to weak */
        shape->shape = SHAPE_CYCLIC;
    } else if (has_self_ref) {
        /* Self-referencing but no back-edge pattern → DAG (shared refs) */
        shape->shape = SHAPE_DAG;
    } else {
        /* No self-references → pure tree */
        shape->shape = SHAPE_TREE;
    }
}

/* Helper: (cadr x) = (car (cdr x)) */
static OmniValue* cadr(OmniValue* v) {
    if (!omni_is_cell(v)) return NULL;
    OmniValue* rest = omni_cdr(v);
    if (!omni_is_cell(rest)) return NULL;
    return omni_car(rest);
}

/* Helper: (caddr x) = (car (cdr (cdr x))) */
static OmniValue* caddr(OmniValue* v) {
    if (!omni_is_cell(v)) return NULL;
    OmniValue* rest = omni_cdr(v);
    if (!omni_is_cell(rest)) return NULL;
    rest = omni_cdr(rest);
    if (!omni_is_cell(rest)) return NULL;
    return omni_car(rest);
}

/* Helper: (cdddr x) = (cdr (cdr (cdr x))) */
static OmniValue* cdddr(OmniValue* v) {
    if (!omni_is_cell(v)) return NULL;
    OmniValue* rest = omni_cdr(v);
    if (!omni_is_cell(rest)) return NULL;
    rest = omni_cdr(rest);
    if (!omni_is_cell(rest)) return NULL;
    return omni_cdr(rest);
}

void omni_analyze_reuse(AnalysisContext* ctx, OmniValue* expr) {
    /* Perceus-style reuse analysis: pair frees with subsequent allocations
     *
     * Algorithm:
     * 1. Walk the expression tree in evaluation order
     * 2. Track when variables are freed (last use)
     * 3. When we see an allocation, check if there's a recent free of same size
     * 4. If so, create a reuse candidate
     */
    if (!expr) return;

    /* For primitives, no reuse analysis needed */
    if (omni_is_int(expr) || omni_is_float(expr) ||
        omni_is_char(expr) || omni_is_nil(expr)) {
        return;
    }

    /* For symbols, check if this is the last use (potential free point) */
    if (omni_is_sym(expr)) {
        /* Tracked in liveness analysis - here we just note the position */
        return;
    }

    /* For cells, analyze the form */
    if (!omni_is_cell(expr)) return;

    OmniValue* head = omni_car(expr);
    if (!omni_is_sym(head)) {
        /* Non-symbol head - just recurse */
        omni_analyze_reuse(ctx, head);
        for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
            omni_analyze_reuse(ctx, omni_car(rest));
        }
        return;
    }

    const char* form = head->str_val;

    /* Handle let bindings - key source of reuse opportunities */
    if (strcmp(form, "let") == 0) {
        OmniValue* bindings = cadr(expr);
        OmniValue* body = caddr(expr);

        /* Analyze each binding */
        for (OmniValue* b = bindings; omni_is_cell(b); b = omni_cdr(b)) {
            OmniValue* binding = omni_car(b);
            if (omni_is_cell(binding)) {
                OmniValue* var = omni_car(binding);
                OmniValue* init = cadr(binding);
                if (omni_is_sym(var)) {
                    /* Analyze init expression */
                    omni_analyze_reuse(ctx, init);

                    /* If init is a constructor (cons, mk_int, etc.), track allocation */
                    if (omni_is_cell(init)) {
                        OmniValue* init_head = omni_car(init);
                        if (omni_is_sym(init_head)) {
                            const char* init_form = init_head->str_val;
                            const char* alloc_type = NULL;

                            if (strcmp(init_form, "cons") == 0 ||
                                strcmp(init_form, "pair") == 0 ||
                                strcmp(init_form, "list") == 0) {
                                alloc_type = "Cell";
                            } else if (strcmp(init_form, "mk-int") == 0 ||
                                       strcmp(init_form, "int") == 0) {
                                alloc_type = "Int";
                            } else if (strcmp(init_form, "mk-float") == 0 ||
                                       strcmp(init_form, "float") == 0) {
                                alloc_type = "Float";
                            }

                            if (alloc_type) {
                                int alloc_pos = ctx->position++;

                                /* Look for a variable that's being freed before this allocation */
                                for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
                                    if (o->must_free && o->free_pos < alloc_pos &&
                                        o->free_pos >= alloc_pos - 3) {
                                        /* Free happens just before alloc - reuse candidate! */
                                        omni_add_reuse_candidate(ctx, o->name, alloc_type, alloc_pos);
                                        break;  /* Only match one */
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        /* Analyze body */
        omni_analyze_reuse(ctx, body);
        return;
    }

    /* Handle if - both branches may have reuse opportunities */
    if (strcmp(form, "if") == 0) {
        omni_analyze_reuse(ctx, cadr(expr));   /* condition */
        omni_analyze_reuse(ctx, caddr(expr));  /* then */
        OmniValue* else_part = cdddr(expr);
        if (else_part && omni_is_cell(else_part)) {
            omni_analyze_reuse(ctx, omni_car(else_part));  /* else */
        }
        return;
    }

    /* Handle lambda - analyze body */
    if (strcmp(form, "lambda") == 0 || strcmp(form, "fn") == 0) {
        OmniValue* body = caddr(expr);
        omni_analyze_reuse(ctx, body);
        return;
    }

    /* Default: recurse on all subexpressions */
    for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
        omni_analyze_reuse(ctx, omni_car(rest));
    }
}

/* ============== Query Functions ============== */

VarUsage* omni_get_var_usage(AnalysisContext* ctx, const char* name) {
    for (VarUsage* u = ctx->var_usages; u; u = u->next) {
        if (strcmp(u->name, name) == 0) return u;
    }
    return NULL;
}

EscapeClass omni_get_escape_class(AnalysisContext* ctx, const char* name) {
    for (EscapeInfo* e = ctx->escape_info; e; e = e->next) {
        if (strcmp(e->name, name) == 0) return e->escape_class;
    }
    return ESCAPE_NONE;
}

OwnerInfo* omni_get_owner_info(AnalysisContext* ctx, const char* name) {
    for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
        if (strcmp(o->name, name) == 0) return o;
    }
    return NULL;
}

const char* omni_free_strategy_name(FreeStrategy strategy) {
    switch (strategy) {
        case FREE_STRATEGY_NONE:    return "none";
        case FREE_STRATEGY_UNIQUE:  return "unique";
        case FREE_STRATEGY_TREE:    return "tree";
        case FREE_STRATEGY_RC:      return "rc";
        case FREE_STRATEGY_RC_TREE: return "rc_tree";
        default:                    return "unknown";
    }
}

FreeStrategy omni_get_free_strategy(AnalysisContext* ctx, const char* name) {
    OwnerInfo* o = omni_get_owner_info(ctx, name);
    if (!o) return FREE_STRATEGY_NONE;

    /* Borrowed/transferred - never free */
    if (o->ownership == OWNER_BORROWED || o->ownership == OWNER_TRANSFERRED) {
        return FREE_STRATEGY_NONE;
    }

    /* If not must_free, don't emit a free */
    if (!o->must_free) {
        return FREE_STRATEGY_NONE;
    }

    /* Determine strategy based on uniqueness and shape */
    if (o->is_unique) {
        /* Single reference - no need for RC check */
        if (o->shape == SHAPE_TREE || o->shape == SHAPE_SCALAR) {
            return FREE_STRATEGY_UNIQUE;
        } else {
            /* DAG/cyclic but unique - still use free_unique for top level */
            return FREE_STRATEGY_UNIQUE;
        }
    }

    /* Shared ownership always uses RC */
    if (o->ownership == OWNER_SHARED) {
        if (o->shape == SHAPE_TREE) {
            return FREE_STRATEGY_RC_TREE;
        }
        return FREE_STRATEGY_RC;
    }

    /* Local ownership, non-unique */
    switch (o->shape) {
        case SHAPE_SCALAR:
            return FREE_STRATEGY_UNIQUE;  /* Scalars are always "unique" */
        case SHAPE_TREE:
            return FREE_STRATEGY_TREE;
        case SHAPE_DAG:
        case SHAPE_CYCLIC:
            return FREE_STRATEGY_RC;
        default:
            /* Unknown shape - conservative RC */
            return FREE_STRATEGY_RC;
    }
}

const char* omni_alloc_strategy_name(AllocStrategy strategy) {
    switch (strategy) {
        case ALLOC_HEAP:  return "heap";
        case ALLOC_STACK: return "stack";
        case ALLOC_POOL:  return "pool";
        case ALLOC_ARENA: return "arena";
        default:          return "unknown";
    }
}

AllocStrategy omni_get_alloc_strategy(AnalysisContext* ctx, const char* name) {
    OwnerInfo* o = omni_get_owner_info(ctx, name);
    if (!o) return ALLOC_HEAP;  /* Default to heap for unknown */
    return o->alloc_strategy;
}

bool omni_can_stack_alloc(AnalysisContext* ctx, const char* name) {
    /* Check escape class - only ESCAPE_NONE allows stack allocation */
    EscapeClass escape = omni_get_escape_class(ctx, name);
    if (escape != ESCAPE_NONE) {
        return false;
    }

    /* Check ownership - borrowed refs are already allocated elsewhere */
    OwnerInfo* o = omni_get_owner_info(ctx, name);
    if (o && o->ownership == OWNER_BORROWED) {
        return false;
    }

    /* Check if value is used in a way that requires heap
     * (e.g., stored in a data structure that escapes) */
    VarUsage* u = omni_get_var_usage(ctx, name);
    if (u && (u->flags & VAR_USAGE_ESCAPED)) {
        return false;
    }

    return true;
}

bool omni_should_free_at(AnalysisContext* ctx, const char* name, int position) {
    OwnerInfo* o = omni_get_owner_info(ctx, name);
    if (!o) return false;
    return o->must_free && o->free_pos == position;
}

char** omni_get_frees_at(AnalysisContext* ctx, int position, size_t* out_count) {
    size_t count = 0;
    for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
        if (o->must_free && o->free_pos == position) count++;
    }

    if (count == 0) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    char** names = malloc(count * sizeof(char*));
    size_t i = 0;
    for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
        if (o->must_free && o->free_pos == position) {
            names[i++] = o->name;
        }
    }

    if (out_count) *out_count = count;
    return names;
}

bool omni_is_cyclic_type(AnalysisContext* ctx, const char* type_name) {
    for (ShapeInfo* s = ctx->shape_info; s; s = s->next) {
        if (strcmp(s->type_name, type_name) == 0) {
            return s->shape == SHAPE_CYCLIC;
        }
    }
    return false;
}

char** omni_get_back_edge_fields(AnalysisContext* ctx, const char* type_name, size_t* out_count) {
    for (ShapeInfo* s = ctx->shape_info; s; s = s->next) {
        if (strcmp(s->type_name, type_name) == 0) {
            if (out_count) *out_count = s->back_edge_count;
            return s->back_edge_fields;
        }
    }
    if (out_count) *out_count = 0;
    return NULL;
}

bool omni_is_back_edge_field(AnalysisContext* ctx, const char* type_name, const char* field_name) {
    size_t count;
    char** fields = omni_get_back_edge_fields(ctx, type_name, &count);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(fields[i], field_name) == 0) return true;
    }
    return false;
}

ShapeClass omni_get_type_shape(AnalysisContext* ctx, const char* type_name) {
    for (ShapeInfo* s = ctx->shape_info; s; s = s->next) {
        if (strcmp(s->type_name, type_name) == 0) {
            return s->shape;
        }
    }
    return SHAPE_UNKNOWN;
}

bool omni_is_back_edge_pattern(const char* field_name) {
    return is_back_edge_name(field_name);
}

/* ============== Free Point Computation ============== */

FreePoint* omni_compute_free_points(AnalysisContext* ctx, OmniValue* func) {
    /* Reset and analyze the function */
    ctx->position = 0;
    analyze_expr(ctx, func);
    omni_analyze_ownership(ctx, func);

    /* Group variables by free position */
    FreePoint* points = NULL;

    for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
        if (!o->must_free) continue;

        /* Find or create FreePoint for this position */
        FreePoint* fp = NULL;
        for (FreePoint* p = points; p; p = p->next) {
            if (p->position == o->free_pos) {
                fp = p;
                break;
            }
        }

        if (!fp) {
            fp = malloc(sizeof(FreePoint));
            fp->position = o->free_pos;
            fp->vars = NULL;
            fp->var_count = 0;
            fp->next = points;
            points = fp;
        }

        /* Add variable to this free point */
        fp->vars = realloc(fp->vars, (fp->var_count + 1) * sizeof(char*));
        fp->vars[fp->var_count++] = o->name;
    }

    return points;
}

void omni_free_points_free(FreePoint* points) {
    while (points) {
        FreePoint* next = points->next;
        free(points->vars);
        free(points);
        points = next;
    }
}

/* ============== Control Flow Graph Implementation ============== */

static CFGNode* cfg_node_new(int id, int node_type) {
    CFGNode* n = malloc(sizeof(CFGNode));
    if (!n) return NULL;
    memset(n, 0, sizeof(CFGNode));
    n->id = id;
    n->node_type = node_type;
    n->position_start = -1;
    n->position_end = -1;
    return n;
}

static void cfg_node_free(CFGNode* n) {
    if (!n) return;
    free(n->successors);
    free(n->predecessors);
    free(n->uses);
    free(n->defs);
    free(n->live_in);
    free(n->live_out);
    free(n);
}

static void cfg_add_edge(CFGNode* from, CFGNode* to) {
    if (!from || !to) return;

    /* Add successor */
    if (from->succ_count >= from->succ_capacity) {
        size_t new_cap = from->succ_capacity == 0 ? 4 : from->succ_capacity * 2;
        from->successors = realloc(from->successors, new_cap * sizeof(CFGNode*));
        from->succ_capacity = new_cap;
    }
    from->successors[from->succ_count++] = to;

    /* Add predecessor */
    if (to->pred_count >= to->pred_capacity) {
        size_t new_cap = to->pred_capacity == 0 ? 4 : to->pred_capacity * 2;
        to->predecessors = realloc(to->predecessors, new_cap * sizeof(CFGNode*));
        to->pred_capacity = new_cap;
    }
    to->predecessors[to->pred_count++] = from;
}

static void cfg_add_node(CFG* cfg, CFGNode* node) {
    if (!cfg || !node) return;
    if (cfg->node_count >= cfg->node_capacity) {
        size_t new_cap = cfg->node_capacity == 0 ? 16 : cfg->node_capacity * 2;
        cfg->nodes = realloc(cfg->nodes, new_cap * sizeof(CFGNode*));
        cfg->node_capacity = new_cap;
    }
    cfg->nodes[cfg->node_count++] = node;
}

static void cfg_node_add_use(CFGNode* n, const char* var) {
    if (!n || !var) return;
    /* Check if already present */
    for (size_t i = 0; i < n->use_count; i++) {
        if (strcmp(n->uses[i], var) == 0) return;
    }
    n->uses = realloc(n->uses, (n->use_count + 1) * sizeof(char*));
    n->uses[n->use_count++] = strdup(var);
}

static void cfg_node_add_def(CFGNode* n, const char* var) {
    if (!n || !var) return;
    /* Check if already present */
    for (size_t i = 0; i < n->def_count; i++) {
        if (strcmp(n->defs[i], var) == 0) return;
    }
    n->defs = realloc(n->defs, (n->def_count + 1) * sizeof(char*));
    n->defs[n->def_count++] = strdup(var);
}

/* Forward declaration for recursive CFG building */
static CFGNode* build_cfg_expr(CFG* cfg, OmniValue* expr, CFGNode* current);

static CFGNode* build_cfg_if(CFG* cfg, OmniValue* expr, CFGNode* entry) {
    /* (if cond then else) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return entry;

    OmniValue* cond = omni_car(args);
    args = omni_cdr(args);
    OmniValue* then_expr = omni_is_nil(args) ? NULL : omni_car(args);
    args = omni_cdr(args);
    OmniValue* else_expr = omni_is_nil(args) ? NULL : omni_car(args);

    /* Create nodes */
    CFGNode* cond_node = cfg_node_new(cfg->node_count, CFG_BRANCH);
    cfg_add_node(cfg, cond_node);
    cfg_add_edge(entry, cond_node);

    /* Build condition (adds uses to cond_node) */
    build_cfg_expr(cfg, cond, cond_node);

    CFGNode* then_entry = cfg_node_new(cfg->node_count, CFG_BASIC);
    cfg_add_node(cfg, then_entry);
    cfg_add_edge(cond_node, then_entry);

    CFGNode* else_entry = cfg_node_new(cfg->node_count, CFG_BASIC);
    cfg_add_node(cfg, else_entry);
    cfg_add_edge(cond_node, else_entry);

    /* Build branches */
    CFGNode* then_exit = then_expr ? build_cfg_expr(cfg, then_expr, then_entry) : then_entry;
    CFGNode* else_exit = else_expr ? build_cfg_expr(cfg, else_expr, else_entry) : else_entry;

    /* Join node */
    CFGNode* join = cfg_node_new(cfg->node_count, CFG_JOIN);
    cfg_add_node(cfg, join);
    cfg_add_edge(then_exit, join);
    cfg_add_edge(else_exit, join);

    return join;
}

static CFGNode* build_cfg_let(CFG* cfg, OmniValue* expr, CFGNode* entry) {
    /* (let ((x val) (y val)) body...) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return entry;

    OmniValue* bindings = omni_car(args);
    OmniValue* body = omni_cdr(args);

    CFGNode* current = entry;

    /* Process bindings */
    if (omni_is_array(bindings)) {
        for (size_t i = 0; i + 1 < bindings->array.len; i += 2) {
            OmniValue* name = bindings->array.data[i];
            OmniValue* val = bindings->array.data[i + 1];

            CFGNode* bind_node = cfg_node_new(cfg->node_count, CFG_BASIC);
            cfg_add_node(cfg, bind_node);
            cfg_add_edge(current, bind_node);

            if (omni_is_sym(name)) {
                cfg_node_add_def(bind_node, name->str_val);
            }
            build_cfg_expr(cfg, val, bind_node);
            current = bind_node;
        }
    } else if (omni_is_cell(bindings)) {
        while (!omni_is_nil(bindings) && omni_is_cell(bindings)) {
            OmniValue* binding = omni_car(bindings);
            if (omni_is_cell(binding)) {
                OmniValue* name = omni_car(binding);
                OmniValue* val = omni_car(omni_cdr(binding));

                CFGNode* bind_node = cfg_node_new(cfg->node_count, CFG_BASIC);
                cfg_add_node(cfg, bind_node);
                cfg_add_edge(current, bind_node);

                if (omni_is_sym(name)) {
                    cfg_node_add_def(bind_node, name->str_val);
                }
                if (val) build_cfg_expr(cfg, val, bind_node);
                current = bind_node;
            }
            bindings = omni_cdr(bindings);
        }
    }

    /* Process body */
    while (!omni_is_nil(body) && omni_is_cell(body)) {
        current = build_cfg_expr(cfg, omni_car(body), current);
        body = omni_cdr(body);
    }

    return current;
}

static CFGNode* build_cfg_lambda(CFG* cfg, OmniValue* expr, CFGNode* entry) {
    /* Lambda creates a closure - the body is analyzed separately */
    /* For now, just treat it as a value (no control flow) */
    (void)cfg;
    (void)expr;
    return entry;
}

static CFGNode* build_cfg_define(CFG* cfg, OmniValue* expr, CFGNode* entry) {
    /* (define name value) or (define (name params...) body) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return entry;

    OmniValue* name_or_sig = omni_car(args);
    OmniValue* body = omni_cdr(args);

    CFGNode* def_node = cfg_node_new(cfg->node_count, CFG_BASIC);
    cfg_add_node(cfg, def_node);
    cfg_add_edge(entry, def_node);

    if (omni_is_sym(name_or_sig)) {
        cfg_node_add_def(def_node, name_or_sig->str_val);
        if (!omni_is_nil(body)) {
            build_cfg_expr(cfg, omni_car(body), def_node);
        }
    } else if (omni_is_cell(name_or_sig)) {
        OmniValue* fname = omni_car(name_or_sig);
        if (omni_is_sym(fname)) {
            cfg_node_add_def(def_node, fname->str_val);
        }
        /* Function body is analyzed separately */
    }

    return def_node;
}

static CFGNode* build_cfg_application(CFG* cfg, OmniValue* expr, CFGNode* entry) {
    /* (func arg1 arg2 ...) */
    OmniValue* func = omni_car(expr);
    OmniValue* args = omni_cdr(expr);

    CFGNode* app_node = cfg_node_new(cfg->node_count, CFG_BASIC);
    cfg_add_node(cfg, app_node);
    cfg_add_edge(entry, app_node);

    /* Add function as use */
    if (omni_is_sym(func)) {
        cfg_node_add_use(app_node, func->str_val);
    }

    /* Add arguments as uses */
    while (!omni_is_nil(args) && omni_is_cell(args)) {
        OmniValue* arg = omni_car(args);
        if (omni_is_sym(arg)) {
            cfg_node_add_use(app_node, arg->str_val);
        }
        args = omni_cdr(args);
    }

    return app_node;
}

static CFGNode* build_cfg_expr(CFG* cfg, OmniValue* expr, CFGNode* current) {
    if (!expr || omni_is_nil(expr)) return current;

    if (omni_is_sym(expr)) {
        /* Variable reference - add as use */
        cfg_node_add_use(current, expr->str_val);
        return current;
    }

    if (!omni_is_cell(expr)) {
        /* Literal value - no control flow */
        return current;
    }

    /* List expression */
    OmniValue* head = omni_car(expr);
    if (omni_is_sym(head)) {
        const char* name = head->str_val;

        if (strcmp(name, "if") == 0) {
            return build_cfg_if(cfg, expr, current);
        }
        if (strcmp(name, "let") == 0 || strcmp(name, "let*") == 0 ||
            strcmp(name, "letrec") == 0) {
            return build_cfg_let(cfg, expr, current);
        }
        if (strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0) {
            return build_cfg_lambda(cfg, expr, current);
        }
        if (strcmp(name, "define") == 0) {
            return build_cfg_define(cfg, expr, current);
        }
        if (strcmp(name, "quote") == 0) {
            return current;  /* Quoted data - no control flow */
        }
        if (strcmp(name, "set!") == 0) {
            OmniValue* args = omni_cdr(expr);
            if (!omni_is_nil(args)) {
                OmniValue* target = omni_car(args);
                if (omni_is_sym(target)) {
                    cfg_node_add_def(current, target->str_val);
                }
                args = omni_cdr(args);
                if (!omni_is_nil(args)) {
                    build_cfg_expr(cfg, omni_car(args), current);
                }
            }
            return current;
        }
    }

    /* Regular function application */
    return build_cfg_application(cfg, expr, current);
}

CFG* omni_build_cfg(OmniValue* expr) {
    CFG* cfg = malloc(sizeof(CFG));
    if (!cfg) return NULL;
    memset(cfg, 0, sizeof(CFG));

    /* Create entry and exit nodes */
    cfg->entry = cfg_node_new(0, CFG_ENTRY);
    cfg_add_node(cfg, cfg->entry);

    cfg->exit = cfg_node_new(1, CFG_EXIT);
    cfg_add_node(cfg, cfg->exit);

    /* Build CFG for expression */
    CFGNode* last = build_cfg_expr(cfg, expr, cfg->entry);

    /* Connect last node to exit */
    cfg_add_edge(last, cfg->exit);

    return cfg;
}

void omni_cfg_free(CFG* cfg) {
    if (!cfg) return;
    for (size_t i = 0; i < cfg->node_count; i++) {
        cfg_node_free(cfg->nodes[i]);
    }
    free(cfg->nodes);
    free(cfg);
}

/* ============== Liveness Analysis (Backward Dataflow) ============== */

static bool string_set_contains(char** set, size_t count, const char* str) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(set[i], str) == 0) return true;
    }
    return false;
}

static bool string_set_add(char*** set, size_t* count, const char* str) {
    if (string_set_contains(*set, *count, str)) return false;
    *set = realloc(*set, (*count + 1) * sizeof(char*));
    (*set)[(*count)++] = strdup(str);
    return true;
}

void omni_compute_liveness(CFG* cfg, AnalysisContext* ctx) {
    if (!cfg || !ctx) return;

    bool changed = true;
    int iterations = 0;
    const int max_iterations = 1000;  /* Safety limit */

    while (changed && iterations++ < max_iterations) {
        changed = false;

        /* Process nodes in reverse order (approximate reverse postorder) */
        for (size_t i = cfg->node_count; i > 0; i--) {
            CFGNode* n = cfg->nodes[i - 1];

            /* live_out = union of successors' live_in */
            for (size_t s = 0; s < n->succ_count; s++) {
                CFGNode* succ = n->successors[s];
                for (size_t v = 0; v < succ->live_in_count; v++) {
                    if (string_set_add(&n->live_out, &n->live_out_count,
                                       succ->live_in[v])) {
                        changed = true;
                    }
                }
            }

            /* live_in = use(n) ∪ (live_out - def(n)) */
            /* First add uses */
            for (size_t u = 0; u < n->use_count; u++) {
                if (string_set_add(&n->live_in, &n->live_in_count, n->uses[u])) {
                    changed = true;
                }
            }

            /* Then add live_out - def */
            for (size_t v = 0; v < n->live_out_count; v++) {
                const char* var = n->live_out[v];
                bool is_def = false;
                for (size_t d = 0; d < n->def_count; d++) {
                    if (strcmp(n->defs[d], var) == 0) {
                        is_def = true;
                        break;
                    }
                }
                if (!is_def) {
                    if (string_set_add(&n->live_in, &n->live_in_count, var)) {
                        changed = true;
                    }
                }
            }
        }
    }
}

char** omni_get_frees_for_node(CFG* cfg, CFGNode* node,
                                AnalysisContext* ctx, size_t* out_count) {
    (void)cfg;  /* Unused for now */

    if (!node || !ctx) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    /* Variables that die at this node:
     * - In live_in but NOT in live_out
     * - AND must_free is true (from ownership analysis)
     */
    size_t count = 0;
    char** to_free = NULL;

    for (size_t i = 0; i < node->live_in_count; i++) {
        const char* var = node->live_in[i];

        /* Check if in live_out */
        if (string_set_contains(node->live_out, node->live_out_count, var)) {
            continue;  /* Still live - don't free */
        }

        /* Check if we own it */
        OwnerInfo* o = omni_get_owner_info(ctx, var);
        if (o && o->must_free) {
            to_free = realloc(to_free, (count + 1) * sizeof(char*));
            to_free[count++] = (char*)var;
        }
    }

    if (out_count) *out_count = count;
    return to_free;
}

void omni_print_cfg(CFG* cfg) {
    if (!cfg) {
        printf("CFG: (null)\n");
        return;
    }

    printf("CFG with %zu nodes:\n", cfg->node_count);
    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* n = cfg->nodes[i];
        const char* type_names[] = {
            "BASIC", "BRANCH", "JOIN", "LOOP_HEAD", "LOOP_EXIT", "ENTRY", "EXIT"
        };
        printf("  Node %d (%s):\n", n->id, type_names[n->node_type]);

        if (n->def_count > 0) {
            printf("    defs: ");
            for (size_t j = 0; j < n->def_count; j++) {
                printf("%s ", n->defs[j]);
            }
            printf("\n");
        }
        if (n->use_count > 0) {
            printf("    uses: ");
            for (size_t j = 0; j < n->use_count; j++) {
                printf("%s ", n->uses[j]);
            }
            printf("\n");
        }
        if (n->live_in_count > 0) {
            printf("    live_in: ");
            for (size_t j = 0; j < n->live_in_count; j++) {
                printf("%s ", n->live_in[j]);
            }
            printf("\n");
        }
        if (n->live_out_count > 0) {
            printf("    live_out: ");
            for (size_t j = 0; j < n->live_out_count; j++) {
                printf("%s ", n->live_out[j]);
            }
            printf("\n");
        }
        if (n->succ_count > 0) {
            printf("    -> ");
            for (size_t j = 0; j < n->succ_count; j++) {
                printf("%d ", n->successors[j]->id);
            }
            printf("\n");
        }
    }
}

CFGFreePoint* omni_compute_cfg_free_points(CFG* cfg, AnalysisContext* ctx) {
    if (!cfg || !ctx) return NULL;

    CFGFreePoint* points = NULL;

    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];
        size_t count;
        char** to_free = omni_get_frees_for_node(cfg, node, ctx, &count);

        if (count > 0) {
            CFGFreePoint* fp = malloc(sizeof(CFGFreePoint));
            fp->node = node;
            fp->vars = to_free;
            fp->var_count = count;
            fp->next = points;
            points = fp;
        }
    }

    return points;
}

void omni_cfg_free_points_free(CFGFreePoint* points) {
    while (points) {
        CFGFreePoint* next = points->next;
        free(points->vars);  /* Don't free individual strings - they're owned by live_in */
        free(points);
        points = next;
    }
}

/* ============== Perceus Reuse Analysis ============== */

size_t omni_type_size(const char* type_name) {
    /* Return size class for types - Perceus matches by size class, not exact size */
    if (!type_name) return 0;

    /* Primitive types - typically 16-32 bytes with header */
    if (strcmp(type_name, "Int") == 0 ||
        strcmp(type_name, "int") == 0 ||
        strcmp(type_name, "long") == 0) {
        return 24;  /* Header (8) + int64 (8) + padding (8) */
    }
    if (strcmp(type_name, "Float") == 0 ||
        strcmp(type_name, "float") == 0 ||
        strcmp(type_name, "double") == 0) {
        return 24;  /* Header (8) + double (8) + padding (8) */
    }
    if (strcmp(type_name, "Bool") == 0 ||
        strcmp(type_name, "bool") == 0) {
        return 16;  /* Header (8) + bool (1) + padding (7) */
    }
    if (strcmp(type_name, "Char") == 0 ||
        strcmp(type_name, "char") == 0) {
        return 16;  /* Header (8) + char (1) + padding (7) */
    }

    /* Pair/Cons cells - two pointers */
    if (strcmp(type_name, "Cell") == 0 ||
        strcmp(type_name, "Cons") == 0 ||
        strcmp(type_name, "Pair") == 0 ||
        strcmp(type_name, "cons") == 0) {
        return 32;  /* Header (8) + car (8) + cdr (8) + padding (8) */
    }

    /* String - variable, use small size class */
    if (strcmp(type_name, "String") == 0 ||
        strcmp(type_name, "Sym") == 0 ||
        strcmp(type_name, "Symbol") == 0 ||
        strcmp(type_name, "string") == 0) {
        return 32;  /* Header (8) + ptr (8) + len (8) + padding (8) */
    }

    /* Default size class */
    return 32;
}

bool omni_can_reuse_for(AnalysisContext* ctx, const char* freed_var,
                        const char* new_type) {
    (void)ctx;  /* May use context for type lookup in future */

    /* Get the type/shape of the freed variable */
    /* For now, match by size class */
    OwnerInfo* o = omni_get_owner_info(ctx, freed_var);
    if (!o) return false;

    /* Can't reuse borrowed or transferred ownership */
    if (o->ownership == OWNER_BORROWED || o->ownership == OWNER_TRANSFERRED) {
        return false;
    }

    /* Can't reuse if not unique (shared ownership) */
    if (!o->is_unique && o->ownership == OWNER_SHARED) {
        return false;
    }

    /* Size class matching - Perceus allows reuse of same-size allocations */
    /* We use the shape to infer the type of freed_var */
    size_t freed_size = 0;
    switch (o->shape) {
        case SHAPE_SCALAR:
            freed_size = 24;  /* Int/Float/Bool/Char */
            break;
        case SHAPE_TREE:
        case SHAPE_DAG:
        case SHAPE_CYCLIC:
            freed_size = 32;  /* Cons cells, structures */
            break;
        default:
            freed_size = 32;  /* Default */
            break;
    }

    size_t new_size = omni_type_size(new_type);
    return freed_size == new_size;
}

void omni_add_reuse_candidate(AnalysisContext* ctx, const char* freed_var,
                              const char* alloc_type, int alloc_pos) {
    /* Find the free position for this variable */
    OwnerInfo* o = omni_get_owner_info(ctx, freed_var);
    if (!o || !o->must_free) return;

    /* Check if size classes match */
    if (!omni_can_reuse_for(ctx, freed_var, alloc_type)) return;

    /* Create reuse candidate */
    ReuseCandidate* rc = malloc(sizeof(ReuseCandidate));
    rc->alloc_pos = alloc_pos;
    rc->free_pos = o->free_pos;
    rc->freed_var = strdup(freed_var);
    rc->type_name = strdup(alloc_type);
    rc->size = omni_type_size(alloc_type);
    rc->can_reuse = true;
    rc->is_consumed = false;
    rc->next = ctx->reuse_candidates;
    ctx->reuse_candidates = rc;
}

ReuseCandidate* omni_get_reuse_at(AnalysisContext* ctx, int alloc_pos) {
    /* Find an available reuse candidate for this allocation position */
    for (ReuseCandidate* rc = ctx->reuse_candidates; rc; rc = rc->next) {
        if (rc->alloc_pos == alloc_pos && rc->can_reuse && !rc->is_consumed) {
            return rc;
        }
    }
    return NULL;
}

/* Mark a reuse candidate as consumed */
void omni_consume_reuse(ReuseCandidate* rc) {
    if (rc) {
        rc->is_consumed = true;
    }
}

/* ============== Region Analysis ============== */

RegionInfo* omni_region_new(AnalysisContext* ctx, const char* name) {
    RegionInfo* r = malloc(sizeof(RegionInfo));
    r->region_id = ctx->next_region_id++;
    r->name = name ? strdup(name) : NULL;
    r->scope_depth = ctx->scope_depth;
    r->start_pos = ctx->position;
    r->end_pos = -1;  /* Set when region ends */
    r->variables = NULL;
    r->var_count = 0;
    r->var_capacity = 0;
    r->external_refcount = 0;
    r->has_escaping_refs = false;
    r->parent = ctx->current_region;
    r->next = ctx->regions;
    ctx->regions = r;
    ctx->current_region = r;
    return r;
}

void omni_region_end(AnalysisContext* ctx) {
    if (ctx->current_region) {
        ctx->current_region->end_pos = ctx->position;
        ctx->current_region = ctx->current_region->parent;
    }
}

void omni_region_add_var(AnalysisContext* ctx, const char* var_name) {
    if (!ctx->current_region) {
        /* Create a default region if none exists */
        omni_region_new(ctx, "default");
    }

    RegionInfo* r = ctx->current_region;

    /* Grow array if needed */
    if (r->var_count >= r->var_capacity) {
        r->var_capacity = r->var_capacity ? r->var_capacity * 2 : 8;
        r->variables = realloc(r->variables, r->var_capacity * sizeof(char*));
    }

    r->variables[r->var_count++] = strdup(var_name);
}

RegionInfo* omni_get_var_region(AnalysisContext* ctx, const char* var_name) {
    for (RegionInfo* r = ctx->regions; r; r = r->next) {
        for (size_t i = 0; i < r->var_count; i++) {
            if (strcmp(r->variables[i], var_name) == 0) {
                return r;
            }
        }
    }
    return NULL;
}

bool omni_same_region(AnalysisContext* ctx, const char* var1, const char* var2) {
    RegionInfo* r1 = omni_get_var_region(ctx, var1);
    RegionInfo* r2 = omni_get_var_region(ctx, var2);

    if (!r1 || !r2) return false;
    return r1->region_id == r2->region_id;
}

/* ============== Per-Region External Refcount ============== */

RegionInfo* omni_get_region_by_id(AnalysisContext* ctx, int region_id) {
    for (RegionInfo* r = ctx->regions; r; r = r->next) {
        if (r->region_id == region_id) {
            return r;
        }
    }
    return NULL;
}

void omni_region_inc_external(AnalysisContext* ctx, int region_id) {
    RegionInfo* r = omni_get_region_by_id(ctx, region_id);
    if (r) {
        r->external_refcount++;
    }
}

void omni_region_dec_external(AnalysisContext* ctx, int region_id) {
    RegionInfo* r = omni_get_region_by_id(ctx, region_id);
    if (r && r->external_refcount > 0) {
        r->external_refcount--;
    }
}

int omni_region_get_external(AnalysisContext* ctx, int region_id) {
    RegionInfo* r = omni_get_region_by_id(ctx, region_id);
    return r ? r->external_refcount : 0;
}

bool omni_is_cross_region_ref(AnalysisContext* ctx, const char* src_var, const char* dst_var) {
    RegionInfo* src_region = omni_get_var_region(ctx, src_var);
    RegionInfo* dst_region = omni_get_var_region(ctx, dst_var);

    /* If either isn't in a region, consider it cross-region */
    if (!src_region || !dst_region) return true;

    /* Different regions = cross-region reference */
    return src_region->region_id != dst_region->region_id;
}

void omni_region_mark_escaping(AnalysisContext* ctx, int region_id) {
    RegionInfo* r = omni_get_region_by_id(ctx, region_id);
    if (r) {
        r->has_escaping_refs = true;
    }
}

bool omni_region_can_bulk_free(AnalysisContext* ctx, int region_id) {
    RegionInfo* r = omni_get_region_by_id(ctx, region_id);
    if (!r) return false;

    /* Can bulk free if:
     * 1. No external references
     * 2. No escaping references
     */
    return r->external_refcount == 0 && !r->has_escaping_refs;
}

/* ============== RC Elision Analysis ============== */

const char* omni_rc_elision_name(RCElisionClass elision) {
    switch (elision) {
        case RC_REQUIRED:   return "required";
        case RC_ELIDE_INC:  return "elide_inc";
        case RC_ELIDE_DEC:  return "elide_dec";
        case RC_ELIDE_BOTH: return "elide_both";
        default:            return "unknown";
    }
}

static RCElisionInfo* find_or_create_elision_info(AnalysisContext* ctx, const char* var_name) {
    for (RCElisionInfo* e = ctx->rc_elision; e; e = e->next) {
        if (strcmp(e->var_name, var_name) == 0) return e;
    }

    RCElisionInfo* e = malloc(sizeof(RCElisionInfo));
    e->var_name = strdup(var_name);
    e->elision = RC_REQUIRED;  /* Default: RC required */
    e->region_id = -1;
    e->same_region_refs = false;
    e->next = ctx->rc_elision;
    ctx->rc_elision = e;
    return e;
}

void omni_analyze_rc_elision(AnalysisContext* ctx, OmniValue* expr) {
    /* Analyze RC elision opportunities
     *
     * RC can be elided when:
     * 1. Variable is unique (only reference)
     * 2. Variable is stack-allocated (no heap involvement)
     * 3. All references are within the same region
     * 4. Variable is arena/pool-allocated (bulk free, no individual RC)
     */
    if (!expr) return;

    /* For symbols, check if RC can be elided */
    if (omni_is_sym(expr)) {
        const char* name = expr->str_val;

        /* Check ownership info */
        OwnerInfo* o = omni_get_owner_info(ctx, name);
        if (!o) return;

        RCElisionInfo* e = find_or_create_elision_info(ctx, name);

        /* Get region info */
        RegionInfo* r = omni_get_var_region(ctx, name);
        if (r) {
            e->region_id = r->region_id;
        }

        /* Determine elision class based on ownership and allocation strategy */
        if (o->is_unique) {
            /* Unique reference - can elide both inc and dec */
            e->elision = RC_ELIDE_BOTH;
        } else if (o->alloc_strategy == ALLOC_STACK) {
            /* Stack allocated - can elide both */
            e->elision = RC_ELIDE_BOTH;
        } else if (o->alloc_strategy == ALLOC_ARENA ||
                   o->alloc_strategy == ALLOC_POOL) {
            /* Arena/pool - can elide dec (bulk free) */
            e->elision = RC_ELIDE_DEC;
        } else if (o->ownership == OWNER_BORROWED) {
            /* Borrowed - can elide inc (don't own it) */
            e->elision = RC_ELIDE_INC;
        } else {
            e->elision = RC_REQUIRED;
        }

        return;
    }

    /* For cells, recurse */
    if (!omni_is_cell(expr)) return;

    OmniValue* head = omni_car(expr);

    /* Handle let bindings - create region for let body */
    if (omni_is_sym(head) && strcmp(head->str_val, "let") == 0) {
        /* Create a new region for this let binding */
        omni_region_new(ctx, "let");

        /* Analyze bindings */
        OmniValue* bindings = cadr(expr);
        for (OmniValue* b = bindings; omni_is_cell(b); b = omni_cdr(b)) {
            OmniValue* binding = omni_car(b);
            if (omni_is_cell(binding)) {
                OmniValue* var = omni_car(binding);
                if (omni_is_sym(var)) {
                    /* Add variable to current region */
                    omni_region_add_var(ctx, var->str_val);
                }
                /* Analyze init expression */
                omni_analyze_rc_elision(ctx, cadr(binding));
            }
        }

        /* Analyze body */
        omni_analyze_rc_elision(ctx, caddr(expr));

        /* End region */
        omni_region_end(ctx);
        return;
    }

    /* Default: recurse on all subexpressions */
    omni_analyze_rc_elision(ctx, head);
    for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
        omni_analyze_rc_elision(ctx, omni_car(rest));
    }
}

RCElisionClass omni_get_rc_elision(AnalysisContext* ctx, const char* var_name) {
    for (RCElisionInfo* e = ctx->rc_elision; e; e = e->next) {
        if (strcmp(e->var_name, var_name) == 0) {
            return e->elision;
        }
    }
    return RC_REQUIRED;  /* Default: RC required */
}

bool omni_can_elide_inc_ref(AnalysisContext* ctx, const char* var_name) {
    RCElisionClass elision = omni_get_rc_elision(ctx, var_name);
    return elision == RC_ELIDE_INC || elision == RC_ELIDE_BOTH;
}

bool omni_can_elide_dec_ref(AnalysisContext* ctx, const char* var_name) {
    RCElisionClass elision = omni_get_rc_elision(ctx, var_name);
    return elision == RC_ELIDE_DEC || elision == RC_ELIDE_BOTH;
}

/* ============== Borrow/Tether Analysis ============== */

const char* omni_borrow_kind_name(BorrowKind kind) {
    switch (kind) {
        case BORROW_NONE:      return "none";
        case BORROW_SHARED:    return "shared";
        case BORROW_EXCLUSIVE: return "exclusive";
        case BORROW_LOOP:      return "loop";
        default:               return "unknown";
    }
}

void omni_borrow_start(AnalysisContext* ctx, const char* borrowed_var,
                       const char* holder, BorrowKind kind) {
    BorrowInfo* b = malloc(sizeof(BorrowInfo));
    b->borrowed_var = strdup(borrowed_var);
    b->borrow_holder = holder ? strdup(holder) : NULL;
    b->kind = kind;
    b->start_pos = ctx->position;
    b->end_pos = -1;  /* Set when borrow ends */
    b->needs_tether = (kind == BORROW_LOOP);  /* Loop borrows need tethering */
    b->next = ctx->borrows;
    ctx->borrows = b;

    /* Add tether entry if needed */
    if (b->needs_tether) {
        omni_add_tether(ctx, borrowed_var, true);
    }
}

void omni_borrow_end(AnalysisContext* ctx, const char* borrowed_var) {
    for (BorrowInfo* b = ctx->borrows; b; b = b->next) {
        if (strcmp(b->borrowed_var, borrowed_var) == 0 && b->end_pos == -1) {
            b->end_pos = ctx->position;

            /* Add tether exit if needed */
            if (b->needs_tether) {
                omni_add_tether(ctx, borrowed_var, false);
            }
            return;
        }
    }
}

bool omni_is_borrowed(AnalysisContext* ctx, const char* var_name) {
    for (BorrowInfo* b = ctx->borrows; b; b = b->next) {
        if (strcmp(b->borrowed_var, var_name) == 0 && b->end_pos == -1) {
            return true;  /* Active borrow */
        }
    }
    return false;
}

BorrowInfo* omni_get_borrow_info(AnalysisContext* ctx, const char* var_name) {
    for (BorrowInfo* b = ctx->borrows; b; b = b->next) {
        if (strcmp(b->borrowed_var, var_name) == 0) {
            return b;
        }
    }
    return NULL;
}

void omni_add_tether(AnalysisContext* ctx, const char* var_name, bool is_entry) {
    TetherPoint* t = malloc(sizeof(TetherPoint));
    t->position = ctx->position;
    t->tethered_var = strdup(var_name);
    t->is_entry = is_entry;
    t->next = ctx->tethers;
    ctx->tethers = t;
}

TetherPoint** omni_get_tethers_at(AnalysisContext* ctx, int position, size_t* count) {
    /* Count tethers at this position */
    size_t n = 0;
    for (TetherPoint* t = ctx->tethers; t; t = t->next) {
        if (t->position == position) n++;
    }

    if (n == 0) {
        if (count) *count = 0;
        return NULL;
    }

    TetherPoint** result = malloc(n * sizeof(TetherPoint*));
    size_t i = 0;
    for (TetherPoint* t = ctx->tethers; t; t = t->next) {
        if (t->position == position) {
            result[i++] = t;
        }
    }

    if (count) *count = n;
    return result;
}

bool omni_needs_tether(AnalysisContext* ctx, const char* var_name, int position) {
    for (BorrowInfo* b = ctx->borrows; b; b = b->next) {
        if (strcmp(b->borrowed_var, var_name) == 0 &&
            b->needs_tether &&
            b->start_pos <= position &&
            (b->end_pos == -1 || b->end_pos >= position)) {
            return true;
        }
    }
    return false;
}

void omni_analyze_borrows(AnalysisContext* ctx, OmniValue* expr) {
    /* Analyze borrow patterns, especially in loops
     *
     * Patterns we detect:
     * 1. (for-each var coll body) - coll is borrowed for duration of loop
     * 2. (loop ((i 0)) (if (< i (length xs)) ... )) - xs is borrowed
     * 3. (map f xs) - xs is borrowed during map
     */
    if (!expr) return;

    /* Skip primitives */
    if (omni_is_int(expr) || omni_is_float(expr) ||
        omni_is_char(expr) || omni_is_nil(expr) || omni_is_sym(expr)) {
        return;
    }

    if (!omni_is_cell(expr)) return;

    OmniValue* head = omni_car(expr);
    if (!omni_is_sym(head)) {
        /* Recurse on all subexpressions */
        for (OmniValue* rest = expr; omni_is_cell(rest); rest = omni_cdr(rest)) {
            omni_analyze_borrows(ctx, omni_car(rest));
        }
        return;
    }

    const char* form = head->str_val;

    /* Detect loop forms */
    if (strcmp(form, "for-each") == 0 || strcmp(form, "foreach") == 0) {
        /* (for-each var coll body) */
        OmniValue* var = cadr(expr);
        OmniValue* coll = caddr(expr);
        OmniValue* body = caddr(omni_cdr(expr));  /* cadddr */

        if (omni_is_sym(coll)) {
            /* Borrow the collection for the loop */
            ctx->in_loop = true;
            omni_borrow_start(ctx, coll->str_val,
                             omni_is_sym(var) ? var->str_val : NULL,
                             BORROW_LOOP);

            /* Analyze body */
            omni_analyze_borrows(ctx, body);

            /* End borrow */
            omni_borrow_end(ctx, coll->str_val);
            ctx->in_loop = false;
        }
        return;
    }

    if (strcmp(form, "map") == 0 || strcmp(form, "filter") == 0 ||
        strcmp(form, "fold") == 0 || strcmp(form, "reduce") == 0) {
        /* (map f xs) - xs is borrowed */
        OmniValue* args = omni_cdr(expr);
        for (OmniValue* a = args; omni_is_cell(a); a = omni_cdr(a)) {
            OmniValue* arg = omni_car(a);
            if (omni_is_sym(arg)) {
                /* Each collection arg is borrowed */
                omni_borrow_start(ctx, arg->str_val, NULL, BORROW_LOOP);
                omni_borrow_end(ctx, arg->str_val);  /* Implicit end */
            }
        }
        return;
    }

    if (strcmp(form, "while") == 0 || strcmp(form, "loop") == 0) {
        /* Mark as in loop for any borrowed variables */
        ctx->in_loop = true;
        for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
            omni_analyze_borrows(ctx, omni_car(rest));
        }
        ctx->in_loop = false;
        return;
    }

    /* Default: recurse */
    for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
        omni_analyze_borrows(ctx, omni_car(rest));
    }
}

/* ============== Interprocedural Summaries ============== */

const char* omni_param_ownership_name(ParamOwnership ownership) {
    switch (ownership) {
        case PARAM_BORROWED:    return "borrowed";
        case PARAM_CONSUMED:    return "consumed";
        case PARAM_PASSTHROUGH: return "passthrough";
        case PARAM_CAPTURED:    return "captured";
        default:                return "unknown";
    }
}

const char* omni_return_ownership_name(ReturnOwnership ownership) {
    switch (ownership) {
        case RETURN_FRESH:       return "fresh";
        case RETURN_PASSTHROUGH: return "passthrough";
        case RETURN_BORROWED:    return "borrowed";
        case RETURN_NONE:        return "none";
        default:                 return "unknown";
    }
}

static FunctionSummary* find_or_create_function_summary(AnalysisContext* ctx, const char* func_name) {
    for (FunctionSummary* f = ctx->function_summaries; f; f = f->next) {
        if (strcmp(f->name, func_name) == 0) return f;
    }

    FunctionSummary* f = malloc(sizeof(FunctionSummary));
    f->name = strdup(func_name);
    f->params = NULL;
    f->param_count = 0;
    f->return_ownership = RETURN_FRESH;
    f->return_param_index = -1;
    f->allocates = false;
    f->has_side_effects = false;
    f->next = ctx->function_summaries;
    ctx->function_summaries = f;
    return f;
}

static ParamSummary* add_param_summary(FunctionSummary* func, const char* param_name) {
    ParamSummary* p = malloc(sizeof(ParamSummary));
    p->name = strdup(param_name);
    p->ownership = PARAM_BORROWED;  /* Default: borrowed */
    p->passthrough_index = -1;
    p->next = func->params;
    func->params = p;
    func->param_count++;
    return p;
}

static ParamSummary* get_param_by_name(FunctionSummary* func, const char* param_name) {
    for (ParamSummary* p = func->params; p; p = p->next) {
        if (strcmp(p->name, param_name) == 0) return p;
    }
    return NULL;
}

static ParamSummary* get_param_by_index(FunctionSummary* func, int index) {
    int i = func->param_count - 1;  /* Params are prepended, so reverse order */
    for (ParamSummary* p = func->params; p; p = p->next) {
        if (i == index) return p;
        i--;
    }
    return NULL;
}

/* Helper to get param index from name */
static int get_param_index(FunctionSummary* func, const char* param_name) {
    int i = func->param_count - 1;
    for (ParamSummary* p = func->params; p; p = p->next) {
        if (strcmp(p->name, param_name) == 0) return i;
        i--;
    }
    return -1;
}

/* Analyze body to determine parameter usage */
static void analyze_body_for_summary(AnalysisContext* ctx, FunctionSummary* func, OmniValue* body, bool in_return_pos);

static void analyze_body_for_summary(AnalysisContext* ctx, FunctionSummary* func, OmniValue* body, bool in_return_pos) {
    if (!body || omni_is_nil(body)) return;

    /* Symbol - check if it's a parameter being returned */
    if (omni_is_sym(body)) {
        if (in_return_pos) {
            int idx = get_param_index(func, body->str_val);
            if (idx >= 0) {
                /* Parameter is returned directly - passthrough */
                func->return_ownership = RETURN_PASSTHROUGH;
                func->return_param_index = idx;
                ParamSummary* p = get_param_by_name(func, body->str_val);
                if (p) p->ownership = PARAM_PASSTHROUGH;
            }
        }
        return;
    }

    if (!omni_is_cell(body)) return;

    OmniValue* head = omni_car(body);
    if (!omni_is_sym(head)) {
        /* Non-symbol head - recurse */
        for (OmniValue* rest = body; omni_is_cell(rest); rest = omni_cdr(rest)) {
            analyze_body_for_summary(ctx, func, omni_car(rest), false);
        }
        return;
    }

    const char* form = head->str_val;

    /* Check for allocation forms */
    if (strcmp(form, "cons") == 0 || strcmp(form, "list") == 0 ||
        strcmp(form, "vector") == 0 || strcmp(form, "make") == 0 ||
        strcmp(form, "mk-int") == 0 || strcmp(form, "mk-float") == 0 ||
        strcmp(form, "new") == 0) {
        func->allocates = true;
        if (in_return_pos) {
            func->return_ownership = RETURN_FRESH;
        }
    }

    /* Check for side effects */
    if (strcmp(form, "set!") == 0 || strcmp(form, "display") == 0 ||
        strcmp(form, "print") == 0 || strcmp(form, "write") == 0 ||
        strcmp(form, "send!") == 0 || strcmp(form, "put!") == 0) {
        func->has_side_effects = true;
    }

    /* Check for consuming operations - these consume their arguments */
    if (strcmp(form, "free") == 0 || strcmp(form, "free!") == 0) {
        /* Argument is consumed */
        OmniValue* arg = cadr(body);
        if (omni_is_sym(arg)) {
            ParamSummary* p = get_param_by_name(func, arg->str_val);
            if (p) p->ownership = PARAM_CONSUMED;
        }
    }

    /* Handle if - analyze branches */
    if (strcmp(form, "if") == 0) {
        analyze_body_for_summary(ctx, func, cadr(body), false);     /* condition */
        analyze_body_for_summary(ctx, func, caddr(body), in_return_pos);  /* then */
        OmniValue* else_branch = cdddr(body);
        if (omni_is_cell(else_branch)) {
            analyze_body_for_summary(ctx, func, omni_car(else_branch), in_return_pos);
        }
        return;
    }

    /* Handle let - check for captures */
    if (strcmp(form, "let") == 0 || strcmp(form, "let*") == 0) {
        OmniValue* bindings = cadr(body);
        /* Check if any param is captured in a binding */
        for (OmniValue* b = bindings; omni_is_cell(b); b = omni_cdr(b)) {
            OmniValue* binding = omni_car(b);
            if (omni_is_cell(binding)) {
                OmniValue* init = cadr(binding);
                if (omni_is_cell(init)) {
                    OmniValue* init_head = omni_car(init);
                    if (omni_is_sym(init_head) &&
                        (strcmp(init_head->str_val, "lambda") == 0 ||
                         strcmp(init_head->str_val, "fn") == 0)) {
                        /* Lambda - check for captured params */
                        for (OmniValue* rest = init; omni_is_cell(rest); rest = omni_cdr(rest)) {
                            OmniValue* elem = omni_car(rest);
                            if (omni_is_sym(elem)) {
                                ParamSummary* p = get_param_by_name(func, elem->str_val);
                                if (p) p->ownership = PARAM_CAPTURED;
                            }
                        }
                    }
                }
            }
        }
        /* Analyze body */
        OmniValue* let_body = caddr(body);
        analyze_body_for_summary(ctx, func, let_body, in_return_pos);
        return;
    }

    /* Handle lambda - check for captured params */
    if (strcmp(form, "lambda") == 0 || strcmp(form, "fn") == 0) {
        /* Check if any parameter is referenced inside lambda */
        OmniValue* lambda_body = caddr(body);
        for (OmniValue* rest = lambda_body; omni_is_cell(rest); rest = omni_cdr(rest)) {
            OmniValue* elem = omni_car(rest);
            if (omni_is_sym(elem)) {
                ParamSummary* p = get_param_by_name(func, elem->str_val);
                if (p) {
                    p->ownership = PARAM_CAPTURED;
                    func->has_side_effects = true;  /* Closure creation is a side effect */
                }
            }
        }
        return;
    }

    /* Handle begin/progn - last expression is in return position */
    if (strcmp(form, "begin") == 0 || strcmp(form, "progn") == 0) {
        OmniValue* exprs = omni_cdr(body);
        while (omni_is_cell(exprs)) {
            OmniValue* e = omni_car(exprs);
            bool is_last = omni_is_nil(omni_cdr(exprs));
            analyze_body_for_summary(ctx, func, e, is_last && in_return_pos);
            exprs = omni_cdr(exprs);
        }
        return;
    }

    /* Default: recurse on all subexpressions */
    for (OmniValue* rest = omni_cdr(body); omni_is_cell(rest); rest = omni_cdr(rest)) {
        analyze_body_for_summary(ctx, func, omni_car(rest), false);
    }
}

void omni_analyze_function_summary(AnalysisContext* ctx, OmniValue* func_def) {
    /*
     * Analyze a function definition and create its summary.
     *
     * Expected forms:
     * (define (name params...) body...)
     * (define name (lambda (params...) body...))
     * (defn name (params...) body...)
     */
    if (!omni_is_cell(func_def)) return;

    OmniValue* head = omni_car(func_def);
    if (!omni_is_sym(head)) return;

    const char* form = head->str_val;
    const char* func_name = NULL;
    OmniValue* params = NULL;
    OmniValue* body = NULL;

    if (strcmp(form, "define") == 0) {
        OmniValue* name_or_sig = cadr(func_def);

        if (omni_is_cell(name_or_sig)) {
            /* (define (name params...) body...) */
            OmniValue* fname = omni_car(name_or_sig);
            if (!omni_is_sym(fname)) return;
            func_name = fname->str_val;
            params = omni_cdr(name_or_sig);
            body = caddr(func_def);
        } else if (omni_is_sym(name_or_sig)) {
            /* (define name (lambda (params...) body...)) */
            func_name = name_or_sig->str_val;
            OmniValue* val = caddr(func_def);
            if (omni_is_cell(val)) {
                OmniValue* val_head = omni_car(val);
                if (omni_is_sym(val_head) &&
                    (strcmp(val_head->str_val, "lambda") == 0 ||
                     strcmp(val_head->str_val, "fn") == 0)) {
                    params = cadr(val);
                    body = caddr(val);
                }
            }
        }
    } else if (strcmp(form, "defn") == 0) {
        /* (defn name (params...) body...) */
        OmniValue* name_val = cadr(func_def);
        if (!omni_is_sym(name_val)) return;
        func_name = name_val->str_val;
        params = caddr(func_def);
        body = caddr(omni_cdr(func_def));  /* cadddr */
    } else if (strcmp(form, "lambda") == 0 || strcmp(form, "fn") == 0) {
        /* Anonymous lambda - use position-based name */
        func_name = "<lambda>";
        params = cadr(func_def);
        body = caddr(func_def);
    } else {
        return;  /* Not a function definition */
    }

    if (!func_name) return;

    /* Create function summary */
    FunctionSummary* summary = find_or_create_function_summary(ctx, func_name);

    /* Add parameter summaries */
    if (omni_is_cell(params)) {
        for (OmniValue* p = params; omni_is_cell(p); p = omni_cdr(p)) {
            OmniValue* param = omni_car(p);
            if (omni_is_sym(param)) {
                add_param_summary(summary, param->str_val);
            }
        }
    } else if (omni_is_array(params)) {
        for (size_t i = 0; i < params->array.len; i++) {
            OmniValue* param = params->array.data[i];
            if (omni_is_sym(param)) {
                add_param_summary(summary, param->str_val);
            }
        }
    }

    /* Analyze body for ownership patterns */
    if (body) {
        analyze_body_for_summary(ctx, summary, body, true);
    }

    /* If body is nil/empty, return RETURN_NONE */
    if (!body || omni_is_nil(body)) {
        summary->return_ownership = RETURN_NONE;
    }
}

FunctionSummary* omni_get_function_summary(AnalysisContext* ctx, const char* func_name) {
    for (FunctionSummary* f = ctx->function_summaries; f; f = f->next) {
        if (strcmp(f->name, func_name) == 0) return f;
    }
    return NULL;
}

ParamOwnership omni_get_param_ownership(AnalysisContext* ctx, const char* func_name,
                                        const char* param_name) {
    FunctionSummary* f = omni_get_function_summary(ctx, func_name);
    if (!f) return PARAM_BORROWED;  /* Default: borrowed */

    for (ParamSummary* p = f->params; p; p = p->next) {
        if (strcmp(p->name, param_name) == 0) {
            return p->ownership;
        }
    }
    return PARAM_BORROWED;
}

ReturnOwnership omni_get_return_ownership(AnalysisContext* ctx, const char* func_name) {
    FunctionSummary* f = omni_get_function_summary(ctx, func_name);
    if (!f) return RETURN_FRESH;  /* Default: fresh allocation */
    return f->return_ownership;
}

bool omni_function_consumes_param(AnalysisContext* ctx, const char* func_name,
                                  const char* param_name) {
    ParamOwnership ownership = omni_get_param_ownership(ctx, func_name, param_name);
    return ownership == PARAM_CONSUMED;
}

bool omni_caller_should_free_arg(AnalysisContext* ctx, const char* func_name,
                                 int arg_index) {
    FunctionSummary* f = omni_get_function_summary(ctx, func_name);
    if (!f) return true;  /* Default: caller should free (borrowed semantics) */

    ParamSummary* p = get_param_by_index(f, arg_index);
    if (!p) return true;

    /* Caller should NOT free if:
     * - Parameter is consumed (callee frees)
     * - Parameter is captured (callee takes ownership)
     * - Parameter is passthrough (returned to caller)
     */
    switch (p->ownership) {
        case PARAM_CONSUMED:
        case PARAM_CAPTURED:
            return false;  /* Callee takes ownership */
        case PARAM_PASSTHROUGH:
            /* Caller gets it back via return */
            return false;
        case PARAM_BORROWED:
        default:
            return true;  /* Caller keeps ownership */
    }
}

/* ============== Concurrency Ownership Inference ============== */

const char* omni_thread_locality_name(ThreadLocality locality) {
    switch (locality) {
        case THREAD_LOCAL:     return "local";
        case THREAD_SHARED:    return "shared";
        case THREAD_TRANSFER:  return "transfer";
        case THREAD_IMMUTABLE: return "immutable";
        default:               return "unknown";
    }
}

const char* omni_channel_op_name(ChannelOp op) {
    switch (op) {
        case CHAN_SEND:  return "send";
        case CHAN_RECV:  return "recv";
        case CHAN_CLOSE: return "close";
        default:         return "unknown";
    }
}

static ThreadLocalityInfo* find_or_create_locality_info(AnalysisContext* ctx, const char* var_name) {
    for (ThreadLocalityInfo* t = ctx->thread_locality; t; t = t->next) {
        if (strcmp(t->var_name, var_name) == 0) return t;
    }

    ThreadLocalityInfo* t = malloc(sizeof(ThreadLocalityInfo));
    t->var_name = strdup(var_name);
    t->locality = THREAD_LOCAL;  /* Default: thread-local */
    t->thread_id = ctx->current_thread_id;
    t->needs_atomic_rc = false;
    t->is_message = false;
    t->next = ctx->thread_locality;
    ctx->thread_locality = t;
    return t;
}

void omni_mark_thread_local(AnalysisContext* ctx, const char* var_name, int thread_id) {
    ThreadLocalityInfo* t = find_or_create_locality_info(ctx, var_name);
    t->locality = THREAD_LOCAL;
    t->thread_id = thread_id;
    t->needs_atomic_rc = false;
}

void omni_mark_thread_shared(AnalysisContext* ctx, const char* var_name) {
    ThreadLocalityInfo* t = find_or_create_locality_info(ctx, var_name);
    t->locality = THREAD_SHARED;
    t->thread_id = -1;  /* Shared = not bound to single thread */
    t->needs_atomic_rc = true;  /* Shared data needs atomic RC */
}

ThreadLocality omni_get_thread_locality(AnalysisContext* ctx, const char* var_name) {
    for (ThreadLocalityInfo* t = ctx->thread_locality; t; t = t->next) {
        if (strcmp(t->var_name, var_name) == 0) {
            return t->locality;
        }
    }
    return THREAD_LOCAL;  /* Default: thread-local */
}

bool omni_needs_atomic_rc(AnalysisContext* ctx, const char* var_name) {
    for (ThreadLocalityInfo* t = ctx->thread_locality; t; t = t->next) {
        if (strcmp(t->var_name, var_name) == 0) {
            return t->needs_atomic_rc;
        }
    }
    return false;  /* Default: no atomic RC needed */
}

bool omni_is_channel_transferred(AnalysisContext* ctx, const char* var_name) {
    for (ThreadLocalityInfo* t = ctx->thread_locality; t; t = t->next) {
        if (strcmp(t->var_name, var_name) == 0) {
            return t->is_message;
        }
    }
    return false;
}

void omni_record_channel_send(AnalysisContext* ctx, const char* channel,
                              const char* value_var, bool transfers_ownership) {
    ChannelOpInfo* op = malloc(sizeof(ChannelOpInfo));
    op->position = ctx->position;
    op->op = CHAN_SEND;
    op->channel_name = strdup(channel);
    op->value_var = strdup(value_var);
    op->transfers_ownership = transfers_ownership;
    op->next = ctx->channel_ops;
    ctx->channel_ops = op;

    /* Mark the value as transferred if ownership moves */
    if (transfers_ownership) {
        ThreadLocalityInfo* t = find_or_create_locality_info(ctx, value_var);
        t->locality = THREAD_TRANSFER;
        t->is_message = true;
    }
}

void omni_record_channel_recv(AnalysisContext* ctx, const char* channel,
                              const char* value_var) {
    ChannelOpInfo* op = malloc(sizeof(ChannelOpInfo));
    op->position = ctx->position;
    op->op = CHAN_RECV;
    op->channel_name = strdup(channel);
    op->value_var = strdup(value_var);
    op->transfers_ownership = true;  /* Recv always receives ownership */
    op->next = ctx->channel_ops;
    ctx->channel_ops = op;

    /* Mark the value as owned by current thread now */
    ThreadLocalityInfo* t = find_or_create_locality_info(ctx, value_var);
    t->locality = THREAD_LOCAL;
    t->thread_id = ctx->current_thread_id;
    t->is_message = true;  /* Came from a channel */
}

void omni_record_thread_spawn(AnalysisContext* ctx, const char* thread_id,
                              char** captured_vars, size_t count) {
    ThreadSpawnInfo* spawn = malloc(sizeof(ThreadSpawnInfo));
    spawn->spawn_pos = ctx->position;
    spawn->thread_id = strdup(thread_id);
    spawn->captured_count = count;
    spawn->captured_vars = NULL;
    spawn->capture_locality = NULL;

    if (count > 0) {
        spawn->captured_vars = malloc(count * sizeof(char*));
        spawn->capture_locality = malloc(count * sizeof(ThreadLocality));
        for (size_t i = 0; i < count; i++) {
            spawn->captured_vars[i] = strdup(captured_vars[i]);

            /* Captured variables become shared by default */
            ThreadLocalityInfo* t = find_or_create_locality_info(ctx, captured_vars[i]);
            t->locality = THREAD_SHARED;
            t->needs_atomic_rc = true;
            spawn->capture_locality[i] = THREAD_SHARED;
        }
    }

    spawn->next = ctx->thread_spawns;
    ctx->thread_spawns = spawn;
}

bool omni_should_free_after_send(AnalysisContext* ctx, const char* channel,
                                 const char* var_name) {
    /* Look up the send operation */
    for (ChannelOpInfo* op = ctx->channel_ops; op; op = op->next) {
        if (op->op == CHAN_SEND &&
            strcmp(op->channel_name, channel) == 0 &&
            strcmp(op->value_var, var_name) == 0) {
            /* If ownership transfers, don't free after send */
            return !op->transfers_ownership;
        }
    }
    /* Default: ownership transfers, so don't free */
    return false;
}

ThreadSpawnInfo** omni_get_threads_capturing(AnalysisContext* ctx, const char* var_name,
                                             size_t* count) {
    /* Count matching spawns */
    size_t n = 0;
    for (ThreadSpawnInfo* spawn = ctx->thread_spawns; spawn; spawn = spawn->next) {
        for (size_t i = 0; i < spawn->captured_count; i++) {
            if (strcmp(spawn->captured_vars[i], var_name) == 0) {
                n++;
                break;
            }
        }
    }

    if (n == 0) {
        if (count) *count = 0;
        return NULL;
    }

    ThreadSpawnInfo** result = malloc(n * sizeof(ThreadSpawnInfo*));
    size_t idx = 0;
    for (ThreadSpawnInfo* spawn = ctx->thread_spawns; spawn; spawn = spawn->next) {
        for (size_t i = 0; i < spawn->captured_count; i++) {
            if (strcmp(spawn->captured_vars[i], var_name) == 0) {
                result[idx++] = spawn;
                break;
            }
        }
    }

    if (count) *count = n;
    return result;
}

void omni_analyze_concurrency(AnalysisContext* ctx, OmniValue* expr) {
    /*
     * Analyze concurrency patterns:
     * - (spawn ...) or (thread ...) - thread creation
     * - (send! channel value) - channel send
     * - (recv! channel) - channel receive
     * - (go ...) - goroutine-style spawn
     * - (async ...) - async block
     * - (atom ...) - atomic reference
     */
    if (!expr) return;

    /* Skip primitives */
    if (omni_is_int(expr) || omni_is_float(expr) ||
        omni_is_char(expr) || omni_is_nil(expr) || omni_is_sym(expr)) {
        return;
    }

    if (!omni_is_cell(expr)) return;

    OmniValue* head = omni_car(expr);
    if (!omni_is_sym(head)) {
        /* Recurse on non-symbol head */
        for (OmniValue* rest = expr; omni_is_cell(rest); rest = omni_cdr(rest)) {
            omni_analyze_concurrency(ctx, omni_car(rest));
        }
        return;
    }

    const char* form = head->str_val;

    /* Detect thread spawn forms */
    if (strcmp(form, "spawn") == 0 || strcmp(form, "thread") == 0 ||
        strcmp(form, "go") == 0 || strcmp(form, "async") == 0) {

        /* Generate thread ID */
        char thread_id[32];
        static int thread_counter = 0;
        snprintf(thread_id, sizeof(thread_id), "thread_%d", thread_counter++);

        /* Collect captured variables from the body */
        /* For now, just scan for symbols in the body */
        OmniValue* body = omni_cdr(expr);
        char** captured = NULL;
        size_t captured_count = 0;
        size_t captured_cap = 0;

        for (OmniValue* b = body; omni_is_cell(b); b = omni_cdr(b)) {
            OmniValue* e = omni_car(b);
            if (omni_is_sym(e)) {
                /* Check if it's a variable from outer scope */
                VarUsage* u = omni_get_var_usage(ctx, e->str_val);
                if (u && u->def_pos < ctx->position) {
                    /* Variable defined before spawn - captured */
                    if (captured_count >= captured_cap) {
                        captured_cap = captured_cap ? captured_cap * 2 : 8;
                        captured = realloc(captured, captured_cap * sizeof(char*));
                    }
                    captured[captured_count++] = e->str_val;
                }
            }
        }

        omni_record_thread_spawn(ctx, thread_id, captured, captured_count);
        free(captured);

        /* Analyze body in new thread context */
        int old_thread = ctx->current_thread_id;
        ctx->current_thread_id = thread_counter - 1;
        for (OmniValue* b = body; omni_is_cell(b); b = omni_cdr(b)) {
            omni_analyze_concurrency(ctx, omni_car(b));
        }
        ctx->current_thread_id = old_thread;
        return;
    }

    /* Detect channel operations */
    if (strcmp(form, "send!") == 0 || strcmp(form, "chan-send") == 0 ||
        strcmp(form, "put!") == 0) {
        /* (send! channel value) */
        OmniValue* channel = cadr(expr);
        OmniValue* value = caddr(expr);

        if (omni_is_sym(channel) && omni_is_sym(value)) {
            omni_record_channel_send(ctx, channel->str_val, value->str_val, true);
        }
        return;
    }

    if (strcmp(form, "recv!") == 0 || strcmp(form, "chan-recv") == 0 ||
        strcmp(form, "take!") == 0) {
        /* (recv! channel) or (let [x (recv! channel)] ...) */
        OmniValue* channel = cadr(expr);
        if (omni_is_sym(channel)) {
            /* The result will be assigned to a variable - track at let binding */
            omni_record_channel_recv(ctx, channel->str_val, "_recv_result");
        }
        return;
    }

    /* Detect let binding with recv */
    if (strcmp(form, "let") == 0) {
        OmniValue* bindings = cadr(expr);
        for (OmniValue* b = bindings; omni_is_cell(b); b = omni_cdr(b)) {
            OmniValue* binding = omni_car(b);
            if (omni_is_cell(binding)) {
                OmniValue* var = omni_car(binding);
                OmniValue* init = cadr(binding);

                /* Check if init is a recv operation */
                if (omni_is_cell(init)) {
                    OmniValue* init_head = omni_car(init);
                    if (omni_is_sym(init_head)) {
                        const char* init_form = init_head->str_val;
                        if (strcmp(init_form, "recv!") == 0 ||
                            strcmp(init_form, "chan-recv") == 0 ||
                            strcmp(init_form, "take!") == 0) {

                            OmniValue* channel = cadr(init);
                            if (omni_is_sym(var) && omni_is_sym(channel)) {
                                omni_record_channel_recv(ctx, channel->str_val, var->str_val);
                            }
                        }
                    }
                }
            }
        }
        /* Continue analyzing body */
        OmniValue* body = caddr(expr);
        omni_analyze_concurrency(ctx, body);
        return;
    }

    /* Detect atomic operations */
    if (strcmp(form, "atom") == 0 || strcmp(form, "atomic") == 0) {
        /* (atom initial-value) - creates atomic reference */
        OmniValue* init = cadr(expr);
        if (omni_is_sym(init)) {
            /* The atom contents are shared */
            omni_mark_thread_shared(ctx, init->str_val);
        }
        return;
    }

    if (strcmp(form, "swap!") == 0 || strcmp(form, "reset!") == 0 ||
        strcmp(form, "compare-and-swap!") == 0) {
        /* Atomic modification - value becomes shared */
        OmniValue* atom = cadr(expr);
        OmniValue* value = caddr(expr);
        if (omni_is_sym(value)) {
            omni_mark_thread_shared(ctx, value->str_val);
        }
        (void)atom;  /* Atom itself is already marked shared */
        return;
    }

    /* Default: recurse on all subexpressions */
    for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
        omni_analyze_concurrency(ctx, omni_car(rest));
    }
}
