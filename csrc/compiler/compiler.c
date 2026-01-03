/*
 * OmniLisp Compiler Implementation
 */

#include "compiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

#define OMNILISP_VERSION "0.1.0"

/* ============== Initialization ============== */

static bool g_initialized = false;

void omni_compiler_init(void) {
    if (g_initialized) return;
    omni_ast_arena_init();
    omni_grammar_init();
    g_initialized = true;
}

void omni_compiler_cleanup(void) {
    if (!g_initialized) return;
    omni_grammar_cleanup();
    omni_ast_arena_cleanup();
    g_initialized = false;
}

const char* omni_compiler_version(void) {
    return OMNILISP_VERSION;
}

/* ============== Compiler Management ============== */

static CompilerOptions default_options(void) {
    CompilerOptions opts = {
        .output_file = NULL,
        .emit_c_only = false,
        .verbose = false,
        .runtime_path = NULL,
        .use_embedded_runtime = true,
        .opt_level = 1,
        .enable_reuse = false,
        .enable_dps = false,
        .emit_debug_info = false,
        .enable_asan = false,
        .enable_tsan = false,
        .cc = "gcc",
        .cflags = NULL,
    };
    return opts;
}

Compiler* omni_compiler_new(void) {
    CompilerOptions opts = default_options();
    return omni_compiler_new_with_options(&opts);
}

Compiler* omni_compiler_new_with_options(const CompilerOptions* options) {
    omni_compiler_init();

    Compiler* c = malloc(sizeof(Compiler));
    if (!c) return NULL;
    memset(c, 0, sizeof(Compiler));

    if (options) {
        c->options = *options;
    } else {
        c->options = default_options();
    }

    return c;
}

void omni_compiler_free(Compiler* compiler) {
    if (!compiler) return;

    if (compiler->analysis) {
        omni_analysis_free(compiler->analysis);
    }
    if (compiler->codegen) {
        omni_codegen_free(compiler->codegen);
    }

    for (size_t i = 0; i < compiler->error_count; i++) {
        free(compiler->errors[i]);
    }
    free(compiler->errors);

    free(compiler);
}

void omni_compiler_set_runtime(Compiler* compiler, const char* path) {
    if (compiler) {
        compiler->options.runtime_path = path;
        compiler->options.use_embedded_runtime = (path == NULL);
    }
}

/* ============== Error Handling ============== */

static void add_error(Compiler* c, const char* fmt, ...) {
    if (c->error_count >= c->error_capacity) {
        c->error_capacity = c->error_capacity ? c->error_capacity * 2 : 8;
        c->errors = realloc(c->errors, c->error_capacity * sizeof(char*));
    }

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    c->errors[c->error_count++] = strdup(buf);
}

bool omni_compiler_has_errors(Compiler* compiler) {
    return compiler && compiler->error_count > 0;
}

size_t omni_compiler_error_count(Compiler* compiler) {
    return compiler ? compiler->error_count : 0;
}

const char* omni_compiler_get_error(Compiler* compiler, size_t index) {
    if (!compiler || index >= compiler->error_count) return NULL;
    return compiler->errors[index];
}

void omni_compiler_clear_errors(Compiler* compiler) {
    if (!compiler) return;
    for (size_t i = 0; i < compiler->error_count; i++) {
        free(compiler->errors[i]);
    }
    compiler->error_count = 0;
}

/* ============== Compilation ============== */

char* omni_compiler_compile_to_c(Compiler* compiler, const char* source) {
    if (!compiler || !source) return NULL;

    omni_compiler_clear_errors(compiler);

    /* Parse */
    OmniParser* parser = omni_parser_new(source);
    size_t expr_count;
    OmniValue** exprs = omni_parser_parse_all(parser, &expr_count);

    if (omni_parser_get_errors(parser)) {
        OmniParseError* err = omni_parser_get_errors(parser);
        while (err) {
            add_error(compiler, "Parse error at line %d, col %d: %s",
                      err->line, err->column, err->message);
            err = err->next;
        }
        omni_parser_free(parser);
        return NULL;
    }
    omni_parser_free(parser);

    if (expr_count == 0) {
        add_error(compiler, "No expressions to compile");
        return NULL;
    }

    /* Generate code */
    CodeGenContext* codegen = omni_codegen_new_buffer();
    if (compiler->options.runtime_path) {
        omni_codegen_set_runtime(codegen, compiler->options.runtime_path);
    }

    omni_codegen_program(codegen, exprs, expr_count);

    char* output = omni_codegen_get_output(codegen);
    omni_codegen_free(codegen);

    free(exprs);

    return output;
}

static char* create_temp_file(const char* suffix) {
    char* path = malloc(256);
    snprintf(path, 256, "/tmp/omnilisp_XXXXXX%s", suffix);
    int fd = mkstemps(path, strlen(suffix));
    if (fd < 0) {
        free(path);
        return NULL;
    }
    close(fd);
    return path;
}

bool omni_compiler_compile_to_binary(Compiler* compiler, const char* source, const char* output) {
    if (!compiler || !source || !output) return false;

    /* Generate C code */
    char* c_code = omni_compiler_compile_to_c(compiler, source);
    if (!c_code) return false;

    /* Write to temp file */
    char* c_file = create_temp_file(".c");
    if (!c_file) {
        add_error(compiler, "Failed to create temp file: %s", strerror(errno));
        free(c_code);
        return false;
    }

    FILE* f = fopen(c_file, "w");
    if (!f) {
        add_error(compiler, "Failed to write temp file: %s", strerror(errno));
        free(c_file);
        free(c_code);
        return false;
    }
    fputs(c_code, f);
    fclose(f);
    free(c_code);

    /* Build gcc command */
    char cmd[2048];
    const char* cc = compiler->options.cc ? compiler->options.cc : "gcc";

    if (compiler->options.runtime_path) {
        snprintf(cmd, sizeof(cmd),
                 "%s -std=c99 -pthread -O%d %s%s%s -I%s/include -o %s %s -L%s -lpurple",
                 cc,
                 compiler->options.opt_level,
                 compiler->options.emit_debug_info ? "-g " : "",
                 compiler->options.enable_asan ? "-fsanitize=address " : "",
                 compiler->options.enable_tsan ? "-fsanitize=thread " : "",
                 compiler->options.runtime_path,
                 output,
                 c_file,
                 compiler->options.runtime_path);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "%s -std=c99 -pthread -O%d %s%s%s -o %s %s",
                 cc,
                 compiler->options.opt_level,
                 compiler->options.emit_debug_info ? "-g " : "",
                 compiler->options.enable_asan ? "-fsanitize=address " : "",
                 compiler->options.enable_tsan ? "-fsanitize=thread " : "",
                 output,
                 c_file);
    }

    if (compiler->options.verbose) {
        fprintf(stderr, "Compiling: %s\n", cmd);
    }

    int status = system(cmd);
    unlink(c_file);
    free(c_file);

    if (status != 0) {
        add_error(compiler, "C compilation failed with status %d", status);
        return false;
    }

    return true;
}

char* omni_compiler_compile_file_to_c(Compiler* compiler, const char* filename) {
    if (!compiler || !filename) return NULL;

    FILE* f = fopen(filename, "r");
    if (!f) {
        add_error(compiler, "Cannot open file: %s", filename);
        return NULL;
    }

    /* Read file contents */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* source = malloc(size + 1);
    size_t read = fread(source, 1, size, f);
    source[read] = '\0';
    fclose(f);

    char* result = omni_compiler_compile_to_c(compiler, source);
    free(source);
    return result;
}

bool omni_compiler_compile_file_to_binary(Compiler* compiler, const char* filename, const char* output) {
    if (!compiler || !filename || !output) return false;

    FILE* f = fopen(filename, "r");
    if (!f) {
        add_error(compiler, "Cannot open file: %s", filename);
        return false;
    }

    /* Read file contents */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* source = malloc(size + 1);
    size_t read = fread(source, 1, size, f);
    source[read] = '\0';
    fclose(f);

    bool result = omni_compiler_compile_to_binary(compiler, source, output);
    free(source);
    return result;
}

int omni_compiler_run(Compiler* compiler, const char* source) {
    if (!compiler || !source) return -1;

    /* Compile to temp binary */
    char* bin_file = create_temp_file("");
    if (!bin_file) {
        add_error(compiler, "Failed to create temp file");
        return -1;
    }

    if (!omni_compiler_compile_to_binary(compiler, source, bin_file)) {
        unlink(bin_file);
        free(bin_file);
        return -1;
    }

    /* Execute */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child process */
        execl(bin_file, bin_file, NULL);
        _exit(127);  /* exec failed */
    } else if (pid < 0) {
        add_error(compiler, "Failed to fork: %s", strerror(errno));
        unlink(bin_file);
        free(bin_file);
        return -1;
    }

    /* Parent process - wait for child */
    int status;
    waitpid(pid, &status, 0);

    unlink(bin_file);
    free(bin_file);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}
