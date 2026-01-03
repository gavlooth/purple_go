/*
 * Pika Parser - Packrat PEG Parser
 *
 * Based on the Pika parsing algorithm.
 * Adapted for OmniLisp C toolchain.
 */

#ifndef PIKA_PARSER_H
#define PIKA_PARSER_H

#include "../ast/ast.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct PikaState;
struct PikaMatch;

/* Match result */
typedef struct PikaMatch {
    bool matched;
    size_t len;
    OmniValue* val;  /* Cached AST node */
} PikaMatch;

/* Semantic action callback */
typedef OmniValue* (*PikaActionFn)(struct PikaState* state, size_t pos, PikaMatch match);

/* Rule types */
typedef enum {
    PIKA_TERMINAL,  /* Literal string */
    PIKA_RANGE,     /* Character range [a-z] */
    PIKA_ANY,       /* Any character (.) */
    PIKA_SEQ,       /* Sequence (A B C) */
    PIKA_ALT,       /* Prioritized Choice (A / B / C) */
    PIKA_REP,       /* Zero-or-more (A*) */
    PIKA_POS,       /* One-or-more (A+) */
    PIKA_OPT,       /* Optional (A?) */
    PIKA_NOT,       /* Negative lookahead (!A) */
    PIKA_AND,       /* Positive lookahead (&A) */
    PIKA_REF        /* Reference to another rule (by ID) */
} PikaRuleType;

/* Rule definition */
typedef struct PikaRule {
    PikaRuleType type;
    union {
        const char* str;
        struct { char min; char max; } range;
        struct { int* subrules; int count; } children;
        struct { int subrule; } ref;
    } data;
    const char* name;       /* Optional name for debugging */
    PikaActionFn action;    /* Semantic action */
} PikaRule;

/* Parser state */
typedef struct PikaState {
    const char* input;
    size_t input_len;

    int num_rules;
    PikaRule* rules;

    /* Memoization table: [input_len + 1][num_rules] */
    PikaMatch* table;
} PikaState;

/* ============== Public API ============== */

/* Create a new parser state */
PikaState* pika_new(const char* input, PikaRule* rules, int num_rules);

/* Free parser state */
void pika_free(PikaState* state);

/* Run the parser and return the result of the root rule at position 0 */
OmniValue* pika_run(PikaState* state, int root_rule_id);

/* Get match at position for rule (useful for semantic actions) */
PikaMatch* pika_get_match(PikaState* state, size_t pos, int rule_id);

#ifdef __cplusplus
}
#endif

#endif /* PIKA_PARSER_H */
