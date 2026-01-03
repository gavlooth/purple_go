/*
 * OmniLisp Compiler
 *
 * Orchestrates parsing, analysis, and code generation.
 * Provides high-level API for compilation workflows.
 */

#ifndef OMNILISP_COMPILER_H
#define OMNILISP_COMPILER_H

#include "../ast/ast.h"
#include "../parser/parser.h"
#include "../analysis/analysis.h"
#include "../codegen/codegen.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Compiler Options ============== */

typedef struct CompilerOptions {
    /* Output options */
    const char* output_file;      /* Output file path (NULL = stdout) */
    bool emit_c_only;             /* Just emit C code, don't compile */
    bool verbose;                 /* Verbose output */

    /* Runtime options */
    const char* runtime_path;     /* Path to runtime library */
    bool use_embedded_runtime;    /* Use embedded runtime */

    /* Optimization options */
    int opt_level;                /* 0=debug, 1=default, 2=aggressive */
    bool enable_reuse;            /* Enable Perceus-style reuse */
    bool enable_dps;              /* Enable destination-passing style */

    /* Debug options */
    bool emit_debug_info;         /* Emit debug symbols */
    bool enable_asan;             /* Enable AddressSanitizer */
    bool enable_tsan;             /* Enable ThreadSanitizer */

    /* C compiler options */
    const char* cc;               /* C compiler (default: gcc) */
    const char* cflags;           /* Additional CFLAGS */
} CompilerOptions;

/* ============== Compiler State ============== */

typedef struct Compiler {
    CompilerOptions options;

    /* Internal state */
    OmniArena* arena;
    AnalysisContext* analysis;
    CodeGenContext* codegen;

    /* Error handling */
    char** errors;
    size_t error_count;
    size_t error_capacity;
} Compiler;

/* ============== Compiler API ============== */

/* Create a new compiler with default options */
Compiler* omni_compiler_new(void);

/* Create a new compiler with custom options */
Compiler* omni_compiler_new_with_options(const CompilerOptions* options);

/* Free compiler resources */
void omni_compiler_free(Compiler* compiler);

/* Set runtime path */
void omni_compiler_set_runtime(Compiler* compiler, const char* path);

/* ============== Compilation ============== */

/* Compile source string to C code */
char* omni_compiler_compile_to_c(Compiler* compiler, const char* source);

/* Compile source string to binary */
bool omni_compiler_compile_to_binary(Compiler* compiler, const char* source, const char* output);

/* Compile source file to C code */
char* omni_compiler_compile_file_to_c(Compiler* compiler, const char* filename);

/* Compile source file to binary */
bool omni_compiler_compile_file_to_binary(Compiler* compiler, const char* filename, const char* output);

/* Compile and run in memory (JIT-style) */
int omni_compiler_run(Compiler* compiler, const char* source);

/* ============== Error Handling ============== */

/* Check if there are errors */
bool omni_compiler_has_errors(Compiler* compiler);

/* Get error count */
size_t omni_compiler_error_count(Compiler* compiler);

/* Get error message at index */
const char* omni_compiler_get_error(Compiler* compiler, size_t index);

/* Clear errors */
void omni_compiler_clear_errors(Compiler* compiler);

/* ============== Utilities ============== */

/* Initialize compiler subsystems */
void omni_compiler_init(void);

/* Cleanup compiler subsystems */
void omni_compiler_cleanup(void);

/* Get compiler version string */
const char* omni_compiler_version(void);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_COMPILER_H */
