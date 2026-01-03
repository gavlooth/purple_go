#ifndef PIKA_PARSER_H
#define PIKA_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
Ownership & lifetime notes
--------------------------
- All PikaClause* values are heap-allocated. The Grammar takes ownership of all clauses
  reachable from the Rules you pass to pika_grammar_new / pika_grammar_from_rules.
- AST label clauses are transient wrappers: when an AST label is used as a subclause or
  as the rule root, the wrapper is consumed (freed) during construction and its label
  is transferred onto the labeled subclause.
- pika_clause_charset_union is non-consuming (caller retains ownership of inputs).
- pika_clause_charset_union_take consumes and frees input charset clauses (deduped).
*/

// Forward declarations
typedef struct PikaClause PikaClause;
typedef struct PikaRule PikaRule;
typedef struct PikaGrammar PikaGrammar;
typedef struct PikaMemoTable PikaMemoTable;
typedef struct PikaMatch PikaMatch;
typedef struct PikaAstNode PikaAstNode;

// Associativity for precedence rules
typedef enum {
    PIKA_ASSOC_NONE = 0,
    PIKA_ASSOC_LEFT,
    PIKA_ASSOC_RIGHT
} PikaAssociativity;

// Parse options (defaults are zeroed)
typedef struct {
    int store_submatches; // 1 = store parse tree (default), 0 = store lengths only
} PikaParseOptions;

// Syntax error info
typedef struct {
    int start;
    int end;
    char* text; // owned by caller (free)
} PikaSyntaxError;

// Labeled match tuple (label may be NULL)
typedef struct {
    const char* label;
    const PikaMatch* match;
} PikaLabeledMatch;

// ----------------- Clause/Rule builder API -----------------

PikaClause* pika_clause_seq(PikaClause** clauses, size_t count);
PikaClause* pika_clause_first(PikaClause** clauses, size_t count);
PikaClause* pika_clause_one_or_more(PikaClause* clause);
PikaClause* pika_clause_optional(PikaClause* clause);
PikaClause* pika_clause_zero_or_more(PikaClause* clause);
PikaClause* pika_clause_followed_by(PikaClause* clause);
PikaClause* pika_clause_not_followed_by(PikaClause* clause);
PikaClause* pika_clause_start(void);
PikaClause* pika_clause_nothing(void);
PikaClause* pika_clause_str(const char* str);
PikaClause* pika_clause_char(char c);
PikaClause* pika_clause_charset_from_chars(const char* chars, size_t len);
PikaClause* pika_clause_charset_from_range(char min_c, char max_c);
PikaClause* pika_clause_charset_from_pattern(const char* range_pattern);
// Does not consume input charset clauses.
PikaClause* pika_clause_charset_union(PikaClause** charsets, size_t count);
// Consumes input charset clauses (they are freed inside).
PikaClause* pika_clause_charset_union_take(PikaClause** charsets, size_t count);
PikaClause* pika_clause_charset_invert(PikaClause* charset);
PikaClause* pika_clause_ast_label(const char* label, PikaClause* clause);
PikaClause* pika_clause_rule_ref(const char* rule_name);

PikaRule* pika_rule(const char* name, PikaClause* clause);
PikaRule* pika_rule_prec(const char* name, int precedence, PikaAssociativity assoc, PikaClause* clause);

// ----------------- Grammar API -----------------

PikaGrammar* pika_grammar_new(PikaRule** rules, size_t rule_count);
PikaGrammar* pika_grammar_from_rules(PikaRule** rules, size_t rule_count); // alias
void pika_grammar_free(PikaGrammar* grammar);

const PikaRule* pika_grammar_get_rule(const PikaGrammar* grammar, const char* rule_name);

// Meta-grammar: parse grammar description text into a Grammar
PikaGrammar* pika_meta_parse(const char* grammar_spec, char** error_out);

// ----------------- Parsing -----------------

PikaMemoTable* pika_grammar_parse(PikaGrammar* grammar, const char* input);
PikaMemoTable* pika_grammar_parse_n(PikaGrammar* grammar, const char* input, size_t input_len);
PikaMemoTable* pika_grammar_parse_with_options(PikaGrammar* grammar, const char* input, const PikaParseOptions* opts);
PikaMemoTable* pika_grammar_parse_with_options_n(PikaGrammar* grammar, const char* input, size_t input_len, const PikaParseOptions* opts);

void pika_memo_free(PikaMemoTable* memo);

// ----------------- Match accessors -----------------

PikaMatch** pika_memo_get_all_matches(PikaMemoTable* memo, const PikaClause* clause, size_t* out_count);
PikaMatch** pika_memo_get_non_overlapping_matches(PikaMemoTable* memo, const PikaClause* clause, size_t* out_count);
PikaMatch** pika_memo_get_all_matches_for_rule(PikaMemoTable* memo, const char* rule_name, size_t* out_count);
PikaMatch** pika_memo_get_non_overlapping_matches_for_rule(PikaMemoTable* memo, const char* rule_name, size_t* out_count);

PikaSyntaxError* pika_memo_get_syntax_errors(PikaMemoTable* memo, const char** coverage_rule_names, size_t rule_count, size_t* out_count);

// Returns a newly allocated array of labeled submatches (caller frees)
PikaLabeledMatch* pika_match_get_submatches(const PikaMatch* match, size_t* out_count);
int pika_match_start(const PikaMatch* match);
int pika_match_len(const PikaMatch* match);

// ----------------- AST -----------------

PikaAstNode* pika_ast_from_match(const char* label, const PikaMatch* match, const char* input);
void pika_ast_free(PikaAstNode* node);

// ----------------- Debug helpers -----------------

char* pika_match_to_string(const PikaMatch* match);
char* pika_match_to_string_with_rules(const PikaMatch* match);
char* pika_clause_to_string_with_rules(const PikaClause* clause);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // PIKA_PARSER_H
