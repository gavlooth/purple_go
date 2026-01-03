/*
 * OmniLisp Parser
 *
 * Wraps the Pika parser to parse OmniLisp syntax into AST nodes.
 * The Pika parser is authoritative for the grammar.
 */

#ifndef OMNILISP_PARSER_H
#define OMNILISP_PARSER_H

#include "../ast/ast.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct OmniParser OmniParser;
typedef struct OmniParseError OmniParseError;

/* Parse error information */
struct OmniParseError {
    int start;
    int end;
    int line;
    int column;
    char* message;
    struct OmniParseError* next;
};

/* Parser state */
struct OmniParser {
    const char* input;
    size_t input_len;
    int pos;

    /* Error tracking */
    OmniParseError* errors;
    int error_count;
};

/* ============== Parser API ============== */

/* Create a new parser for the given input */
OmniParser* omni_parser_new(const char* input);
OmniParser* omni_parser_new_n(const char* input, size_t len);

/* Free parser resources */
void omni_parser_free(OmniParser* parser);

/* Parse a single expression, returns NULL on error/EOF */
OmniValue* omni_parser_parse(OmniParser* parser);

/* Parse all expressions in the input */
OmniValue** omni_parser_parse_all(OmniParser* parser, size_t* out_count);

/* Get parse errors */
OmniParseError* omni_parser_get_errors(OmniParser* parser);
void omni_parse_error_free(OmniParseError* err);

/* ============== Convenience Functions ============== */

/* Parse a single expression from a string */
OmniValue* omni_parse_string(const char* input);

/* Parse all expressions from a string */
OmniValue** omni_parse_all_string(const char* input, size_t* out_count);

/* ============== Grammar Initialization ============== */

/* Initialize the OmniLisp grammar (call once at startup) */
void omni_grammar_init(void);

/* Cleanup grammar resources */
void omni_grammar_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_PARSER_H */
