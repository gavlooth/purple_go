#include <stdio.h>
#include "include/omnilisp.h"

int main(int argc, char** argv) {
    omni_init();

    if (argc > 1) {
        // Parse command line argument
        printf("Parsing: %s\n", argv[1]);
        Value* v = omni_read(argv[1]);
        if (is_error(v)) {
            printf("Error: %s\n", v->s);
        } else {
            char* s = val_to_str(v);
            printf("Result: %s\n", s);
            free(s);
        }
        return 0;
    }

    // REPL mode (basic)
    char buffer[1024];
    printf("Omnilisp Runtime (Pika Parser)\n> ");
    while (fgets(buffer, sizeof(buffer), stdin)) {
        // Remove newline
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = '\0';

        Value* v = omni_read(buffer);
        if (is_error(v)) {
            printf("Error: %s\n", v->s);
        } else {
            char* s = val_to_str(v);
            printf("=> %s\n", s);
            free(s);
        }
        printf("> ");
    }
    return 0;
}

