/*
 * OmniLisp CLI - Command Line Interface
 *
 * Pure C replacement for main.go
 * Provides: compile, run, REPL modes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>

#include "../compiler/compiler.h"
#include "../parser/parser.h"
#include "../ast/ast.h"

/* ============== Options ============== */

typedef struct {
    bool compile_mode;        /* -c: emit C code only */
    bool verbose;             /* -v: verbose output */
    const char* output_file;  /* -o: output file */
    const char* eval_expr;    /* -e: evaluate expression */
    const char* runtime_path; /* --runtime: runtime path */
    const char* input_file;   /* Input file */
} CliOptions;

static void print_usage(const char* prog) {
    fprintf(stderr, "OmniLisp - Native Compiler with ASAP Memory Management\n\n");
    fprintf(stderr, "Usage: %s [options] [file.omni]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c             Compile to C code instead of binary\n");
    fprintf(stderr, "  -o <file>      Output file (default: stdout for -c, a.out for binary)\n");
    fprintf(stderr, "  -e <expr>      Evaluate expression from command line\n");
    fprintf(stderr, "  -v             Verbose output\n");
    fprintf(stderr, "  --runtime <path>  Path to runtime library\n");
    fprintf(stderr, "  -h, --help     Show this help\n");
    fprintf(stderr, "  --version      Show version\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s -e '(+ 1 2)'              # Compile and run expression\n", prog);
    fprintf(stderr, "  %s -c -e '(+ 1 2)'           # Emit C code to stdout\n", prog);
    fprintf(stderr, "  %s program.omni              # Compile and run file\n", prog);
    fprintf(stderr, "  %s -c program.omni -o out.c  # Compile file to C\n", prog);
    fprintf(stderr, "  %s -o prog program.omni      # Compile to binary 'prog'\n", prog);
}

static void print_version(void) {
    printf("OmniLisp Compiler version %s\n", omni_compiler_version());
    printf("Built with ASAP (As Static As Possible) memory management\n");
    printf("Target: C99 + POSIX\n");
}

/* ============== REPL ============== */

static void run_repl(Compiler* compiler) {
    printf("OmniLisp Native REPL - ASAP Memory Management\n");
    printf("Type 'help' for commands, 'quit' to exit\n\n");

    char line[4096];
    char** definitions = NULL;
    size_t def_count = 0;
    size_t def_capacity = 0;
    bool show_code = false;

    while (1) {
        if (show_code) {
            printf("omni(c)> ");
        } else {
            printf("omni> ");
        }
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        /* Remove trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        /* Skip empty lines */
        if (len == 0) continue;

        /* Handle commands */
        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            printf("Goodbye!\n");
            break;
        }
        if (strcmp(line, "help") == 0) {
            printf("Commands:\n");
            printf("  quit     - exit the REPL\n");
            printf("  code     - toggle C code display\n");
            printf("  defs     - show current definitions\n");
            printf("  clear    - clear all definitions\n");
            printf("  help     - show this help\n");
            printf("\nLanguage:\n");
            printf("  (define name value)     - define a variable\n");
            printf("  (define (f x) body)     - define a function\n");
            printf("  (lambda (x) body)       - anonymous function\n");
            printf("  (let [x val] body)      - local binding\n");
            printf("  (if cond then else)     - conditional\n");
            printf("\nPrimitives:\n");
            printf("  Arithmetic: + - * / %%\n");
            printf("  Comparison: < > <= >= =\n");
            printf("  Lists: cons car cdr null?\n");
            printf("  I/O: display print newline\n");
            continue;
        }
        if (strcmp(line, "code") == 0) {
            show_code = !show_code;
            printf("C code display %s\n", show_code ? "ON" : "OFF");
            continue;
        }
        if (strcmp(line, "clear") == 0) {
            for (size_t i = 0; i < def_count; i++) {
                free(definitions[i]);
            }
            def_count = 0;
            printf("Definitions cleared\n");
            continue;
        }
        if (strcmp(line, "defs") == 0) {
            if (def_count == 0) {
                printf("No definitions\n");
            } else {
                printf("Current definitions:\n");
                for (size_t i = 0; i < def_count; i++) {
                    printf("  %s\n", definitions[i]);
                }
            }
            continue;
        }

        /* Skip bare words */
        if (line[0] != '(' && line[0] != '\'' && line[0] != '[') {
            printf("Unknown command: %s (use 'help' for commands)\n", line);
            continue;
        }

        /* Parse to check if it's a definition */
        OmniValue* expr = omni_parse_string(line);
        if (!expr) {
            printf("Parse error\n");
            continue;
        }

        bool is_define = omni_is_cell(expr) && omni_is_sym(omni_car(expr)) &&
                         strcmp(omni_car(expr)->str_val, "define") == 0;

        if (is_define) {
            /* Store definition */
            if (def_count >= def_capacity) {
                def_capacity = def_capacity ? def_capacity * 2 : 8;
                definitions = realloc(definitions, def_capacity * sizeof(char*));
            }
            definitions[def_count++] = strdup(line);
            printf("Defined\n");
            continue;
        }

        /* Build full program with definitions */
        size_t total_len = 0;
        for (size_t i = 0; i < def_count; i++) {
            total_len += strlen(definitions[i]) + 1;
        }
        total_len += len + 1;

        char* full_input = malloc(total_len);
        char* p = full_input;
        for (size_t i = 0; i < def_count; i++) {
            size_t dlen = strlen(definitions[i]);
            memcpy(p, definitions[i], dlen);
            p += dlen;
            *p++ = '\n';
        }
        memcpy(p, line, len + 1);

        /* Compile and run */
        if (show_code) {
            char* code = omni_compiler_compile_to_c(compiler, full_input);
            if (code) {
                printf("--- C code ---\n%s--- end ---\n", code);
                free(code);
            }
        }

        int result = omni_compiler_run(compiler, full_input);
        if (omni_compiler_has_errors(compiler)) {
            for (size_t i = 0; i < omni_compiler_error_count(compiler); i++) {
                fprintf(stderr, "Error: %s\n", omni_compiler_get_error(compiler, i));
            }
        }

        free(full_input);
        (void)result;
    }

    /* Cleanup */
    for (size_t i = 0; i < def_count; i++) {
        free(definitions[i]);
    }
    free(definitions);
}

/* ============== Main ============== */

int main(int argc, char** argv) {
    CliOptions opts = {0};

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {"runtime", required_argument, 0, 'r'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "cho:e:vr:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            opts.compile_mode = true;
            break;
        case 'o':
            opts.output_file = optarg;
            break;
        case 'e':
            opts.eval_expr = optarg;
            break;
        case 'v':
            opts.verbose = true;
            break;
        case 'r':
            opts.runtime_path = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        case 'V':
            print_version();
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (optind < argc) {
        opts.input_file = argv[optind];
    }

    /* Auto-detect runtime path */
    if (!opts.runtime_path) {
        /* Check relative to executable */
        char* exe_dir = realpath(argv[0], NULL);
        if (exe_dir) {
            char* slash = strrchr(exe_dir, '/');
            if (slash) *slash = '\0';

            char runtime_check[1024];
            snprintf(runtime_check, sizeof(runtime_check), "%s/../runtime/libpurple.a", exe_dir);
            if (access(runtime_check, F_OK) == 0) {
                snprintf(runtime_check, sizeof(runtime_check), "%s/../runtime", exe_dir);
                opts.runtime_path = strdup(runtime_check);
            }
            free(exe_dir);
        }

        /* Check current directory */
        if (!opts.runtime_path && access("runtime/libpurple.a", F_OK) == 0) {
            opts.runtime_path = "runtime";
        }
    }

    /* Create compiler */
    CompilerOptions comp_opts = {
        .output_file = opts.output_file,
        .emit_c_only = opts.compile_mode,
        .verbose = opts.verbose,
        .runtime_path = opts.runtime_path,
        .use_embedded_runtime = (opts.runtime_path == NULL),
        .opt_level = 2,
        .cc = "gcc",
    };

    Compiler* compiler = omni_compiler_new_with_options(&comp_opts);

    /* Get input */
    char* input = NULL;

    if (opts.eval_expr) {
        input = strdup(opts.eval_expr);
    } else if (opts.input_file) {
        FILE* f = fopen(opts.input_file, "r");
        if (!f) {
            fprintf(stderr, "Error: cannot open file: %s\n", opts.input_file);
            omni_compiler_free(compiler);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        input = malloc(size + 1);
        size_t read = fread(input, 1, size, f);
        input[read] = '\0';
        fclose(f);
    } else {
        /* Check if stdin is a terminal */
        if (isatty(STDIN_FILENO)) {
            /* Interactive REPL mode */
            run_repl(compiler);
            omni_compiler_free(compiler);
            return 0;
        }

        /* Read from stdin */
        size_t capacity = 4096;
        size_t len = 0;
        input = malloc(capacity);
        int c;
        while ((c = getchar()) != EOF) {
            if (len + 1 >= capacity) {
                capacity *= 2;
                input = realloc(input, capacity);
            }
            input[len++] = c;
        }
        input[len] = '\0';
    }

    /* Skip empty input */
    bool empty = true;
    for (const char* p = input; *p; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            empty = false;
            break;
        }
    }

    if (empty) {
        /* Empty input - go to REPL */
        free(input);
        run_repl(compiler);
        omni_compiler_free(compiler);
        return 0;
    }

    int exit_code = 0;

    if (opts.compile_mode) {
        /* Emit C code */
        char* code = omni_compiler_compile_to_c(compiler, input);
        if (code) {
            if (opts.output_file) {
                FILE* f = fopen(opts.output_file, "w");
                if (f) {
                    fputs(code, f);
                    fclose(f);
                    if (opts.verbose) {
                        fprintf(stderr, "C code written to %s\n", opts.output_file);
                    }
                } else {
                    fprintf(stderr, "Error: cannot write to %s\n", opts.output_file);
                    exit_code = 1;
                }
            } else {
                printf("%s", code);
            }
            free(code);
        } else {
            for (size_t i = 0; i < omni_compiler_error_count(compiler); i++) {
                fprintf(stderr, "Error: %s\n", omni_compiler_get_error(compiler, i));
            }
            exit_code = 1;
        }
    } else if (opts.output_file) {
        /* Compile to binary */
        if (!omni_compiler_compile_to_binary(compiler, input, opts.output_file)) {
            for (size_t i = 0; i < omni_compiler_error_count(compiler); i++) {
                fprintf(stderr, "Error: %s\n", omni_compiler_get_error(compiler, i));
            }
            exit_code = 1;
        } else if (opts.verbose) {
            fprintf(stderr, "Binary written to %s\n", opts.output_file);
        }
    } else {
        /* Compile and run */
        exit_code = omni_compiler_run(compiler, input);
        if (omni_compiler_has_errors(compiler)) {
            for (size_t i = 0; i < omni_compiler_error_count(compiler); i++) {
                fprintf(stderr, "Error: %s\n", omni_compiler_get_error(compiler, i));
            }
            exit_code = 1;
        }
    }

    free(input);
    omni_compiler_free(compiler);
    omni_compiler_cleanup();

    return exit_code;
}
