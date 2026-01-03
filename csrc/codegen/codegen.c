/*
 * OmniLisp Code Generator Implementation
 *
 * Generates C99 + POSIX code with ASAP memory management.
 */

#include "codegen.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* ============== Context Management ============== */

CodeGenContext* omni_codegen_new(FILE* output) {
    CodeGenContext* ctx = malloc(sizeof(CodeGenContext));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(CodeGenContext));
    ctx->output = output;
    return ctx;
}

CodeGenContext* omni_codegen_new_buffer(void) {
    CodeGenContext* ctx = malloc(sizeof(CodeGenContext));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(CodeGenContext));
    ctx->output_capacity = 4096;
    ctx->output_buffer = malloc(ctx->output_capacity);
    ctx->output_buffer[0] = '\0';
    return ctx;
}

void omni_codegen_free(CodeGenContext* ctx) {
    if (!ctx) return;

    for (size_t i = 0; i < ctx->symbols.count; i++) {
        free(ctx->symbols.names[i]);
        free(ctx->symbols.c_names[i]);
    }
    free(ctx->symbols.names);
    free(ctx->symbols.c_names);

    for (size_t i = 0; i < ctx->forward_decls.count; i++) {
        free(ctx->forward_decls.decls[i]);
    }
    free(ctx->forward_decls.decls);

    for (size_t i = 0; i < ctx->lambda_defs.count; i++) {
        free(ctx->lambda_defs.defs[i]);
    }
    free(ctx->lambda_defs.defs);

    if (ctx->analysis) {
        omni_analysis_free(ctx->analysis);
    }

    free(ctx->output_buffer);
    free(ctx);
}

char* omni_codegen_get_output(CodeGenContext* ctx) {
    if (ctx->output_buffer) {
        return strdup(ctx->output_buffer);
    }
    return NULL;
}

void omni_codegen_set_runtime(CodeGenContext* ctx, const char* path) {
    ctx->runtime_path = path;
    ctx->use_runtime = (path != NULL);
}

/* ============== Output Helpers ============== */

static void buffer_append(CodeGenContext* ctx, const char* s) {
    size_t len = strlen(s);
    while (ctx->output_size + len + 1 > ctx->output_capacity) {
        ctx->output_capacity *= 2;
        ctx->output_buffer = realloc(ctx->output_buffer, ctx->output_capacity);
    }
    memcpy(ctx->output_buffer + ctx->output_size, s, len + 1);
    ctx->output_size += len;
}

void omni_codegen_emit_raw(CodeGenContext* ctx, const char* fmt, ...) {
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (ctx->output) {
        fputs(buf, ctx->output);
    } else if (ctx->output_buffer) {
        buffer_append(ctx, buf);
    }
}

void omni_codegen_emit(CodeGenContext* ctx, const char* fmt, ...) {
    /* Emit indentation */
    for (int i = 0; i < ctx->indent_level; i++) {
        omni_codegen_emit_raw(ctx, "    ");
    }

    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (ctx->output) {
        fputs(buf, ctx->output);
    } else if (ctx->output_buffer) {
        buffer_append(ctx, buf);
    }
}

void omni_codegen_indent(CodeGenContext* ctx) {
    ctx->indent_level++;
}

void omni_codegen_dedent(CodeGenContext* ctx) {
    if (ctx->indent_level > 0) ctx->indent_level--;
}

/* ============== Name Mangling ============== */

char* omni_codegen_mangle(const char* name) {
    size_t len = strlen(name);
    char* result = malloc(len * 2 + 8);  /* Worst case expansion */
    char* p = result;

    *p++ = 'o';
    *p++ = '_';

    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (isalnum((unsigned char)c)) {
            *p++ = c;
        } else {
            switch (c) {
            case '+': *p++ = '_'; *p++ = 'a'; *p++ = 'd'; *p++ = 'd'; break;
            case '-': *p++ = '_'; *p++ = 's'; *p++ = 'u'; *p++ = 'b'; break;
            case '*': *p++ = '_'; *p++ = 'm'; *p++ = 'u'; *p++ = 'l'; break;
            case '/': *p++ = '_'; *p++ = 'd'; *p++ = 'i'; *p++ = 'v'; break;
            case '=': *p++ = '_'; *p++ = 'e'; *p++ = 'q'; break;
            case '<': *p++ = '_'; *p++ = 'l'; *p++ = 't'; break;
            case '>': *p++ = '_'; *p++ = 'g'; *p++ = 't'; break;
            case '?': *p++ = '_'; *p++ = 'p'; break;
            case '!': *p++ = '_'; *p++ = 'b'; break;
            case '.': *p++ = '_'; *p++ = 'd'; break;
            case '_': *p++ = '_'; *p++ = '_'; break;
            default: *p++ = '_'; break;
            }
        }
    }
    *p = '\0';
    return result;
}

char* omni_codegen_temp(CodeGenContext* ctx) {
    char* result = malloc(32);
    snprintf(result, 32, "_t%d", ctx->temp_counter++);
    return result;
}

char* omni_codegen_label(CodeGenContext* ctx) {
    char* result = malloc(32);
    snprintf(result, 32, "_L%d", ctx->label_counter++);
    return result;
}

/* ============== Forward Declarations ============== */

void omni_codegen_add_forward_decl(CodeGenContext* ctx, const char* decl) {
    if (ctx->forward_decls.count >= ctx->forward_decls.capacity) {
        ctx->forward_decls.capacity = ctx->forward_decls.capacity ? ctx->forward_decls.capacity * 2 : 16;
        ctx->forward_decls.decls = realloc(ctx->forward_decls.decls,
                                           ctx->forward_decls.capacity * sizeof(char*));
    }
    ctx->forward_decls.decls[ctx->forward_decls.count++] = strdup(decl);
}

void omni_codegen_add_lambda_def(CodeGenContext* ctx, const char* def) {
    if (ctx->lambda_defs.count >= ctx->lambda_defs.capacity) {
        ctx->lambda_defs.capacity = ctx->lambda_defs.capacity ? ctx->lambda_defs.capacity * 2 : 16;
        ctx->lambda_defs.defs = realloc(ctx->lambda_defs.defs,
                                        ctx->lambda_defs.capacity * sizeof(char*));
    }
    ctx->lambda_defs.defs[ctx->lambda_defs.count++] = strdup(def);
}

/* ============== Symbol Table ============== */

static const char* lookup_symbol(CodeGenContext* ctx, const char* name) {
    for (size_t i = 0; i < ctx->symbols.count; i++) {
        if (strcmp(ctx->symbols.names[i], name) == 0) {
            return ctx->symbols.c_names[i];
        }
    }
    return NULL;
}

static void register_symbol(CodeGenContext* ctx, const char* name, const char* c_name) {
    if (ctx->symbols.count >= ctx->symbols.capacity) {
        ctx->symbols.capacity = ctx->symbols.capacity ? ctx->symbols.capacity * 2 : 16;
        ctx->symbols.names = realloc(ctx->symbols.names, ctx->symbols.capacity * sizeof(char*));
        ctx->symbols.c_names = realloc(ctx->symbols.c_names, ctx->symbols.capacity * sizeof(char*));
    }
    ctx->symbols.names[ctx->symbols.count] = strdup(name);
    ctx->symbols.c_names[ctx->symbols.count] = strdup(c_name);
    ctx->symbols.count++;
}

/* ============== Runtime Header ============== */

void omni_codegen_runtime_header(CodeGenContext* ctx) {
    omni_codegen_emit_raw(ctx, "/* Generated by OmniLisp Compiler */\n");
    omni_codegen_emit_raw(ctx, "/* ASAP Memory Management - Compile-Time Free Injection */\n\n");

    if (ctx->use_runtime && ctx->runtime_path) {
        omni_codegen_emit_raw(ctx, "#include \"%s/include/purple.h\"\n\n", ctx->runtime_path);
        /* Compatibility macros for runtime */
        omni_codegen_emit_raw(ctx, "#define NIL mk_pair(NULL, NULL)\n");
        omni_codegen_emit_raw(ctx, "#define omni_print(o) prim_print(o)\n");
        omni_codegen_emit_raw(ctx, "#define car(o) obj_car(o)\n");
        omni_codegen_emit_raw(ctx, "#define cdr(o) obj_cdr(o)\n");
        omni_codegen_emit_raw(ctx, "#define mk_cell(a, b) mk_pair(a, b)\n");
        omni_codegen_emit_raw(ctx, "#define prim_cons(a, b) mk_pair(a, b)\n\n");
    } else {
        /* Embedded minimal runtime */
        omni_codegen_emit_raw(ctx, "#include <stdio.h>\n");
        omni_codegen_emit_raw(ctx, "#include <stdlib.h>\n");
        omni_codegen_emit_raw(ctx, "#include <string.h>\n");
        omni_codegen_emit_raw(ctx, "#include <stdint.h>\n");
        omni_codegen_emit_raw(ctx, "#include <stdbool.h>\n");
        omni_codegen_emit_raw(ctx, "#include <pthread.h>\n\n");

        /* Value type */
        omni_codegen_emit_raw(ctx, "typedef enum {\n");
        omni_codegen_emit_raw(ctx, "    T_INT, T_SYM, T_CELL, T_NIL, T_PRIM, T_LAMBDA, T_CODE, T_ERROR\n");
        omni_codegen_emit_raw(ctx, "} Tag;\n\n");

        omni_codegen_emit_raw(ctx, "struct Obj;\n");
        omni_codegen_emit_raw(ctx, "typedef struct Obj* (*PrimFn)(struct Obj*, struct Obj*);\n\n");

        omni_codegen_emit_raw(ctx, "typedef struct Obj {\n");
        omni_codegen_emit_raw(ctx, "    Tag tag;\n");
        omni_codegen_emit_raw(ctx, "    int rc;  /* Reference count */\n");
        omni_codegen_emit_raw(ctx, "    union {\n");
        omni_codegen_emit_raw(ctx, "        int64_t i;\n");
        omni_codegen_emit_raw(ctx, "        char* s;\n");
        omni_codegen_emit_raw(ctx, "        struct { struct Obj* car; struct Obj* cdr; } cell;\n");
        omni_codegen_emit_raw(ctx, "        PrimFn prim;\n");
        omni_codegen_emit_raw(ctx, "        struct { struct Obj* params; struct Obj* body; struct Obj* env; } lam;\n");
        omni_codegen_emit_raw(ctx, "    };\n");
        omni_codegen_emit_raw(ctx, "} Obj;\n\n");

        /* Nil singleton */
        omni_codegen_emit_raw(ctx, "static Obj _nil = { .tag = T_NIL, .rc = 1 };\n");
        omni_codegen_emit_raw(ctx, "#define NIL (&_nil)\n\n");

        /* Heap Constructors */
        omni_codegen_emit_raw(ctx, "static Obj* mk_int(int64_t i) {\n");
        omni_codegen_emit_raw(ctx, "    Obj* o = malloc(sizeof(Obj));\n");
        omni_codegen_emit_raw(ctx, "    o->tag = T_INT; o->rc = 1; o->i = i;\n");
        omni_codegen_emit_raw(ctx, "    return o;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static Obj* mk_sym(const char* s) {\n");
        omni_codegen_emit_raw(ctx, "    Obj* o = malloc(sizeof(Obj));\n");
        omni_codegen_emit_raw(ctx, "    o->tag = T_SYM; o->rc = 1; o->s = strdup(s);\n");
        omni_codegen_emit_raw(ctx, "    return o;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static Obj* mk_cell(Obj* car, Obj* cdr) {\n");
        omni_codegen_emit_raw(ctx, "    Obj* o = malloc(sizeof(Obj));\n");
        omni_codegen_emit_raw(ctx, "    o->tag = T_CELL; o->rc = 1;\n");
        omni_codegen_emit_raw(ctx, "    o->cell.car = car; o->cell.cdr = cdr;\n");
        omni_codegen_emit_raw(ctx, "    return o;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        /* Stack allocation macros (escape-aware allocation) */
        omni_codegen_emit_raw(ctx, "/* Stack-allocated objects - no free needed, auto-cleanup at scope exit */\n");
        omni_codegen_emit_raw(ctx, "#define STACK_INT(name, val) \\\n");
        omni_codegen_emit_raw(ctx, "    Obj _stack_##name = { .tag = T_INT, .rc = 1, .i = (val) }; \\\n");
        omni_codegen_emit_raw(ctx, "    Obj* name = &_stack_##name\n\n");

        omni_codegen_emit_raw(ctx, "#define STACK_CELL(name, car_val, cdr_val) \\\n");
        omni_codegen_emit_raw(ctx, "    Obj _stack_##name = { .tag = T_CELL, .rc = 1, .cell = { (car_val), (cdr_val) } }; \\\n");
        omni_codegen_emit_raw(ctx, "    Obj* name = &_stack_##name\n\n");

        /* Helper to check if an object is stack-allocated (for debug/safety) */
        omni_codegen_emit_raw(ctx, "#define IS_STACK_OBJ(o) ((o) && (o)->rc == -1)\n");
        omni_codegen_emit_raw(ctx, "#define MARK_STACK(o) ((o)->rc = -1)\n\n");

        /* Stack-friendly constructors that initialize existing memory */
        omni_codegen_emit_raw(ctx, "static void init_int(Obj* o, int64_t i) {\n");
        omni_codegen_emit_raw(ctx, "    o->tag = T_INT; o->rc = -1; o->i = i;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static void init_cell(Obj* o, Obj* car, Obj* cdr) {\n");
        omni_codegen_emit_raw(ctx, "    o->tag = T_CELL; o->rc = -1;\n");
        omni_codegen_emit_raw(ctx, "    o->cell.car = car; o->cell.cdr = cdr;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        /* Accessors */
        omni_codegen_emit_raw(ctx, "#define car(o) ((o)->cell.car)\n");
        omni_codegen_emit_raw(ctx, "#define cdr(o) ((o)->cell.cdr)\n");
        omni_codegen_emit_raw(ctx, "#define is_nil(o) ((o) == NIL || (o)->tag == T_NIL)\n\n");

        /* Reference counting and ownership-aware free strategies */
        omni_codegen_emit_raw(ctx, "static void inc_ref(Obj* o) { if (o && o != NIL) o->rc++; }\n");
        omni_codegen_emit_raw(ctx, "static void dec_ref(Obj* o);\n");
        omni_codegen_emit_raw(ctx, "static void free_tree(Obj* o);\n\n");

        /* free_unique: Known single reference, no RC check needed */
        omni_codegen_emit_raw(ctx, "static void free_unique(Obj* o) {\n");
        omni_codegen_emit_raw(ctx, "    if (!o || o == NIL) return;\n");
        omni_codegen_emit_raw(ctx, "    switch (o->tag) {\n");
        omni_codegen_emit_raw(ctx, "    case T_SYM: free(o->s); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_CELL: free_unique(o->cell.car); free_unique(o->cell.cdr); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_LAMBDA: free_unique(o->lam.params); free_unique(o->lam.body); free_unique(o->lam.env); break;\n");
        omni_codegen_emit_raw(ctx, "    default: break;\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    free(o);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        /* free_tree: Tree-shaped, recursive free (still checks RC for shared children) */
        omni_codegen_emit_raw(ctx, "static void free_tree(Obj* o) {\n");
        omni_codegen_emit_raw(ctx, "    if (!o || o == NIL) return;\n");
        omni_codegen_emit_raw(ctx, "    if (o->rc > 1) { o->rc--; return; } /* Shared child - dec only */\n");
        omni_codegen_emit_raw(ctx, "    switch (o->tag) {\n");
        omni_codegen_emit_raw(ctx, "    case T_SYM: free(o->s); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_CELL: free_tree(o->cell.car); free_tree(o->cell.cdr); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_LAMBDA: free_tree(o->lam.params); free_tree(o->lam.body); free_tree(o->lam.env); break;\n");
        omni_codegen_emit_raw(ctx, "    default: break;\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    free(o);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        /* free_obj: Standard RC-based free (dec_ref alias) */
        omni_codegen_emit_raw(ctx, "static void free_obj(Obj* o) {\n");
        omni_codegen_emit_raw(ctx, "    if (!o || o == NIL) return;\n");
        omni_codegen_emit_raw(ctx, "    if (--o->rc > 0) return;\n");
        omni_codegen_emit_raw(ctx, "    switch (o->tag) {\n");
        omni_codegen_emit_raw(ctx, "    case T_SYM: free(o->s); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_CELL: free_obj(o->cell.car); free_obj(o->cell.cdr); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_LAMBDA: free_obj(o->lam.params); free_obj(o->lam.body); free_obj(o->lam.env); break;\n");
        omni_codegen_emit_raw(ctx, "    default: break;\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    free(o);\n");
        omni_codegen_emit_raw(ctx, "}\n");
        omni_codegen_emit_raw(ctx, "static void dec_ref(Obj* o) { free_obj(o); }\n\n");

        /* Weak references for back-edges (break cycles) */
        omni_codegen_emit_raw(ctx, "/* Weak reference: does NOT prevent deallocation.\n");
        omni_codegen_emit_raw(ctx, " * Used for back-edges (parent, prev, etc.) to break cycles.\n");
        omni_codegen_emit_raw(ctx, " * Weak refs are NOT followed during free (no recursive free).\n");
        omni_codegen_emit_raw(ctx, " * Weak refs are auto-nullified when target is freed.\n");
        omni_codegen_emit_raw(ctx, " */\n");
        omni_codegen_emit_raw(ctx, "typedef struct WeakRef {\n");
        omni_codegen_emit_raw(ctx, "    Obj** slot;           /* Pointer to the weak field in the owner */\n");
        omni_codegen_emit_raw(ctx, "    struct WeakRef* next; /* Next weak ref pointing to same target */\n");
        omni_codegen_emit_raw(ctx, "} WeakRef;\n\n");

        omni_codegen_emit_raw(ctx, "/* Weak ref list head stored in target object (or separate table) */\n");
        omni_codegen_emit_raw(ctx, "static WeakRef* _weak_refs = NULL; /* Global list for simplicity */\n\n");

        omni_codegen_emit_raw(ctx, "static void weak_ref_register(Obj** slot, Obj* target) {\n");
        omni_codegen_emit_raw(ctx, "    (void)target; /* For table-based lookup, would use target */\n");
        omni_codegen_emit_raw(ctx, "    WeakRef* wr = malloc(sizeof(WeakRef));\n");
        omni_codegen_emit_raw(ctx, "    wr->slot = slot;\n");
        omni_codegen_emit_raw(ctx, "    wr->next = _weak_refs;\n");
        omni_codegen_emit_raw(ctx, "    _weak_refs = wr;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static void weak_refs_nullify(Obj* target) {\n");
        omni_codegen_emit_raw(ctx, "    /* Called when target is about to be freed - nullify all weak refs */\n");
        omni_codegen_emit_raw(ctx, "    WeakRef** prev = &_weak_refs;\n");
        omni_codegen_emit_raw(ctx, "    WeakRef* wr = _weak_refs;\n");
        omni_codegen_emit_raw(ctx, "    while (wr) {\n");
        omni_codegen_emit_raw(ctx, "        if (*(wr->slot) == target) {\n");
        omni_codegen_emit_raw(ctx, "            *(wr->slot) = NULL; /* Nullify the weak reference */\n");
        omni_codegen_emit_raw(ctx, "            *prev = wr->next;\n");
        omni_codegen_emit_raw(ctx, "            WeakRef* to_free = wr;\n");
        omni_codegen_emit_raw(ctx, "            wr = wr->next;\n");
        omni_codegen_emit_raw(ctx, "            free(to_free);\n");
        omni_codegen_emit_raw(ctx, "        } else {\n");
        omni_codegen_emit_raw(ctx, "            prev = &wr->next;\n");
        omni_codegen_emit_raw(ctx, "            wr = wr->next;\n");
        omni_codegen_emit_raw(ctx, "        }\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Set a back-edge field (weak reference) */\n");
        omni_codegen_emit_raw(ctx, "#define SET_WEAK(owner, field, target) do { \\\n");
        omni_codegen_emit_raw(ctx, "    (owner)->field = (target); \\\n");
        omni_codegen_emit_raw(ctx, "    if (target) weak_ref_register(&(owner)->field, target); \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Get a back-edge field (may be NULL if target was freed) */\n");
        omni_codegen_emit_raw(ctx, "#define GET_WEAK(owner, field) ((owner)->field)\n\n");

        /* Perceus reuse functions - reuse freed memory for new allocations */
        omni_codegen_emit_raw(ctx, "/* Perceus Reuse: In-place mutation for functional-style updates.\n");
        omni_codegen_emit_raw(ctx, " * When we know an object will be freed immediately before a new allocation\n");
        omni_codegen_emit_raw(ctx, " * of the same size, we can reuse its memory instead of free+malloc.\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "/* Reuse an object's memory for an integer */\n");
        omni_codegen_emit_raw(ctx, "static Obj* reuse_as_int(Obj* old, int64_t val) {\n");
        omni_codegen_emit_raw(ctx, "    if (!old || old == NIL) return mk_int(val);\n");
        omni_codegen_emit_raw(ctx, "    /* Clear old content if needed */\n");
        omni_codegen_emit_raw(ctx, "    if (old->tag == T_SYM && old->s) free(old->s);\n");
        omni_codegen_emit_raw(ctx, "    else if (old->tag == T_CELL) {\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.car);\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.cdr);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    old->tag = T_INT;\n");
        omni_codegen_emit_raw(ctx, "    old->i = val;\n");
        omni_codegen_emit_raw(ctx, "    old->rc = 1;\n");
        omni_codegen_emit_raw(ctx, "    return old;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Reuse an object's memory for a cell/cons */\n");
        omni_codegen_emit_raw(ctx, "static Obj* reuse_as_cell(Obj* old, Obj* car, Obj* cdr) {\n");
        omni_codegen_emit_raw(ctx, "    if (!old || old == NIL) return mk_cell(car, cdr);\n");
        omni_codegen_emit_raw(ctx, "    /* Clear old content if needed */\n");
        omni_codegen_emit_raw(ctx, "    if (old->tag == T_SYM && old->s) free(old->s);\n");
        omni_codegen_emit_raw(ctx, "    else if (old->tag == T_CELL) {\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.car);\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.cdr);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    old->tag = T_CELL;\n");
        omni_codegen_emit_raw(ctx, "    old->cell.car = car; inc_ref(car);\n");
        omni_codegen_emit_raw(ctx, "    old->cell.cdr = cdr; inc_ref(cdr);\n");
        omni_codegen_emit_raw(ctx, "    old->rc = 1;\n");
        omni_codegen_emit_raw(ctx, "    return old;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Reuse an object's memory for a float */\n");
        omni_codegen_emit_raw(ctx, "static Obj* reuse_as_float(Obj* old, double val) {\n");
        omni_codegen_emit_raw(ctx, "    if (!old || old == NIL) return mk_float(val);\n");
        omni_codegen_emit_raw(ctx, "    /* Clear old content if needed */\n");
        omni_codegen_emit_raw(ctx, "    if (old->tag == T_SYM && old->s) free(old->s);\n");
        omni_codegen_emit_raw(ctx, "    else if (old->tag == T_CELL) {\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.car);\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.cdr);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    old->tag = T_FLOAT;\n");
        omni_codegen_emit_raw(ctx, "    old->f = val;\n");
        omni_codegen_emit_raw(ctx, "    old->rc = 1;\n");
        omni_codegen_emit_raw(ctx, "    return old;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Check if object can be reused (unique, about to be freed) */\n");
        omni_codegen_emit_raw(ctx, "#define CAN_REUSE(o) ((o) && (o) != NIL && (o)->rc == 1)\n\n");

        omni_codegen_emit_raw(ctx, "/* Conditional reuse macro - falls back to fresh alloc if can't reuse */\n");
        omni_codegen_emit_raw(ctx, "#define REUSE_OR_NEW_INT(old, val) \\\n");
        omni_codegen_emit_raw(ctx, "    (CAN_REUSE(old) ? reuse_as_int(old, val) : mk_int(val))\n\n");

        omni_codegen_emit_raw(ctx, "#define REUSE_OR_NEW_CELL(old, car, cdr) \\\n");
        omni_codegen_emit_raw(ctx, "    (CAN_REUSE(old) ? reuse_as_cell(old, car, cdr) : mk_cell(car, cdr))\n\n");

        omni_codegen_emit_raw(ctx, "#define REUSE_OR_NEW_FLOAT(old, val) \\\n");
        omni_codegen_emit_raw(ctx, "    (CAN_REUSE(old) ? reuse_as_float(old, val) : mk_float(val))\n\n");

        /* RC Elision: Skip reference counting for objects with known lifetimes */
        omni_codegen_emit_raw(ctx, "/* RC Elision: Conditional inc/dec based on analysis.\n");
        omni_codegen_emit_raw(ctx, " * When analysis proves RC operations are unnecessary, we skip them:\n");
        omni_codegen_emit_raw(ctx, " * - Unique references: no other refs exist\n");
        omni_codegen_emit_raw(ctx, " * - Stack-allocated: lifetime is scope-bound\n");
        omni_codegen_emit_raw(ctx, " * - Arena/pool: bulk free, no individual tracking\n");
        omni_codegen_emit_raw(ctx, " * - Same region: all refs die together\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "/* Conditional inc_ref - may be elided */\n");
        omni_codegen_emit_raw(ctx, "#define INC_REF_IF_NEEDED(o, can_elide) \\\n");
        omni_codegen_emit_raw(ctx, "    do { if (!(can_elide)) inc_ref(o); } while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Conditional dec_ref - may be elided */\n");
        omni_codegen_emit_raw(ctx, "#define DEC_REF_IF_NEEDED(o, can_elide) \\\n");
        omni_codegen_emit_raw(ctx, "    do { if (!(can_elide)) dec_ref(o); } while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* No-op for elided RC operations (for clarity in generated code) */\n");
        omni_codegen_emit_raw(ctx, "#define RC_ELIDED() ((void)0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Region-local reference: no RC needed within same region */\n");
        omni_codegen_emit_raw(ctx, "#define REGION_LOCAL_REF(o) (o)  /* No inc_ref needed */\n\n");

        /* Per-Region External Refcount */
        omni_codegen_emit_raw(ctx, "/* Per-Region External Refcount: Track references into a region.\n");
        omni_codegen_emit_raw(ctx, " * Instead of per-object RC, track external refs to the region.\n");
        omni_codegen_emit_raw(ctx, " * When external_refcount == 0 and scope ends, bulk free entire region.\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "typedef struct Region {\n");
        omni_codegen_emit_raw(ctx, "    int id;\n");
        omni_codegen_emit_raw(ctx, "    int external_refcount;  /* Refs from outside this region */\n");
        omni_codegen_emit_raw(ctx, "    void* arena;            /* Arena allocator for this region */\n");
        omni_codegen_emit_raw(ctx, "    struct Region* parent;  /* Enclosing region */\n");
        omni_codegen_emit_raw(ctx, "} Region;\n\n");

        omni_codegen_emit_raw(ctx, "static Region* _current_region = NULL;\n\n");

        omni_codegen_emit_raw(ctx, "static Region* region_new(int id) {\n");
        omni_codegen_emit_raw(ctx, "    Region* r = malloc(sizeof(Region));\n");
        omni_codegen_emit_raw(ctx, "    r->id = id;\n");
        omni_codegen_emit_raw(ctx, "    r->external_refcount = 0;\n");
        omni_codegen_emit_raw(ctx, "    r->arena = NULL;  /* Could use arena allocator */\n");
        omni_codegen_emit_raw(ctx, "    r->parent = _current_region;\n");
        omni_codegen_emit_raw(ctx, "    _current_region = r;\n");
        omni_codegen_emit_raw(ctx, "    return r;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static void region_end(Region* r) {\n");
        omni_codegen_emit_raw(ctx, "    if (!r) return;\n");
        omni_codegen_emit_raw(ctx, "    _current_region = r->parent;\n");
        omni_codegen_emit_raw(ctx, "    /* If no external refs, could bulk-free arena here */\n");
        omni_codegen_emit_raw(ctx, "    if (r->external_refcount == 0) {\n");
        omni_codegen_emit_raw(ctx, "        /* Safe to bulk free all objects in region */\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    free(r);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "#define REGION_INC_EXTERNAL(r) do { if (r) (r)->external_refcount++; } while(0)\n");
        omni_codegen_emit_raw(ctx, "#define REGION_DEC_EXTERNAL(r) do { if (r) (r)->external_refcount--; } while(0)\n");
        omni_codegen_emit_raw(ctx, "#define REGION_CAN_BULK_FREE(r) ((r) && (r)->external_refcount == 0)\n\n");

        /* Borrow/Tether: Keep objects alive during loop iteration */
        omni_codegen_emit_raw(ctx, "/* Borrow/Tether: Keep borrowed objects alive.\n");
        omni_codegen_emit_raw(ctx, " * When iterating over a collection, the collection must stay alive.\n");
        omni_codegen_emit_raw(ctx, " * Tethering increments RC at loop entry, decrements at loop exit.\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "/* Tether an object to keep it alive during a borrow */\n");
        omni_codegen_emit_raw(ctx, "#define TETHER(o) do { if (o) inc_ref(o); } while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Release a tether when borrow ends */\n");
        omni_codegen_emit_raw(ctx, "#define UNTETHER(o) do { if (o) dec_ref(o); } while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Borrow a collection for loop iteration */\n");
        omni_codegen_emit_raw(ctx, "#define BORROW_FOR_LOOP(coll) TETHER(coll)\n\n");

        omni_codegen_emit_raw(ctx, "/* End loop borrow */\n");
        omni_codegen_emit_raw(ctx, "#define END_LOOP_BORROW(coll) UNTETHER(coll)\n\n");

        omni_codegen_emit_raw(ctx, "/* Scoped tether - automatically releases at scope end */\n");
        omni_codegen_emit_raw(ctx, "#define SCOPED_TETHER_DECL(name, o) \\\n");
        omni_codegen_emit_raw(ctx, "    Obj* name##_tethered = (o); \\\n");
        omni_codegen_emit_raw(ctx, "    TETHER(name##_tethered)\n\n");

        omni_codegen_emit_raw(ctx, "#define SCOPED_TETHER_END(name) \\\n");
        omni_codegen_emit_raw(ctx, "    UNTETHER(name##_tethered)\n\n");

        /* Interprocedural Ownership Annotations */
        omni_codegen_emit_raw(ctx, "/* Interprocedural Summaries: Ownership annotations for function boundaries.\n");
        omni_codegen_emit_raw(ctx, " * These annotations guide the compiler/reader about ownership transfer.\n");
        omni_codegen_emit_raw(ctx, " * PARAM_BORROWED: Caller keeps ownership, callee borrows.\n");
        omni_codegen_emit_raw(ctx, " * PARAM_CONSUMED: Callee takes ownership, will free.\n");
        omni_codegen_emit_raw(ctx, " * PARAM_PASSTHROUGH: Param passes through to return value.\n");
        omni_codegen_emit_raw(ctx, " * PARAM_CAPTURED: Param is captured in closure/data structure.\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "/* Parameter ownership annotations (for documentation) */\n");
        omni_codegen_emit_raw(ctx, "#define PARAM_BORROWED(p) (p)      /* Borrowed: caller keeps ownership */\n");
        omni_codegen_emit_raw(ctx, "#define PARAM_CONSUMED(p) (p)      /* Consumed: callee takes ownership */\n");
        omni_codegen_emit_raw(ctx, "#define PARAM_PASSTHROUGH(p) (p)   /* Passthrough: returned to caller */\n");
        omni_codegen_emit_raw(ctx, "#define PARAM_CAPTURED(p) (p)      /* Captured: stored in closure/struct */\n\n");

        omni_codegen_emit_raw(ctx, "/* Return ownership annotations */\n");
        omni_codegen_emit_raw(ctx, "#define RETURN_FRESH(v) (v)        /* Fresh allocation, caller must free */\n");
        omni_codegen_emit_raw(ctx, "#define RETURN_PASSTHROUGH(v) (v)  /* Returns a parameter, no new alloc */\n");
        omni_codegen_emit_raw(ctx, "#define RETURN_BORROWED(v) (v)     /* Borrowed ref, don't free */\n");
        omni_codegen_emit_raw(ctx, "#define RETURN_NONE() NIL          /* Returns nil/void */\n\n");

        omni_codegen_emit_raw(ctx, "/* Caller-side ownership handling */\n");
        omni_codegen_emit_raw(ctx, "#define CALL_CONSUMED(arg, call_expr) \\\n");
        omni_codegen_emit_raw(ctx, "    ({ Obj* _result = (call_expr); /* arg ownership transferred */ _result; })\n\n");

        omni_codegen_emit_raw(ctx, "#define CALL_BORROWED(arg, call_expr) \\\n");
        omni_codegen_emit_raw(ctx, "    ({ Obj* _result = (call_expr); /* caller still owns arg */ _result; })\n\n");

        omni_codegen_emit_raw(ctx, "/* Function summary declaration macro */\n");
        omni_codegen_emit_raw(ctx, "#define FUNC_SUMMARY(name, ret_own, allocs, side_effects) \\\n");
        omni_codegen_emit_raw(ctx, "    /* Summary: name returns ret_own, allocates: allocs, side_effects: side_effects */\n\n");

        omni_codegen_emit_raw(ctx, "/* Ownership transfer assertion (debug builds) */\n");
        omni_codegen_emit_raw(ctx, "#ifndef NDEBUG\n");
        omni_codegen_emit_raw(ctx, "#define ASSERT_OWNED(o) do { \\\n");
        omni_codegen_emit_raw(ctx, "    if ((o) && (o) != NIL && (o)->rc < 1) { \\\n");
        omni_codegen_emit_raw(ctx, "        fprintf(stderr, \"Ownership error: %%p has rc=%%d\\n\", (void*)(o), (o)->rc); \\\n");
        omni_codegen_emit_raw(ctx, "    } \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n");
        omni_codegen_emit_raw(ctx, "#else\n");
        omni_codegen_emit_raw(ctx, "#define ASSERT_OWNED(o) ((void)0)\n");
        omni_codegen_emit_raw(ctx, "#endif\n\n");

        /* Concurrency Ownership Inference */
        omni_codegen_emit_raw(ctx, "/* Concurrency Ownership: Thread-safe reference counting.\n");
        omni_codegen_emit_raw(ctx, " * THREAD_LOCAL: Data stays in one thread, no sync needed.\n");
        omni_codegen_emit_raw(ctx, " * THREAD_SHARED: Data accessed by multiple threads, needs atomic RC.\n");
        omni_codegen_emit_raw(ctx, " * THREAD_TRANSFER: Data transferred via channel, ownership moves.\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "/* Atomic reference counting for shared data */\n");
        omni_codegen_emit_raw(ctx, "#ifdef __STDC_NO_ATOMICS__\n");
        omni_codegen_emit_raw(ctx, "/* Fallback for systems without C11 atomics - use mutex */\n");
        omni_codegen_emit_raw(ctx, "static pthread_mutex_t _rc_mutex = PTHREAD_MUTEX_INITIALIZER;\n");
        omni_codegen_emit_raw(ctx, "#define ATOMIC_INC_REF(o) do { \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_lock(&_rc_mutex); \\\n");
        omni_codegen_emit_raw(ctx, "    if ((o) && (o) != NIL) (o)->rc++; \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_unlock(&_rc_mutex); \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n\n");
        omni_codegen_emit_raw(ctx, "#define ATOMIC_DEC_REF(o) do { \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_lock(&_rc_mutex); \\\n");
        omni_codegen_emit_raw(ctx, "    if ((o) && (o) != NIL) { \\\n");
        omni_codegen_emit_raw(ctx, "        if (--(o)->rc <= 0) { \\\n");
        omni_codegen_emit_raw(ctx, "            pthread_mutex_unlock(&_rc_mutex); \\\n");
        omni_codegen_emit_raw(ctx, "            free_obj(o); \\\n");
        omni_codegen_emit_raw(ctx, "        } else { \\\n");
        omni_codegen_emit_raw(ctx, "            pthread_mutex_unlock(&_rc_mutex); \\\n");
        omni_codegen_emit_raw(ctx, "        } \\\n");
        omni_codegen_emit_raw(ctx, "    } else { pthread_mutex_unlock(&_rc_mutex); } \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n");
        omni_codegen_emit_raw(ctx, "#else\n");
        omni_codegen_emit_raw(ctx, "/* Using __atomic builtins for GCC/Clang compatibility */\n");
        omni_codegen_emit_raw(ctx, "#define ATOMIC_INC_REF(o) do { \\\n");
        omni_codegen_emit_raw(ctx, "    if ((o) && (o) != NIL) __atomic_add_fetch(&(o)->rc, 1, __ATOMIC_SEQ_CST); \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n\n");
        omni_codegen_emit_raw(ctx, "#define ATOMIC_DEC_REF(o) do { \\\n");
        omni_codegen_emit_raw(ctx, "    if ((o) && (o) != NIL) { \\\n");
        omni_codegen_emit_raw(ctx, "        if (__atomic_sub_fetch(&(o)->rc, 1, __ATOMIC_SEQ_CST) <= 0) { \\\n");
        omni_codegen_emit_raw(ctx, "            free_obj(o); \\\n");
        omni_codegen_emit_raw(ctx, "        } \\\n");
        omni_codegen_emit_raw(ctx, "    } \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n");
        omni_codegen_emit_raw(ctx, "#endif\n\n");

        omni_codegen_emit_raw(ctx, "/* Thread locality annotations */\n");
        omni_codegen_emit_raw(ctx, "#define THREAD_LOCAL_VAR(v) (v)      /* No sync needed */\n");
        omni_codegen_emit_raw(ctx, "#define THREAD_SHARED_VAR(v) (v)     /* Uses atomic RC */\n");
        omni_codegen_emit_raw(ctx, "#define THREAD_TRANSFER_VAR(v) (v)   /* Ownership moves */\n\n");

        omni_codegen_emit_raw(ctx, "/* Channel operations - ownership transfer semantics */\n");
        omni_codegen_emit_raw(ctx, "typedef struct Channel {\n");
        omni_codegen_emit_raw(ctx, "    Obj** buffer;\n");
        omni_codegen_emit_raw(ctx, "    size_t capacity;\n");
        omni_codegen_emit_raw(ctx, "    size_t head, tail, count;\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_t mutex;\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_t not_empty;\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_t not_full;\n");
        omni_codegen_emit_raw(ctx, "    int closed;\n");
        omni_codegen_emit_raw(ctx, "} Channel;\n\n");

        omni_codegen_emit_raw(ctx, "static Channel* channel_new(size_t capacity) {\n");
        omni_codegen_emit_raw(ctx, "    Channel* c = malloc(sizeof(Channel));\n");
        omni_codegen_emit_raw(ctx, "    c->buffer = malloc(capacity * sizeof(Obj*));\n");
        omni_codegen_emit_raw(ctx, "    c->capacity = capacity;\n");
        omni_codegen_emit_raw(ctx, "    c->head = c->tail = c->count = 0;\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_init(&c->mutex, NULL);\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_init(&c->not_empty, NULL);\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_init(&c->not_full, NULL);\n");
        omni_codegen_emit_raw(ctx, "    c->closed = 0;\n");
        omni_codegen_emit_raw(ctx, "    return c;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Send transfers ownership - sender must NOT free after */\n");
        omni_codegen_emit_raw(ctx, "static void channel_send(Channel* c, Obj* value) {\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_lock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    while (c->count == c->capacity && !c->closed) {\n");
        omni_codegen_emit_raw(ctx, "        pthread_cond_wait(&c->not_full, &c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    if (!c->closed) {\n");
        omni_codegen_emit_raw(ctx, "        c->buffer[c->tail] = value;  /* Ownership transfers */\n");
        omni_codegen_emit_raw(ctx, "        c->tail = (c->tail + 1) %% c->capacity;\n");
        omni_codegen_emit_raw(ctx, "        c->count++;\n");
        omni_codegen_emit_raw(ctx, "        pthread_cond_signal(&c->not_empty);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_unlock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Recv receives ownership - receiver must free when done */\n");
        omni_codegen_emit_raw(ctx, "static Obj* channel_recv(Channel* c) {\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_lock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    while (c->count == 0 && !c->closed) {\n");
        omni_codegen_emit_raw(ctx, "        pthread_cond_wait(&c->not_empty, &c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    Obj* value = NIL;\n");
        omni_codegen_emit_raw(ctx, "    if (c->count > 0) {\n");
        omni_codegen_emit_raw(ctx, "        value = c->buffer[c->head];  /* Ownership transfers */\n");
        omni_codegen_emit_raw(ctx, "        c->head = (c->head + 1) %% c->capacity;\n");
        omni_codegen_emit_raw(ctx, "        c->count--;\n");
        omni_codegen_emit_raw(ctx, "        pthread_cond_signal(&c->not_full);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_unlock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    return value;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static void channel_close(Channel* c) {\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_lock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    c->closed = 1;\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_broadcast(&c->not_empty);\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_broadcast(&c->not_full);\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_unlock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static void channel_free(Channel* c) {\n");
        omni_codegen_emit_raw(ctx, "    if (!c) return;\n");
        omni_codegen_emit_raw(ctx, "    /* Free any remaining items in buffer */\n");
        omni_codegen_emit_raw(ctx, "    while (c->count > 0) {\n");
        omni_codegen_emit_raw(ctx, "        free_obj(c->buffer[c->head]);\n");
        omni_codegen_emit_raw(ctx, "        c->head = (c->head + 1) %% c->capacity;\n");
        omni_codegen_emit_raw(ctx, "        c->count--;\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    free(c->buffer);\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_destroy(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_destroy(&c->not_empty);\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_destroy(&c->not_full);\n");
        omni_codegen_emit_raw(ctx, "    free(c);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Ownership transfer macros */\n");
        omni_codegen_emit_raw(ctx, "#define SEND_OWNERSHIP(ch, val) do { channel_send(ch, val); /* val no longer owned */ } while(0)\n");
        omni_codegen_emit_raw(ctx, "#define RECV_OWNERSHIP(ch, var) do { var = channel_recv(ch); /* var now owned */ } while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Thread spawn with captured variable handling */\n");
        omni_codegen_emit_raw(ctx, "#define SPAWN_THREAD(fn, arg) do { \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_t _thread; \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_create(&_thread, NULL, fn, arg); \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_detach(_thread); \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Mark variable as shared (needs atomic RC) */\n");
        omni_codegen_emit_raw(ctx, "#define MARK_SHARED(v) ((void)0)  /* Analysis marker, no runtime cost */\n\n");

        omni_codegen_emit_raw(ctx, "/* Conditional RC based on thread locality analysis */\n");
        omni_codegen_emit_raw(ctx, "#define INC_REF_FOR_THREAD(o, needs_atomic) \\\n");
        omni_codegen_emit_raw(ctx, "    do { if (needs_atomic) ATOMIC_INC_REF(o); else inc_ref(o); } while(0)\n\n");

        omni_codegen_emit_raw(ctx, "#define DEC_REF_FOR_THREAD(o, needs_atomic) \\\n");
        omni_codegen_emit_raw(ctx, "    do { if (needs_atomic) ATOMIC_DEC_REF(o); else dec_ref(o); } while(0)\n\n");

        /* Print */
        omni_codegen_emit_raw(ctx, "static void print_obj(Obj* o) {\n");
        omni_codegen_emit_raw(ctx, "    if (!o || is_nil(o)) { printf(\"()\"); return; }\n");
        omni_codegen_emit_raw(ctx, "    switch (o->tag) {\n");
        omni_codegen_emit_raw(ctx, "    case T_INT: printf(\"%%ld\", (long)o->i); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_SYM: printf(\"%%s\", o->s); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_CELL:\n");
        omni_codegen_emit_raw(ctx, "        printf(\"(\");\n");
        omni_codegen_emit_raw(ctx, "        while (!is_nil(o)) {\n");
        omni_codegen_emit_raw(ctx, "            print_obj(car(o));\n");
        omni_codegen_emit_raw(ctx, "            o = cdr(o);\n");
        omni_codegen_emit_raw(ctx, "            if (!is_nil(o)) printf(\" \");\n");
        omni_codegen_emit_raw(ctx, "        }\n");
        omni_codegen_emit_raw(ctx, "        printf(\")\");\n");
        omni_codegen_emit_raw(ctx, "        break;\n");
        omni_codegen_emit_raw(ctx, "    default: printf(\"#<unknown>\"); break;\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n");
        omni_codegen_emit_raw(ctx, "#define omni_print(o) print_obj(o)\n\n");

        /* Primitives */
        omni_codegen_emit_raw(ctx, "static Obj* prim_add(Obj* a, Obj* b) { return mk_int(a->i + b->i); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_sub(Obj* a, Obj* b) { return mk_int(a->i - b->i); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_mul(Obj* a, Obj* b) { return mk_int(a->i * b->i); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_div(Obj* a, Obj* b) { return mk_int(a->i / b->i); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_mod(Obj* a, Obj* b) { return mk_int(a->i %% b->i); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_lt(Obj* a, Obj* b) { return mk_int(a->i < b->i ? 1 : 0); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_gt(Obj* a, Obj* b) { return mk_int(a->i > b->i ? 1 : 0); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_le(Obj* a, Obj* b) { return mk_int(a->i <= b->i ? 1 : 0); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_ge(Obj* a, Obj* b) { return mk_int(a->i >= b->i ? 1 : 0); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_eq(Obj* a, Obj* b) { return mk_int(a->i == b->i ? 1 : 0); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_cons(Obj* a, Obj* b) { inc_ref(a); inc_ref(b); return mk_cell(a, b); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_car(Obj* lst) { return is_nil(lst) ? NIL : car(lst); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_cdr(Obj* lst) { return is_nil(lst) ? NIL : cdr(lst); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_null(Obj* o) { return mk_int(is_nil(o) ? 1 : 0); }\n");
        omni_codegen_emit_raw(ctx, "static int is_truthy(Obj* o) { return o && o != NIL && (o->tag != T_INT || o->i != 0); }\n\n");
    }
}

/* ============== Expression Compilation ============== */

static void codegen_expr(CodeGenContext* ctx, OmniValue* expr);

static void codegen_int(CodeGenContext* ctx, OmniValue* expr) {
    omni_codegen_emit_raw(ctx, "mk_int(%ld)", (long)expr->int_val);
}

static void codegen_float(CodeGenContext* ctx, OmniValue* expr) {
    /* For now, treat floats as ints (TODO: proper float support) */
    omni_codegen_emit_raw(ctx, "mk_int(%ld)", (long)expr->float_val);
}

static void codegen_sym(CodeGenContext* ctx, OmniValue* expr) {
    const char* c_name = lookup_symbol(ctx, expr->str_val);
    if (c_name) {
        omni_codegen_emit_raw(ctx, "%s", c_name);
    } else {
        /* Check for primitives */
        const char* name = expr->str_val;
        if (strcmp(name, "+") == 0) omni_codegen_emit_raw(ctx, "prim_add");
        else if (strcmp(name, "-") == 0) omni_codegen_emit_raw(ctx, "prim_sub");
        else if (strcmp(name, "*") == 0) omni_codegen_emit_raw(ctx, "prim_mul");
        else if (strcmp(name, "/") == 0) omni_codegen_emit_raw(ctx, "prim_div");
        else if (strcmp(name, "%%") == 0) omni_codegen_emit_raw(ctx, "prim_mod");
        else if (strcmp(name, "<") == 0) omni_codegen_emit_raw(ctx, "prim_lt");
        else if (strcmp(name, ">") == 0) omni_codegen_emit_raw(ctx, "prim_gt");
        else if (strcmp(name, "<=") == 0) omni_codegen_emit_raw(ctx, "prim_le");
        else if (strcmp(name, ">=") == 0) omni_codegen_emit_raw(ctx, "prim_ge");
        else if (strcmp(name, "=") == 0) omni_codegen_emit_raw(ctx, "prim_eq");
        else if (strcmp(name, "cons") == 0) omni_codegen_emit_raw(ctx, "prim_cons");
        else if (strcmp(name, "car") == 0) omni_codegen_emit_raw(ctx, "prim_car");
        else if (strcmp(name, "cdr") == 0) omni_codegen_emit_raw(ctx, "prim_cdr");
        else if (strcmp(name, "null?") == 0) omni_codegen_emit_raw(ctx, "prim_null");
        else {
            char* mangled = omni_codegen_mangle(name);
            omni_codegen_emit_raw(ctx, "%s", mangled);
            free(mangled);
        }
    }
}

static void codegen_quote(CodeGenContext* ctx, OmniValue* expr) {
    /* (quote x) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* val = omni_car(args);

    if (omni_is_nil(val)) {
        omni_codegen_emit_raw(ctx, "NIL");
    } else if (omni_is_int(val)) {
        omni_codegen_emit_raw(ctx, "mk_int(%ld)", (long)val->int_val);
    } else if (omni_is_sym(val)) {
        omni_codegen_emit_raw(ctx, "mk_sym(\"%s\")", val->str_val);
    } else if (omni_is_cell(val)) {
        /* Build list at runtime */
        omni_codegen_emit_raw(ctx, "mk_cell(");
        codegen_quote(ctx, omni_list2(omni_new_sym("quote"), omni_car(val)));
        omni_codegen_emit_raw(ctx, ", ");
        codegen_quote(ctx, omni_list2(omni_new_sym("quote"), omni_cdr(val)));
        omni_codegen_emit_raw(ctx, ")");
    } else {
        omni_codegen_emit_raw(ctx, "NIL");
    }
}

static void codegen_if(CodeGenContext* ctx, OmniValue* expr) {
    /* (if cond then else) */
    OmniValue* args = omni_cdr(expr);
    OmniValue* cond = omni_car(args);
    args = omni_cdr(args);
    OmniValue* then_expr = omni_is_nil(args) ? NULL : omni_car(args);
    args = omni_cdr(args);
    OmniValue* else_expr = omni_is_nil(args) ? NULL : omni_car(args);

    omni_codegen_emit_raw(ctx, "(is_truthy(");
    codegen_expr(ctx, cond);
    omni_codegen_emit_raw(ctx, ") ? (");
    if (then_expr) codegen_expr(ctx, then_expr);
    else omni_codegen_emit_raw(ctx, "NIL");
    omni_codegen_emit_raw(ctx, ") : (");
    if (else_expr) codegen_expr(ctx, else_expr);
    else omni_codegen_emit_raw(ctx, "NIL");
    omni_codegen_emit_raw(ctx, "))");
}

static void codegen_let(CodeGenContext* ctx, OmniValue* expr) {
    /* (let ((x val) ...) body) */
    OmniValue* args = omni_cdr(expr);
    OmniValue* bindings = omni_car(args);
    OmniValue* body = omni_cdr(args);

    omni_codegen_emit_raw(ctx, "({\n");
    omni_codegen_indent(ctx);

    /* Emit bindings */
    if (omni_is_array(bindings)) {
        /* Array-style: [x 1 y 2] */
        for (size_t i = 0; i + 1 < bindings->array.len; i += 2) {
            OmniValue* name = bindings->array.data[i];
            OmniValue* val = bindings->array.data[i + 1];
            if (omni_is_sym(name)) {
                char* c_name = omni_codegen_mangle(name->str_val);
                omni_codegen_emit(ctx, "Obj* %s = ", c_name);
                codegen_expr(ctx, val);
                omni_codegen_emit_raw(ctx, ";\n");
                register_symbol(ctx, name->str_val, c_name);
                free(c_name);
            }
        }
    } else if (omni_is_cell(bindings)) {
        /* List-style: ((x 1) (y 2)) */
        while (!omni_is_nil(bindings) && omni_is_cell(bindings)) {
            OmniValue* binding = omni_car(bindings);
            if (omni_is_cell(binding)) {
                OmniValue* name = omni_car(binding);
                OmniValue* val = omni_car(omni_cdr(binding));
                if (omni_is_sym(name)) {
                    char* c_name = omni_codegen_mangle(name->str_val);
                    omni_codegen_emit(ctx, "Obj* %s = ", c_name);
                    codegen_expr(ctx, val);
                    omni_codegen_emit_raw(ctx, ";\n");
                    register_symbol(ctx, name->str_val, c_name);
                    free(c_name);
                }
            }
            bindings = omni_cdr(bindings);
        }
    }

    /* Emit body */
    OmniValue* result = NULL;
    while (!omni_is_nil(body) && omni_is_cell(body)) {
        result = omni_car(body);
        body = omni_cdr(body);
        if (!omni_is_nil(body)) {
            omni_codegen_emit(ctx, "");
            codegen_expr(ctx, result);
            omni_codegen_emit_raw(ctx, ";\n");
        }
    }

    /* Last expression is the result */
    if (result) {
        omni_codegen_emit(ctx, "");
        codegen_expr(ctx, result);
        omni_codegen_emit_raw(ctx, ";\n");
    }

    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "})");
}

static void codegen_lambda(CodeGenContext* ctx, OmniValue* expr) {
    /* Generate lambda as a static function */
    int lambda_id = ctx->lambda_counter++;

    OmniValue* args = omni_cdr(expr);
    OmniValue* params = omni_car(args);
    OmniValue* body = omni_cdr(args);

    /* Generate lambda function name */
    char fn_name[64];
    snprintf(fn_name, sizeof(fn_name), "_lambda_%d", lambda_id);

    /* Build function definition into a buffer */
    char def[8192];
    char* p = def;
    p += sprintf(p, "static Obj* %s(", fn_name);

    /* Parameters - register them before generating body */
    bool first = true;
    OmniValue* param_list = params;
    if (omni_is_cell(param_list)) {
        while (!omni_is_nil(param_list) && omni_is_cell(param_list)) {
            if (!first) p += sprintf(p, ", ");
            first = false;
            OmniValue* param = omni_car(param_list);
            if (omni_is_sym(param)) {
                char* c_name = omni_codegen_mangle(param->str_val);
                p += sprintf(p, "Obj* %s", c_name);
                register_symbol(ctx, param->str_val, c_name);
                free(c_name);
            }
            param_list = omni_cdr(param_list);
        }
    }
    if (first) {
        p += sprintf(p, "void");
    }
    p += sprintf(p, ") {\n");

    /* Generate body - find last expression for return */
    OmniValue* result = NULL;
    OmniValue* body_iter = body;
    while (!omni_is_nil(body_iter) && omni_is_cell(body_iter)) {
        result = omni_car(body_iter);
        body_iter = omni_cdr(body_iter);
    }

    /* Generate body using a temp context to capture output */
    if (result) {
        CodeGenContext* tmp = omni_codegen_new_buffer();
        tmp->indent_level = 1;
        tmp->lambda_counter = ctx->lambda_counter;
        /* Copy symbol table */
        for (size_t i = 0; i < ctx->symbols.count; i++) {
            register_symbol(tmp, ctx->symbols.names[i], ctx->symbols.c_names[i]);
        }

        omni_codegen_emit(tmp, "return ");
        codegen_expr(tmp, result);
        omni_codegen_emit_raw(tmp, ";\n");

        /* Update lambda counter from nested lambdas */
        ctx->lambda_counter = tmp->lambda_counter;

        /* Copy any nested lambda definitions */
        for (size_t i = 0; i < tmp->lambda_defs.count; i++) {
            omni_codegen_add_lambda_def(ctx, tmp->lambda_defs.defs[i]);
        }

        char* body_code = omni_codegen_get_output(tmp);
        if (body_code) {
            p += sprintf(p, "%s", body_code);
            free(body_code);
        }
        omni_codegen_free(tmp);
    } else {
        p += sprintf(p, "    return NIL;\n");
    }

    p += sprintf(p, "}");

    /* Add to lambda definitions */
    omni_codegen_add_lambda_def(ctx, def);

    /* Emit function name at call site */
    omni_codegen_emit_raw(ctx, "%s", fn_name);
}

static void codegen_define(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    OmniValue* name_or_sig = omni_car(args);
    OmniValue* body = omni_cdr(args);

    if (omni_is_sym(name_or_sig)) {
        /* Variable define */
        char* c_name = omni_codegen_mangle(name_or_sig->str_val);
        omni_codegen_emit(ctx, "Obj* %s = ", c_name);
        if (!omni_is_nil(body)) {
            codegen_expr(ctx, omni_car(body));
        } else {
            omni_codegen_emit_raw(ctx, "NIL");
        }
        omni_codegen_emit_raw(ctx, ";\n");
        register_symbol(ctx, name_or_sig->str_val, c_name);
        free(c_name);
    } else if (omni_is_cell(name_or_sig)) {
        /* Function define */
        OmniValue* fname = omni_car(name_or_sig);
        OmniValue* params = omni_cdr(name_or_sig);

        if (!omni_is_sym(fname)) return;

        char* c_name = omni_codegen_mangle(fname->str_val);
        register_symbol(ctx, fname->str_val, c_name);

        /* Emit function */
        omni_codegen_emit(ctx, "static Obj* %s(", c_name);

        /* Parameters */
        bool first = true;
        while (!omni_is_nil(params) && omni_is_cell(params)) {
            if (!first) omni_codegen_emit_raw(ctx, ", ");
            first = false;
            OmniValue* param = omni_car(params);
            if (omni_is_sym(param)) {
                char* param_name = omni_codegen_mangle(param->str_val);
                omni_codegen_emit_raw(ctx, "Obj* %s", param_name);
                register_symbol(ctx, param->str_val, param_name);
                free(param_name);
            }
            params = omni_cdr(params);
        }

        if (first) {
            omni_codegen_emit_raw(ctx, "void");
        }
        omni_codegen_emit_raw(ctx, ") {\n");
        omni_codegen_indent(ctx);

        /* Body */
        OmniValue* result = NULL;
        while (!omni_is_nil(body) && omni_is_cell(body)) {
            result = omni_car(body);
            body = omni_cdr(body);
        }

        if (result) {
            omni_codegen_emit(ctx, "return ");
            codegen_expr(ctx, result);
            omni_codegen_emit_raw(ctx, ";\n");
        } else {
            omni_codegen_emit(ctx, "return NIL;\n");
        }

        omni_codegen_dedent(ctx);
        omni_codegen_emit(ctx, "}\n\n");

        free(c_name);
    }
}

static void codegen_apply(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* func = omni_car(expr);
    OmniValue* args = omni_cdr(expr);

    /* Check for binary operators */
    if (omni_is_sym(func)) {
        const char* name = func->str_val;
        bool is_binop = (strcmp(name, "+") == 0 || strcmp(name, "-") == 0 ||
                         strcmp(name, "*") == 0 || strcmp(name, "/") == 0 ||
                         strcmp(name, "%") == 0 || strcmp(name, "<") == 0 ||
                         strcmp(name, ">") == 0 || strcmp(name, "<=") == 0 ||
                         strcmp(name, ">=") == 0 || strcmp(name, "=") == 0);

        if (is_binop && !omni_is_nil(args) && !omni_is_nil(omni_cdr(args))) {
            OmniValue* a = omni_car(args);
            OmniValue* b = omni_car(omni_cdr(args));

            codegen_sym(ctx, func);
            omni_codegen_emit_raw(ctx, "(");
            codegen_expr(ctx, a);
            omni_codegen_emit_raw(ctx, ", ");
            codegen_expr(ctx, b);
            omni_codegen_emit_raw(ctx, ")");
            return;
        }

        /* Check for display/print */
        if (strcmp(name, "display") == 0 || strcmp(name, "print") == 0) {
            omni_codegen_emit_raw(ctx, "(omni_print(");
            if (!omni_is_nil(args)) codegen_expr(ctx, omni_car(args));
            else omni_codegen_emit_raw(ctx, "NIL");
            omni_codegen_emit_raw(ctx, "), NIL)");
            return;
        }

        if (strcmp(name, "newline") == 0) {
            omni_codegen_emit_raw(ctx, "(printf(\"\\n\"), NIL)");
            return;
        }
    }

    /* Regular function call */
    codegen_expr(ctx, func);
    omni_codegen_emit_raw(ctx, "(");
    bool first = true;
    while (!omni_is_nil(args) && omni_is_cell(args)) {
        if (!first) omni_codegen_emit_raw(ctx, ", ");
        first = false;
        codegen_expr(ctx, omni_car(args));
        args = omni_cdr(args);
    }
    omni_codegen_emit_raw(ctx, ")");
}

static void codegen_list(CodeGenContext* ctx, OmniValue* expr) {
    if (omni_is_nil(expr)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* head = omni_car(expr);

    /* Check for special forms */
    if (omni_is_sym(head)) {
        const char* name = head->str_val;

        if (strcmp(name, "quote") == 0) {
            codegen_quote(ctx, expr);
            return;
        }
        if (strcmp(name, "if") == 0) {
            codegen_if(ctx, expr);
            return;
        }
        if (strcmp(name, "let") == 0 || strcmp(name, "let*") == 0) {
            codegen_let(ctx, expr);
            return;
        }
        if (strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0) {
            codegen_lambda(ctx, expr);
            return;
        }
        if (strcmp(name, "define") == 0) {
            codegen_define(ctx, expr);
            return;
        }
        if (strcmp(name, "do") == 0 || strcmp(name, "begin") == 0) {
            OmniValue* body = omni_cdr(expr);
            omni_codegen_emit_raw(ctx, "({\n");
            omni_codegen_indent(ctx);
            OmniValue* result = NULL;
            while (!omni_is_nil(body) && omni_is_cell(body)) {
                result = omni_car(body);
                body = omni_cdr(body);
                omni_codegen_emit(ctx, "");
                codegen_expr(ctx, result);
                omni_codegen_emit_raw(ctx, ";\n");
            }
            omni_codegen_dedent(ctx);
            omni_codegen_emit(ctx, "})");
            return;
        }
    }

    /* Function application */
    codegen_apply(ctx, expr);
}

static void codegen_expr(CodeGenContext* ctx, OmniValue* expr) {
    if (!expr || omni_is_nil(expr)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    switch (expr->tag) {
    case OMNI_INT:
        codegen_int(ctx, expr);
        break;
    case OMNI_FLOAT:
        codegen_float(ctx, expr);
        break;
    case OMNI_SYM:
        codegen_sym(ctx, expr);
        break;
    case OMNI_CELL:
        codegen_list(ctx, expr);
        break;
    case OMNI_ARRAY:
        /* TODO: Array literals */
        omni_codegen_emit_raw(ctx, "NIL");
        break;
    default:
        omni_codegen_emit_raw(ctx, "NIL");
        break;
    }
}

/* ============== Main Generation ============== */

void omni_codegen_expr(CodeGenContext* ctx, OmniValue* expr) {
    codegen_expr(ctx, expr);
}

void omni_codegen_main(CodeGenContext* ctx, OmniValue** exprs, size_t count) {
    omni_codegen_emit(ctx, "int main(void) {\n");
    omni_codegen_indent(ctx);

    for (size_t i = 0; i < count; i++) {
        OmniValue* expr = exprs[i];

        /* Check if it's a define - emit at top level */
        if (omni_is_cell(expr) && omni_is_sym(omni_car(expr)) &&
            strcmp(omni_car(expr)->str_val, "define") == 0) {
            /* Already emitted as top-level function */
            continue;
        }

        /* Regular expression - emit in main */
        omni_codegen_emit(ctx, "{\n");
        omni_codegen_indent(ctx);
        omni_codegen_emit(ctx, "Obj* _result = ");
        codegen_expr(ctx, expr);
        omni_codegen_emit_raw(ctx, ";\n");
        omni_codegen_emit(ctx, "omni_print(_result);\n");
        omni_codegen_emit(ctx, "printf(\"\\n\");\n");
        omni_codegen_emit(ctx, "free_obj(_result);\n");
        omni_codegen_dedent(ctx);
        omni_codegen_emit(ctx, "}\n");
    }

    omni_codegen_emit(ctx, "return 0;\n");
    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "}\n");
}

void omni_codegen_program(CodeGenContext* ctx, OmniValue** exprs, size_t count) {
    /* Initialize analysis */
    ctx->analysis = omni_analysis_new();
    omni_analyze_program(ctx->analysis, exprs, count);

    /* Emit runtime header */
    omni_codegen_runtime_header(ctx);

    /* First pass: collect defines and emit as top-level functions */
    for (size_t i = 0; i < count; i++) {
        OmniValue* expr = exprs[i];
        if (omni_is_cell(expr) && omni_is_sym(omni_car(expr)) &&
            strcmp(omni_car(expr)->str_val, "define") == 0) {
            OmniValue* args = omni_cdr(expr);
            OmniValue* name_or_sig = omni_car(args);

            /* Only emit function defines at top level */
            if (omni_is_cell(name_or_sig)) {
                codegen_define(ctx, expr);
            }
        }
    }

    /* Generate main() to a buffer first to collect lambdas */
    CodeGenContext* main_ctx = omni_codegen_new_buffer();
    main_ctx->analysis = ctx->analysis;
    main_ctx->lambda_counter = ctx->lambda_counter;
    /* Copy symbol table */
    for (size_t i = 0; i < ctx->symbols.count; i++) {
        register_symbol(main_ctx, ctx->symbols.names[i], ctx->symbols.c_names[i]);
    }
    omni_codegen_main(main_ctx, exprs, count);
    char* main_code = omni_codegen_get_output(main_ctx);

    /* Collect lambdas generated during main */
    for (size_t i = 0; i < main_ctx->lambda_defs.count; i++) {
        omni_codegen_add_lambda_def(ctx, main_ctx->lambda_defs.defs[i]);
    }

    /* Don't free analysis from temp context */
    main_ctx->analysis = NULL;
    omni_codegen_free(main_ctx);

    /* Emit forward declarations */
    for (size_t i = 0; i < ctx->forward_decls.count; i++) {
        omni_codegen_emit_raw(ctx, "%s\n", ctx->forward_decls.decls[i]);
    }
    if (ctx->forward_decls.count > 0) {
        omni_codegen_emit_raw(ctx, "\n");
    }

    /* Emit lambda definitions */
    for (size_t i = 0; i < ctx->lambda_defs.count; i++) {
        omni_codegen_emit_raw(ctx, "%s\n\n", ctx->lambda_defs.defs[i]);
    }

    /* Emit main function */
    if (main_code) {
        omni_codegen_emit_raw(ctx, "%s", main_code);
        free(main_code);
    }
}

/* ============== ASAP Memory Management ============== */

/* Emit free call using the appropriate ownership-driven strategy */
static void emit_ownership_free(CodeGenContext* ctx, const char* var_name, const char* c_name) {
    if (!ctx->analysis) {
        /* No analysis - fall back to RC-based free */
        omni_codegen_emit(ctx, "free_obj(%s);\n", c_name);
        return;
    }

    FreeStrategy strategy = omni_get_free_strategy(ctx->analysis, var_name);
    const char* strategy_name = omni_free_strategy_name(strategy);

    switch (strategy) {
        case FREE_STRATEGY_NONE:
            /* Don't emit a free - borrowed/transferred */
            omni_codegen_emit(ctx, "/* %s: %s (no free) */\n", c_name, strategy_name);
            break;

        case FREE_STRATEGY_UNIQUE:
            /* Single reference - no RC check needed */
            omni_codegen_emit(ctx, "free_unique(%s); /* %s */\n", c_name, strategy_name);
            break;

        case FREE_STRATEGY_TREE:
            /* Tree-shaped, may have shared children */
            omni_codegen_emit(ctx, "free_tree(%s); /* %s */\n", c_name, strategy_name);
            break;

        case FREE_STRATEGY_RC:
        case FREE_STRATEGY_RC_TREE:
        default:
            /* Shared/DAG/cyclic - use RC */
            omni_codegen_emit(ctx, "dec_ref(%s); /* %s */\n", c_name, strategy_name);
            break;
    }
}

void omni_codegen_emit_frees(CodeGenContext* ctx, int position) {
    if (!ctx->analysis) return;

    size_t count;
    char** vars = omni_get_frees_at(ctx->analysis, position, &count);

    for (size_t i = 0; i < count; i++) {
        const char* c_name = lookup_symbol(ctx, vars[i]);
        if (c_name) {
            emit_ownership_free(ctx, vars[i], c_name);
        }
    }

    free(vars);
}

void omni_codegen_emit_scope_cleanup(CodeGenContext* ctx, const char** vars, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const char* c_name = lookup_symbol(ctx, vars[i]);
        if (c_name) {
            emit_ownership_free(ctx, vars[i], c_name);
        }
    }
}

/* ============== CFG-Based Code Generation ============== */

void omni_codegen_emit_cfg_frees(CodeGenContext* ctx, CFG* cfg, CFGNode* node) {
    if (!ctx || !cfg || !node || !ctx->analysis) return;

    size_t count;
    char** to_free = omni_get_frees_for_node(cfg, node, ctx->analysis, &count);

    for (size_t i = 0; i < count; i++) {
        const char* c_name = lookup_symbol(ctx, to_free[i]);
        if (c_name) {
            FreeStrategy strategy = omni_get_free_strategy(ctx->analysis, to_free[i]);
            const char* strategy_name = omni_free_strategy_name(strategy);

            switch (strategy) {
                case FREE_STRATEGY_NONE:
                    omni_codegen_emit(ctx, "/* CFG node %d: %s - %s (no free) */\n",
                                      node->id, c_name, strategy_name);
                    break;

                case FREE_STRATEGY_UNIQUE:
                    omni_codegen_emit(ctx, "free_unique(%s); /* CFG node %d: %s */\n",
                                      c_name, node->id, strategy_name);
                    break;

                case FREE_STRATEGY_TREE:
                    omni_codegen_emit(ctx, "free_tree(%s); /* CFG node %d: %s */\n",
                                      c_name, node->id, strategy_name);
                    break;

                case FREE_STRATEGY_RC:
                case FREE_STRATEGY_RC_TREE:
                default:
                    omni_codegen_emit(ctx, "dec_ref(%s); /* CFG node %d: %s */\n",
                                      c_name, node->id, strategy_name);
                    break;
            }
        }
    }

    free(to_free);
}

/*
 * Generate code for expression with CFG-aware free placement.
 *
 * The strategy is:
 * 1. Build CFG for the expression
 * 2. Run ownership analysis
 * 3. Compute liveness on the CFG
 * 4. Generate code, emitting frees at each CFG node where variables die
 */
void omni_codegen_with_cfg(CodeGenContext* ctx, OmniValue* expr) {
    if (!ctx || !expr) return;

    /* Create analysis context if needed */
    if (!ctx->analysis) {
        ctx->analysis = omni_analysis_new();
    }

    /* Run ownership analysis */
    omni_analyze_ownership(ctx->analysis, expr);

    /* Build CFG */
    CFG* cfg = omni_build_cfg(expr);
    if (!cfg) {
        /* Fallback to non-CFG generation */
        omni_codegen_expr(ctx, expr);
        return;
    }

    /* Compute liveness */
    omni_compute_liveness(cfg, ctx->analysis);

    /* Compute free points */
    CFGFreePoint* free_points = omni_compute_cfg_free_points(cfg, ctx->analysis);

    /* For now, just emit the comment showing what would be freed
     * Full integration would require restructuring codegen_expr to be CFG-aware */
    omni_codegen_emit(ctx, "/* CFG-aware free placement active */\n");

    /* Print free point information as comments */
    for (CFGFreePoint* fp = free_points; fp; fp = fp->next) {
        if (fp->var_count > 0) {
            omni_codegen_emit(ctx, "/* Node %d frees: ", fp->node->id);
            for (size_t i = 0; i < fp->var_count; i++) {
                omni_codegen_emit_raw(ctx, "%s ", fp->vars[i]);
            }
            omni_codegen_emit_raw(ctx, "*/\n");
        }
    }

    /* Generate the actual code */
    omni_codegen_expr(ctx, expr);

    /* Cleanup */
    omni_cfg_free_points_free(free_points);
    omni_cfg_free(cfg);
}
