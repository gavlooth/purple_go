/*
 * OmniLisp AST Implementation
 */

#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ============== Singleton Values ============== */

static OmniValue _omni_nil = { .tag = OMNI_NIL };
static OmniValue _omni_nothing = { .tag = OMNI_NOTHING };

OmniValue* omni_nil = &_omni_nil;
OmniValue* omni_nothing = &_omni_nothing;

/* ============== Arena Allocator ============== */

struct OmniArena {
    char* data;
    size_t size;
    size_t used;
    struct OmniArena* next;  /* For chaining when full */
};

OmniArena* omni_arena_new(size_t initial_size) {
    OmniArena* arena = malloc(sizeof(OmniArena));
    if (!arena) return NULL;
    arena->data = malloc(initial_size);
    if (!arena->data) {
        free(arena);
        return NULL;
    }
    arena->size = initial_size;
    arena->used = 0;
    arena->next = NULL;
    return arena;
}

void omni_arena_free(OmniArena* arena) {
    while (arena) {
        OmniArena* next = arena->next;
        free(arena->data);
        free(arena);
        arena = next;
    }
}

void* omni_arena_alloc(OmniArena* arena, size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~7;

    /* Find arena with space, or allocate new one */
    OmniArena* current = arena;
    while (current->used + size > current->size) {
        if (current->next == NULL) {
            size_t new_size = current->size * 2;
            if (new_size < size) new_size = size * 2;
            current->next = omni_arena_new(new_size);
            if (!current->next) return NULL;
        }
        current = current->next;
    }

    void* ptr = current->data + current->used;
    current->used += size;
    return ptr;
}

char* omni_arena_strdup(OmniArena* arena, const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = omni_arena_alloc(arena, len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

/* Global AST arena */
static OmniArena* g_ast_arena = NULL;

void omni_ast_arena_init(void) {
    if (!g_ast_arena) {
        g_ast_arena = omni_arena_new(1024 * 1024);  /* 1MB initial */
    }
}

void omni_ast_arena_cleanup(void) {
    if (g_ast_arena) {
        omni_arena_free(g_ast_arena);
        g_ast_arena = NULL;
    }
}

OmniArena* omni_ast_arena_get(void) {
    if (!g_ast_arena) omni_ast_arena_init();
    return g_ast_arena;
}

/* ============== Internal Allocation ============== */

static OmniValue* omni_alloc_value(void) {
    OmniArena* arena = omni_ast_arena_get();
    OmniValue* v = omni_arena_alloc(arena, sizeof(OmniValue));
    if (v) memset(v, 0, sizeof(OmniValue));
    return v;
}

/* ============== Constructors ============== */

OmniValue* omni_new_int(int64_t i) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_INT;
    v->int_val = i;
    return v;
}

OmniValue* omni_new_float(double f) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_FLOAT;
    v->float_val = f;
    return v;
}

OmniValue* omni_new_sym(const char* s) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_SYM;
    v->str_val = omni_arena_strdup(omni_ast_arena_get(), s);
    return v;
}

OmniValue* omni_new_char(int32_t c) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_CHAR;
    v->int_val = c;
    return v;
}

OmniValue* omni_new_cell(OmniValue* car, OmniValue* cdr) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_CELL;
    v->cell.car = car;
    v->cell.cdr = cdr ? cdr : omni_nil;
    return v;
}

OmniValue* omni_new_prim(OmniPrimFn fn) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_PRIM;
    v->prim_fn = fn;
    return v;
}

OmniValue* omni_new_code(const char* s) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_CODE;
    v->str_val = omni_arena_strdup(omni_ast_arena_get(), s);
    return v;
}

OmniValue* omni_new_lambda(OmniValue* params, OmniValue* body, OmniValue* env) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_LAMBDA;
    v->lambda.params = params;
    v->lambda.body = body;
    v->lambda.env = env;
    v->lambda.self_name = NULL;
    return v;
}

OmniValue* omni_new_rec_lambda(OmniValue* self_name, OmniValue* params, OmniValue* body, OmniValue* env) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_REC_LAMBDA;
    v->lambda.params = params;
    v->lambda.body = body;
    v->lambda.env = env;
    v->lambda.self_name = self_name;
    return v;
}

OmniValue* omni_new_error(const char* msg) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_ERROR;
    v->str_val = omni_arena_strdup(omni_ast_arena_get(), msg);
    return v;
}

OmniValue* omni_new_box(OmniValue* initial) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_BOX;
    v->box_value = initial;
    return v;
}

OmniValue* omni_new_cont(OmniContFn fn, OmniValue* menv, int tag) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_CONT;
    v->cont.fn = fn;
    v->cont.menv = menv;
    v->cont.tag = tag;
    return v;
}

OmniValue* omni_new_chan(int capacity) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_CHAN;
    OmniChannel* ch = omni_arena_alloc(omni_ast_arena_get(), sizeof(OmniChannel));
    if (!ch) return NULL;
    memset(ch, 0, sizeof(OmniChannel));
    ch->capacity = capacity;
    if (capacity > 0) {
        ch->buffer = omni_arena_alloc(omni_ast_arena_get(), capacity * sizeof(OmniValue*));
    }
    v->chan = ch;
    return v;
}

OmniValue* omni_new_green_chan(int capacity) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_GREEN_CHAN;
    OmniGreenChannel* ch = omni_arena_alloc(omni_ast_arena_get(), sizeof(OmniGreenChannel));
    if (!ch) return NULL;
    memset(ch, 0, sizeof(OmniGreenChannel));
    ch->capacity = capacity;
    if (capacity > 0) {
        ch->buffer = omni_arena_alloc(omni_ast_arena_get(), capacity * sizeof(OmniValue*));
    }
    v->green_chan = ch;
    return v;
}

OmniValue* omni_new_atom(OmniValue* initial) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_ATOM;
    v->atom_value = initial;
    return v;
}

OmniValue* omni_new_thread(void) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_THREAD;
    v->thread = omni_arena_alloc(omni_ast_arena_get(), sizeof(OmniThreadHandle));
    if (!v->thread) return NULL;
    memset(v->thread, 0, sizeof(OmniThreadHandle));
    return v;
}

OmniValue* omni_new_process(OmniValue* thunk) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_PROCESS;
    v->proc.thunk = thunk;
    v->proc.state = OMNI_PROC_READY;
    return v;
}

OmniValue* omni_new_menv(OmniValue* env, OmniValue* parent, int level) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_MENV;
    v->menv.env = env;
    v->menv.parent = parent;
    v->menv.level = level;
    memset(v->menv.handlers, 0, sizeof(v->menv.handlers));
    return v;
}

OmniValue* omni_new_keyword(const char* name) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_KEYWORD;
    v->str_val = omni_arena_strdup(omni_ast_arena_get(), name);
    return v;
}

/* OmniLisp collection constructors */

OmniValue* omni_new_array(size_t initial_cap) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_ARRAY;
    v->array.len = 0;
    v->array.cap = initial_cap > 0 ? initial_cap : 8;
    v->array.data = omni_arena_alloc(omni_ast_arena_get(), v->array.cap * sizeof(OmniValue*));
    return v;
}

OmniValue* omni_new_array_from(OmniValue** elements, size_t len) {
    OmniValue* v = omni_new_array(len);
    if (!v) return NULL;
    for (size_t i = 0; i < len; i++) {
        v->array.data[i] = elements[i];
    }
    v->array.len = len;
    return v;
}

OmniValue* omni_new_dict(void) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_DICT;
    v->dict.len = 0;
    v->dict.cap = 8;
    v->dict.keys = omni_arena_alloc(omni_ast_arena_get(), 8 * sizeof(OmniValue*));
    v->dict.values = omni_arena_alloc(omni_ast_arena_get(), 8 * sizeof(OmniValue*));
    return v;
}

OmniValue* omni_new_tuple(OmniValue** elements, size_t len) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_TUPLE;
    v->tuple.len = len;
    v->tuple.data = omni_arena_alloc(omni_ast_arena_get(), len * sizeof(OmniValue*));
    if (v->tuple.data) {
        for (size_t i = 0; i < len; i++) {
            v->tuple.data[i] = elements[i];
        }
    }
    return v;
}

OmniValue* omni_new_type_lit(const char* name, OmniValue** params, size_t param_count) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_TYPE_LIT;
    v->type_lit.type_name = omni_arena_strdup(omni_ast_arena_get(), name);
    v->type_lit.param_count = param_count;
    if (param_count > 0) {
        v->type_lit.params = omni_arena_alloc(omni_ast_arena_get(), param_count * sizeof(OmniValue*));
        for (size_t i = 0; i < param_count; i++) {
            v->type_lit.params[i] = params[i];
        }
    }
    return v;
}

OmniValue* omni_new_user_type(const char* type_name, OmniField* fields, size_t field_count) {
    OmniValue* v = omni_alloc_value();
    if (!v) return NULL;
    v->tag = OMNI_USER_TYPE;
    v->user_type.type_name = omni_arena_strdup(omni_ast_arena_get(), type_name);
    v->user_type.field_count = field_count;
    v->user_type.fields = omni_arena_alloc(omni_ast_arena_get(), field_count * sizeof(OmniField));
    for (size_t i = 0; i < field_count; i++) {
        v->user_type.fields[i].name = omni_arena_strdup(omni_ast_arena_get(), fields[i].name);
        v->user_type.fields[i].value = fields[i].value;
    }
    return v;
}

/* ============== Symbol Comparison ============== */

bool omni_sym_eq(OmniValue* s1, OmniValue* s2) {
    if (!s1 || !s2) return false;
    if (s1->tag != OMNI_SYM || s2->tag != OMNI_SYM) return false;
    return strcmp(s1->str_val, s2->str_val) == 0;
}

bool omni_sym_eq_str(OmniValue* s, const char* str) {
    if (!s || !str) return false;
    if (s->tag != OMNI_SYM) return false;
    return strcmp(s->str_val, str) == 0;
}

/* ============== Value Equality ============== */

bool omni_values_equal(OmniValue* a, OmniValue* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->tag != b->tag) return false;

    switch (a->tag) {
    case OMNI_INT:
    case OMNI_CHAR:
        return a->int_val == b->int_val;
    case OMNI_FLOAT:
        return a->float_val == b->float_val;
    case OMNI_SYM:
    case OMNI_KEYWORD:
    case OMNI_CODE:
    case OMNI_ERROR:
        return strcmp(a->str_val, b->str_val) == 0;
    case OMNI_NIL:
    case OMNI_NOTHING:
        return true;
    default:
        return a == b;  /* Pointer equality for complex types */
    }
}

/* ============== List Helpers ============== */

OmniValue* omni_list1(OmniValue* a) {
    return omni_new_cell(a, omni_nil);
}

OmniValue* omni_list2(OmniValue* a, OmniValue* b) {
    return omni_new_cell(a, omni_new_cell(b, omni_nil));
}

OmniValue* omni_list3(OmniValue* a, OmniValue* b, OmniValue* c) {
    return omni_new_cell(a, omni_new_cell(b, omni_new_cell(c, omni_nil)));
}

size_t omni_list_len(OmniValue* v) {
    size_t n = 0;
    while (!omni_is_nil(v) && omni_is_cell(v)) {
        n++;
        v = v->cell.cdr;
    }
    return n;
}

OmniValue** omni_list_to_array(OmniValue* v, size_t* out_len) {
    size_t len = omni_list_len(v);
    if (out_len) *out_len = len;
    if (len == 0) return NULL;

    OmniValue** arr = omni_arena_alloc(omni_ast_arena_get(), len * sizeof(OmniValue*));
    size_t i = 0;
    while (!omni_is_nil(v) && omni_is_cell(v)) {
        arr[i++] = v->cell.car;
        v = v->cell.cdr;
    }
    return arr;
}

OmniValue* omni_array_to_list(OmniValue** items, size_t len) {
    OmniValue* result = omni_nil;
    for (size_t i = len; i > 0; i--) {
        result = omni_new_cell(items[i - 1], result);
    }
    return result;
}

/* ============== Box Operations ============== */

OmniValue* omni_box_get(OmniValue* box) {
    if (!box || box->tag != OMNI_BOX) return omni_nil;
    return box->box_value;
}

void omni_box_set(OmniValue* box, OmniValue* val) {
    if (box && box->tag == OMNI_BOX) {
        box->box_value = val;
    }
}

/* ============== Array Operations ============== */

size_t omni_array_len(OmniValue* arr) {
    if (!arr || arr->tag != OMNI_ARRAY) return 0;
    return arr->array.len;
}

OmniValue* omni_array_get(OmniValue* arr, size_t idx) {
    if (!arr || arr->tag != OMNI_ARRAY || idx >= arr->array.len) return omni_nil;
    return arr->array.data[idx];
}

void omni_array_set(OmniValue* arr, size_t idx, OmniValue* val) {
    if (arr && arr->tag == OMNI_ARRAY && idx < arr->array.len) {
        arr->array.data[idx] = val;
    }
}

void omni_array_push(OmniValue* arr, OmniValue* val) {
    if (!arr || arr->tag != OMNI_ARRAY) return;
    if (arr->array.len >= arr->array.cap) {
        size_t new_cap = arr->array.cap * 2;
        OmniValue** new_data = omni_arena_alloc(omni_ast_arena_get(), new_cap * sizeof(OmniValue*));
        memcpy(new_data, arr->array.data, arr->array.len * sizeof(OmniValue*));
        arr->array.data = new_data;
        arr->array.cap = new_cap;
    }
    arr->array.data[arr->array.len++] = val;
}

OmniValue* omni_array_pop(OmniValue* arr) {
    if (!arr || arr->tag != OMNI_ARRAY || arr->array.len == 0) return omni_nil;
    return arr->array.data[--arr->array.len];
}

/* ============== Dict Operations ============== */

size_t omni_dict_len(OmniValue* dict) {
    if (!dict || dict->tag != OMNI_DICT) return 0;
    return dict->dict.len;
}

OmniValue* omni_dict_get(OmniValue* dict, OmniValue* key) {
    if (!dict || dict->tag != OMNI_DICT) return omni_nil;
    for (size_t i = 0; i < dict->dict.len; i++) {
        if (omni_values_equal(dict->dict.keys[i], key)) {
            return dict->dict.values[i];
        }
    }
    return omni_nil;
}

void omni_dict_set(OmniValue* dict, OmniValue* key, OmniValue* val) {
    if (!dict || dict->tag != OMNI_DICT) return;

    /* Check if key exists */
    for (size_t i = 0; i < dict->dict.len; i++) {
        if (omni_values_equal(dict->dict.keys[i], key)) {
            dict->dict.values[i] = val;
            return;
        }
    }

    /* Grow if needed */
    if (dict->dict.len >= dict->dict.cap) {
        size_t new_cap = dict->dict.cap * 2;
        OmniValue** new_keys = omni_arena_alloc(omni_ast_arena_get(), new_cap * sizeof(OmniValue*));
        OmniValue** new_vals = omni_arena_alloc(omni_ast_arena_get(), new_cap * sizeof(OmniValue*));
        memcpy(new_keys, dict->dict.keys, dict->dict.len * sizeof(OmniValue*));
        memcpy(new_vals, dict->dict.values, dict->dict.len * sizeof(OmniValue*));
        dict->dict.keys = new_keys;
        dict->dict.values = new_vals;
        dict->dict.cap = new_cap;
    }

    dict->dict.keys[dict->dict.len] = key;
    dict->dict.values[dict->dict.len] = val;
    dict->dict.len++;
}

bool omni_dict_has(OmniValue* dict, OmniValue* key) {
    if (!dict || dict->tag != OMNI_DICT) return false;
    for (size_t i = 0; i < dict->dict.len; i++) {
        if (omni_values_equal(dict->dict.keys[i], key)) {
            return true;
        }
    }
    return false;
}

/* ============== Tuple Operations ============== */

size_t omni_tuple_len(OmniValue* tuple) {
    if (!tuple || tuple->tag != OMNI_TUPLE) return 0;
    return tuple->tuple.len;
}

OmniValue* omni_tuple_get(OmniValue* tuple, size_t idx) {
    if (!tuple || tuple->tag != OMNI_TUPLE || idx >= tuple->tuple.len) return omni_nil;
    return tuple->tuple.data[idx];
}

/* ============== User Type Operations ============== */

OmniValue* omni_user_type_get_field(OmniValue* v, const char* field_name) {
    if (!v || v->tag != OMNI_USER_TYPE || !field_name) return omni_nil;
    for (size_t i = 0; i < v->user_type.field_count; i++) {
        if (strcmp(v->user_type.fields[i].name, field_name) == 0) {
            return v->user_type.fields[i].value;
        }
    }
    return omni_nil;
}

void omni_user_type_set_field(OmniValue* v, const char* field_name, OmniValue* val) {
    if (!v || v->tag != OMNI_USER_TYPE || !field_name) return;
    for (size_t i = 0; i < v->user_type.field_count; i++) {
        if (strcmp(v->user_type.fields[i].name, field_name) == 0) {
            v->user_type.fields[i].value = val;
            return;
        }
    }
}

bool omni_user_type_is(OmniValue* v, const char* type_name) {
    if (!v || v->tag != OMNI_USER_TYPE || !type_name) return false;
    return strcmp(v->user_type.type_name, type_name) == 0;
}

/* ============== String Representation ============== */

static void string_builder_init(char** buf, size_t* cap, size_t* len) {
    *cap = 256;
    *buf = malloc(*cap);
    *len = 0;
    (*buf)[0] = '\0';
}

static void string_builder_append(char** buf, size_t* cap, size_t* len, const char* s) {
    size_t slen = strlen(s);
    while (*len + slen + 1 > *cap) {
        *cap *= 2;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, slen + 1);
    *len += slen;
}

static void string_builder_append_char(char** buf, size_t* cap, size_t* len, char c) {
    char tmp[2] = {c, '\0'};
    string_builder_append(buf, cap, len, tmp);
}

static char* list_to_string_impl(OmniValue* v);

static char* value_to_string_impl(OmniValue* v) {
    if (!v) return strdup("nil");

    char tmp[64];
    char *buf;
    size_t cap, len;

    switch (v->tag) {
    case OMNI_INT:
        snprintf(tmp, sizeof(tmp), "%ld", (long)v->int_val);
        return strdup(tmp);

    case OMNI_FLOAT:
        snprintf(tmp, sizeof(tmp), "%g", v->float_val);
        return strdup(tmp);

    case OMNI_SYM:
        return strdup(v->str_val);

    case OMNI_CHAR:
        if (v->int_val == '\n') return strdup("#\\newline");
        if (v->int_val == '\t') return strdup("#\\tab");
        if (v->int_val == '\r') return strdup("#\\return");
        if (v->int_val == ' ') return strdup("#\\space");
        snprintf(tmp, sizeof(tmp), "#\\%c", (char)v->int_val);
        return strdup(tmp);

    case OMNI_CODE:
        return strdup(v->str_val);

    case OMNI_CELL:
        return list_to_string_impl(v);

    case OMNI_NIL:
        return strdup("()");

    case OMNI_NOTHING:
        return strdup("nothing");

    case OMNI_PRIM:
        return strdup("#<prim>");

    case OMNI_LAMBDA:
        return strdup("#<lambda>");

    case OMNI_REC_LAMBDA:
        return strdup("#<rec-lambda>");

    case OMNI_ERROR:
        string_builder_init(&buf, &cap, &len);
        string_builder_append(&buf, &cap, &len, "#<error: ");
        string_builder_append(&buf, &cap, &len, v->str_val);
        string_builder_append_char(&buf, &cap, &len, '>');
        return buf;

    case OMNI_MENV:
        return strdup("#<menv>");

    case OMNI_BOX:
        string_builder_init(&buf, &cap, &len);
        string_builder_append(&buf, &cap, &len, "#<box ");
        {
            char* inner = value_to_string_impl(v->box_value);
            string_builder_append(&buf, &cap, &len, inner);
            free(inner);
        }
        string_builder_append_char(&buf, &cap, &len, '>');
        return buf;

    case OMNI_CONT:
        return strdup("#<continuation>");

    case OMNI_CHAN:
        snprintf(tmp, sizeof(tmp), "#<channel cap=%d>", v->chan ? v->chan->capacity : 0);
        return strdup(tmp);

    case OMNI_GREEN_CHAN:
        return strdup("#<green-channel>");

    case OMNI_ATOM:
        string_builder_init(&buf, &cap, &len);
        string_builder_append(&buf, &cap, &len, "#<atom ");
        {
            char* inner = value_to_string_impl(v->atom_value);
            string_builder_append(&buf, &cap, &len, inner);
            free(inner);
        }
        string_builder_append_char(&buf, &cap, &len, '>');
        return buf;

    case OMNI_THREAD:
        return strdup("#<thread>");

    case OMNI_PROCESS:
        {
            const char* states[] = {"ready", "running", "parked", "done"};
            const char* state = (v->proc.state >= 0 && v->proc.state < 4) ? states[v->proc.state] : "unknown";
            snprintf(tmp, sizeof(tmp), "#<process %s>", state);
            return strdup(tmp);
        }

    case OMNI_USER_TYPE:
        string_builder_init(&buf, &cap, &len);
        string_builder_append(&buf, &cap, &len, "#<");
        string_builder_append(&buf, &cap, &len, v->user_type.type_name);
        for (size_t i = 0; i < v->user_type.field_count; i++) {
            string_builder_append_char(&buf, &cap, &len, ' ');
            string_builder_append(&buf, &cap, &len, v->user_type.fields[i].name);
            string_builder_append_char(&buf, &cap, &len, '=');
            char* fval = value_to_string_impl(v->user_type.fields[i].value);
            string_builder_append(&buf, &cap, &len, fval);
            free(fval);
        }
        string_builder_append_char(&buf, &cap, &len, '>');
        return buf;

    case OMNI_ARRAY:
        string_builder_init(&buf, &cap, &len);
        string_builder_append_char(&buf, &cap, &len, '[');
        for (size_t i = 0; i < v->array.len; i++) {
            if (i > 0) string_builder_append_char(&buf, &cap, &len, ' ');
            char* elem = value_to_string_impl(v->array.data[i]);
            string_builder_append(&buf, &cap, &len, elem);
            free(elem);
        }
        string_builder_append_char(&buf, &cap, &len, ']');
        return buf;

    case OMNI_DICT:
        string_builder_init(&buf, &cap, &len);
        string_builder_append(&buf, &cap, &len, "#{");
        for (size_t i = 0; i < v->dict.len; i++) {
            if (i > 0) string_builder_append_char(&buf, &cap, &len, ' ');
            char* key = value_to_string_impl(v->dict.keys[i]);
            string_builder_append(&buf, &cap, &len, key);
            free(key);
            string_builder_append_char(&buf, &cap, &len, ' ');
            char* val = value_to_string_impl(v->dict.values[i]);
            string_builder_append(&buf, &cap, &len, val);
            free(val);
        }
        string_builder_append_char(&buf, &cap, &len, '}');
        return buf;

    case OMNI_TUPLE:
        string_builder_init(&buf, &cap, &len);
        string_builder_append(&buf, &cap, &len, "(tuple");
        for (size_t i = 0; i < v->tuple.len; i++) {
            string_builder_append_char(&buf, &cap, &len, ' ');
            char* elem = value_to_string_impl(v->tuple.data[i]);
            string_builder_append(&buf, &cap, &len, elem);
            free(elem);
        }
        string_builder_append_char(&buf, &cap, &len, ')');
        return buf;

    case OMNI_TYPE_LIT:
        string_builder_init(&buf, &cap, &len);
        string_builder_append_char(&buf, &cap, &len, '{');
        string_builder_append(&buf, &cap, &len, v->type_lit.type_name);
        for (size_t i = 0; i < v->type_lit.param_count; i++) {
            string_builder_append_char(&buf, &cap, &len, ' ');
            char* param = value_to_string_impl(v->type_lit.params[i]);
            string_builder_append(&buf, &cap, &len, param);
            free(param);
        }
        string_builder_append_char(&buf, &cap, &len, '}');
        return buf;

    case OMNI_KEYWORD:
        string_builder_init(&buf, &cap, &len);
        string_builder_append_char(&buf, &cap, &len, ':');
        string_builder_append(&buf, &cap, &len, v->str_val);
        return buf;

    default:
        return strdup("?");
    }
}

static char* list_to_string_impl(OmniValue* v) {
    char *buf;
    size_t cap, len;
    string_builder_init(&buf, &cap, &len);

    string_builder_append_char(&buf, &cap, &len, '(');
    bool first = true;
    while (!omni_is_nil(v) && omni_is_cell(v)) {
        if (!first) string_builder_append_char(&buf, &cap, &len, ' ');
        first = false;
        char* elem = value_to_string_impl(v->cell.car);
        string_builder_append(&buf, &cap, &len, elem);
        free(elem);
        v = v->cell.cdr;
    }
    if (!omni_is_nil(v)) {
        string_builder_append(&buf, &cap, &len, " . ");
        char* rest = value_to_string_impl(v);
        string_builder_append(&buf, &cap, &len, rest);
        free(rest);
    }
    string_builder_append_char(&buf, &cap, &len, ')');
    return buf;
}

char* omni_value_to_string(OmniValue* v) {
    return value_to_string_impl(v);
}

const char* omni_tag_name(OmniTag tag) {
    switch (tag) {
    case OMNI_INT: return "INT";
    case OMNI_SYM: return "SYM";
    case OMNI_CELL: return "CELL";
    case OMNI_NIL: return "NIL";
    case OMNI_PRIM: return "PRIM";
    case OMNI_MENV: return "MENV";
    case OMNI_CODE: return "CODE";
    case OMNI_LAMBDA: return "LAMBDA";
    case OMNI_REC_LAMBDA: return "REC_LAMBDA";
    case OMNI_ERROR: return "ERROR";
    case OMNI_CHAR: return "CHAR";
    case OMNI_FLOAT: return "FLOAT";
    case OMNI_BOX: return "BOX";
    case OMNI_CONT: return "CONT";
    case OMNI_CHAN: return "CHAN";
    case OMNI_GREEN_CHAN: return "GREEN_CHAN";
    case OMNI_ATOM: return "ATOM";
    case OMNI_THREAD: return "THREAD";
    case OMNI_PROCESS: return "PROCESS";
    case OMNI_USER_TYPE: return "USER_TYPE";
    case OMNI_ARRAY: return "ARRAY";
    case OMNI_DICT: return "DICT";
    case OMNI_TUPLE: return "TUPLE";
    case OMNI_NOTHING: return "NOTHING";
    case OMNI_TYPE_LIT: return "TYPE_LIT";
    case OMNI_KEYWORD: return "KEYWORD";
    default: return "UNKNOWN";
    }
}
