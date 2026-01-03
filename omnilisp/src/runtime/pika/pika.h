#ifndef PURPLE_PIKA_H
#define PURPLE_PIKA_H

#include "../types.h"
#include <stddef.h>
#include <stdbool.h>

// -- Forward Declarations --
struct PikaState;
struct PikaMatch;

// -- Type Definitions --

typedef struct PikaMatch {
    bool matched;
    size_t len;
    Value* val; // Cached AST node
} PikaMatch;

// Semantic action callback type
typedef Value* (*PikaActionFn)(struct PikaState* state, size_t pos, PikaMatch match);

typedef enum {
    PIKA_TERMINAL,  // Literal string
    PIKA_RANGE,     // Character range [a-z]
    PIKA_ANY,       // Any character (.)
    PIKA_SEQ,       // Sequence (A B C)
    PIKA_ALT,       // Prioritized Choice (A / B / C)
    PIKA_REP,       // Zero-or-more (A*)
    PIKA_POS,       // One-or-more (A+)
    PIKA_OPT,       // Optional (A?)
    PIKA_NOT,       // Negative lookahead (!A)
    PIKA_AND,       // Positive lookahead (&A)
    PIKA_REF        // Reference to another rule (by ID)
} PikaRuleType;

typedef struct PikaRule {
    PikaRuleType type;
    union {
        const char* str;
        struct { char min; char max; } range;
        struct { int* subrules; int count; } children;
        struct { int subrule; } ref;
    } data;
    const char* name; // Optional name for debugging
    PikaActionFn action;
} PikaRule;

typedef struct PikaState {
    const char* input;
    size_t input_len;
    
    int num_rules;
    PikaRule* rules;
    
    // Memoization table: [input_len + 1][num_rules]
    PikaMatch* table;
} PikaState;

// -- Public API --

PikaState* pika_new(const char* input, PikaRule* rules, int num_rules);
void pika_free(PikaState* state);

// Run the parser and return the result of the root rule at position 0.
Value* pika_run(PikaState* state, int root_rule_id);

// Helper for accessing matches (useful for semantic actions)
PikaMatch* pika_get_match(PikaState* state, size_t pos, int rule_id);

#endif // PURPLE_PIKA_H