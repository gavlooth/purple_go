#include "../include/omnilisp.h"
#include "../pika/pika.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// -- Grammar Rule IDs --
// defined locally for now, could be in header
enum {
    R_EPSILON,      // Matches empty string
    R_CHAR_SPACE, R_CHAR_TAB, R_CHAR_NL,
    R_SPACE,        // Single whitespace char
    R_WS,           // Optional whitespace sequence
    
    R_DIGIT, R_DIGIT1, R_INT,
    
    R_ALPHA, R_SYM_CHAR, R_SYM,
    
    R_LPAREN, R_RPAREN,
    R_LBRACKET, R_RBRACKET,
    R_LBRACE, R_RBRACE,
    R_HASHBRACE,
    
    R_EXPR,         // Top-level expression
    
    // Lists
    R_LIST_INNER,   // Content of list
    R_LIST,         // ( ... )
    
    // Arrays
    R_ARRAY_INNER,
    R_ARRAY,        // [ ... ]
    
    // Dicts
    R_DICT_INNER,
    R_DICT,         // #{ ... }
    
    // Types
    R_TYPE_INNER,
    R_TYPE,         // { ... }

    R_ROOT,
    NUM_RULES
};

static PikaRule rules[NUM_RULES];

// -- Helper to create int arrays for children --
static int* ids(int count, ...) {
    int* arr = malloc(sizeof(int) * count);
    va_list args;
    va_start(args, count);
    for(int i=0; i<count; i++) arr[i] = va_arg(args, int);
    va_end(args);
    return arr;
}

// -- Actions --

static Value* act_int(PikaState* state, size_t pos, PikaMatch match) {
    char buf[64];
    size_t len = match.len > 63 ? 63 : match.len;
    memcpy(buf, state->input + pos, len);
    buf[len] = '\0';
    return mk_int(atol(buf));
}

static Value* act_sym(PikaState* state, size_t pos, PikaMatch match) {
    char* s = malloc(match.len + 1);
    memcpy(s, state->input + pos, match.len);
    s[match.len] = '\0';
    Value* v = mk_sym(s);
    free(s);
    return v;
}

static Value* act_list(PikaState* state, size_t pos, PikaMatch match) {
    // R_LIST = SEQ(LPAREN, R_WS, R_LIST_INNER, R_RPAREN)
    // Manually traverse the matched sequence to find the inner content
    
    size_t current = pos;
    
    // Skip LPAREN (Assume len 1 for "(")
    current += 1;
    
    // Skip WS if present
    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;
    
    // Get LIST_INNER
    PikaMatch* inner_m = pika_get_match(state, current, R_LIST_INNER);
    if (inner_m && inner_m->matched && inner_m->val) return inner_m->val;
    
    return mk_nil();
}

static Value* act_list_inner(PikaState* state, size_t pos, PikaMatch match) {
    // R_LIST_INNER = SEQ(EXPR, R_WS, R_LIST_INNER) / EPSILON
    if (match.len == 0) return mk_nil();
    
    size_t current = pos;
    
    // EXPR
    PikaMatch* expr_m = pika_get_match(state, current, R_EXPR);
    if (!expr_m || !expr_m->matched) return mk_nil();
    Value* head = expr_m->val;
    current += expr_m->len;
    
    // WS
    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;
    
    // Recursive tail
    PikaMatch* rest_m = pika_get_match(state, current, R_LIST_INNER);
    Value* tail = (rest_m && rest_m->matched && rest_m->val) ? rest_m->val : mk_nil();
    
    return mk_cell(head, tail);
}
// -- Initialization --

void omni_init(void) {
    static int inited = 0;
    if (inited) return;
    inited = 1;
    
    // Epsilon (Empty string)
    rules[R_EPSILON] = (PikaRule){ PIKA_TERMINAL, .data.str = "" };
    
    // Whitespace
    rules[R_CHAR_SPACE] = (PikaRule){ PIKA_TERMINAL, .data.str = " " };
    rules[R_CHAR_TAB] = (PikaRule){ PIKA_TERMINAL, .data.str = "\t" };
    rules[R_CHAR_NL] = (PikaRule){ PIKA_TERMINAL, .data.str = "\n" };
    rules[R_SPACE] = (PikaRule){ PIKA_ALT, .data.children = { ids(3, R_CHAR_SPACE, R_CHAR_TAB, R_CHAR_NL), 3 } };
    rules[R_WS] = (PikaRule){ PIKA_REP, .data.children = { ids(1, R_SPACE), 1 } };
    
    // Integers
    rules[R_DIGIT] = (PikaRule){ PIKA_RANGE, .data.range = {'0', '9'} };
    rules[R_DIGIT1] = (PikaRule){ PIKA_RANGE, .data.range = {'1', '9'} }; // if needed
    rules[R_INT] = (PikaRule){ PIKA_POS, .data.children = { ids(1, R_DIGIT), 1 }, .action = act_int };
    
    // Symbols
    rules[R_ALPHA] = (PikaRule){ PIKA_RANGE, .data.range = {'a', 'z'} }; // Need A-Z and special chars
    // rules[R_SYM_CHAR] = ... 
    rules[R_SYM] = (PikaRule){ PIKA_POS, .data.children = { ids(1, R_ALPHA), 1 }, .action = act_sym };
    
    // Brackets
    rules[R_LPAREN] = (PikaRule){ PIKA_TERMINAL, .data.str = "(" };
    rules[R_RPAREN] = (PikaRule){ PIKA_TERMINAL, .data.str = ")" };
    
    // Recursive List Structure:
    // LIST_INNER = (EXPR WS LIST_INNER) / EPSILON
    // We approximate the sequence using logic in the action, 
    // but the matching rule needs to be valid.
    // For Pika, we need explicit rule IDs for the sequence components.
    // Ideally we'd have R_LIST_CONS = SEQ(EXPR, WS, LIST_INNER).
    // For now, let's just make R_LIST_INNER = SEQ(EXPR, WS, LIST_INNER) OPTIONAL?
    // No, standard Right Recursion is: A -> (a A) / epsilon.
    
    // Hack for this step: Define LIST_INNER as SEQ(EXPR, WS, LIST_INNER)
    // AND wrap it in an OPTIONAL or ALT(..., EPSILON).
    // But wait, PIKA_SEQ fails if child fails.
    // So we need R_LIST_REC = SEQ(EXPR, WS, LIST_INNER)
    // And R_LIST_INNER = ALT(R_LIST_REC, EPSILON)
    
    // I don't have R_LIST_REC in the enum.
    // I will abuse R_LIST_CONTENT (from old code) or R_ARRAY_INNER which is unused.
    // Let's repurpose R_TYPE for the sequence part since we aren't parsing types yet.
    #define R_LIST_SEQ R_TYPE 
    
    rules[R_LIST_SEQ] = (PikaRule){ PIKA_SEQ, .data.children = { ids(3, R_EXPR, R_WS, R_LIST_INNER), 3 } };
    rules[R_LIST_INNER] = (PikaRule){ PIKA_ALT, .data.children = { ids(2, R_LIST_SEQ, R_EPSILON), 2 }, .action = act_list_inner };
    
    rules[R_LIST] = (PikaRule){ PIKA_SEQ, .data.children = { ids(3, R_LPAREN, R_WS, R_LIST_INNER), 3 }, .action = act_list };
    // Note: R_LIST used (LPAREN, WS, LIST_INNER) but earlier I used (LPAREN, LIST_INNER, RPAREN)
    // Let's stick to: LPAREN + WS + LIST_INNER (which includes RPAREN? No)
    // Actually standard: LPAREN + WS + LIST_INNER + RPAREN?
    // My act_list expects: LPAREN (1), WS (skip), LIST_INNER (val).
    // Wait, where is RPAREN?
    // If LIST_INNER consumes until epsilon, it stops at RPAREN?
    // No, EPSILON matches empty string.
    // R_LIST needs to match RPAREN explicitly at the end.
    
    // R_LIST = SEQ(LPAREN, WS, LIST_INNER, RPAREN)
    // But LIST_INNER is greedy?
    // LIST_INNER consumes EXPRs. It fails on RPAREN (not EXPR).
    // So it matches EPSILON at RPAREN.
    // Then R_LIST matches RPAREN.
    
    rules[R_LIST] = (PikaRule){ PIKA_SEQ, .data.children = { ids(4, R_LPAREN, R_WS, R_LIST_INNER, R_RPAREN), 4 }, .action = act_list };

    rules[R_ROOT] = (PikaRule){ PIKA_SEQ, .data.children = { ids(2, R_WS, R_EXPR), 2 } }; // Allow leading WS
}

Value* omni_read(const char* input) {
    if (!input) return mk_error("Null input");
    
    // Ensure init
    omni_init();
    
    // Create state
    PikaState* state = pika_new(input, rules, NUM_RULES);
    if (!state) return mk_error("OOM");
    
    // Run
    Value* res = pika_run(state, R_ROOT);
    
    // If result is a SEQ (WS, EXPR), we want the EXPR value.
    // Pika default return for SEQ is ???
    // Our pika_core returns the *text* if no action.
    // But R_ROOT has no action.
    // R_EXPR has an action (via its children).
    
    // We should put an action on R_ROOT to unwrap.
    if (!is_error(res) && pika_get_match(state, 0, R_ROOT)->matched) {
        // We can manually extract the EXPR part
        // Child 0 is WS, Child 1 is EXPR
        PikaMatch* ws = pika_get_match(state, 0, R_WS);
        size_t expr_pos = 0;
        if (ws && ws->matched) expr_pos += ws->len;
        
        PikaMatch* expr = pika_get_match(state, expr_pos, R_EXPR);
        if (expr && expr->matched && expr->val) {
            res = expr->val;
        }
    }
    
    pika_free(state);
    return res;
}

void omni_print(Value* v) {
    char* s = val_to_str(v);
    if (s) {
        printf("%s\n", s);
        free(s);
    } else {
        printf("NULL\n");
    }
}
