/*
 * OmniLisp Parser Implementation
 *
 * Uses the Pika packrat PEG parser for parsing OmniLisp syntax.
 * Grammar follows the OmniLisp design documents.
 */

#include "parser.h"
#include "pika.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

/* ============== Grammar Rule IDs ============== */

enum {
    R_EPSILON,
    R_CHAR_SPACE, R_CHAR_TAB, R_CHAR_NL, R_CHAR_CR, R_SEMICOLON,
    R_SPACE,
    R_COMMENT,
    R_WS,

    R_DIGIT, R_DIGIT1, R_INT, R_SIGN, R_SIGNED_INT,
    R_FLOAT_FRAC, R_FLOAT,

    R_ALPHA, R_ALPHA_UPPER, R_SYM_SPECIAL, R_SYM_CHAR, R_SYM_FIRST, R_SYM,
    R_KEYWORD,

    R_CHAR_ESCAPE, R_CHAR_LIT, R_STRING_CHAR, R_STRING,

    R_LPAREN, R_RPAREN,
    R_LBRACKET, R_RBRACKET,
    R_LBRACE, R_RBRACE,
    R_HASHBRACE, R_HASHPAREN, R_HASHBRACKET,

    R_QUOTE_CHAR, R_QUASIQUOTE_CHAR, R_UNQUOTE_SPLICE_CHARS, R_UNQUOTE_CHAR,

    R_EXPR,
    R_ATOM,

    R_LIST_INNER, R_LIST_SEQ,
    R_LIST,

    R_ARRAY_INNER, R_ARRAY_SEQ,
    R_ARRAY,

    R_QUOTED,

    R_PROGRAM_INNER, R_PROGRAM_SEQ,
    R_PROGRAM,

    NUM_RULES
};

static PikaRule g_rules[NUM_RULES] = {{0}};
static int* g_rule_ids[NUM_RULES] = {NULL};
static bool g_grammar_initialized = false;

/* ============== Helper Functions ============== */

static int* ids(int count, ...) {
    int* arr = malloc(sizeof(int) * count);
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) arr[i] = va_arg(args, int);
    va_end(args);
    return arr;
}

/* ============== Semantic Actions ============== */

static OmniValue* act_int(PikaState* state, size_t pos, PikaMatch match) {
    char buf[64];
    size_t len = match.len > 63 ? 63 : match.len;
    memcpy(buf, state->input + pos, len);
    buf[len] = '\0';
    return omni_new_int(atol(buf));
}

static OmniValue* act_sym(PikaState* state, size_t pos, PikaMatch match) {
    char* s = malloc(match.len + 1);
    memcpy(s, state->input + pos, match.len);
    s[match.len] = '\0';
    OmniValue* v = omni_new_sym(s);
    free(s);
    return v;
}

static OmniValue* act_list(PikaState* state, size_t pos, PikaMatch match) {
    /* Get LIST_INNER content */
    size_t current = pos + 1;  /* Skip ( */

    /* Skip whitespace */
    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;

    /* Get inner content */
    PikaMatch* inner_m = pika_get_match(state, current, R_LIST_INNER);
    if (inner_m && inner_m->matched && inner_m->val) return inner_m->val;

    return omni_nil;
}

static OmniValue* act_list_inner(PikaState* state, size_t pos, PikaMatch match) {
    if (match.len == 0) return omni_nil;

    size_t current = pos;

    /* EXPR */
    PikaMatch* expr_m = pika_get_match(state, current, R_EXPR);
    if (!expr_m || !expr_m->matched) return omni_nil;
    OmniValue* head = expr_m->val;
    current += expr_m->len;

    /* WS */
    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;

    /* Recursive tail */
    PikaMatch* rest_m = pika_get_match(state, current, R_LIST_INNER);
    OmniValue* tail = (rest_m && rest_m->matched && rest_m->val) ? rest_m->val : omni_nil;

    return omni_new_cell(head, tail);
}

static OmniValue* act_array(PikaState* state, size_t pos, PikaMatch match) {
    /* Get ARRAY_INNER content, convert to array */
    size_t current = pos + 1;  /* Skip [ */

    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;

    PikaMatch* inner_m = pika_get_match(state, current, R_ARRAY_INNER);
    if (inner_m && inner_m->matched && inner_m->val) {
        /* Convert list to array */
        OmniValue* list = inner_m->val;
        size_t len = 0;
        for (OmniValue* p = list; !omni_is_nil(p) && omni_is_cell(p); p = omni_cdr(p)) len++;

        OmniValue* arr = omni_new_array(len);
        for (OmniValue* p = list; !omni_is_nil(p) && omni_is_cell(p); p = omni_cdr(p)) {
            omni_array_push(arr, omni_car(p));
        }
        return arr;
    }
    return omni_new_array(0);
}

static OmniValue* act_array_inner(PikaState* state, size_t pos, PikaMatch match) {
    /* Same as list_inner, just builds a list */
    return act_list_inner(state, pos, match);
}

static OmniValue* act_quoted(PikaState* state, size_t pos, PikaMatch match) {
    char quote_char = state->input[pos];
    size_t expr_pos = pos + 1;

    /* Handle ,@ (unquote-splicing) */
    if (quote_char == ',' && pos + 1 < state->input_len && state->input[pos + 1] == '@') {
        expr_pos = pos + 2;
        PikaMatch* ws_m = pika_get_match(state, expr_pos, R_WS);
        if (ws_m && ws_m->matched) expr_pos += ws_m->len;

        PikaMatch* expr_m = pika_get_match(state, expr_pos, R_EXPR);
        if (expr_m && expr_m->matched && expr_m->val) {
            return omni_new_cell(omni_new_sym("unquote-splicing"),
                                 omni_new_cell(expr_m->val, omni_nil));
        }
    }

    PikaMatch* ws_m = pika_get_match(state, expr_pos, R_WS);
    if (ws_m && ws_m->matched) expr_pos += ws_m->len;

    PikaMatch* expr_m = pika_get_match(state, expr_pos, R_EXPR);
    if (expr_m && expr_m->matched && expr_m->val) {
        const char* quote_sym;
        switch (quote_char) {
            case '\'': quote_sym = "quote"; break;
            case '`': quote_sym = "quasiquote"; break;
            case ',': quote_sym = "unquote"; break;
            default: quote_sym = "quote"; break;
        }
        return omni_new_cell(omni_new_sym(quote_sym),
                             omni_new_cell(expr_m->val, omni_nil));
    }

    return omni_nil;
}

static OmniValue* act_program(PikaState* state, size_t pos, PikaMatch match) {
    size_t current = pos;

    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;

    PikaMatch* inner_m = pika_get_match(state, current, R_PROGRAM_INNER);
    if (inner_m && inner_m->matched && inner_m->val) return inner_m->val;

    return omni_nil;
}

static OmniValue* act_program_inner(PikaState* state, size_t pos, PikaMatch match) {
    return act_list_inner(state, pos, match);
}

/* ============== Grammar Initialization ============== */

void omni_grammar_init(void) {
    if (g_grammar_initialized) return;

    /* Epsilon */
    g_rules[R_EPSILON] = (PikaRule){ PIKA_TERMINAL, .data.str = "" };

    /* Whitespace characters */
    g_rules[R_CHAR_SPACE] = (PikaRule){ PIKA_TERMINAL, .data.str = " " };
    g_rules[R_CHAR_TAB] = (PikaRule){ PIKA_TERMINAL, .data.str = "\t" };
    g_rules[R_CHAR_NL] = (PikaRule){ PIKA_TERMINAL, .data.str = "\n" };
    g_rules[R_CHAR_CR] = (PikaRule){ PIKA_TERMINAL, .data.str = "\r" };
    g_rules[R_SEMICOLON] = (PikaRule){ PIKA_TERMINAL, .data.str = ";" };

    /* Single whitespace */
    g_rule_ids[R_SPACE] = ids(4, R_CHAR_SPACE, R_CHAR_TAB, R_CHAR_NL, R_CHAR_CR);
    g_rules[R_SPACE] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_SPACE], 4 } };

    /* Comments: ; to end of line */
    g_rule_ids[R_COMMENT] = ids(2, R_SEMICOLON, R_CHAR_NL);
    g_rules[R_COMMENT] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_COMMENT], 2 } };

    /* Whitespace sequence */
    g_rule_ids[R_WS] = ids(1, R_SPACE);
    g_rules[R_WS] = (PikaRule){ PIKA_REP, .data.children = { g_rule_ids[R_WS], 1 } };

    /* Digits */
    g_rules[R_DIGIT] = (PikaRule){ PIKA_RANGE, .data.range = { '0', '9' } };
    g_rules[R_DIGIT1] = (PikaRule){ PIKA_RANGE, .data.range = { '1', '9' } };

    /* Integer */
    g_rule_ids[R_INT] = ids(1, R_DIGIT);
    g_rules[R_INT] = (PikaRule){ PIKA_POS, .data.children = { g_rule_ids[R_INT], 1 }, .action = act_int };

    /* Signed integer */
    g_rules[R_SIGNED_INT] = g_rules[R_INT];

    /* Alphabetic */
    g_rules[R_ALPHA] = (PikaRule){ PIKA_RANGE, .data.range = { 'a', 'z' } };
    g_rules[R_ALPHA_UPPER] = (PikaRule){ PIKA_RANGE, .data.range = { 'A', 'Z' } };

    /* Symbol characters: alpha, alpha-upper, digit, and common operators
     * We need to EXCLUDE delimiters: ( ) [ ] { } and whitespace
     * Valid symbol chars (by ASCII range):
     * - '!' to '\'' (33-39): ! " # $ % & '   (excludes ( ) starting at 40)
     * - '*' to '/' (42-47): * + , - . /      (excludes ( ) at 40-41)
     * - ':' to '@' (58-64): : ; < = > ? @    (comparison operators)
     * - '_' (95): underscore
     * - '|' (124), '~' (126): other operators
     * We use multiple rules and combine with ALT
     */
    /* We'll define the ranges we need as temporary rules using R_SIGN, R_FLOAT_FRAC, etc. */
    /* R_SIGN (12) - range ':'  to '@' for < > = etc */
    g_rules[R_SIGN] = (PikaRule){ PIKA_RANGE, .data.range = { ':', '@' } };  /* :;<=>?@ */
    /* R_FLOAT_FRAC (15) - range '!' to '\'' for other symbols */
    g_rules[R_FLOAT_FRAC] = (PikaRule){ PIKA_RANGE, .data.range = { '!', '\'' } };  /* !"#$%&' */
    /* R_SYM_SPECIAL - range '*' to '/' */
    g_rules[R_SYM_SPECIAL] = (PikaRule){ PIKA_RANGE, .data.range = { '*', '/' } };  /* *+,-./ */

    /* R_SYM_FIRST: first char of symbol (not a digit) */
    g_rule_ids[R_SYM_FIRST] = ids(5, R_ALPHA, R_ALPHA_UPPER, R_SYM_SPECIAL, R_SIGN, R_FLOAT_FRAC);
    g_rules[R_SYM_FIRST] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_SYM_FIRST], 5 } };

    /* Symbol characters: alpha, alpha-upper, digit, and operators */
    g_rule_ids[R_SYM_CHAR] = ids(6, R_ALPHA, R_ALPHA_UPPER, R_DIGIT, R_SYM_SPECIAL, R_SIGN, R_FLOAT_FRAC);
    g_rules[R_SYM_CHAR] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_SYM_CHAR], 6 } };

    /* Symbol: first char then rest */
    g_rule_ids[R_SYM] = ids(1, R_SYM_CHAR);
    g_rules[R_SYM] = (PikaRule){ PIKA_POS, .data.children = { g_rule_ids[R_SYM], 1 }, .action = act_sym };

    /* Brackets */
    g_rules[R_LPAREN] = (PikaRule){ PIKA_TERMINAL, .data.str = "(" };
    g_rules[R_RPAREN] = (PikaRule){ PIKA_TERMINAL, .data.str = ")" };
    g_rules[R_LBRACKET] = (PikaRule){ PIKA_TERMINAL, .data.str = "[" };
    g_rules[R_RBRACKET] = (PikaRule){ PIKA_TERMINAL, .data.str = "]" };
    g_rules[R_LBRACE] = (PikaRule){ PIKA_TERMINAL, .data.str = "{" };
    g_rules[R_RBRACE] = (PikaRule){ PIKA_TERMINAL, .data.str = "}" };

    /* Quote characters */
    g_rules[R_QUOTE_CHAR] = (PikaRule){ PIKA_TERMINAL, .data.str = "'" };
    g_rules[R_QUASIQUOTE_CHAR] = (PikaRule){ PIKA_TERMINAL, .data.str = "`" };
    g_rules[R_UNQUOTE_CHAR] = (PikaRule){ PIKA_TERMINAL, .data.str = "," };

    /* ATOM = INT / SYM */
    g_rule_ids[R_ATOM] = ids(2, R_INT, R_SYM);
    g_rules[R_ATOM] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_ATOM], 2 } };

    /* LIST_SEQ = EXPR WS LIST_INNER */
    g_rule_ids[R_LIST_SEQ] = ids(3, R_EXPR, R_WS, R_LIST_INNER);
    g_rules[R_LIST_SEQ] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_LIST_SEQ], 3 } };

    /* LIST_INNER = LIST_SEQ / EPSILON */
    g_rule_ids[R_LIST_INNER] = ids(2, R_LIST_SEQ, R_EPSILON);
    g_rules[R_LIST_INNER] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_LIST_INNER], 2 }, .action = act_list_inner };

    /* LIST = ( WS LIST_INNER ) */
    g_rule_ids[R_LIST] = ids(4, R_LPAREN, R_WS, R_LIST_INNER, R_RPAREN);
    g_rules[R_LIST] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_LIST], 4 }, .action = act_list };

    /* ARRAY_SEQ = EXPR WS ARRAY_INNER */
    g_rule_ids[R_ARRAY_SEQ] = ids(3, R_EXPR, R_WS, R_ARRAY_INNER);
    g_rules[R_ARRAY_SEQ] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_ARRAY_SEQ], 3 } };

    /* ARRAY_INNER = ARRAY_SEQ / EPSILON */
    g_rule_ids[R_ARRAY_INNER] = ids(2, R_ARRAY_SEQ, R_EPSILON);
    g_rules[R_ARRAY_INNER] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_ARRAY_INNER], 2 }, .action = act_array_inner };

    /* ARRAY = [ WS ARRAY_INNER ] */
    g_rule_ids[R_ARRAY] = ids(4, R_LBRACKET, R_WS, R_ARRAY_INNER, R_RBRACKET);
    g_rules[R_ARRAY] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_ARRAY], 4 }, .action = act_array };

    /* QUOTED = 'EXPR | `EXPR | ,EXPR */
    g_rule_ids[R_QUOTED] = ids(2, R_QUOTE_CHAR, R_EXPR);
    g_rules[R_QUOTED] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_QUOTED], 2 }, .action = act_quoted };

    /* EXPR = LIST / ARRAY / QUOTED / ATOM */
    g_rule_ids[R_EXPR] = ids(4, R_LIST, R_ARRAY, R_QUOTED, R_ATOM);
    g_rules[R_EXPR] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_EXPR], 4 } };

    /* PROGRAM_SEQ = EXPR WS PROGRAM_INNER */
    g_rule_ids[R_PROGRAM_SEQ] = ids(3, R_EXPR, R_WS, R_PROGRAM_INNER);
    g_rules[R_PROGRAM_SEQ] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_PROGRAM_SEQ], 3 } };

    /* PROGRAM_INNER = PROGRAM_SEQ / EPSILON */
    g_rule_ids[R_PROGRAM_INNER] = ids(2, R_PROGRAM_SEQ, R_EPSILON);
    g_rules[R_PROGRAM_INNER] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_PROGRAM_INNER], 2 }, .action = act_program_inner };

    /* PROGRAM = WS PROGRAM_INNER */
    g_rule_ids[R_PROGRAM] = ids(2, R_WS, R_PROGRAM_INNER);
    g_rules[R_PROGRAM] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_PROGRAM], 2 }, .action = act_program };

    g_grammar_initialized = true;
}

void omni_grammar_cleanup(void) {
    if (!g_grammar_initialized) return;

    /* Free rule ID arrays */
    for (int i = 0; i < NUM_RULES; i++) {
        if (g_rule_ids[i]) {
            free(g_rule_ids[i]);
            g_rule_ids[i] = NULL;
        }
    }

    g_grammar_initialized = false;
}

/* ============== Parser API ============== */

OmniParser* omni_parser_new(const char* input) {
    return omni_parser_new_n(input, strlen(input));
}

OmniParser* omni_parser_new_n(const char* input, size_t len) {
    OmniParser* p = malloc(sizeof(OmniParser));
    if (!p) return NULL;
    p->input = input;
    p->input_len = len;
    p->pos = 0;
    p->errors = NULL;
    p->error_count = 0;
    return p;
}

void omni_parser_free(OmniParser* parser) {
    if (!parser) return;

    /* Free error chain */
    OmniParseError* err = parser->errors;
    while (err) {
        OmniParseError* next = err->next;
        free(err);
        err = next;
    }

    free(parser);
}

OmniValue* omni_parse_string(const char* source) {
    omni_grammar_init();

    PikaState* state = pika_new(source, g_rules, NUM_RULES);
    if (!state) return omni_new_error("Failed to create parser state");

    OmniValue* result = pika_run(state, R_EXPR);

#ifdef DEBUG
    fprintf(stderr, "[DEBUG] parse_string input='%s'\n", source);
    PikaMatch* m = pika_get_match(state, 0, R_EXPR);
    if (m) {
        fprintf(stderr, "[DEBUG] R_EXPR at 0: matched=%d len=%zu\n", m->matched, m->len);
    }
    m = pika_get_match(state, 0, R_LIST);
    if (m) {
        fprintf(stderr, "[DEBUG] R_LIST at 0: matched=%d len=%zu\n", m->matched, m->len);
    }
    m = pika_get_match(state, 0, R_ATOM);
    if (m) {
        fprintf(stderr, "[DEBUG] R_ATOM at 0: matched=%d len=%zu\n", m->matched, m->len);
    }
    m = pika_get_match(state, 0, R_INT);
    if (m) {
        fprintf(stderr, "[DEBUG] R_INT at 0: matched=%d len=%zu\n", m->matched, m->len);
    }
    m = pika_get_match(state, 0, R_LPAREN);
    if (m) {
        fprintf(stderr, "[DEBUG] R_LPAREN at 0: matched=%d len=%zu\n", m->matched, m->len);
    }
#endif

    pika_free(state);

    return result;
}

OmniValue** omni_parser_parse_all(OmniParser* parser, size_t* count) {
    omni_grammar_init();

    PikaState* state = pika_new(parser->input, g_rules, NUM_RULES);
    if (!state) {
        *count = 0;
        return NULL;
    }

    OmniValue* program = pika_run(state, R_PROGRAM);

#ifdef DEBUG
    fprintf(stderr, "[DEBUG] parse_all input='%.50s'\n", parser->input);
    PikaMatch* m = pika_get_match(state, 0, R_PROGRAM);
    if (m) fprintf(stderr, "[DEBUG] R_PROGRAM at 0: matched=%d len=%zu\n", m->matched, m->len);
    m = pika_get_match(state, 0, R_EXPR);
    if (m) fprintf(stderr, "[DEBUG] R_EXPR at 0: matched=%d len=%zu\n", m->matched, m->len);
    m = pika_get_match(state, 0, R_LIST);
    if (m) fprintf(stderr, "[DEBUG] R_LIST at 0: matched=%d len=%zu\n", m->matched, m->len);
    m = pika_get_match(state, 0, R_LPAREN);
    if (m) fprintf(stderr, "[DEBUG] R_LPAREN at 0: matched=%d len=%zu\n", m->matched, m->len);
    m = pika_get_match(state, 0, R_LIST_INNER);
    if (m) fprintf(stderr, "[DEBUG] R_LIST_INNER at 0: matched=%d len=%zu\n", m->matched, m->len);
    m = pika_get_match(state, 1, R_SYM);
    if (m) fprintf(stderr, "[DEBUG] R_SYM at 1: matched=%d len=%zu\n", m->matched, m->len);
#endif

    pika_free(state);

    if (omni_is_error(program) || omni_is_nil(program)) {
        *count = 0;
        return NULL;
    }

    /* Count expressions */
    size_t n = 0;
    for (OmniValue* p = program; !omni_is_nil(p) && omni_is_cell(p); p = omni_cdr(p)) n++;

    /* Build array */
    OmniValue** exprs = malloc(sizeof(OmniValue*) * (n + 1));
    size_t i = 0;
    for (OmniValue* p = program; !omni_is_nil(p) && omni_is_cell(p); p = omni_cdr(p)) {
        exprs[i++] = omni_car(p);
    }
    exprs[n] = NULL;

    *count = n;
    return exprs;
}

OmniParseError* omni_parser_get_errors(OmniParser* parser) {
    return parser ? parser->errors : NULL;
}
