/*
 * Pika Parser Core Implementation
 *
 * Based on the Pika parsing algorithm - a packrat PEG parser.
 * Uses right-to-left pass with memoization for O(n) parsing.
 */

#include "pika.h"
#include <stdlib.h>
#include <string.h>

PikaState* pika_new(const char* input, PikaRule* rules, int num_rules) {
    PikaState* state = malloc(sizeof(PikaState));
    if (!state) return NULL;

    state->input = input;
    state->input_len = strlen(input);
    state->num_rules = num_rules;
    state->rules = rules;

    size_t table_size = (state->input_len + 1) * num_rules;
    state->table = calloc(table_size, sizeof(PikaMatch));
    if (!state->table) {
        free(state);
        return NULL;
    }

    return state;
}

void pika_free(PikaState* state) {
    if (!state) return;
    if (state->table) free(state->table);
    free(state);
}

PikaMatch* pika_get_match(PikaState* state, size_t pos, int rule_id) {
    if (pos > state->input_len || rule_id < 0 || rule_id >= state->num_rules) return NULL;
    return &state->table[pos * state->num_rules + rule_id];
}

static inline PikaMatch* get_match(PikaState* state, size_t pos, int rule_id) {
    return pika_get_match(state, pos, rule_id);
}

static PikaMatch evaluate_rule(PikaState* state, size_t pos, int rule_id) {
    PikaRule* rule = &state->rules[rule_id];
    PikaMatch m = {false, 0, NULL};

    switch (rule->type) {
        case PIKA_TERMINAL: {
            if (!rule->data.str) break;  /* Uninitialized rule */
            size_t len = strlen(rule->data.str);
            if (pos + len <= state->input_len &&
                strncmp(state->input + pos, rule->data.str, len) == 0) {
                m.matched = true;
                m.len = len;
            }
            break;
        }

        case PIKA_RANGE: {
            if (pos < state->input_len) {
                char c = state->input[pos];
                if (c >= rule->data.range.min && c <= rule->data.range.max) {
                    m.matched = true;
                    m.len = 1;
                }
            }
            break;
        }

        case PIKA_ANY: {
            if (pos < state->input_len) {
                m.matched = true;
                m.len = 1;
            }
            break;
        }

        case PIKA_SEQ: {
            size_t current_pos = pos;
            bool all_matched = true;
            for (int i = 0; i < rule->data.children.count; i++) {
                PikaMatch* sub = get_match(state, current_pos, rule->data.children.subrules[i]);
                if (!sub || !sub->matched) {
                    all_matched = false;
                    break;
                }
                current_pos += sub->len;
            }
            if (all_matched) {
                m.matched = true;
                m.len = current_pos - pos;
            }
            break;
        }

        case PIKA_ALT: {
            /* PEG Prioritized Choice: First one that matches wins */
            for (int i = 0; i < rule->data.children.count; i++) {
                PikaMatch* sub = get_match(state, pos, rule->data.children.subrules[i]);
                if (sub && sub->matched) {
                    m = *sub;
                    break;
                }
            }
            break;
        }

        case PIKA_REP: {
            /* Zero or more: A* */
            PikaMatch* first = get_match(state, pos, rule->data.children.subrules[0]);
            if (first && first->matched && first->len > 0) {
                PikaMatch* rest = get_match(state, pos + first->len, rule_id);
                if (rest && rest->matched) {
                    m.matched = true;
                    m.len = first->len + rest->len;
                } else {
                    m = *first;
                }
            } else {
                /* Match empty */
                m.matched = true;
                m.len = 0;
            }
            break;
        }

        case PIKA_POS: {
            /* One or more: A+ */
            PikaMatch* first = get_match(state, pos, rule->data.children.subrules[0]);
            if (first && first->matched) {
                m.matched = true;
                m.len = first->len;

                if (pos + first->len <= state->input_len) {
                    PikaMatch* more = get_match(state, pos + first->len, rule_id);
                    if (more && more->matched) {
                        m.len += more->len;
                    }
                }
            }
            break;
        }

        case PIKA_OPT: {
            PikaMatch* sub = get_match(state, pos, rule->data.children.subrules[0]);
            if (sub && sub->matched) {
                m = *sub;
            } else {
                m.matched = true;
                m.len = 0;
            }
            break;
        }

        case PIKA_NOT: {
            PikaMatch* sub = get_match(state, pos, rule->data.children.subrules[0]);
            if (!sub || !sub->matched) {
                m.matched = true;
                m.len = 0;
            }
            break;
        }

        case PIKA_AND: {
            PikaMatch* sub = get_match(state, pos, rule->data.children.subrules[0]);
            if (sub && sub->matched) {
                m.matched = true;
                m.len = 0;
            }
            break;
        }

        case PIKA_REF: {
            PikaMatch* sub = get_match(state, pos, rule->data.ref.subrule);
            if (sub) m = *sub;
            break;
        }
    }

    return m;
}

OmniValue* pika_run(PikaState* state, int root_rule_id) {
    /* Right-to-Left Pass with fixpoint iteration */
    for (ptrdiff_t pos = (ptrdiff_t)state->input_len; pos >= 0; pos--) {
        bool changed = true;
        int fixpoint_limit = state->num_rules * 2;
        int iters = 0;

        while (changed && iters < fixpoint_limit) {
            changed = false;
            iters++;

            for (int r = 0; r < state->num_rules; r++) {
                PikaMatch result = evaluate_rule(state, (size_t)pos, r);
                PikaMatch* existing = get_match(state, (size_t)pos, r);

                if (result.matched != existing->matched || result.len != existing->len) {
                    *existing = result;
                    if (result.matched && state->rules[r].action) {
                        existing->val = state->rules[r].action(state, (size_t)pos, result);
                    }
                    changed = true;
                }
            }
        }
    }

    PikaMatch* root = get_match(state, 0, root_rule_id);
    if (root && root->matched) {
        if (root->val) return root->val;
        /* Return matched text as symbol if no action */
        char* s = malloc(root->len + 1);
        memcpy(s, state->input, root->len);
        s[root->len] = '\0';
        OmniValue* v = omni_new_sym(s);
        free(s);
        return v;
    }

    return omni_new_error("Parse failed");
}
