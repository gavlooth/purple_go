#include "../pika.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int test_arithmetic(void) {
    const char* grammar_spec =
        "Program <- Statement+;\n"
        "Statement <- var:[a-z]+ '=' E ';';\n"
        "E[4] <- '(' E ')';\n"
        "E[3] <- num:[0-9]+ / sym:[a-z]+;\n"
        "E[2] <- arith:(op:'-' E);\n"
        "E[1,L] <- arith:(E op:('*' / '/') E);\n"
        "E[0,L] <- arith:(E op:('+' / '-') E);\n";

    char* err = NULL;
    PikaGrammar* grammar = pika_meta_parse(grammar_spec, &err);
    if (!grammar) {
        fprintf(stderr, "meta_parse failed: %s\n", err ? err : "(unknown)");
        free(err);
        return 0;
    }

    const char* input = "discriminant=b*b-4*a*c;";
    PikaMemoTable* memo = pika_grammar_parse(grammar, input);
    if (!memo) {
        fprintf(stderr, "parse failed\n");
        pika_grammar_free(grammar);
        return 0;
    }

    size_t count = 0;
    PikaMatch** matches = pika_memo_get_non_overlapping_matches_for_rule(memo, "Program", &count);
    if (!matches || count == 0) {
        fprintf(stderr, "no Program matches\n");
        free(matches);
        pika_memo_free(memo);
        pika_grammar_free(grammar);
        return 0;
    }

    PikaMatch* top = matches[0];
    int top_start = pika_match_start(top);
    int top_len = pika_match_len(top);
    if (top_start != 0 || top_len != (int)strlen(input)) {
        fprintf(stderr, "top match span mismatch: %d + %d\n", top_start, top_len);
        free(matches);
        pika_memo_free(memo);
        pika_grammar_free(grammar);
        return 0;
    }

    char* s = pika_match_to_string_with_rules(top);
    if (!s || strncmp(s, "Program <- Statement+ : 0+", 26) != 0) {
        fprintf(stderr, "unexpected match string: %s\n", s ? s : "(null)");
        free(s);
        free(matches);
        pika_memo_free(memo);
        pika_grammar_free(grammar);
        return 0;
    }
    free(s);

    free(matches);
    pika_memo_free(memo);
    pika_grammar_free(grammar);
    return 1;
}

static int test_charset_inversion(void) {
    const char* grammar_spec = "P <- C+; C <- \"//\" [^\\r\\n]* [\\r\\n]?;";
    char* err = NULL;
    PikaGrammar* grammar = pika_meta_parse(grammar_spec, &err);
    if (!grammar) {
        fprintf(stderr, "meta_parse failed: %s\n", err ? err : "(unknown)");
        free(err);
        return 0;
    }
    const char* input = "// xyz\n//";
    PikaMemoTable* memo = pika_grammar_parse(grammar, input);
    if (!memo) {
        pika_grammar_free(grammar);
        return 0;
    }
    const char* coverage[] = { "P", "C" };
    size_t err_count = 0;
    PikaSyntaxError* errs = pika_memo_get_syntax_errors(memo, coverage, 2, &err_count);
    if (err_count != 0) {
        fprintf(stderr, "expected no syntax errors, got %zu\n", err_count);
        for (size_t i = 0; i < err_count; i++) free(errs[i].text);
        free(errs);
        pika_memo_free(memo);
        pika_grammar_free(grammar);
        return 0;
    }
    free(errs);
    pika_memo_free(memo);
    pika_grammar_free(grammar);
    return 1;
}

int main(void) {
    int ok = 1;
    if (!test_arithmetic()) ok = 0;
    if (!test_charset_inversion()) ok = 0;
    if (!ok) {
        fprintf(stderr, "tests failed\n");
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
