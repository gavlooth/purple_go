/*
 * OmniLisp AST - Abstract Syntax Tree
 *
 * Core tagged union type for all values in the language.
 * Based on pkg/ast/value.go semantics, implemented in C99.
 */

#ifndef OMNILISP_AST_H
#define OMNILISP_AST_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct OmniValue OmniValue;
typedef struct OmniArena OmniArena;

/* Value tags - mirrors Go Tag enum */
typedef enum {
    OMNI_INT = 0,      /* Integer */
    OMNI_SYM,          /* Symbol */
    OMNI_CELL,         /* Cons cell */
    OMNI_NIL,          /* Nil/empty list */
    OMNI_PRIM,         /* Primitive function */
    OMNI_MENV,         /* Meta-environment */
    OMNI_CODE,         /* Generated C code */
    OMNI_LAMBDA,       /* Lambda/closure */
    OMNI_REC_LAMBDA,   /* Recursive lambda with self-reference */
    OMNI_ERROR,        /* Error value */
    OMNI_CHAR,         /* Character */
    OMNI_FLOAT,        /* Float64 */
    OMNI_BOX,          /* Mutable reference cell */
    OMNI_CONT,         /* First-class continuation */
    OMNI_CHAN,         /* CSP channel (pthread-based) */
    OMNI_GREEN_CHAN,   /* Green channel (continuation-based) */
    OMNI_ATOM,         /* Atomic reference */
    OMNI_THREAD,       /* OS thread handle */
    OMNI_PROCESS,      /* Green thread/process */
    OMNI_USER_TYPE,    /* User-defined type instance */
    /* OmniLisp extensions */
    OMNI_ARRAY,        /* Mutable array [1 2 3] */
    OMNI_DICT,         /* Dictionary #{:a 1 :b 2} */
    OMNI_TUPLE,        /* Immutable tuple */
    OMNI_NOTHING,      /* Unit value */
    OMNI_TYPE_LIT,     /* Type literal {Int} */
    OMNI_KEYWORD,      /* Keyword :symbol */
} OmniTag;

/* Primitive function signature */
typedef OmniValue* (*OmniPrimFn)(OmniValue* args, OmniValue* menv);

/* Handler function signature */
typedef OmniValue* (*OmniHandlerFn)(OmniValue* exp, OmniValue* menv);

/* Continuation function signature */
typedef OmniValue* (*OmniContFn)(OmniValue* val);

/* Handler indices for meta-environment */
#define OMNI_HIDX_LIT  0
#define OMNI_HIDX_VAR  1
#define OMNI_HIDX_LAM  2
#define OMNI_HIDX_APP  3
#define OMNI_HIDX_IF   4
#define OMNI_HIDX_LFT  5
#define OMNI_HIDX_RUN  6
#define OMNI_HIDX_EM   7
#define OMNI_HIDX_CLAM 8

/* Process states */
#define OMNI_PROC_READY   0
#define OMNI_PROC_RUNNING 1
#define OMNI_PROC_PARKED  2
#define OMNI_PROC_DONE    3

/* Handler wrapper - native or closure */
typedef struct OmniHandlerWrapper {
    OmniHandlerFn native;   /* For built-in handlers */
    OmniValue* closure;     /* For user-defined handlers */
} OmniHandlerWrapper;

/* Channel structure (for CSP) */
typedef struct OmniChannel {
    OmniValue** buffer;     /* Circular buffer */
    int capacity;           /* 0 = unbuffered */
    int head;
    int tail;
    int count;
    int closed;
    OmniValue* send_waiters;
    OmniValue* recv_waiters;
} OmniChannel;

/* Green channel (continuation-based) */
typedef struct OmniGreenChannel {
    OmniValue** buffer;
    int capacity;
    int head;
    int tail;
    int count;
    int closed;
    OmniValue* send_waiters;
    OmniValue* recv_waiters;
} OmniGreenChannel;

/* Thread handle */
typedef struct OmniThreadHandle {
    void* thread;           /* pthread_t */
    OmniValue* result;
    int done;
} OmniThreadHandle;

/* User-defined type field */
typedef struct OmniField {
    char* name;
    OmniValue* value;
} OmniField;

/*
 * Core Value structure - tagged union for all values
 */
struct OmniValue {
    OmniTag tag;

    union {
        /* OMNI_INT, OMNI_CHAR */
        int64_t int_val;

        /* OMNI_FLOAT */
        double float_val;

        /* OMNI_SYM, OMNI_CODE, OMNI_ERROR, OMNI_KEYWORD */
        char* str_val;

        /* OMNI_CELL */
        struct {
            OmniValue* car;
            OmniValue* cdr;
        } cell;

        /* OMNI_PRIM */
        OmniPrimFn prim_fn;

        /* OMNI_MENV */
        struct {
            OmniValue* env;
            OmniValue* parent;
            int level;
            OmniHandlerWrapper* handlers[9];
        } menv;

        /* OMNI_LAMBDA, OMNI_REC_LAMBDA */
        struct {
            OmniValue* params;
            OmniValue* body;
            OmniValue* env;
            OmniValue* self_name;  /* Only for REC_LAMBDA */
        } lambda;

        /* OMNI_BOX */
        OmniValue* box_value;

        /* OMNI_CONT */
        struct {
            OmniContFn fn;
            OmniValue* menv;
            int tag;
        } cont;

        /* OMNI_CHAN */
        OmniChannel* chan;

        /* OMNI_GREEN_CHAN */
        OmniGreenChannel* green_chan;

        /* OMNI_ATOM */
        OmniValue* atom_value;

        /* OMNI_THREAD */
        OmniThreadHandle* thread;

        /* OMNI_PROCESS */
        struct {
            OmniValue* thunk;
            OmniValue* cont;
            OmniValue* menv;
            OmniValue* result;
            int state;
        } proc;

        /* OMNI_USER_TYPE */
        struct {
            char* type_name;
            OmniField* fields;
            size_t field_count;
        } user_type;

        /* OMNI_ARRAY */
        struct {
            OmniValue** data;
            size_t len;
            size_t cap;
        } array;

        /* OMNI_DICT */
        struct {
            OmniValue** keys;
            OmniValue** values;
            size_t len;
            size_t cap;
        } dict;

        /* OMNI_TUPLE */
        struct {
            OmniValue** data;
            size_t len;
        } tuple;

        /* OMNI_TYPE_LIT */
        struct {
            char* type_name;
            OmniValue** params;
            size_t param_count;
        } type_lit;
    };
};

/* ============== Singleton Values ============== */

extern OmniValue* omni_nil;
extern OmniValue* omni_nothing;

/* ============== Arena Allocator ============== */

OmniArena* omni_arena_new(size_t initial_size);
void omni_arena_free(OmniArena* arena);
void* omni_arena_alloc(OmniArena* arena, size_t size);
char* omni_arena_strdup(OmniArena* arena, const char* s);

/* Global arena for AST allocations */
void omni_ast_arena_init(void);
void omni_ast_arena_cleanup(void);
OmniArena* omni_ast_arena_get(void);

/* ============== Constructors ============== */

OmniValue* omni_new_int(int64_t i);
OmniValue* omni_new_float(double f);
OmniValue* omni_new_sym(const char* s);
OmniValue* omni_new_char(int32_t c);
OmniValue* omni_new_cell(OmniValue* car, OmniValue* cdr);
OmniValue* omni_new_prim(OmniPrimFn fn);
OmniValue* omni_new_code(const char* s);
OmniValue* omni_new_lambda(OmniValue* params, OmniValue* body, OmniValue* env);
OmniValue* omni_new_rec_lambda(OmniValue* self_name, OmniValue* params, OmniValue* body, OmniValue* env);
OmniValue* omni_new_error(const char* msg);
OmniValue* omni_new_box(OmniValue* initial);
OmniValue* omni_new_cont(OmniContFn fn, OmniValue* menv, int tag);
OmniValue* omni_new_chan(int capacity);
OmniValue* omni_new_green_chan(int capacity);
OmniValue* omni_new_atom(OmniValue* initial);
OmniValue* omni_new_thread(void);
OmniValue* omni_new_process(OmniValue* thunk);
OmniValue* omni_new_menv(OmniValue* env, OmniValue* parent, int level);
OmniValue* omni_new_keyword(const char* name);

/* OmniLisp collection constructors */
OmniValue* omni_new_array(size_t initial_cap);
OmniValue* omni_new_array_from(OmniValue** elements, size_t len);
OmniValue* omni_new_dict(void);
OmniValue* omni_new_tuple(OmniValue** elements, size_t len);
OmniValue* omni_new_type_lit(const char* name, OmniValue** params, size_t param_count);
OmniValue* omni_new_user_type(const char* type_name, OmniField* fields, size_t field_count);

/* ============== Type Predicates ============== */

static inline bool omni_is_nil(OmniValue* v) { return v == NULL || v == omni_nil || v->tag == OMNI_NIL; }
static inline bool omni_is_int(OmniValue* v) { return v != NULL && v->tag == OMNI_INT; }
static inline bool omni_is_float(OmniValue* v) { return v != NULL && v->tag == OMNI_FLOAT; }
static inline bool omni_is_sym(OmniValue* v) { return v != NULL && v->tag == OMNI_SYM; }
static inline bool omni_is_char(OmniValue* v) { return v != NULL && v->tag == OMNI_CHAR; }
static inline bool omni_is_cell(OmniValue* v) { return v != NULL && v->tag == OMNI_CELL; }
static inline bool omni_is_prim(OmniValue* v) { return v != NULL && v->tag == OMNI_PRIM; }
static inline bool omni_is_code(OmniValue* v) { return v != NULL && v->tag == OMNI_CODE; }
static inline bool omni_is_lambda(OmniValue* v) { return v != NULL && v->tag == OMNI_LAMBDA; }
static inline bool omni_is_rec_lambda(OmniValue* v) { return v != NULL && v->tag == OMNI_REC_LAMBDA; }
static inline bool omni_is_error(OmniValue* v) { return v != NULL && v->tag == OMNI_ERROR; }
static inline bool omni_is_box(OmniValue* v) { return v != NULL && v->tag == OMNI_BOX; }
static inline bool omni_is_cont(OmniValue* v) { return v != NULL && v->tag == OMNI_CONT; }
static inline bool omni_is_chan(OmniValue* v) { return v != NULL && v->tag == OMNI_CHAN; }
static inline bool omni_is_green_chan(OmniValue* v) { return v != NULL && v->tag == OMNI_GREEN_CHAN; }
static inline bool omni_is_atom(OmniValue* v) { return v != NULL && v->tag == OMNI_ATOM; }
static inline bool omni_is_thread(OmniValue* v) { return v != NULL && v->tag == OMNI_THREAD; }
static inline bool omni_is_process(OmniValue* v) { return v != NULL && v->tag == OMNI_PROCESS; }
static inline bool omni_is_menv(OmniValue* v) { return v != NULL && v->tag == OMNI_MENV; }
static inline bool omni_is_array(OmniValue* v) { return v != NULL && v->tag == OMNI_ARRAY; }
static inline bool omni_is_dict(OmniValue* v) { return v != NULL && v->tag == OMNI_DICT; }
static inline bool omni_is_tuple(OmniValue* v) { return v != NULL && v->tag == OMNI_TUPLE; }
static inline bool omni_is_nothing(OmniValue* v) { return v == omni_nothing || (v != NULL && v->tag == OMNI_NOTHING); }
static inline bool omni_is_type_lit(OmniValue* v) { return v != NULL && v->tag == OMNI_TYPE_LIT; }
static inline bool omni_is_keyword(OmniValue* v) { return v != NULL && v->tag == OMNI_KEYWORD; }
static inline bool omni_is_user_type(OmniValue* v) { return v != NULL && v->tag == OMNI_USER_TYPE; }

/* ============== Accessors ============== */

static inline OmniValue* omni_car(OmniValue* v) {
    return (v != NULL && v->tag == OMNI_CELL) ? v->cell.car : omni_nil;
}

static inline OmniValue* omni_cdr(OmniValue* v) {
    return (v != NULL && v->tag == OMNI_CELL) ? v->cell.cdr : omni_nil;
}

/* Symbol comparison */
bool omni_sym_eq(OmniValue* s1, OmniValue* s2);
bool omni_sym_eq_str(OmniValue* s, const char* str);

/* Value equality */
bool omni_values_equal(OmniValue* a, OmniValue* b);

/* ============== List Helpers ============== */

OmniValue* omni_list1(OmniValue* a);
OmniValue* omni_list2(OmniValue* a, OmniValue* b);
OmniValue* omni_list3(OmniValue* a, OmniValue* b, OmniValue* c);
size_t omni_list_len(OmniValue* v);
OmniValue** omni_list_to_array(OmniValue* v, size_t* out_len);
OmniValue* omni_array_to_list(OmniValue** items, size_t len);

/* ============== Box Operations ============== */

OmniValue* omni_box_get(OmniValue* box);
void omni_box_set(OmniValue* box, OmniValue* val);

/* ============== Array Operations ============== */

size_t omni_array_len(OmniValue* arr);
OmniValue* omni_array_get(OmniValue* arr, size_t idx);
void omni_array_set(OmniValue* arr, size_t idx, OmniValue* val);
void omni_array_push(OmniValue* arr, OmniValue* val);
OmniValue* omni_array_pop(OmniValue* arr);

/* ============== Dict Operations ============== */

size_t omni_dict_len(OmniValue* dict);
OmniValue* omni_dict_get(OmniValue* dict, OmniValue* key);
void omni_dict_set(OmniValue* dict, OmniValue* key, OmniValue* val);
bool omni_dict_has(OmniValue* dict, OmniValue* key);

/* ============== Tuple Operations ============== */

size_t omni_tuple_len(OmniValue* tuple);
OmniValue* omni_tuple_get(OmniValue* tuple, size_t idx);

/* ============== User Type Operations ============== */

OmniValue* omni_user_type_get_field(OmniValue* v, const char* field_name);
void omni_user_type_set_field(OmniValue* v, const char* field_name, OmniValue* val);
bool omni_user_type_is(OmniValue* v, const char* type_name);

/* ============== String Representation ============== */

char* omni_value_to_string(OmniValue* v);
const char* omni_tag_name(OmniTag tag);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_AST_H */
