#include "pika.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// ----------------- Internal Types -----------------

static char* pika_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

typedef enum {
    PIKA_CLAUSE_CHARSEQ,
    PIKA_CLAUSE_CHARSET,
    PIKA_CLAUSE_START,
    PIKA_CLAUSE_NOTHING,
    PIKA_CLAUSE_SEQ,
    PIKA_CLAUSE_FIRST,
    PIKA_CLAUSE_ONEORMORE,
    PIKA_CLAUSE_FOLLOWEDBY,
    PIKA_CLAUSE_NOTFOLLOWEDBY,
    PIKA_CLAUSE_RULEREF,
    PIKA_CLAUSE_ASTLABEL
} PikaClauseType;

typedef struct PikaLabeledClause {
    struct PikaClause* clause;
    char* ast_label; // optional
} PikaLabeledClause;

typedef struct PikaClause {
    PikaClauseType type;
    int clause_idx;
    int can_match_zero;

    PikaLabeledClause* subclauses;
    size_t subclause_count;

    struct PikaClause** seed_parents;
    size_t seed_parent_count;
    size_t seed_parent_cap;

    struct PikaRule** rules;
    size_t rules_count;
    size_t rules_cap;

    char* to_string_cache;
    char* to_string_with_rules_cache;

    union {
        struct {
            char* str;
            int ignore_case;
        } charseq;
        struct {
            uint32_t* include_bits;
            uint32_t* exclude_bits;
        } charset;
        struct {
            char* ref_name;
        } ruleref;
        struct {
            char* label;
        } astlabel;
    } data;
} PikaClause;

struct PikaRule {
    char* rule_name;
    int precedence;
    PikaAssociativity assoc;
    PikaLabeledClause labeled_clause;
};

struct PikaGrammar {
    PikaRule** all_rules;
    size_t rule_count;
    PikaClause** all_clauses;
    size_t clause_count;
    // rule name -> rule
    struct PikaStringMap* rule_map;
};

struct PikaMatch {
    const PikaClause* clause;
    int start_pos;
    int len;
    int first_matching_subclause_idx;
    struct PikaMatch** submatches;
    size_t submatch_count;
};

struct PikaMemoTable {
    PikaGrammar* grammar;
    const char* input;
    size_t input_len;
    PikaParseOptions opts;
    PikaMatch** table; // (input_len+1) * clause_count
    struct PikaArena* arena;
};

struct PikaAstNode {
    char* label;
    const PikaClause* node_type;
    int start_pos;
    int len;
    const char* input;
    struct PikaAstNode** children;
    size_t child_count;
};

// ----------------- Arena -----------------

typedef struct PikaArenaBlock {
    struct PikaArenaBlock* next;
    size_t used;
    size_t size;
    unsigned char data[];
} PikaArenaBlock;

typedef struct PikaArena {
    PikaArenaBlock* head;
} PikaArena;

static PikaArena* pika_arena_new(void) {
    PikaArena* arena = (PikaArena*)calloc(1, sizeof(PikaArena));
    return arena;
}

static void pika_arena_free(PikaArena* arena) {
    if (!arena) return;
    PikaArenaBlock* blk = arena->head;
    while (blk) {
        PikaArenaBlock* next = blk->next;
        free(blk);
        blk = next;
    }
    free(arena);
}

static void* pika_arena_alloc(PikaArena* arena, size_t size) {
    if (!arena || size == 0) return NULL;
    const size_t block_size = 64 * 1024;
    if (!arena->head || arena->head->used + size > arena->head->size) {
        size_t alloc_size = block_size;
        if (size + sizeof(PikaArenaBlock) > alloc_size) {
            alloc_size = size + sizeof(PikaArenaBlock) + 1024;
        }
        PikaArenaBlock* blk = (PikaArenaBlock*)malloc(sizeof(PikaArenaBlock) + alloc_size);
        if (!blk) return NULL;
        blk->next = arena->head;
        blk->used = 0;
        blk->size = alloc_size;
        arena->head = blk;
    }
    void* ptr = arena->head->data + arena->head->used;
    arena->head->used += size;
    // align to 8 bytes
    arena->head->used = (arena->head->used + 7) & ~((size_t)7);
    return ptr;
}

// ----------------- Dynamic array helpers -----------------

static void* pika_grow_array(void* ptr, size_t elem_size, size_t* cap, size_t new_len) {
    if (new_len <= *cap) return ptr;
    size_t new_cap = *cap ? *cap : 8;
    while (new_cap < new_len) new_cap *= 2;
    void* new_ptr = realloc(ptr, elem_size * new_cap);
    if (!new_ptr) return NULL;
    *cap = new_cap;
    return new_ptr;
}

// ----------------- String builder -----------------

typedef struct {
    char* buf;
    size_t len;
    size_t cap;
} PikaStrBuf;

static void sb_init(PikaStrBuf* sb) {
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int sb_ensure(PikaStrBuf* sb, size_t extra) {
    size_t needed = sb->len + extra + 1;
    if (needed <= sb->cap) return 1;
    size_t new_cap = sb->cap ? sb->cap : 64;
    while (new_cap < needed) new_cap *= 2;
    char* new_buf = (char*)realloc(sb->buf, new_cap);
    if (!new_buf) return 0;
    sb->buf = new_buf;
    sb->cap = new_cap;
    return 1;
}

static int sb_append(PikaStrBuf* sb, const char* s) {
    size_t len = strlen(s);
    if (!sb_ensure(sb, len)) return 0;
    memcpy(sb->buf + sb->len, s, len);
    sb->len += len;
    sb->buf[sb->len] = '\0';
    return 1;
}

static int sb_append_char(PikaStrBuf* sb, char c) {
    if (!sb_ensure(sb, 1)) return 0;
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
    return 1;
}

static char* sb_finish(PikaStrBuf* sb) {
    char* out = sb->buf;
    sb->buf = NULL;
    sb->len = sb->cap = 0;
    return out;
}

// ----------------- String map (string -> void*) -----------------

typedef struct {
    char* key;
    void* value;
    int used;
} PikaStringMapEntry;

typedef struct PikaStringMap {
    PikaStringMapEntry* entries;
    size_t cap;
    size_t len;
} PikaStringMap;

static uint64_t fnv1a_hash(const char* s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

static PikaStringMap* strmap_new(size_t cap) {
    PikaStringMap* m = (PikaStringMap*)calloc(1, sizeof(PikaStringMap));
    if (!m) return NULL;
    m->cap = cap ? cap : 64;
    m->entries = (PikaStringMapEntry*)calloc(m->cap, sizeof(PikaStringMapEntry));
    if (!m->entries) {
        free(m);
        return NULL;
    }
    return m;
}

static void strmap_free(PikaStringMap* m) {
    if (!m) return;
    free(m->entries);
    free(m);
}

static void strmap_free_with_keys(PikaStringMap* m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->entries[i].used) {
            free(m->entries[i].key);
        }
    }
    free(m->entries);
    free(m);
}

static int strmap_resize(PikaStringMap* m, size_t new_cap) {
    PikaStringMapEntry* old_entries = m->entries;
    size_t old_cap = m->cap;
    PikaStringMapEntry* entries = (PikaStringMapEntry*)calloc(new_cap, sizeof(PikaStringMapEntry));
    if (!entries) return 0;
    m->entries = entries;
    m->cap = new_cap;
    m->len = 0;
    for (size_t i = 0; i < old_cap; i++) {
        if (!old_entries[i].used) continue;
        // reinsert
        const char* key = old_entries[i].key;
        uint64_t h = fnv1a_hash(key);
        size_t idx = (size_t)(h % m->cap);
        while (m->entries[idx].used) {
            idx = (idx + 1) % m->cap;
        }
        m->entries[idx] = old_entries[i];
        m->entries[idx].used = 1;
        m->len++;
    }
    free(old_entries);
    return 1;
}

static void* strmap_get(PikaStringMap* m, const char* key) {
    if (!m || !key) return NULL;
    uint64_t h = fnv1a_hash(key);
    size_t idx = (size_t)(h % m->cap);
    for (size_t probe = 0; probe < m->cap; probe++) {
        PikaStringMapEntry* ent = &m->entries[idx];
        if (!ent->used) return NULL;
        if (ent->key && strcmp(ent->key, key) == 0) return ent->value;
        idx = (idx + 1) % m->cap;
    }
    return NULL;
}

static int strmap_put(PikaStringMap* m, char* key, void* value) {
    if (!m) return 0;
    if ((m->len + 1) * 10 > m->cap * 7) {
        if (!strmap_resize(m, m->cap * 2)) return 0;
    }
    uint64_t h = fnv1a_hash(key);
    size_t idx = (size_t)(h % m->cap);
    for (size_t probe = 0; probe < m->cap; probe++) {
        PikaStringMapEntry* ent = &m->entries[idx];
        if (!ent->used) {
            ent->used = 1;
            ent->key = key;
            ent->value = value;
            m->len++;
            return 1;
        }
        if (ent->key && strcmp(ent->key, key) == 0) {
            ent->value = value;
            return 1;
        }
        idx = (idx + 1) % m->cap;
    }
    return 0;
}

// ----------------- Pointer set (for DFS visited) -----------------

typedef struct {
    void** items;
    size_t len;
    size_t cap;
} PikaPtrSet;

static void ptrset_init(PikaPtrSet* s) {
    s->items = NULL;
    s->len = 0;
    s->cap = 0;
}

static void ptrset_free(PikaPtrSet* s) {
    free(s->items);
    s->items = NULL;
    s->len = s->cap = 0;
}

static int ptrset_contains(PikaPtrSet* s, void* p) {
    for (size_t i = 0; i < s->len; i++) {
        if (s->items[i] == p) return 1;
    }
    return 0;
}

static int ptrset_add(PikaPtrSet* s, void* p) {
    if (ptrset_contains(s, p)) return 0;
    void** new_items = (void**)pika_grow_array(s->items, sizeof(void*), &s->cap, s->len + 1);
    if (!new_items) return 0;
    s->items = new_items;
    s->items[s->len++] = p;
    return 1;
}

static void ptrset_remove(PikaPtrSet* s, void* p) {
    for (size_t i = 0; i < s->len; i++) {
        if (s->items[i] == p) {
            s->items[i] = s->items[s->len - 1];
            s->len--;
            return;
        }
    }
}

// ----------------- CharSet helpers -----------------

#define PIKA_CHARSET_BITS 65536
#define PIKA_CHARSET_WORDS (PIKA_CHARSET_BITS / 32)

static uint32_t* bitset_new(void) {
    return (uint32_t*)calloc(PIKA_CHARSET_WORDS, sizeof(uint32_t));
}

static void bitset_free(uint32_t* bs) {
    free(bs);
}

static void bitset_set(uint32_t* bs, uint16_t c) {
    if (!bs) return;
    bs[c >> 5] |= (uint32_t)1u << (c & 31);
}

static int bitset_get(const uint32_t* bs, uint16_t c) {
    if (!bs) return 0;
    return (bs[c >> 5] >> (c & 31)) & 1u;
}

static void bitset_or(uint32_t* dst, const uint32_t* src) {
    if (!dst || !src) return;
    for (size_t i = 0; i < PIKA_CHARSET_WORDS; i++) {
        dst[i] |= src[i];
    }
}

static int bitset_cardinality(const uint32_t* bs) {
    if (!bs) return 0;
    int count = 0;
    for (size_t i = 0; i < PIKA_CHARSET_WORDS; i++) {
        uint32_t v = bs[i];
        while (v) {
            v &= v - 1;
            count++;
        }
    }
    return count;
}

static int bitset_next_set_bit(const uint32_t* bs, int start) {
    if (!bs) return -1;
    if (start < 0) start = 0;
    int idx = start;
    int word = idx >> 5;
    int bit = idx & 31;
    if (word >= (int)PIKA_CHARSET_WORDS) return -1;
    uint32_t w = bs[word] & (~0u << bit);
    while (1) {
        if (w) {
            int offset;
#if defined(__GNUC__)
            offset = __builtin_ctz(w);
#else
            offset = 0;
            while (offset < 32 && ((w >> offset) & 1u) == 0) offset++;
#endif
            return (word << 5) + offset;
        }
        word++;
        if (word >= (int)PIKA_CHARSET_WORDS) break;
        w = bs[word];
    }
    return -1;
}

// ----------------- String utils (escape/unescape) -----------------

static int hex_digit_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int unescape_char(const char* s, size_t len, uint16_t* out) {
    if (len == 0) return 0;
    if (len == 1) {
        *out = (uint8_t)s[0];
        return 1;
    }
    if (len == 2 && s[0] == '\\') {
        switch (s[1]) {
            case 't': *out = '\t'; return 1;
            case 'b': *out = '\b'; return 1;
            case 'n': *out = '\n'; return 1;
            case 'r': *out = '\r'; return 1;
            case 'f': *out = '\f'; return 1;
            case '\\': *out = '\\'; return 1;
            case '\'': *out = '\''; return 1;
            case '"': *out = '"'; return 1;
            default: break;
        }
    }
    if (len == 6 && s[0] == '\\' && s[1] == 'u') {
        int c0 = hex_digit_to_int(s[2]);
        int c1 = hex_digit_to_int(s[3]);
        int c2 = hex_digit_to_int(s[4]);
        int c3 = hex_digit_to_int(s[5]);
        if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) return 0;
        *out = (uint16_t)((c0 << 12) | (c1 << 8) | (c2 << 4) | c3);
        return 1;
    }
    return 0;
}

static char* unescape_string(const char* s, size_t len) {
    PikaStrBuf sb;
    sb_init(&sb);
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\\') {
            if (i + 1 >= len) break;
            if (s[i + 1] == 'u') {
                if (i + 5 >= len) break;
                uint16_t outc = 0;
                if (unescape_char(s + i, 6, &outc)) {
                    sb_append_char(&sb, (char)outc);
                    i += 5;
                    continue;
                }
            }
            uint16_t outc = 0;
            if (unescape_char(s + i, 2, &outc)) {
                sb_append_char(&sb, (char)outc);
                i += 1;
                continue;
            }
        }
        sb_append_char(&sb, c);
    }
    return sb_finish(&sb);
}

static char* escape_char(uint16_t c) {
    char buf[8];
    if (c >= 32 && c <= 126) {
        buf[0] = (char)c;
        buf[1] = '\0';
        return pika_strdup(buf);
    }
    switch (c) {
        case '\n': return pika_strdup("\\n");
        case '\r': return pika_strdup("\\r");
        case '\t': return pika_strdup("\\t");
        case '\f': return pika_strdup("\\f");
        case '\b': return pika_strdup("\\b");
        default:
            snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
            return pika_strdup(buf);
    }
}

static char* escape_quoted_char(uint16_t c) {
    if (c == '\'') return pika_strdup("\\'");
    if (c == '\\') return pika_strdup("\\\\");
    return escape_char(c);
}

static char* escape_quoted_string_char(uint16_t c) {
    if (c == '"') return pika_strdup("\\\"");
    if (c == '\\') return pika_strdup("\\\\");
    return escape_char(c);
}

static char* escape_char_range_char(uint16_t c) {
    if (c == ']') return pika_strdup("\\]");
    if (c == '^') return pika_strdup("\\^");
    if (c == '-') return pika_strdup("\\-");
    if (c == '\\') return pika_strdup("\\\\");
    return escape_char(c);
}

static void clause_clear_to_string_cache(PikaClause* c) {
    if (!c) return;
    free(c->to_string_cache);
    c->to_string_cache = NULL;
    free(c->to_string_with_rules_cache);
    c->to_string_with_rules_cache = NULL;
}

// ----------------- Clause constructors -----------------

static void clause_free_shallow(PikaClause* c);

static PikaClause* clause_new(PikaClauseType type, PikaClause** subs, size_t sub_count) {
    PikaClause* c = (PikaClause*)calloc(1, sizeof(PikaClause));
    if (!c) return NULL;
    c->type = type;
    if (sub_count > 0) {
        c->subclauses = (PikaLabeledClause*)calloc(sub_count, sizeof(PikaLabeledClause));
        if (!c->subclauses) { free(c); return NULL; }
        c->subclause_count = sub_count;
        for (size_t i = 0; i < sub_count; i++) {
            PikaClause* sub = subs[i];
            if (sub && sub->type == PIKA_CLAUSE_ASTLABEL) {
                PikaClause* labeled = sub->subclauses[0].clause;
                c->subclauses[i].ast_label = pika_strdup(sub->data.astlabel.label);
                c->subclauses[i].clause = labeled;
                clause_free_shallow(sub);
            } else {
                c->subclauses[i].ast_label = NULL;
                c->subclauses[i].clause = sub;
            }
        }
    }
    return c;
}

static void clause_free_shallow(PikaClause* c) {
    if (!c) return;
    if (c->subclauses) {
        for (size_t i = 0; i < c->subclause_count; i++) {
            free(c->subclauses[i].ast_label);
        }
        free(c->subclauses);
    }
    free(c->seed_parents);
    free(c->rules);
    free(c->to_string_cache);
    free(c->to_string_with_rules_cache);
    switch (c->type) {
        case PIKA_CLAUSE_CHARSEQ:
            free(c->data.charseq.str);
            break;
        case PIKA_CLAUSE_CHARSET:
            bitset_free(c->data.charset.include_bits);
            bitset_free(c->data.charset.exclude_bits);
            break;
        case PIKA_CLAUSE_RULEREF:
            free(c->data.ruleref.ref_name);
            break;
        case PIKA_CLAUSE_ASTLABEL:
            free(c->data.astlabel.label);
            break;
        default:
            break;
    }
    free(c);
}

static void clause_register_rule(PikaClause* c, PikaRule* rule) {
    if (!c) return;
    PikaRule** new_rules = (PikaRule**)pika_grow_array(c->rules, sizeof(PikaRule*), &c->rules_cap, c->rules_count + 1);
    if (!new_rules) return;
    c->rules = new_rules;
    c->rules[c->rules_count++] = rule;
}

PikaClause* pika_clause_seq(PikaClause** clauses, size_t count) {
    if (count < 2) return NULL;
    return clause_new(PIKA_CLAUSE_SEQ, clauses, count);
}

PikaClause* pika_clause_first(PikaClause** clauses, size_t count) {
    if (count < 2) return NULL;
    return clause_new(PIKA_CLAUSE_FIRST, clauses, count);
}

PikaClause* pika_clause_one_or_more(PikaClause* clause) {
    if (!clause) return NULL;
    if (clause->type == PIKA_CLAUSE_ONEORMORE || clause->type == PIKA_CLAUSE_NOTHING
            || clause->type == PIKA_CLAUSE_FOLLOWEDBY || clause->type == PIKA_CLAUSE_NOTFOLLOWEDBY
            || clause->type == PIKA_CLAUSE_START) {
        return clause;
    }
    PikaClause* subs[1] = { clause };
    return clause_new(PIKA_CLAUSE_ONEORMORE, subs, 1);
}

PikaClause* pika_clause_optional(PikaClause* clause) {
    if (!clause) return NULL;
    PikaClause* subs[2] = { clause, pika_clause_nothing() };
    return pika_clause_first(subs, 2);
}

PikaClause* pika_clause_zero_or_more(PikaClause* clause) {
    return pika_clause_optional(pika_clause_one_or_more(clause));
}

PikaClause* pika_clause_followed_by(PikaClause* clause) {
    if (!clause) return NULL;
    if (clause->type == PIKA_CLAUSE_NOTHING) return clause;
    if (clause->type == PIKA_CLAUSE_FOLLOWEDBY || clause->type == PIKA_CLAUSE_NOTFOLLOWEDBY
            || clause->type == PIKA_CLAUSE_START) {
        return NULL;
    }
    PikaClause* subs[1] = { clause };
    return clause_new(PIKA_CLAUSE_FOLLOWEDBY, subs, 1);
}

PikaClause* pika_clause_not_followed_by(PikaClause* clause) {
    if (!clause) return NULL;
    if (clause->type == PIKA_CLAUSE_NOTHING) return NULL;
    if (clause->type == PIKA_CLAUSE_NOTFOLLOWEDBY) {
        return pika_clause_followed_by(clause->subclauses[0].clause);
    }
    if (clause->type == PIKA_CLAUSE_FOLLOWEDBY || clause->type == PIKA_CLAUSE_START) return NULL;
    PikaClause* subs[1] = { clause };
    return clause_new(PIKA_CLAUSE_NOTFOLLOWEDBY, subs, 1);
}

PikaClause* pika_clause_start(void) {
    return clause_new(PIKA_CLAUSE_START, NULL, 0);
}

PikaClause* pika_clause_nothing(void) {
    return clause_new(PIKA_CLAUSE_NOTHING, NULL, 0);
}

PikaClause* pika_clause_str(const char* str) {
    if (!str) return NULL;
    PikaClause* c = clause_new(PIKA_CLAUSE_CHARSEQ, NULL, 0);
    if (!c) return NULL;
    c->data.charseq.str = pika_strdup(str);
    c->data.charseq.ignore_case = 0;
    return c;
}

PikaClause* pika_clause_char(char c) {
    return pika_clause_charset_from_chars(&c, 1);
}

PikaClause* pika_clause_charset_from_chars(const char* chars, size_t len) {
    PikaClause* c = clause_new(PIKA_CLAUSE_CHARSET, NULL, 0);
    if (!c) return NULL;
    c->data.charset.include_bits = bitset_new();
    if (!c->data.charset.include_bits) { clause_free_shallow(c); return NULL; }
    for (size_t i = 0; i < len; i++) {
        bitset_set(c->data.charset.include_bits, (uint8_t)chars[i]);
    }
    return c;
}

PikaClause* pika_clause_charset_from_range(char min_c, char max_c) {
    if ((unsigned char)max_c < (unsigned char)min_c) return NULL;
    PikaClause* c = clause_new(PIKA_CLAUSE_CHARSET, NULL, 0);
    if (!c) return NULL;
    c->data.charset.include_bits = bitset_new();
    if (!c->data.charset.include_bits) { clause_free_shallow(c); return NULL; }
    for (uint16_t ch = (uint8_t)min_c; ch <= (uint8_t)max_c; ch++) {
        bitset_set(c->data.charset.include_bits, ch);
        if (ch == (uint8_t)max_c) break;
    }
    return c;
}

// Parse a character range pattern (without surrounding brackets)
typedef struct {
    uint16_t ch;
    int is_range_marker;
} PikaRangeTok;

static PikaRangeTok* parse_range_tokens(const char* s, size_t* out_count) {
    PikaRangeTok* toks = NULL;
    size_t len = 0, cap = 0;
    size_t i = 0;
    size_t slen = strlen(s);
    while (i < slen) {
        char c = s[i];
        if (c == '\\' && i + 1 < slen) {
            if (s[i + 1] == 'u' && i + 5 < slen) {
                uint16_t outc = 0;
                if (unescape_char(s + i, 6, &outc)) {
                    PikaRangeTok tok = { outc, 0 };
                    PikaRangeTok* arr = (PikaRangeTok*)pika_grow_array(toks, sizeof(PikaRangeTok), &cap, len + 1);
                    if (arr) { toks = arr; toks[len++] = tok; }
                    i += 6;
                    continue;
                }
            }
            // Escaped special chars in char ranges
            char next = s[i + 1];
            if (next == '-' || next == '^' || next == ']' || next == '\\') {
                PikaRangeTok tok = { (uint16_t)next, 0 };
                PikaRangeTok* arr = (PikaRangeTok*)pika_grow_array(toks, sizeof(PikaRangeTok), &cap, len + 1);
                if (arr) { toks = arr; toks[len++] = tok; }
                i += 2;
                continue;
            }
            uint16_t outc = 0;
            if (unescape_char(s + i, 2, &outc)) {
                PikaRangeTok tok = { outc, 0 };
                PikaRangeTok* arr = (PikaRangeTok*)pika_grow_array(toks, sizeof(PikaRangeTok), &cap, len + 1);
                if (arr) { toks = arr; toks[len++] = tok; }
                i += 2;
                continue;
            }
        }
        if (c == '-') {
            PikaRangeTok tok = { 0, 1 };
            PikaRangeTok* arr = (PikaRangeTok*)pika_grow_array(toks, sizeof(PikaRangeTok), &cap, len + 1);
            if (arr) { toks = arr; toks[len++] = tok; }
            i++;
            continue;
        }
        PikaRangeTok tok = { (uint16_t)(unsigned char)c, 0 };
        PikaRangeTok* arr = (PikaRangeTok*)pika_grow_array(toks, sizeof(PikaRangeTok), &cap, len + 1);
        if (arr) { toks = arr; toks[len++] = tok; }
        i++;
    }
    if (out_count) *out_count = len;
    return toks;
}

PikaClause* pika_clause_charset_from_pattern(const char* range_pattern) {
    if (!range_pattern) return NULL;
    int invert = (range_pattern[0] == '^');
    const char* p = invert ? range_pattern + 1 : range_pattern;
    PikaClause* c = clause_new(PIKA_CLAUSE_CHARSET, NULL, 0);
    if (!c) return NULL;
    c->data.charset.include_bits = bitset_new();
    if (!c->data.charset.include_bits) { clause_free_shallow(c); return NULL; }

    size_t tok_count = 0;
    PikaRangeTok* toks = parse_range_tokens(p, &tok_count);
    for (size_t i = 0; i < tok_count; i++) {
        if (toks[i].is_range_marker) {
            bitset_set(c->data.charset.include_bits, (uint16_t)'-');
            continue;
        }
        if (i + 2 < tok_count && toks[i + 1].is_range_marker && !toks[i + 2].is_range_marker) {
            uint16_t start = toks[i].ch;
            uint16_t end = toks[i + 2].ch;
            if (end < start) { free(toks); clause_free_shallow(c); return NULL; }
            for (uint16_t cc = start; cc <= end; cc++) {
                bitset_set(c->data.charset.include_bits, cc);
                if (cc == end) break;
            }
            i += 2;
        } else {
            bitset_set(c->data.charset.include_bits, toks[i].ch);
        }
    }
    free(toks);

    if (invert) return pika_clause_charset_invert(c);
    return c;
}

static PikaClause* clause_charset_union_internal(PikaClause** charsets, size_t count, int consume) {
    if (count == 0) return NULL;
    PikaClause* c = clause_new(PIKA_CLAUSE_CHARSET, NULL, 0);
    if (!c) return NULL;
    c->data.charset.include_bits = bitset_new();
    c->data.charset.exclude_bits = NULL;
    if (!c->data.charset.include_bits) { clause_free_shallow(c); return NULL; }
    PikaPtrSet seen; if (consume) ptrset_init(&seen);
    for (size_t i = 0; i < count; i++) {
        PikaClause* cs = charsets[i];
        if (!cs || cs->type != PIKA_CLAUSE_CHARSET) continue;
        if (cs->data.charset.include_bits) {
            bitset_or(c->data.charset.include_bits, cs->data.charset.include_bits);
        }
        if (cs->data.charset.exclude_bits) {
            if (!c->data.charset.exclude_bits) {
                c->data.charset.exclude_bits = bitset_new();
                if (!c->data.charset.exclude_bits) { clause_free_shallow(c); return NULL; }
            }
            bitset_or(c->data.charset.exclude_bits, cs->data.charset.exclude_bits);
        }
        if (consume && ptrset_add(&seen, cs)) {
            clause_free_shallow(cs);
        }
    }
    if (consume) ptrset_free(&seen);
    return c;
}

PikaClause* pika_clause_charset_union(PikaClause** charsets, size_t count) {
    return clause_charset_union_internal(charsets, count, 0);
}

PikaClause* pika_clause_charset_union_take(PikaClause** charsets, size_t count) {
    return clause_charset_union_internal(charsets, count, 1);
}

PikaClause* pika_clause_charset_invert(PikaClause* charset) {
    if (!charset || charset->type != PIKA_CLAUSE_CHARSET) return NULL;
    uint32_t* tmp = charset->data.charset.include_bits;
    charset->data.charset.include_bits = charset->data.charset.exclude_bits;
    charset->data.charset.exclude_bits = tmp;
    clause_clear_to_string_cache(charset);
    return charset;
}

PikaClause* pika_clause_ast_label(const char* label, PikaClause* clause) {
    if (!label || !clause) return NULL;
    PikaClause* c = clause_new(PIKA_CLAUSE_ASTLABEL, &clause, 1);
    if (!c) return NULL;
    c->data.astlabel.label = pika_strdup(label);
    return c;
}

PikaClause* pika_clause_rule_ref(const char* rule_name) {
    if (!rule_name) return NULL;
    PikaClause* c = clause_new(PIKA_CLAUSE_RULEREF, NULL, 0);
    if (!c) return NULL;
    c->data.ruleref.ref_name = pika_strdup(rule_name);
    return c;
}

PikaRule* pika_rule(const char* name, PikaClause* clause) {
    return pika_rule_prec(name, -1, PIKA_ASSOC_NONE, clause);
}

PikaRule* pika_rule_prec(const char* name, int precedence, PikaAssociativity assoc, PikaClause* clause) {
    if (!name || !clause) return NULL;
    PikaRule* r = (PikaRule*)calloc(1, sizeof(PikaRule));
    if (!r) return NULL;
    r->rule_name = pika_strdup(name);
    r->precedence = precedence;
    r->assoc = assoc;
    // unwrap ASTNodeLabel at rule root
    if (clause->type == PIKA_CLAUSE_ASTLABEL) {
        r->labeled_clause.ast_label = pika_strdup(clause->data.astlabel.label);
        r->labeled_clause.clause = clause->subclauses[0].clause;
        clause_free_shallow(clause);
    } else {
        r->labeled_clause.ast_label = NULL;
        r->labeled_clause.clause = clause;
    }
    return r;
}

// ----------------- Clause toString -----------------

static char* clause_to_string(PikaClause* clause);

static int need_to_add_parens_around_subclause(PikaClause* parent, PikaClause* sub);
static int need_to_add_parens_around_ast_label(PikaClause* sub);

static char* labeled_clause_to_string_with_label(PikaLabeledClause* lc, PikaClause* parent) {
    int add_parens = parent ? need_to_add_parens_around_subclause(parent, lc->clause) : 0;
    if (!lc->ast_label && !add_parens) {
        return pika_strdup(clause_to_string(lc->clause));
    }
    PikaStrBuf sb; sb_init(&sb);
    if (lc->ast_label) {
        sb_append(&sb, lc->ast_label);
        sb_append_char(&sb, ':');
        if (need_to_add_parens_around_ast_label(lc->clause)) add_parens = 1;
    }
    if (add_parens) sb_append_char(&sb, '(');
    sb_append(&sb, clause_to_string(lc->clause));
    if (add_parens) sb_append_char(&sb, ')');
    return sb_finish(&sb);
}

static char* clause_to_string(PikaClause* clause) {
    if (!clause) return pika_strdup("<null>");
    if (clause->to_string_cache) return clause->to_string_cache;
    PikaStrBuf sb; sb_init(&sb);
    switch (clause->type) {
        case PIKA_CLAUSE_CHARSEQ: {
            sb_append_char(&sb, '"');
            const char* s = clause->data.charseq.str ? clause->data.charseq.str : "";
            for (size_t i = 0; i < strlen(s); i++) {
                char* esc = escape_quoted_string_char((uint8_t)s[i]);
                sb_append(&sb, esc);
                free(esc);
            }
            sb_append_char(&sb, '"');
            break;
        }
        case PIKA_CLAUSE_CHARSET: {
            int inc = bitset_cardinality(clause->data.charset.include_bits);
            int exc = bitset_cardinality(clause->data.charset.exclude_bits);
            int invert_and_not = inc > 0 && exc > 0;
            if (invert_and_not) sb_append_char(&sb, '(');
            if (inc > 0) {
                if (inc == 1) {
                    int bit = bitset_next_set_bit(clause->data.charset.include_bits, 0);
                    char* esc = escape_quoted_char((uint16_t)bit);
                    sb_append_char(&sb, '\'');
                    sb_append(&sb, esc);
                    sb_append_char(&sb, '\'');
                    free(esc);
                } else {
                    sb_append_char(&sb, '[');
                    for (int i = bitset_next_set_bit(clause->data.charset.include_bits, 0); i >= 0; ) {
                        int start = i;
                        int end = i;
                        int next = bitset_next_set_bit(clause->data.charset.include_bits, i + 1);
                        while (next == end + 1) {
                            end = next;
                            next = bitset_next_set_bit(clause->data.charset.include_bits, end + 1);
                        }
                        char* esc_start = escape_char_range_char((uint16_t)start);
                        sb_append(&sb, esc_start);
                        free(esc_start);
                        if (end > start + 1) {
                            sb_append_char(&sb, '-');
                        }
                        if (end > start) {
                            char* esc_end = escape_char_range_char((uint16_t)end);
                            sb_append(&sb, esc_end);
                            free(esc_end);
                        }
                        if (next < 0) break;
                        i = next;
                    }
                    sb_append_char(&sb, ']');
                }
            }
            if (invert_and_not) sb_append(&sb, " | ");
            if (exc > 0) {
                sb_append_char(&sb, '[');
                sb_append_char(&sb, '^');
                for (int i = bitset_next_set_bit(clause->data.charset.exclude_bits, 0); i >= 0; ) {
                    int start = i;
                    int end = i;
                    int next = bitset_next_set_bit(clause->data.charset.exclude_bits, i + 1);
                    while (next == end + 1) {
                        end = next;
                        next = bitset_next_set_bit(clause->data.charset.exclude_bits, end + 1);
                    }
                    char* esc_start = escape_char_range_char((uint16_t)start);
                    sb_append(&sb, esc_start);
                    free(esc_start);
                    if (end > start + 1) {
                        sb_append_char(&sb, '-');
                    }
                    if (end > start) {
                        char* esc_end = escape_char_range_char((uint16_t)end);
                        sb_append(&sb, esc_end);
                        free(esc_end);
                    }
                    if (next < 0) break;
                    i = next;
                }
                sb_append_char(&sb, ']');
            }
            if (invert_and_not) sb_append_char(&sb, ')');
            break;
        }
        case PIKA_CLAUSE_START:
            sb_append_char(&sb, '^');
            break;
        case PIKA_CLAUSE_NOTHING:
            sb_append(&sb, "()");
            break;
        case PIKA_CLAUSE_SEQ: {
            for (size_t i = 0; i < clause->subclause_count; i++) {
                if (i > 0) sb_append_char(&sb, ' ');
                char* s = labeled_clause_to_string_with_label(&clause->subclauses[i], clause);
                sb_append(&sb, s);
                free(s);
            }
            break;
        }
        case PIKA_CLAUSE_FIRST: {
            for (size_t i = 0; i < clause->subclause_count; i++) {
                if (i > 0) sb_append(&sb, " / ");
                char* s = labeled_clause_to_string_with_label(&clause->subclauses[i], clause);
                sb_append(&sb, s);
                free(s);
            }
            break;
        }
        case PIKA_CLAUSE_ONEORMORE: {
            char* s = labeled_clause_to_string_with_label(&clause->subclauses[0], clause);
            sb_append(&sb, s);
            sb_append_char(&sb, '+');
            free(s);
            break;
        }
        case PIKA_CLAUSE_FOLLOWEDBY: {
            sb_append_char(&sb, '&');
            char* s = labeled_clause_to_string_with_label(&clause->subclauses[0], clause);
            sb_append(&sb, s);
            free(s);
            break;
        }
        case PIKA_CLAUSE_NOTFOLLOWEDBY: {
            sb_append_char(&sb, '!');
            char* s = labeled_clause_to_string_with_label(&clause->subclauses[0], clause);
            sb_append(&sb, s);
            free(s);
            break;
        }
        case PIKA_CLAUSE_RULEREF:
            sb_append(&sb, clause->data.ruleref.ref_name);
            break;
        case PIKA_CLAUSE_ASTLABEL: {
            sb_append(&sb, clause->data.astlabel.label);
            sb_append(&sb, ":(");
            sb_append(&sb, clause_to_string(clause->subclauses[0].clause));
            sb_append_char(&sb, ')');
            break;
        }
        default:
            sb_append(&sb, "<unknown>");
            break;
    }
    clause->to_string_cache = sb_finish(&sb);
    return clause->to_string_cache;
}

static char* clause_to_string_with_rules(PikaClause* clause) {
    if (!clause) return pika_strdup("<null>");
    if (clause->to_string_with_rules_cache) return clause->to_string_with_rules_cache;
    if (clause->rules_count == 0) {
        clause->to_string_with_rules_cache = pika_strdup(clause_to_string(clause));
        return clause->to_string_with_rules_cache;
    }
    PikaStrBuf sb; sb_init(&sb);
    // rule names
    for (size_t i = 0; i < clause->rules_count; i++) {
        if (i > 0) sb_append(&sb, ", ");
        sb_append(&sb, clause->rules[i]->rule_name);
    }
    sb_append(&sb, " <- ");
    int added_ast_label = 0;
    for (size_t i = 0; i < clause->rules_count; i++) {
        if (clause->rules[i]->labeled_clause.ast_label) {
            sb_append(&sb, clause->rules[i]->labeled_clause.ast_label);
            sb_append_char(&sb, ':');
            added_ast_label = 1;
        }
    }
    int add_parens = added_ast_label && need_to_add_parens_around_ast_label(clause);
    if (add_parens) sb_append_char(&sb, '(');
    sb_append(&sb, clause_to_string(clause));
    if (add_parens) sb_append_char(&sb, ')');
    clause->to_string_with_rules_cache = sb_finish(&sb);
    return clause->to_string_with_rules_cache;
}

char* pika_clause_to_string_with_rules(const PikaClause* clause) {
    return clause ? pika_strdup(clause_to_string_with_rules((PikaClause*)clause)) : NULL;
}

// ----------------- Match helpers -----------------

static PikaMatch* match_new(PikaMemoTable* memo, const PikaClause* clause, int start, int len,
                            int first_idx, PikaMatch** subs, size_t sub_count) {
    if (!memo || !memo->arena) return NULL;
    PikaMatch* m = (PikaMatch*)pika_arena_alloc(memo->arena, sizeof(PikaMatch));
    if (!m) return NULL;
    m->clause = clause;
    m->start_pos = start;
    m->len = len;
    m->first_matching_subclause_idx = first_idx;
    m->submatches = subs;
    m->submatch_count = sub_count;
    return m;
}

static int match_is_better(const PikaMatch* new_m, const PikaMatch* old_m) {
    if (!new_m || !old_m) return 0;
    return new_m->len > old_m->len;
}

// ----------------- Priority queue -----------------

typedef struct {
    PikaClause** items;
    size_t len;
    size_t cap;
} PikaClauseHeap;

static void heap_init(PikaClauseHeap* h) {
    h->items = NULL;
    h->len = 0;
    h->cap = 0;
}

static void heap_free(PikaClauseHeap* h) {
    free(h->items);
    h->items = NULL;
    h->len = h->cap = 0;
}

static void heap_push(PikaClauseHeap* h, PikaClause* c) {
    if (!h || !c) return;
    PikaClause** new_items = (PikaClause**)pika_grow_array(h->items, sizeof(PikaClause*), &h->cap, h->len + 1);
    if (!new_items) return;
    h->items = new_items;
    size_t idx = h->len++;
    h->items[idx] = c;
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (h->items[parent]->clause_idx <= h->items[idx]->clause_idx) break;
        PikaClause* tmp = h->items[parent];
        h->items[parent] = h->items[idx];
        h->items[idx] = tmp;
        idx = parent;
    }
}

static PikaClause* heap_pop(PikaClauseHeap* h) {
    if (!h || h->len == 0) return NULL;
    PikaClause* out = h->items[0];
    h->items[0] = h->items[h->len - 1];
    h->len--;
    size_t idx = 0;
    while (1) {
        size_t left = idx * 2 + 1;
        size_t right = idx * 2 + 2;
        size_t smallest = idx;
        if (left < h->len && h->items[left]->clause_idx < h->items[smallest]->clause_idx) smallest = left;
        if (right < h->len && h->items[right]->clause_idx < h->items[smallest]->clause_idx) smallest = right;
        if (smallest == idx) break;
        PikaClause* tmp = h->items[idx];
        h->items[idx] = h->items[smallest];
        h->items[smallest] = tmp;
        idx = smallest;
    }
    return out;
}

// ----------------- Grammar utils (precedence, interning, rule refs) -----------------

static void check_no_ref_cycles(PikaClause* clause, const char* self_name, PikaPtrSet* visited) {
    if (ptrset_contains(visited, clause)) {
        fprintf(stderr, "Rule cycle detected for %s\n", self_name);
        return;
    }
    ptrset_add(visited, clause);
    for (size_t i = 0; i < clause->subclause_count; i++) {
        check_no_ref_cycles(clause->subclauses[i].clause, self_name, visited);
    }
    ptrset_remove(visited, clause);
}

static int count_rule_self_refs(PikaClause* clause, const char* rule_name) {
    if (clause->type == PIKA_CLAUSE_RULEREF && strcmp(clause->data.ruleref.ref_name, rule_name) == 0) {
        return 1;
    }
    int count = 0;
    for (size_t i = 0; i < clause->subclause_count; i++) {
        count += count_rule_self_refs(clause->subclauses[i].clause, rule_name);
    }
    return count;
}

static int rewrite_self_refs(PikaClause* clause, PikaAssociativity assoc, int num_so_far, int num_total,
                             const char* self_name, int is_highest, const char* curr_name,
                             const char* next_name) {
    if (num_so_far >= num_total) return num_so_far;
    for (size_t i = 0; i < clause->subclause_count; i++) {
        PikaClause* sub = clause->subclauses[i].clause;
        if (sub->type == PIKA_CLAUSE_RULEREF && strcmp(sub->data.ruleref.ref_name, self_name) == 0) {
            if (num_total >= 2) {
                const char* new_name = ((assoc == PIKA_ASSOC_LEFT && num_so_far == 0) ||
                                        (assoc == PIKA_ASSOC_RIGHT && num_so_far == num_total - 1))
                                            ? curr_name
                                            : next_name;
                free(sub->data.ruleref.ref_name);
                sub->data.ruleref.ref_name = pika_strdup(new_name);
            } else {
                if (!is_highest) {
                    // Wrap RuleRef in First(sub, RuleRef(next))
                    PikaClause* rr_next = pika_clause_rule_ref(next_name);
                    PikaClause* subs[2] = { sub, rr_next };
                    PikaClause* first = pika_clause_first(subs, 2);
                    clause->subclauses[i].clause = first;
                } else {
                    free(sub->data.ruleref.ref_name);
                    sub->data.ruleref.ref_name = pika_strdup(next_name);
                }
            }
            num_so_far++;
        } else {
            num_so_far = rewrite_self_refs(sub, assoc, num_so_far, num_total, self_name, is_highest, curr_name, next_name);
        }
        clause_clear_to_string_cache(sub);
    }
    clause_clear_to_string_cache(clause);
    return num_so_far;
}

typedef struct {
    char* name;
    PikaRule** rules;
    size_t count;
    size_t cap;
} RuleGroup;

static RuleGroup* rulegroup_find(RuleGroup* groups, size_t group_count, const char* name) {
    for (size_t i = 0; i < group_count; i++) {
        if (strcmp(groups[i].name, name) == 0) return &groups[i];
    }
    return NULL;
}

static void handle_precedence(const char* base_name, RuleGroup* group,
                              PikaClause*** lowest_prec_clauses, size_t* lowest_count, size_t* lowest_cap,
                              PikaStringMap* lowest_prec_map) {
    // sort rules by precedence (ascending)
    // check duplicates
    for (size_t i = 0; i < group->count; i++) {
        for (size_t j = i + 1; j < group->count; j++) {
            if (group->rules[i]->precedence == group->rules[j]->precedence) {
                fprintf(stderr, "Duplicate precedence level for rule %s\n", base_name);
                return;
            }
        }
    }
    // simple insertion sort
    for (size_t i = 1; i < group->count; i++) {
        PikaRule* key = group->rules[i];
        size_t j = i;
        while (j > 0 && group->rules[j - 1]->precedence > key->precedence) {
            group->rules[j] = group->rules[j - 1];
            j--;
        }
        group->rules[j] = key;
    }

    // rename rules with precedence suffix
    for (size_t i = 0; i < group->count; i++) {
        PikaRule* r = group->rules[i];
        char buf[128];
        snprintf(buf, sizeof(buf), "%s[%d]", r->rule_name, r->precedence);
        free(r->rule_name);
        r->rule_name = pika_strdup(buf);
    }

    // rewrite rules for precedence climbing
    for (size_t i = 0; i < group->count; i++) {
        PikaRule* r = group->rules[i];
        int num_self_refs = count_rule_self_refs(r->labeled_clause.clause, base_name);
        const char* curr_name = r->rule_name;
        const char* next_name = group->rules[(i + 1) % group->count]->rule_name;
        int is_highest = (i == group->count - 1);
        if (num_self_refs >= 1) {
            rewrite_self_refs(r->labeled_clause.clause, r->assoc, 0, num_self_refs, base_name, is_highest,
                              curr_name, next_name);
        }
        if (!is_highest) {
            PikaClause* rr_next = pika_clause_rule_ref(next_name);
            PikaClause* subs[2] = { r->labeled_clause.clause, rr_next };
            PikaClause* first = pika_clause_first(subs, 2);
            // move AST label down
            if (r->labeled_clause.ast_label) {
                first->subclauses[0].ast_label = r->labeled_clause.ast_label;
                r->labeled_clause.ast_label = NULL;
            }
            r->labeled_clause.clause = first;
        }
    }

    // record lowest precedence clause
    PikaRule* lowest = group->rules[0];
    PikaClause** new_arr = (PikaClause**)pika_grow_array(*lowest_prec_clauses, sizeof(PikaClause*), lowest_cap,
                                                        *lowest_count + 1);
    if (new_arr) {
        *lowest_prec_clauses = new_arr;
        (*lowest_prec_clauses)[(*lowest_count)++] = lowest->labeled_clause.clause;
    }
    strmap_put(lowest_prec_map, pika_strdup(base_name), lowest->rule_name);
}

static PikaClause* clause_intern(PikaClause* clause, PikaStringMap* map,
                                 PikaClause*** interned, size_t* interned_len, size_t* interned_cap) {
    for (size_t i = 0; i < clause->subclause_count; i++) {
        clause->subclauses[i].clause = clause_intern(clause->subclauses[i].clause, map,
                                                     interned, interned_len, interned_cap);
    }
    char* s = clause_to_string(clause);
    PikaClause* existing = (PikaClause*)strmap_get(map, s);
    if (existing) {
        // free shallow new clause
        clause_free_shallow(clause);
        return existing;
    }
    strmap_put(map, s, clause);
    if (interned && interned_len && interned_cap) {
        PikaClause** arr = (PikaClause**)pika_grow_array(*interned, sizeof(PikaClause*),
                                                         interned_cap, *interned_len + 1);
        if (arr) {
            *interned = arr;
            (*interned)[(*interned_len)++] = clause;
        }
    }
    return clause;
}

static void resolve_rule_refs(PikaLabeledClause* labeled, PikaStringMap* rule_map,
                              PikaStringMap* lowest_prec_map, PikaPtrSet* visited) {
    if (labeled->clause->type == PIKA_CLAUSE_RULEREF) {
        PikaLabeledClause* curr = labeled;
        PikaPtrSet visited_refs; ptrset_init(&visited_refs);
        while (curr->clause->type == PIKA_CLAUSE_RULEREF) {
            if (ptrset_contains(&visited_refs, curr->clause)) {
                fprintf(stderr, "RuleRef cycle detected\n");
                break;
            }
            ptrset_add(&visited_refs, curr->clause);
            const char* ref_name = curr->clause->data.ruleref.ref_name;
            char* lowest_name = (char*)strmap_get(lowest_prec_map, ref_name);
            const char* lookup = lowest_name ? lowest_name : ref_name;
            PikaRule* ref_rule = (PikaRule*)strmap_get(rule_map, lookup);
            if (!ref_rule) {
                fprintf(stderr, "Unknown rule name: %s\n", ref_name);
                break;
            }
            curr = &ref_rule->labeled_clause;
        }
        labeled->clause = curr->clause;
        if (!labeled->ast_label && curr->ast_label) {
            labeled->ast_label = pika_strdup(curr->ast_label);
        }
        ptrset_free(&visited_refs);
        return;
    }
    if (!ptrset_add(visited, labeled->clause)) return;
    for (size_t i = 0; i < labeled->clause->subclause_count; i++) {
        resolve_rule_refs(&labeled->clause->subclauses[i], rule_map, lowest_prec_map, visited);
    }
}

static void find_terminals(PikaClause* clause, PikaPtrSet* visited, PikaClause*** out, size_t* out_len, size_t* out_cap) {
    if (!ptrset_add(visited, clause)) return;
    if (clause->type == PIKA_CLAUSE_CHARSEQ || clause->type == PIKA_CLAUSE_CHARSET
            || clause->type == PIKA_CLAUSE_START || clause->type == PIKA_CLAUSE_NOTHING) {
        PikaClause** arr = (PikaClause**)pika_grow_array(*out, sizeof(PikaClause*), out_cap, *out_len + 1);
        if (arr) {
            *out = arr;
            (*out)[(*out_len)++] = clause;
        }
    } else {
        for (size_t i = 0; i < clause->subclause_count; i++) {
            find_terminals(clause->subclauses[i].clause, visited, out, out_len, out_cap);
        }
    }
}

static void find_reachable(PikaClause* clause, PikaPtrSet* visited, PikaClause*** out, size_t* out_len, size_t* out_cap) {
    if (!ptrset_add(visited, clause)) return;
    for (size_t i = 0; i < clause->subclause_count; i++) {
        find_reachable(clause->subclauses[i].clause, visited, out, out_len, out_cap);
    }
    PikaClause** arr = (PikaClause**)pika_grow_array(*out, sizeof(PikaClause*), out_cap, *out_len + 1);
    if (arr) {
        *out = arr;
        (*out)[(*out_len)++] = clause;
    }
}

static void find_cycle_heads(PikaClause* clause, PikaPtrSet* discovered, PikaPtrSet* finished, PikaPtrSet* out) {
    if (clause->type == PIKA_CLAUSE_RULEREF) {
        fprintf(stderr, "RuleRef should be resolved before cycle detection\n");
        return;
    }
    ptrset_add(discovered, clause);
    for (size_t i = 0; i < clause->subclause_count; i++) {
        PikaClause* sub = clause->subclauses[i].clause;
        if (ptrset_contains(discovered, sub)) {
            ptrset_add(out, sub);
        } else if (!ptrset_contains(finished, sub)) {
            find_cycle_heads(sub, discovered, finished, out);
        }
    }
    ptrset_remove(discovered, clause);
    ptrset_add(finished, clause);
}

static PikaClause** find_clause_topo_order(PikaRule* top_rule, PikaRule** all_rules, size_t rule_count,
                                           PikaClause** lowest_prec_clauses, size_t lowest_count,
                                           size_t* out_count) {
    PikaClause** all_unordered = NULL;
    size_t all_len = 0, all_cap = 0;
    PikaPtrSet visited; ptrset_init(&visited);

    if (top_rule) {
        PikaClause** arr = (PikaClause**)pika_grow_array(all_unordered, sizeof(PikaClause*), &all_cap, all_len + 1);
        if (arr) {
            all_unordered = arr;
            all_unordered[all_len++] = top_rule->labeled_clause.clause;
            ptrset_add(&visited, top_rule->labeled_clause.clause);
        }
    }

    for (size_t i = 0; i < rule_count; i++) {
        find_reachable(all_rules[i]->labeled_clause.clause, &visited, &all_unordered, &all_len, &all_cap);
    }

    // find top-level clauses (not subclause of others)
    PikaPtrSet top_level; ptrset_init(&top_level);
    for (size_t i = 0; i < all_len; i++) {
        ptrset_add(&top_level, all_unordered[i]);
    }
    for (size_t i = 0; i < all_len; i++) {
        PikaClause* clause = all_unordered[i];
        for (size_t j = 0; j < clause->subclause_count; j++) {
            ptrset_remove(&top_level, clause->subclauses[j].clause);
        }
    }

    // roots for DFS
    PikaClause** roots = NULL; size_t roots_len = 0, roots_cap = 0;
    for (size_t i = 0; i < top_level.len; i++) {
        PikaClause* c = (PikaClause*)top_level.items[i];
        PikaClause** arr = (PikaClause**)pika_grow_array(roots, sizeof(PikaClause*), &roots_cap, roots_len + 1);
        if (arr) { roots = arr; roots[roots_len++] = c; }
    }
    for (size_t i = 0; i < lowest_count; i++) {
        PikaClause** arr = (PikaClause**)pika_grow_array(roots, sizeof(PikaClause*), &roots_cap, roots_len + 1);
        if (arr) { roots = arr; roots[roots_len++] = lowest_prec_clauses[i]; }
    }

    // cycle heads
    PikaPtrSet discovered; ptrset_init(&discovered);
    PikaPtrSet finished; ptrset_init(&finished);
    PikaPtrSet cycle_heads; ptrset_init(&cycle_heads);
    for (size_t i = 0; i < roots_len; i++) {
        find_cycle_heads(roots[i], &discovered, &finished, &cycle_heads);
    }
    for (size_t i = 0; i < rule_count; i++) {
        find_cycle_heads(all_rules[i]->labeled_clause.clause, &discovered, &finished, &cycle_heads);
    }
    for (size_t i = 0; i < cycle_heads.len; i++) {
        PikaClause* c = (PikaClause*)cycle_heads.items[i];
        PikaClause** arr = (PikaClause**)pika_grow_array(roots, sizeof(PikaClause*), &roots_cap, roots_len + 1);
        if (arr) { roots = arr; roots[roots_len++] = c; }
    }

    // terminals first
    PikaClause** terminals = NULL; size_t term_len = 0, term_cap = 0;
    PikaPtrSet term_visited; ptrset_init(&term_visited);
    for (size_t i = 0; i < rule_count; i++) {
        find_terminals(all_rules[i]->labeled_clause.clause, &term_visited, &terminals, &term_len, &term_cap);
    }

    // all clauses: terminals + reachable from roots (postorder)
    PikaClause** all = NULL; size_t allc_len = 0, allc_cap = 0;
    for (size_t i = 0; i < term_len; i++) {
        PikaClause** arr = (PikaClause**)pika_grow_array(all, sizeof(PikaClause*), &allc_cap, allc_len + 1);
        if (arr) { all = arr; all[allc_len++] = terminals[i]; }
    }
    PikaPtrSet reachable; ptrset_init(&reachable);
    for (size_t i = 0; i < term_len; i++) ptrset_add(&reachable, terminals[i]);
    for (size_t i = 0; i < roots_len; i++) {
        find_reachable(roots[i], &reachable, &all, &allc_len, &allc_cap);
    }

    for (size_t i = 0; i < allc_len; i++) {
        all[i]->clause_idx = (int)i;
    }

    *out_count = allc_len;

    free(roots);
    free(terminals);
    free(all_unordered);
    ptrset_free(&visited);
    ptrset_free(&top_level);
    ptrset_free(&discovered);
    ptrset_free(&finished);
    ptrset_free(&cycle_heads);
    ptrset_free(&term_visited);
    ptrset_free(&reachable);

    return all;
}

// ----------------- Clause properties -----------------

static int clause_determine_can_match_zero(PikaClause* clause) {
    int old = clause->can_match_zero;
    switch (clause->type) {
        case PIKA_CLAUSE_CHARSEQ:
            if (clause->data.charseq.str && clause->data.charseq.str[0] == '\0') clause->can_match_zero = 1;
            break;
        case PIKA_CLAUSE_CHARSET:
            break;
        case PIKA_CLAUSE_START:
        case PIKA_CLAUSE_NOTHING:
            clause->can_match_zero = 1;
            break;
        case PIKA_CLAUSE_SEQ: {
            clause->can_match_zero = 1;
            for (size_t i = 0; i < clause->subclause_count; i++) {
                if (!clause->subclauses[i].clause->can_match_zero) {
                    clause->can_match_zero = 0;
                    break;
                }
            }
            break;
        }
        case PIKA_CLAUSE_FIRST: {
            for (size_t i = 0; i < clause->subclause_count; i++) {
                if (clause->subclauses[i].clause->can_match_zero) {
                    clause->can_match_zero = 1;
                    if (i < clause->subclause_count - 1) {
                        fprintf(stderr, "First subclause matches zero characters before end: %s\n", clause_to_string(clause));
                    }
                    break;
                }
            }
            break;
        }
        case PIKA_CLAUSE_ONEORMORE:
            if (clause->subclauses[0].clause->can_match_zero) clause->can_match_zero = 1;
            break;
        case PIKA_CLAUSE_FOLLOWEDBY:
            if (clause->subclauses[0].clause->can_match_zero) {
                fprintf(stderr, "FollowedBy subclause matches zero characters: %s\n", clause_to_string(clause));
            }
            break;
        case PIKA_CLAUSE_NOTFOLLOWEDBY:
            clause->can_match_zero = 1;
            if (clause->subclauses[0].clause->can_match_zero) {
                fprintf(stderr, "NotFollowedBy subclause matches zero characters: %s\n", clause_to_string(clause));
            }
            break;
        case PIKA_CLAUSE_RULEREF:
        case PIKA_CLAUSE_ASTLABEL:
            break;
        default:
            break;
    }
    return (!old && clause->can_match_zero);
}

static void clause_add_seed_parents(PikaClause* clause) {
    // Default: all subclauses
    for (size_t i = 0; i < clause->subclause_count; i++) {
        PikaClause* sub = clause->subclauses[i].clause;
        int exists = 0;
        for (size_t j = 0; j < sub->seed_parent_count; j++) {
            if (sub->seed_parents[j] == clause) { exists = 1; break; }
        }
        if (!exists) {
            PikaClause** new_arr = (PikaClause**)pika_grow_array(sub->seed_parents, sizeof(PikaClause*), &sub->seed_parent_cap, sub->seed_parent_count + 1);
            if (!new_arr) continue;
            sub->seed_parents = new_arr;
            sub->seed_parents[sub->seed_parent_count++] = clause;
        }
    }
}

static void clause_add_seed_parents_seq(PikaClause* clause) {
    for (size_t i = 0; i < clause->subclause_count; i++) {
        PikaClause* sub = clause->subclauses[i].clause;
        int exists = 0;
        for (size_t j = 0; j < sub->seed_parent_count; j++) {
            if (sub->seed_parents[j] == clause) { exists = 1; break; }
        }
        if (!exists) {
            PikaClause** new_arr = (PikaClause**)pika_grow_array(sub->seed_parents, sizeof(PikaClause*), &sub->seed_parent_cap, sub->seed_parent_count + 1);
            if (!new_arr) continue;
            sub->seed_parents = new_arr;
            sub->seed_parents[sub->seed_parent_count++] = clause;
        }
        if (!sub->can_match_zero) break;
    }
}

// ----------------- Clause match -----------------

static PikaMatch* memo_lookup_best_match(PikaMemoTable* memo, PikaClause* clause, int start_pos);

static PikaMatch* clause_match(PikaMemoTable* memo, PikaClause* clause, int start_pos) {
    const char* input = memo->input;
    size_t input_len = memo->input_len;
    switch (clause->type) {
        case PIKA_CLAUSE_CHARSEQ: {
            const char* s = clause->data.charseq.str ? clause->data.charseq.str : "";
            size_t slen = strlen(s);
            if ((size_t)start_pos + slen <= input_len && strncmp(input + start_pos, s, slen) == 0) {
                return match_new(memo, clause, start_pos, (int)slen, 0, NULL, 0);
            }
            return NULL;
        }
        case PIKA_CLAUSE_CHARSET: {
            if (start_pos < (int)input_len) {
                unsigned char ch = (unsigned char)input[start_pos];
                int inc = clause->data.charset.include_bits ? bitset_get(clause->data.charset.include_bits, ch) : 0;
                int exc = clause->data.charset.exclude_bits ? bitset_get(clause->data.charset.exclude_bits, ch) : 0;
                if (inc || (clause->data.charset.exclude_bits && !exc)) {
                    return match_new(memo, clause, start_pos, 1, 0, NULL, 0);
                }
            }
            return NULL;
        }
        case PIKA_CLAUSE_START:
            if (start_pos == 0) return match_new(memo, clause, start_pos, 0, 0, NULL, 0);
            return NULL;
        case PIKA_CLAUSE_NOTHING:
            return match_new(memo, clause, start_pos, 0, 0, NULL, 0);
        case PIKA_CLAUSE_SEQ: {
            int curr = start_pos;
            PikaMatch** subs = NULL;
            if (memo->opts.store_submatches) {
                subs = (PikaMatch**)pika_arena_alloc(memo->arena, sizeof(PikaMatch*) * clause->subclause_count);
            }
            for (size_t i = 0; i < clause->subclause_count; i++) {
                PikaClause* sub = clause->subclauses[i].clause;
                PikaMatch* m = memo_lookup_best_match(memo, sub, curr);
                if (!m) return NULL;
                if (subs) subs[i] = m;
                curr += m->len;
            }
            return match_new(memo, clause, start_pos, curr - start_pos, 0, subs, clause->subclause_count);
        }
        case PIKA_CLAUSE_FIRST: {
            for (size_t i = 0; i < clause->subclause_count; i++) {
                PikaClause* sub = clause->subclauses[i].clause;
                PikaMatch* m = memo_lookup_best_match(memo, sub, start_pos);
                if (m) {
                    PikaMatch** subs = NULL;
                    if (memo->opts.store_submatches) {
                        subs = (PikaMatch**)pika_arena_alloc(memo->arena, sizeof(PikaMatch*));
                        if (subs) subs[0] = m;
                    }
                    return match_new(memo, clause, start_pos, m->len, (int)i, subs, subs ? 1 : 0);
                }
            }
            return NULL;
        }
        case PIKA_CLAUSE_ONEORMORE: {
            PikaClause* sub = clause->subclauses[0].clause;
            PikaMatch* m1 = memo_lookup_best_match(memo, sub, start_pos);
            if (!m1) return NULL;
            int next_pos = start_pos + m1->len;
            PikaMatch* tail = memo_lookup_best_match(memo, clause, next_pos);
            PikaMatch** subs = NULL;
            size_t count = 0;
            if (memo->opts.store_submatches) {
                if (tail) {
                    subs = (PikaMatch**)pika_arena_alloc(memo->arena, sizeof(PikaMatch*) * 2);
                    if (subs) { subs[0] = m1; subs[1] = tail; count = 2; }
                } else {
                    subs = (PikaMatch**)pika_arena_alloc(memo->arena, sizeof(PikaMatch*));
                    if (subs) { subs[0] = m1; count = 1; }
                }
            }
            int total_len = m1->len + (tail ? tail->len : 0);
            return match_new(memo, clause, start_pos, total_len, 0, subs, count);
        }
        case PIKA_CLAUSE_FOLLOWEDBY: {
            PikaClause* sub = clause->subclauses[0].clause;
            PikaMatch* m = memo_lookup_best_match(memo, sub, start_pos);
            if (m) return match_new(memo, clause, start_pos, 0, 0, NULL, 0);
            return NULL;
        }
        case PIKA_CLAUSE_NOTFOLLOWEDBY: {
            PikaClause* sub = clause->subclauses[0].clause;
            PikaMatch* m = memo_lookup_best_match(memo, sub, start_pos);
            if (!m) return match_new(memo, clause, start_pos, 0, 0, NULL, 0);
            return NULL;
        }
        default:
            return NULL;
    }
}

static PikaMatch* memo_lookup_best_match(PikaMemoTable* memo, PikaClause* clause, int start_pos) {
    size_t idx = (size_t)start_pos * memo->grammar->clause_count + (size_t)clause->clause_idx;
    if (start_pos < 0 || (size_t)start_pos > memo->input_len) return NULL;
    PikaMatch* best = memo->table[idx];
    if (best) return best;
    if (clause->type == PIKA_CLAUSE_NOTFOLLOWEDBY) {
        return clause_match(memo, clause, start_pos);
    }
    if (clause->can_match_zero) {
        return match_new(memo, clause, start_pos, 0, 0, NULL, 0);
    }
    return NULL;
}

static void memo_add_match(PikaMemoTable* memo, PikaClause* clause, int start_pos, PikaMatch* new_match, PikaClauseHeap* heap) {
    int updated = 0;
    if (new_match) {
        size_t idx = (size_t)start_pos * memo->grammar->clause_count + (size_t)clause->clause_idx;
        PikaMatch* old = memo->table[idx];
        if (!old || match_is_better(new_match, old)) {
            memo->table[idx] = new_match;
            updated = 1;
        }
    }
    for (size_t i = 0; i < clause->seed_parent_count; i++) {
        PikaClause* parent = clause->seed_parents[i];
        if (updated || parent->can_match_zero) {
            heap_push(heap, parent);
        }
    }
}

// ----------------- Grammar construction -----------------

PikaGrammar* pika_grammar_new(PikaRule** rules, size_t rule_count) {
    if (!rules || rule_count == 0) return NULL;
    PikaGrammar* g = (PikaGrammar*)calloc(1, sizeof(PikaGrammar));
    if (!g) return NULL;
    g->all_rules = (PikaRule**)calloc(rule_count, sizeof(PikaRule*));
    if (!g->all_rules) { free(g); return NULL; }
    g->rule_count = rule_count;
    for (size_t i = 0; i < rule_count; i++) {
        g->all_rules[i] = rules[i];
    }

    // group rules by base name
    RuleGroup* groups = NULL; size_t group_len = 0, group_cap = 0;
    for (size_t i = 0; i < rule_count; i++) {
        PikaRule* r = g->all_rules[i];
        if (!r || !r->rule_name) continue;
        if (r->labeled_clause.clause->type == PIKA_CLAUSE_RULEREF &&
            strcmp(r->labeled_clause.clause->data.ruleref.ref_name, r->rule_name) == 0) {
            fprintf(stderr, "Rule cannot refer only to itself: %s\n", r->rule_name);
        }
        RuleGroup* grp = rulegroup_find(groups, group_len, r->rule_name);
        if (!grp) {
            RuleGroup* new_groups = (RuleGroup*)pika_grow_array(groups, sizeof(RuleGroup), &group_cap, group_len + 1);
            if (!new_groups) continue;
            groups = new_groups;
            grp = &groups[group_len++];
            memset(grp, 0, sizeof(*grp));
            grp->name = pika_strdup(r->rule_name);
        }
        PikaRule** arr = (PikaRule**)pika_grow_array(grp->rules, sizeof(PikaRule*), &grp->cap, grp->count + 1);
        if (!arr) continue;
        grp->rules = arr;
        grp->rules[grp->count++] = r;

        PikaPtrSet visited; ptrset_init(&visited);
        check_no_ref_cycles(r->labeled_clause.clause, r->rule_name, &visited);
        ptrset_free(&visited);
    }

    // handle precedence groups
    PikaClause** lowest_prec_clauses = NULL; size_t lowest_count = 0, lowest_cap = 0;
    PikaStringMap* lowest_prec_map = strmap_new(64);
    for (size_t i = 0; i < group_len; i++) {
        RuleGroup* grp = &groups[i];
        if (grp->count > 1) {
            handle_precedence(grp->name, grp, &lowest_prec_clauses, &lowest_count, &lowest_cap, lowest_prec_map);
        }
    }

    // build rule map
    g->rule_map = strmap_new(rule_count * 2);
    for (size_t i = 0; i < rule_count; i++) {
        strmap_put(g->rule_map, g->all_rules[i]->rule_name, g->all_rules[i]);
    }

    // register rules with toplevel clauses
    for (size_t i = 0; i < rule_count; i++) {
        clause_register_rule(g->all_rules[i]->labeled_clause.clause, g->all_rules[i]);
    }

    // intern clauses
    PikaStringMap* intern_map = strmap_new(128);
    PikaClause** interned = NULL;
    size_t interned_len = 0, interned_cap = 0;
    for (size_t i = 0; i < rule_count; i++) {
        g->all_rules[i]->labeled_clause.clause = clause_intern(
            g->all_rules[i]->labeled_clause.clause, intern_map, &interned, &interned_len, &interned_cap);
    }

    // resolve RuleRefs
    PikaPtrSet visited; ptrset_init(&visited);
    for (size_t i = 0; i < rule_count; i++) {
        resolve_rule_refs(&g->all_rules[i]->labeled_clause, g->rule_map, lowest_prec_map, &visited);
    }
    ptrset_free(&visited);

    // topo order and clause properties
    g->all_clauses = find_clause_topo_order(g->all_rules[0], g->all_rules, g->rule_count,
                                           lowest_prec_clauses, lowest_count, &g->clause_count);

    // can_match_zero fixpoint
    int changed = 1;
    while (changed) {
        changed = 0;
        for (size_t i = 0; i < g->clause_count; i++) {
            if (clause_determine_can_match_zero(g->all_clauses[i])) changed = 1;
        }
    }

    // seed parent clauses
    for (size_t i = 0; i < g->clause_count; i++) {
        if (g->all_clauses[i]->type == PIKA_CLAUSE_SEQ) {
            clause_add_seed_parents_seq(g->all_clauses[i]);
        } else {
            clause_add_seed_parents(g->all_clauses[i]);
        }
    }

    // free orphaned interned clauses (not reachable from any rule)
    PikaPtrSet reachable; ptrset_init(&reachable);
    for (size_t i = 0; i < g->clause_count; i++) {
        ptrset_add(&reachable, g->all_clauses[i]);
    }
    for (size_t i = 0; i < interned_len; i++) {
        if (!ptrset_contains(&reachable, interned[i])) {
            clause_free_shallow(interned[i]);
        }
    }
    ptrset_free(&reachable);
    free(interned);

    // cleanup groups
    for (size_t i = 0; i < group_len; i++) {
        free(groups[i].name);
        free(groups[i].rules);
    }
    free(groups);
    free(lowest_prec_clauses);
    strmap_free(intern_map);
    strmap_free_with_keys(lowest_prec_map);

    return g;
}

PikaGrammar* pika_grammar_from_rules(PikaRule** rules, size_t rule_count) {
    return pika_grammar_new(rules, rule_count);
}

void pika_grammar_free(PikaGrammar* grammar) {
    if (!grammar) return;
    // free clauses (may be shared) - naive: free all from list
    for (size_t i = 0; i < grammar->clause_count; i++) {
        clause_free_shallow(grammar->all_clauses[i]);
    }
    for (size_t i = 0; i < grammar->rule_count; i++) {
        free(grammar->all_rules[i]->rule_name);
        free(grammar->all_rules[i]->labeled_clause.ast_label);
        free(grammar->all_rules[i]);
    }
    free(grammar->all_rules);
    free(grammar->all_clauses);
    strmap_free(grammar->rule_map);
    free(grammar);
}

const PikaRule* pika_grammar_get_rule(const PikaGrammar* grammar, const char* rule_name) {
    if (!grammar || !rule_name) return NULL;
    return (const PikaRule*)strmap_get(grammar->rule_map, rule_name);
}

// ----------------- Parsing -----------------

static PikaMemoTable* parse_internal(PikaGrammar* grammar, const char* input, size_t input_len, const PikaParseOptions* opts) {
    if (!grammar || !input) return NULL;
    PikaMemoTable* memo = (PikaMemoTable*)calloc(1, sizeof(PikaMemoTable));
    if (!memo) return NULL;
    memo->grammar = grammar;
    memo->input = input;
    memo->input_len = input_len;
    memo->opts.store_submatches = opts ? opts->store_submatches : 1;
    memo->arena = pika_arena_new();
    size_t table_size = (input_len + 1) * grammar->clause_count;
    memo->table = (PikaMatch**)calloc(table_size, sizeof(PikaMatch*));
    if (!memo->table) { pika_arena_free(memo->arena); free(memo); return NULL; }

    // gather terminals (exclude Nothing)
    PikaClause** terminals = NULL; size_t term_len = 0, term_cap = 0;
    for (size_t i = 0; i < grammar->clause_count; i++) {
        PikaClause* c = grammar->all_clauses[i];
        if ((c->type == PIKA_CLAUSE_CHARSEQ || c->type == PIKA_CLAUSE_CHARSET || c->type == PIKA_CLAUSE_START)
                && c->type != PIKA_CLAUSE_NOTHING) {
            PikaClause** arr = (PikaClause**)pika_grow_array(terminals, sizeof(PikaClause*), &term_cap, term_len + 1);
            if (arr) { terminals = arr; terminals[term_len++] = c; }
        }
    }

    PikaClauseHeap heap; heap_init(&heap);
    for (int start = (int)input_len - 1; start >= 0; start--) {
        for (size_t i = 0; i < term_len; i++) {
            heap_push(&heap, terminals[i]);
        }
        while (heap.len > 0) {
            PikaClause* clause = heap_pop(&heap);
            PikaMatch* m = clause_match(memo, clause, start);
            memo_add_match(memo, clause, start, m, &heap);
        }
    }

    heap_free(&heap);
    free(terminals);
    return memo;
}

PikaMemoTable* pika_grammar_parse(PikaGrammar* grammar, const char* input) {
    return parse_internal(grammar, input, strlen(input), NULL);
}

PikaMemoTable* pika_grammar_parse_n(PikaGrammar* grammar, const char* input, size_t input_len) {
    return parse_internal(grammar, input, input_len, NULL);
}

PikaMemoTable* pika_grammar_parse_with_options(PikaGrammar* grammar, const char* input, const PikaParseOptions* opts) {
    return parse_internal(grammar, input, strlen(input), opts);
}

PikaMemoTable* pika_grammar_parse_with_options_n(PikaGrammar* grammar, const char* input, size_t input_len, const PikaParseOptions* opts) {
    return parse_internal(grammar, input, input_len, opts);
}

void pika_memo_free(PikaMemoTable* memo) {
    if (!memo) return;
    free(memo->table);
    pika_arena_free(memo->arena);
    free(memo);
}

// ----------------- Matches & syntax errors -----------------

PikaMatch** pika_memo_get_all_matches(PikaMemoTable* memo, const PikaClause* clause, size_t* out_count) {
    if (!memo || !clause) return NULL;
    size_t cap = 0, len = 0;
    PikaMatch** arr = NULL;
    for (size_t pos = 0; pos <= memo->input_len; pos++) {
        size_t idx = pos * memo->grammar->clause_count + (size_t)clause->clause_idx;
        PikaMatch* m = memo->table[idx];
        if (m) {
            PikaMatch** new_arr = (PikaMatch**)pika_grow_array(arr, sizeof(PikaMatch*), &cap, len + 1);
            if (new_arr) { arr = new_arr; arr[len++] = m; }
        }
    }
    if (out_count) *out_count = len;
    return arr;
}

PikaMatch** pika_memo_get_non_overlapping_matches(PikaMemoTable* memo, const PikaClause* clause, size_t* out_count) {
    size_t all_count = 0;
    PikaMatch** all = pika_memo_get_all_matches(memo, clause, &all_count);
    if (!all) return NULL;
    size_t cap = 0, len = 0;
    PikaMatch** out = NULL;
    int prev_end = 0;
    for (size_t i = 0; i < all_count; i++) {
        PikaMatch* m = all[i];
        int start = m->start_pos;
        if (start >= prev_end) {
            PikaMatch** new_arr = (PikaMatch**)pika_grow_array(out, sizeof(PikaMatch*), &cap, len + 1);
            if (new_arr) { out = new_arr; out[len++] = m; }
            prev_end = start + m->len;
        }
    }
    free(all);
    if (out_count) *out_count = len;
    return out;
}

PikaMatch** pika_memo_get_all_matches_for_rule(PikaMemoTable* memo, const char* rule_name, size_t* out_count) {
    if (!memo) return NULL;
    const PikaRule* r = pika_grammar_get_rule(memo->grammar, rule_name);
    if (!r) return NULL;
    return pika_memo_get_all_matches(memo, r->labeled_clause.clause, out_count);
}

PikaMatch** pika_memo_get_non_overlapping_matches_for_rule(PikaMemoTable* memo, const char* rule_name, size_t* out_count) {
    if (!memo) return NULL;
    const PikaRule* r = pika_grammar_get_rule(memo->grammar, rule_name);
    if (!r) return NULL;
    return pika_memo_get_non_overlapping_matches(memo, r->labeled_clause.clause, out_count);
}

typedef struct {
    int start;
    int end;
} PikaRange;

static void ranges_add(PikaRange** ranges, size_t* len, size_t* cap, int start, int end) {
    if (end <= start) return;
    // insert and merge
    PikaRange* arr = *ranges;
    size_t n = *len;
    size_t c = *cap;
    if (n == 0) {
        arr = (PikaRange*)pika_grow_array(arr, sizeof(PikaRange), &c, 1);
        if (!arr) return;
        arr[0].start = start;
        arr[0].end = end;
        *ranges = arr; *len = 1; *cap = c;
        return;
    }
    // append then sort/merge (simple)
    arr = (PikaRange*)pika_grow_array(arr, sizeof(PikaRange), &c, n + 1);
    if (!arr) return;
    arr[n].start = start;
    arr[n].end = end;
    n++;
    // simple insertion sort by start
    for (size_t i = 1; i < n; i++) {
        PikaRange key = arr[i];
        size_t j = i;
        while (j > 0 && arr[j - 1].start > key.start) {
            arr[j] = arr[j - 1];
            j--;
        }
        arr[j] = key;
    }
    // merge
    size_t out = 0;
    for (size_t i = 0; i < n; i++) {
        if (out == 0 || arr[i].start > arr[out - 1].end) {
            arr[out++] = arr[i];
        } else if (arr[i].end > arr[out - 1].end) {
            arr[out - 1].end = arr[i].end;
        }
    }
    *ranges = arr; *len = out; *cap = c;
}

PikaSyntaxError* pika_memo_get_syntax_errors(PikaMemoTable* memo, const char** coverage_rule_names, size_t rule_count, size_t* out_count) {
    if (!memo || !coverage_rule_names || rule_count == 0) return NULL;
    PikaRange* ranges = NULL; size_t range_len = 0, range_cap = 0;
    for (size_t i = 0; i < rule_count; i++) {
        const PikaRule* rule = pika_grammar_get_rule(memo->grammar, coverage_rule_names[i]);
        if (!rule) continue;
        size_t mcount = 0;
        PikaMatch** matches = pika_memo_get_non_overlapping_matches(memo, rule->labeled_clause.clause, &mcount);
        for (size_t j = 0; j < mcount; j++) {
            PikaMatch* m = matches[j];
            ranges_add(&ranges, &range_len, &range_cap, m->start_pos, m->start_pos + m->len);
        }
        free(matches);
    }
    // invert ranges
    PikaSyntaxError* errors = NULL; size_t err_len = 0, err_cap = 0;
    int pos = 0;
    for (size_t i = 0; i < range_len; i++) {
        if (pos < ranges[i].start) {
            int start = pos;
            int end = ranges[i].start;
            PikaSyntaxError* arr = (PikaSyntaxError*)pika_grow_array(errors, sizeof(PikaSyntaxError), &err_cap, err_len + 1);
            if (arr) {
                errors = arr;
                errors[err_len].start = start;
                errors[err_len].end = end;
                size_t len = (size_t)(end - start);
                char* text = (char*)malloc(len + 1);
                if (text) {
                    memcpy(text, memo->input + start, len);
                    text[len] = '\0';
                }
                errors[err_len].text = text;
                err_len++;
            }
        }
        if (ranges[i].end > pos) pos = ranges[i].end;
    }
    if (pos < (int)memo->input_len) {
        int start = pos;
        int end = (int)memo->input_len;
        PikaSyntaxError* arr = (PikaSyntaxError*)pika_grow_array(errors, sizeof(PikaSyntaxError), &err_cap, err_len + 1);
        if (arr) {
            errors = arr;
            errors[err_len].start = start;
            errors[err_len].end = end;
            size_t len = (size_t)(end - start);
            char* text = (char*)malloc(len + 1);
            if (text) {
                memcpy(text, memo->input + start, len);
                text[len] = '\0';
            }
            errors[err_len].text = text;
            err_len++;
        }
    }
    free(ranges);
    if (out_count) *out_count = err_len;
    return errors;
}

PikaLabeledMatch* pika_match_get_submatches(const PikaMatch* match, size_t* out_count) {
    if (!match || !match->submatches || match->submatch_count == 0) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    const PikaClause* clause = match->clause;
    if (clause->type == PIKA_CLAUSE_ONEORMORE) {
        // flatten right-recursive list
        size_t cap = 0, len = 0;
        PikaLabeledMatch* out = NULL;
        const PikaMatch* curr = match;
        while (curr && curr->submatch_count > 0) {
            PikaLabeledMatch* arr = (PikaLabeledMatch*)pika_grow_array(out, sizeof(PikaLabeledMatch), &cap, len + 1);
            if (!arr) break;
            out = arr;
            out[len].label = clause->subclauses[0].ast_label;
            out[len].match = curr->submatches[0];
            len++;
            if (curr->submatch_count == 1) break;
            curr = curr->submatches[1];
        }
        if (out_count) *out_count = len;
        return out;
    } else if (clause->type == PIKA_CLAUSE_FIRST) {
        PikaLabeledMatch* out = (PikaLabeledMatch*)malloc(sizeof(PikaLabeledMatch));
        if (!out) return NULL;
        out[0].label = clause->subclauses[match->first_matching_subclause_idx].ast_label;
        out[0].match = match->submatches[0];
        if (out_count) *out_count = 1;
        return out;
    } else {
        size_t count = match->submatch_count;
        PikaLabeledMatch* out = (PikaLabeledMatch*)malloc(sizeof(PikaLabeledMatch) * count);
        if (!out) return NULL;
        for (size_t i = 0; i < count; i++) {
            out[i].label = clause->subclauses[i].ast_label;
            out[i].match = match->submatches[i];
        }
        if (out_count) *out_count = count;
        return out;
    }
}

int pika_match_start(const PikaMatch* match) {
    return match ? match->start_pos : -1;
}

int pika_match_len(const PikaMatch* match) {
    return match ? match->len : -1;
}

// ----------------- AST -----------------

static void ast_add_nodes(PikaAstNode* parent, const PikaMatch* match, const char* input) {
    size_t count = 0;
    PikaLabeledMatch* subs = pika_match_get_submatches(match, &count);
    for (size_t i = 0; i < count; i++) {
        const char* label = subs[i].label;
        const PikaMatch* subm = subs[i].match;
        if (label) {
            PikaAstNode* child = pika_ast_from_match(label, subm, input);
            if (!child) continue;
            size_t cap = 0;
            PikaAstNode** arr = (PikaAstNode**)pika_grow_array(parent->children, sizeof(PikaAstNode*), &cap, parent->child_count + 1);
            if (arr) {
                parent->children = arr;
                parent->children[parent->child_count++] = child;
            }
        } else {
            ast_add_nodes(parent, subm, input);
        }
    }
    free(subs);
}

PikaAstNode* pika_ast_from_match(const char* label, const PikaMatch* match, const char* input) {
    if (!label || !match) return NULL;
    PikaAstNode* node = (PikaAstNode*)calloc(1, sizeof(PikaAstNode));
    if (!node) return NULL;
    node->label = pika_strdup(label);
    node->node_type = match->clause;
    node->start_pos = match->start_pos;
    node->len = match->len;
    node->input = input;
    ast_add_nodes(node, match, input);
    return node;
}

void pika_ast_free(PikaAstNode* node) {
    if (!node) return;
    for (size_t i = 0; i < node->child_count; i++) {
        pika_ast_free(node->children[i]);
    }
    free(node->children);
    free(node->label);
    free(node);
}

// ----------------- Debug strings -----------------

char* pika_match_to_string(const PikaMatch* match) {
    if (!match) return NULL;
    PikaStrBuf sb; sb_init(&sb);
    sb_append(&sb, clause_to_string((PikaClause*)match->clause));
    sb_append(&sb, " : ");
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", match->start_pos);
    sb_append(&sb, buf);
    sb_append_char(&sb, '+');
    snprintf(buf, sizeof(buf), "%d", match->len);
    sb_append(&sb, buf);
    return sb_finish(&sb);
}

char* pika_match_to_string_with_rules(const PikaMatch* match) {
    if (!match) return NULL;
    PikaStrBuf sb; sb_init(&sb);
    sb_append(&sb, clause_to_string_with_rules((PikaClause*)match->clause));
    sb_append(&sb, " : ");
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", match->start_pos);
    sb_append(&sb, buf);
    sb_append_char(&sb, '+');
    snprintf(buf, sizeof(buf), "%d", match->len);
    sb_append(&sb, buf);
    return sb_finish(&sb);
}

// ----------------- MetaGrammar -----------------

// Precedence mapping for metagrammar formatting
static int clause_type_prec(PikaClauseType t) {
    switch (t) {
        case PIKA_CLAUSE_CHARSEQ:
        case PIKA_CLAUSE_CHARSET:
        case PIKA_CLAUSE_START:
        case PIKA_CLAUSE_NOTHING:
        case PIKA_CLAUSE_RULEREF:
            return 7;
        case PIKA_CLAUSE_ONEORMORE:
            return 6;
        case PIKA_CLAUSE_NOTFOLLOWEDBY:
        case PIKA_CLAUSE_FOLLOWEDBY:
            return 5;
        case PIKA_CLAUSE_ASTLABEL:
            return 3;
        case PIKA_CLAUSE_SEQ:
            return 2;
        case PIKA_CLAUSE_FIRST:
            return 1;
        default:
            return 0;
    }
}

static int need_to_add_parens_around_subclause(PikaClause* parent, PikaClause* sub) {
    int parent_prec = clause_type_prec(parent->type);
    int sub_prec = clause_type_prec(sub->type);
    if (parent->type == PIKA_CLAUSE_FIRST && sub->type == PIKA_CLAUSE_SEQ) return 1;
    return sub_prec <= parent_prec;
}

static int need_to_add_parens_around_ast_label(PikaClause* sub) {
    int ast_prec = clause_type_prec(PIKA_CLAUSE_ASTLABEL);
    int sub_prec = clause_type_prec(sub->type);
    return sub_prec < ast_prec;
}

// Build the meta-grammar (returns a new Grammar)
static PikaGrammar* build_meta_grammar(void) {
    // Rule names
    const char* GRAMMAR = "GRAMMAR";
    const char* WSC = "WSC";
    const char* COMMENT = "COMMENT";
    const char* RULE = "RULE";
    const char* CLAUSE = "CLAUSE";
    const char* IDENT = "IDENT";
    const char* PREC = "PREC";
    const char* NUM = "NUM";
    const char* NAME_CHAR = "NAME_CHAR";
    const char* CHAR_SET = "CHARSET";
    const char* HEX = "Hex";
    const char* CHAR_RANGE = "CHAR_RANGE";
    const char* CHAR_RANGE_CHAR = "CHAR_RANGE_CHAR";
    const char* QUOTED_STRING = "QUOTED_STR";
    const char* ESCAPED_CTRL_CHAR = "ESCAPED_CTRL_CHAR";
    const char* SINGLE_QUOTED_CHAR = "SINGLE_QUOTED_CHAR";
    const char* STR_QUOTED_CHAR = "STR_QUOTED_CHAR";
    const char* NOTHING = "NOTHING";
    const char* START = "START";

    // AST labels
    const char* RULE_AST = "RuleAST";
    const char* PREC_AST = "PrecAST";
    const char* R_ASSOC_AST = "RAssocAST";
    const char* L_ASSOC_AST = "LAssocAST";
    const char* IDENT_AST = "IdentAST";
    const char* LABEL_AST = "LabelAST";
    const char* LABEL_NAME_AST = "LabelNameAST";
    const char* LABEL_CLAUSE_AST = "LabelClauseAST";
    const char* SEQ_AST = "SeqAST";
    const char* FIRST_AST = "FirstAST";
    const char* FOLLOWED_BY_AST = "FollowedByAST";
    const char* NOT_FOLLOWED_BY_AST = "NotFollowedByAST";
    const char* ONE_OR_MORE_AST = "OneOrMoreAST";
    const char* ZERO_OR_MORE_AST = "ZeroOrMoreAST";
    const char* OPTIONAL_AST = "OptionalAST";
    const char* SINGLE_QUOTED_CHAR_AST = "SingleQuotedCharAST";
    const char* CHAR_RANGE_AST = "CharRangeAST";
    const char* QUOTED_STRING_AST = "QuotedStringAST";
    const char* START_AST = "StartAST";
    const char* NOTHING_AST = "NothingAST";

    // Build rules using ClauseFactory semantics
    PikaRule* rules[64];
    size_t idx = 0;

    // GRAMMAR <- ^ WSC RULE+
    {
        PikaClause* subs[3] = { pika_clause_start(), pika_clause_rule_ref(WSC), pika_clause_one_or_more(pika_clause_rule_ref(RULE)) };
        PikaClause* seq = pika_clause_seq(subs, 3);
        rules[idx++] = pika_rule(GRAMMAR, seq);
    }

    // RULE <- ident WSC [prec]? <- WSC clause WSC ';' WSC
    {
        PikaClause* prec_opt = pika_clause_optional(pika_clause_rule_ref(PREC));
        PikaClause* rule_clause = pika_clause_ast_label(RULE_AST, pika_clause_seq((PikaClause*[]){
            pika_clause_rule_ref(IDENT),
            pika_clause_rule_ref(WSC),
            prec_opt,
            pika_clause_str("<-"),
            pika_clause_rule_ref(WSC),
            pika_clause_rule_ref(CLAUSE),
            pika_clause_rule_ref(WSC),
            pika_clause_char(';'),
            pika_clause_rule_ref(WSC)
        }, 9));
        rules[idx++] = pika_rule(RULE, rule_clause);
    }

    // CLAUSE precedence levels
    // parens
    {
        PikaClause* seq = pika_clause_seq((PikaClause*[]){
            pika_clause_char('('),
            pika_clause_rule_ref(WSC),
            pika_clause_rule_ref(CLAUSE),
            pika_clause_rule_ref(WSC),
            pika_clause_char(')')
        }, 5);
        rules[idx++] = pika_rule_prec(CLAUSE, 8, PIKA_ASSOC_NONE, seq);
    }
    // terminals
    {
        PikaClause* first = pika_clause_first((PikaClause*[]){
            pika_clause_rule_ref(IDENT),
            pika_clause_rule_ref(QUOTED_STRING),
            pika_clause_rule_ref(CHAR_SET),
            pika_clause_rule_ref(NOTHING),
            pika_clause_rule_ref(START)
        }, 5);
        rules[idx++] = pika_rule_prec(CLAUSE, 7, PIKA_ASSOC_NONE, first);
    }
    // one or more / zero or more
    {
        PikaClause* one = pika_clause_seq((PikaClause*[]){
            pika_clause_ast_label(ONE_OR_MORE_AST, pika_clause_rule_ref(CLAUSE)),
            pika_clause_rule_ref(WSC),
            pika_clause_char('+')
        }, 3);
        PikaClause* zero = pika_clause_seq((PikaClause*[]){
            pika_clause_ast_label(ZERO_OR_MORE_AST, pika_clause_rule_ref(CLAUSE)),
            pika_clause_rule_ref(WSC),
            pika_clause_char('*')
        }, 3);
        PikaClause* first = pika_clause_first((PikaClause*[]){ one, zero }, 2);
        rules[idx++] = pika_rule_prec(CLAUSE, 6, PIKA_ASSOC_NONE, first);
    }
    // followed by / not followed by
    {
        PikaClause* fb = pika_clause_seq((PikaClause*[]){
            pika_clause_char('&'),
            pika_clause_ast_label(FOLLOWED_BY_AST, pika_clause_rule_ref(CLAUSE))
        }, 2);
        PikaClause* nfb = pika_clause_seq((PikaClause*[]){
            pika_clause_char('!'),
            pika_clause_ast_label(NOT_FOLLOWED_BY_AST, pika_clause_rule_ref(CLAUSE))
        }, 2);
        PikaClause* first = pika_clause_first((PikaClause*[]){ fb, nfb }, 2);
        rules[idx++] = pika_rule_prec(CLAUSE, 5, PIKA_ASSOC_NONE, first);
    }
    // optional
    {
        PikaClause* seq = pika_clause_seq((PikaClause*[]){
            pika_clause_ast_label(OPTIONAL_AST, pika_clause_rule_ref(CLAUSE)),
            pika_clause_rule_ref(WSC),
            pika_clause_char('?')
        }, 3);
        rules[idx++] = pika_rule_prec(CLAUSE, 4, PIKA_ASSOC_NONE, seq);
    }
    // label
    {
        PikaClause* seq = pika_clause_seq((PikaClause*[]){
            pika_clause_ast_label(LABEL_NAME_AST, pika_clause_rule_ref(IDENT)),
            pika_clause_rule_ref(WSC),
            pika_clause_char(':'),
            pika_clause_rule_ref(WSC),
            pika_clause_ast_label(LABEL_CLAUSE_AST, pika_clause_rule_ref(CLAUSE)),
            pika_clause_rule_ref(WSC)
        }, 6);
        PikaClause* ast = pika_clause_ast_label(LABEL_AST, seq);
        rules[idx++] = pika_rule_prec(CLAUSE, 3, PIKA_ASSOC_NONE, ast);
    }
    // seq
    {
        PikaClause* seq = pika_clause_seq((PikaClause*[]){
            pika_clause_rule_ref(CLAUSE),
            pika_clause_rule_ref(WSC),
            pika_clause_one_or_more(pika_clause_seq((PikaClause*[]){
                pika_clause_rule_ref(CLAUSE),
                pika_clause_rule_ref(WSC)
            }, 2))
        }, 3);
        PikaClause* ast = pika_clause_ast_label(SEQ_AST, seq);
        rules[idx++] = pika_rule_prec(CLAUSE, 2, PIKA_ASSOC_NONE, ast);
    }
    // first
    {
        PikaClause* seq = pika_clause_seq((PikaClause*[]){
            pika_clause_rule_ref(CLAUSE),
            pika_clause_rule_ref(WSC),
            pika_clause_one_or_more(pika_clause_seq((PikaClause*[]){
                pika_clause_char('/'),
                pika_clause_rule_ref(WSC),
                pika_clause_rule_ref(CLAUSE),
                pika_clause_rule_ref(WSC)
            }, 4))
        }, 3);
        PikaClause* ast = pika_clause_ast_label(FIRST_AST, seq);
        rules[idx++] = pika_rule_prec(CLAUSE, 1, PIKA_ASSOC_NONE, ast);
    }
    // WSC
    {
        PikaClause* ws = pika_clause_first((PikaClause*[]){
            pika_clause_charset_from_chars(" \n\r\t", 4),
            pika_clause_rule_ref(COMMENT)
        }, 2);
        rules[idx++] = pika_rule(WSC, pika_clause_zero_or_more(ws));
    }
    // COMMENT
    {
        PikaClause* seq = pika_clause_seq((PikaClause*[]){
            pika_clause_char('#'),
            pika_clause_zero_or_more(pika_clause_charset_invert(pika_clause_char('\n')))
        }, 2);
        rules[idx++] = pika_rule(COMMENT, seq);
    }
    // IDENT
    {
        PikaClause* seq = pika_clause_seq((PikaClause*[]){
            pika_clause_rule_ref(NAME_CHAR),
            pika_clause_zero_or_more(pika_clause_first((PikaClause*[]){
                pika_clause_rule_ref(NAME_CHAR),
                pika_clause_charset_from_range('0','9')
            }, 2))
        }, 2);
        PikaClause* ast = pika_clause_ast_label(IDENT_AST, seq);
        rules[idx++] = pika_rule(IDENT, ast);
    }
    // NUM
    {
        rules[idx++] = pika_rule(NUM, pika_clause_one_or_more(pika_clause_charset_from_range('0','9')));
    }
    // NAME_CHAR
    {
        PikaClause* cs = pika_clause_charset_union_take((PikaClause*[]){
            pika_clause_charset_from_range('a','z'),
            pika_clause_charset_from_range('A','Z'),
            pika_clause_char('_'),
            pika_clause_char('-')
        }, 4);
        rules[idx++] = pika_rule(NAME_CHAR, cs);
    }
    // PREC
    {
        PikaClause* seq = pika_clause_seq((PikaClause*[]){
            pika_clause_char('['),
            pika_clause_rule_ref(WSC),
            pika_clause_ast_label(PREC_AST, pika_clause_rule_ref(NUM)),
            pika_clause_rule_ref(WSC),
            pika_clause_optional(pika_clause_seq((PikaClause*[]){
                pika_clause_char(','),
                pika_clause_rule_ref(WSC),
                pika_clause_first((PikaClause*[]){
                    pika_clause_ast_label(R_ASSOC_AST, pika_clause_first((PikaClause*[]){pika_clause_char('r'), pika_clause_char('R')},2)),
                    pika_clause_ast_label(L_ASSOC_AST, pika_clause_first((PikaClause*[]){pika_clause_char('l'), pika_clause_char('L')},2))
                },2),
                pika_clause_rule_ref(WSC)
            },4)),
            pika_clause_char(']'),
            pika_clause_rule_ref(WSC)
        }, 7);
        rules[idx++] = pika_rule(PREC, seq);
    }
    // CHAR_SET
    {
        PikaClause* single = pika_clause_seq((PikaClause*[]){
            pika_clause_char('\''),
            pika_clause_ast_label(SINGLE_QUOTED_CHAR_AST, pika_clause_rule_ref(SINGLE_QUOTED_CHAR)),
            pika_clause_char('\'')
        }, 3);
        PikaClause* range = pika_clause_seq((PikaClause*[]){
            pika_clause_char('['),
            pika_clause_ast_label(CHAR_RANGE_AST, pika_clause_seq((PikaClause*[]){
                pika_clause_optional(pika_clause_char('^')),
                pika_clause_one_or_more(pika_clause_first((PikaClause*[]){
                    pika_clause_rule_ref(CHAR_RANGE),
                    pika_clause_rule_ref(CHAR_RANGE_CHAR)
                },2))
            },2)),
            pika_clause_char(']')
        }, 3);
        rules[idx++] = pika_rule(CHAR_SET, pika_clause_first((PikaClause*[]){single, range},2));
    }
    // SINGLE_QUOTED_CHAR
    {
        PikaClause* first = pika_clause_first((PikaClause*[]){
            pika_clause_rule_ref(ESCAPED_CTRL_CHAR),
            pika_clause_charset_invert(pika_clause_char('\''))
        }, 2);
        rules[idx++] = pika_rule(SINGLE_QUOTED_CHAR, first);
    }
    // CHAR_RANGE
    {
        PikaClause* seq = pika_clause_seq((PikaClause*[]){
            pika_clause_rule_ref(CHAR_RANGE_CHAR),
            pika_clause_char('-'),
            pika_clause_rule_ref(CHAR_RANGE_CHAR)
        }, 3);
        rules[idx++] = pika_rule(CHAR_RANGE, seq);
    }
    // CHAR_RANGE_CHAR
    {
        PikaClause* first = pika_clause_first((PikaClause*[]){
            pika_clause_charset_invert(pika_clause_charset_from_chars("\\]", 2)),
            pika_clause_rule_ref(ESCAPED_CTRL_CHAR),
            pika_clause_str("\\-"),
            pika_clause_str("\\\\"),
            pika_clause_str("\\]"),
            pika_clause_str("\\^")
        }, 6);
        rules[idx++] = pika_rule(CHAR_RANGE_CHAR, first);
    }
    // QUOTED_STRING
    {
        PikaClause* seq = pika_clause_seq((PikaClause*[]){
            pika_clause_char('"'),
            pika_clause_ast_label(QUOTED_STRING_AST, pika_clause_zero_or_more(pika_clause_rule_ref(STR_QUOTED_CHAR))),
            pika_clause_char('"')
        }, 3);
        rules[idx++] = pika_rule(QUOTED_STRING, seq);
    }
    // STR_QUOTED_CHAR
    {
        PikaClause* first = pika_clause_first((PikaClause*[]){
            pika_clause_rule_ref(ESCAPED_CTRL_CHAR),
            pika_clause_charset_invert(pika_clause_charset_from_chars("\"\\", 2))
        }, 2);
        rules[idx++] = pika_rule(STR_QUOTED_CHAR, first);
    }
    // HEX
    {
        PikaClause* cs = pika_clause_charset_union_take((PikaClause*[]){
            pika_clause_charset_from_range('0','9'),
            pika_clause_charset_from_range('a','f'),
            pika_clause_charset_from_range('A','F')
        }, 3);
        rules[idx++] = pika_rule(HEX, cs);
    }
    // ESCAPED_CTRL_CHAR
    {
        PikaClause* seq_u = pika_clause_seq((PikaClause*[]){
            pika_clause_str("\\u"),
            pika_clause_rule_ref(HEX),
            pika_clause_rule_ref(HEX),
            pika_clause_rule_ref(HEX),
            pika_clause_rule_ref(HEX)
        }, 5);
        PikaClause* first = pika_clause_first((PikaClause*[]){
            pika_clause_str("\\t"),
            pika_clause_str("\\b"),
            pika_clause_str("\\n"),
            pika_clause_str("\\r"),
            pika_clause_str("\\f"),
            pika_clause_str("\\'"),
            pika_clause_str("\\\""),
            pika_clause_str("\\\\"),
            seq_u
        }, 9);
        rules[idx++] = pika_rule(ESCAPED_CTRL_CHAR, first);
    }
    // NOTHING
    {
        PikaClause* seq = pika_clause_seq((PikaClause*[]){
            pika_clause_char('('),
            pika_clause_rule_ref(WSC),
            pika_clause_char(')')
        }, 3);
        rules[idx++] = pika_rule(NOTHING, pika_clause_ast_label(NOTHING_AST, seq));
    }
    // START
    {
        rules[idx++] = pika_rule(START, pika_clause_ast_label(START_AST, pika_clause_char('^')));
    }

    return pika_grammar_new(rules, idx);
}

// Parse AST nodes into clauses
static PikaClause* parse_ast_node(PikaAstNode* node);

static PikaClause** parse_ast_nodes(PikaAstNode** nodes, size_t count, size_t* out_count) {
    PikaClause** clauses = NULL; size_t len = 0, cap = 0;
    for (size_t i = 0; i < count; i++) {
        PikaClause* c = parse_ast_node(nodes[i]);
        if (!c) continue;
        PikaClause** arr = (PikaClause**)pika_grow_array(clauses, sizeof(PikaClause*), &cap, len + 1);
        if (arr) { clauses = arr; clauses[len++] = c; }
    }
    if (out_count) *out_count = len;
    return clauses;
}

static PikaClause* parse_ast_node(PikaAstNode* node) {
    if (!node) return NULL;
    if (strcmp(node->label, "SeqAST") == 0) {
        size_t n = 0; PikaClause** subs = parse_ast_nodes(node->children, node->child_count, &n);
        PikaClause* c = pika_clause_seq(subs, n);
        free(subs);
        return c;
    } else if (strcmp(node->label, "FirstAST") == 0) {
        size_t n = 0; PikaClause** subs = parse_ast_nodes(node->children, node->child_count, &n);
        PikaClause* c = pika_clause_first(subs, n);
        free(subs);
        return c;
    } else if (strcmp(node->label, "OneOrMoreAST") == 0) {
        if (node->child_count != 1) return NULL;
        return pika_clause_one_or_more(parse_ast_node(node->children[0]));
    } else if (strcmp(node->label, "ZeroOrMoreAST") == 0) {
        if (node->child_count != 1) return NULL;
        return pika_clause_zero_or_more(parse_ast_node(node->children[0]));
    } else if (strcmp(node->label, "OptionalAST") == 0) {
        if (node->child_count != 1) return NULL;
        return pika_clause_optional(parse_ast_node(node->children[0]));
    } else if (strcmp(node->label, "FollowedByAST") == 0) {
        if (node->child_count != 1) return NULL;
        return pika_clause_followed_by(parse_ast_node(node->children[0]));
    } else if (strcmp(node->label, "NotFollowedByAST") == 0) {
        if (node->child_count != 1) return NULL;
        return pika_clause_not_followed_by(parse_ast_node(node->children[0]));
    } else if (strcmp(node->label, "LabelAST") == 0) {
        if (node->child_count < 2) return NULL;
        const char* label = node->children[0]->input + node->children[0]->start_pos;
        size_t len = (size_t)node->children[0]->len;
        char* lbl = (char*)malloc(len + 1);
        if (!lbl) return NULL;
        memcpy(lbl, label, len);
        lbl[len] = '\0';
        PikaClause* sub = parse_ast_node(node->children[1]->children[0]);
        PikaClause* c = pika_clause_ast_label(lbl, sub);
        free(lbl);
        return c;
    } else if (strcmp(node->label, "IdentAST") == 0) {
        size_t len = (size_t)node->len;
        char* name = (char*)malloc(len + 1);
        if (!name) return NULL;
        memcpy(name, node->input + node->start_pos, len);
        name[len] = '\0';
        PikaClause* c = pika_clause_rule_ref(name);
        free(name);
        return c;
    } else if (strcmp(node->label, "QuotedStringAST") == 0) {
        size_t len = (size_t)node->len;
        char* raw = (char*)malloc(len + 1);
        if (!raw) return NULL;
        memcpy(raw, node->input + node->start_pos, len);
        raw[len] = '\0';
        char* unesc = unescape_string(raw, len);
        free(raw);
        PikaClause* c = pika_clause_str(unesc);
        free(unesc);
        return c;
    } else if (strcmp(node->label, "SingleQuotedCharAST") == 0) {
        size_t len = (size_t)node->len;
        char* raw = (char*)malloc(len + 1);
        if (!raw) return NULL;
        memcpy(raw, node->input + node->start_pos, len);
        raw[len] = '\0';
        uint16_t outc = 0;
        unescape_char(raw, len, &outc);
        free(raw);
        return pika_clause_char((char)outc);
    } else if (strcmp(node->label, "StartAST") == 0) {
        return pika_clause_start();
    } else if (strcmp(node->label, "NothingAST") == 0) {
        return pika_clause_nothing();
    } else if (strcmp(node->label, "CharRangeAST") == 0) {
        size_t len = (size_t)node->len;
        char* raw = (char*)malloc(len + 1);
        if (!raw) return NULL;
        memcpy(raw, node->input + node->start_pos, len);
        raw[len] = '\0';
        PikaClause* c = pika_clause_charset_from_pattern(raw);
        free(raw);
        return c;
    }
    // default: recurse
    if (node->child_count == 1) return parse_ast_node(node->children[0]);
    return NULL;
}

static PikaRule* parse_rule_node(PikaAstNode* rule_node) {
    if (!rule_node || rule_node->child_count < 2) return NULL;
    PikaAstNode* name_node = rule_node->children[0];
    size_t name_len = (size_t)name_node->len;
    char* name = (char*)malloc(name_len + 1);
    if (!name) return NULL;
    memcpy(name, name_node->input + name_node->start_pos, name_len);
    name[name_len] = '\0';

    int has_prec = rule_node->child_count > 2;
    PikaAssociativity assoc = PIKA_ASSOC_NONE;
    int precedence = -1;
    if (has_prec) {
        PikaAstNode* prec_node = rule_node->children[1];
        char tmp[32];
        size_t plen = (size_t)prec_node->len;
        size_t len = plen < sizeof(tmp) - 1 ? plen : sizeof(tmp) - 1;
        memcpy(tmp, prec_node->input + prec_node->start_pos, len);
        tmp[len] = '\0';
        precedence = atoi(tmp);
        if (rule_node->child_count >= 4) {
            PikaAstNode* assoc_node = rule_node->children[2];
            if (strcmp(assoc_node->label, "LAssocAST") == 0) assoc = PIKA_ASSOC_LEFT;
            else if (strcmp(assoc_node->label, "RAssocAST") == 0) assoc = PIKA_ASSOC_RIGHT;
        }
    }
    PikaAstNode* clause_node = rule_node->children[rule_node->child_count - 1];
    PikaClause* clause = parse_ast_node(clause_node);
    PikaRule* rule = pika_rule_prec(name, precedence, assoc, clause);
    free(name);
    return rule;
}

PikaGrammar* pika_meta_parse(const char* grammar_spec, char** error_out) {
    if (error_out) *error_out = NULL;
    if (!grammar_spec) return NULL;
    PikaGrammar* meta = build_meta_grammar();
    if (!meta) return NULL;
    PikaMemoTable* memo = pika_grammar_parse(meta, grammar_spec);
    if (!memo) { pika_grammar_free(meta); return NULL; }

    // syntax errors
    const char* coverage[3] = { "GRAMMAR", "RULE", "CLAUSE[1]" };
    size_t err_count = 0;
    PikaSyntaxError* errs = pika_memo_get_syntax_errors(memo, coverage, 3, &err_count);
    if (err_count > 0) {
        if (error_out) {
            PikaStrBuf sb; sb_init(&sb);
            sb_append(&sb, "Syntax errors in grammar spec\n");
            for (size_t i = 0; i < err_count; i++) {
                char buf[64];
                snprintf(buf, sizeof(buf), "[%d,%d] ", errs[i].start, errs[i].end);
                sb_append(&sb, buf);
                if (errs[i].text) sb_append(&sb, errs[i].text);
                sb_append_char(&sb, '\n');
            }
            *error_out = sb_finish(&sb);
        }
        for (size_t i = 0; i < err_count; i++) free(errs[i].text);
        free(errs);
        pika_memo_free(memo);
        pika_grammar_free(meta);
        return NULL;
    }
    for (size_t i = 0; i < err_count; i++) free(errs[i].text);
    free(errs);

    // get top-level match for GRAMMAR
    size_t match_count = 0;
    PikaMatch** matches = pika_memo_get_non_overlapping_matches_for_rule(memo, "GRAMMAR", &match_count);
    if (!matches || match_count == 0) {
        if (error_out) *error_out = pika_strdup("Top-level rule GRAMMAR did not match");
        free(matches);
        pika_memo_free(memo);
        pika_grammar_free(meta);
        return NULL;
    }
    if (match_count > 1) {
        if (error_out) *error_out = pika_strdup("Multiple top-level matches in grammar spec");
        free(matches);
        pika_memo_free(memo);
        pika_grammar_free(meta);
        return NULL;
    }
    PikaMatch* top = matches[0];
    free(matches);

    const PikaRule* top_rule = pika_grammar_get_rule(meta, "GRAMMAR");
    const char* root_label = top_rule && top_rule->labeled_clause.ast_label ? top_rule->labeled_clause.ast_label : "<root>";
    PikaAstNode* ast = pika_ast_from_match(root_label, top, grammar_spec);
    if (!ast) {
        if (error_out) *error_out = pika_strdup("Failed to build AST for grammar spec");
        pika_memo_free(memo);
        pika_grammar_free(meta);
        return NULL;
    }

    // build rules from AST
    PikaRule** rules = NULL; size_t rlen = 0, rcap = 0;
    for (size_t i = 0; i < ast->child_count; i++) {
        PikaAstNode* node = ast->children[i];
        if (strcmp(node->label, "RuleAST") != 0) continue;
        PikaRule* rule = parse_rule_node(node);
        if (!rule) continue;
        PikaRule** arr = (PikaRule**)pika_grow_array(rules, sizeof(PikaRule*), &rcap, rlen + 1);
        if (arr) { rules = arr; rules[rlen++] = rule; }
    }

    pika_ast_free(ast);
    pika_memo_free(memo);
    pika_grammar_free(meta);

    if (rlen == 0) {
        if (error_out) *error_out = pika_strdup("No rules parsed from grammar spec");
        free(rules);
        return NULL;
    }

    PikaGrammar* g = pika_grammar_new(rules, rlen);
    free(rules);
    return g;
}
