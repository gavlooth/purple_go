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
        free(r->type_name);
        free(r);
        r = next;
    }
}

void omni_analysis_free(AnalysisContext* ctx) {
    if (!ctx) return;
    free_var_usages(ctx->var_usages);
    free_escape_info(ctx->escape_info);
    free_owner_info(ctx->owner_info);
    free_shape_info(ctx->shape_info);
    free_reuse_candidates(ctx->reuse_candidates);
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

        if (u->flags & VAR_USAGE_CAPTURED) {
            /* Captured by closure - don't free in parent scope */
            o->ownership = OWNER_TRANSFERRED;
            o->must_free = false;
        } else if (e && e->escape_class >= ESCAPE_RETURN) {
            /* Escapes via return - don't free */
            o->ownership = OWNER_TRANSFERRED;
            o->must_free = false;
        } else if (u->is_param) {
            /* Parameter - borrowed by default */
            o->ownership = OWNER_BORROWED;
            o->must_free = false;
        } else {
            /* Local variable - owned and must free */
            o->ownership = OWNER_LOCAL;
            o->must_free = true;
            o->free_pos = u->last_use;
        }
    }
}

void omni_analyze_shape(AnalysisContext* ctx, OmniValue* type_def) {
    /* Analyze a type definition for cyclic references */
    if (!omni_is_cell(type_def)) return;

    OmniValue* head = omni_car(type_def);
    if (!omni_is_sym(head)) return;

    /* Look for struct or enum definitions */
    /* TODO: Full implementation */
    (void)ctx;
}

void omni_analyze_reuse(AnalysisContext* ctx, OmniValue* expr) {
    /* TODO: Implement Perceus-style reuse analysis */
    (void)ctx;
    (void)expr;
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
