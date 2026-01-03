/*
 * OmniLisp Code Generator
 *
 * Generates C99 + POSIX code with ASAP memory management.
 * - Emits standalone C programs
 * - Injects free_obj() calls based on analysis
 * - Supports closures, continuations, and concurrency
 */

#ifndef OMNILISP_CODEGEN_H
#define OMNILISP_CODEGEN_H

#include "../ast/ast.h"
#include "../analysis/analysis.h"
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Code Generator State ============== */

typedef struct CodeGenContext {
    /* Output stream */
    FILE* output;
    char* output_buffer;      /* For in-memory generation */
    size_t output_size;
    size_t output_capacity;

    /* Analysis context */
    AnalysisContext* analysis;

    /* Generation state */
    int indent_level;
    int temp_counter;
    int label_counter;
    int lambda_counter;

    /* Symbol table for generated names */
    struct {
        char** names;
        char** c_names;
        size_t count;
        size_t capacity;
    } symbols;

    /* Forward declarations needed */
    struct {
        char** decls;
        size_t count;
        size_t capacity;
    } forward_decls;

    /* Lambda (closure) definitions */
    struct {
        char** defs;
        size_t count;
        size_t capacity;
    } lambda_defs;

    /* Flags */
    bool in_tail_position;
    bool generating_header;
    bool use_runtime;         /* Use external runtime library */
    const char* runtime_path;
} CodeGenContext;

/* ============== Code Generator API ============== */

/* Create a new code generator writing to a file */
CodeGenContext* omni_codegen_new(FILE* output);

/* Create a new code generator writing to memory */
CodeGenContext* omni_codegen_new_buffer(void);

/* Free code generator resources */
void omni_codegen_free(CodeGenContext* ctx);

/* Get generated code as string (for buffer mode) */
char* omni_codegen_get_output(CodeGenContext* ctx);

/* Set external runtime path */
void omni_codegen_set_runtime(CodeGenContext* ctx, const char* path);

/* ============== Code Generation ============== */

/* Generate a complete C program from parsed expressions */
void omni_codegen_program(CodeGenContext* ctx, OmniValue** exprs, size_t count);

/* Generate code for a single expression */
void omni_codegen_expr(CodeGenContext* ctx, OmniValue* expr);

/* Generate the runtime header (types, macros, etc.) */
void omni_codegen_runtime_header(CodeGenContext* ctx);

/* Generate the main function wrapper */
void omni_codegen_main(CodeGenContext* ctx, OmniValue** exprs, size_t count);

/* ============== Expression Compilation ============== */

/* Generate code for a define expression */
void omni_codegen_define(CodeGenContext* ctx, OmniValue* expr);

/* Generate code for a let expression */
void omni_codegen_let(CodeGenContext* ctx, OmniValue* expr);

/* Generate code for a lambda expression */
void omni_codegen_lambda(CodeGenContext* ctx, OmniValue* expr);

/* Generate code for an if expression */
void omni_codegen_if(CodeGenContext* ctx, OmniValue* expr);

/* Generate code for a function application */
void omni_codegen_apply(CodeGenContext* ctx, OmniValue* expr);

/* Generate code for a quote expression */
void omni_codegen_quote(CodeGenContext* ctx, OmniValue* expr);

/* ============== Utilities ============== */

/* Mangle a symbol name for C */
char* omni_codegen_mangle(const char* name);

/* Generate a fresh temporary variable name */
char* omni_codegen_temp(CodeGenContext* ctx);

/* Generate a fresh label name */
char* omni_codegen_label(CodeGenContext* ctx);

/* Emit indented text */
void omni_codegen_emit(CodeGenContext* ctx, const char* fmt, ...);

/* Emit text without indentation */
void omni_codegen_emit_raw(CodeGenContext* ctx, const char* fmt, ...);

/* Increase/decrease indentation */
void omni_codegen_indent(CodeGenContext* ctx);
void omni_codegen_dedent(CodeGenContext* ctx);

/* Register a forward declaration */
void omni_codegen_add_forward_decl(CodeGenContext* ctx, const char* decl);

/* Register a lambda definition */
void omni_codegen_add_lambda_def(CodeGenContext* ctx, const char* def);

/* ============== ASAP Memory Management ============== */

/* Emit free_obj calls for variables at given position */
void omni_codegen_emit_frees(CodeGenContext* ctx, int position);

/* Emit cleanup code for scope exit */
void omni_codegen_emit_scope_cleanup(CodeGenContext* ctx, const char** vars, size_t count);

/* ============== CFG-Based Code Generation ============== */

/*
 * Generate code with CFG-aware free placement.
 * This uses liveness analysis to free variables at their last use point
 * on each control flow path, rather than at scope exit.
 *
 * Example:
 *   (let ((x (mk-obj)))
 *     (if cond
 *       (use x)     ;; x freed here on true branch
 *       (other)))   ;; x freed here on false branch (if unused)
 *
 * Instead of:
 *   (let ((x (mk-obj)))
 *     (if cond
 *       (use x)
 *       (other))
 *     ;; x freed here at scope end (too late!)
 */
void omni_codegen_with_cfg(CodeGenContext* ctx, OmniValue* expr);

/*
 * Emit frees for a specific CFG node.
 * Called during CFG-aware code generation.
 */
void omni_codegen_emit_cfg_frees(CodeGenContext* ctx, CFG* cfg, CFGNode* node);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_CODEGEN_H */
