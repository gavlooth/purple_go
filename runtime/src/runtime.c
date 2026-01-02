/* Purple + ASAP C Compiler Output */
/* Primary Strategy: ASAP + ISMM 2024 (Deeply Immutable Cycles) */
/* Generated ANSI C99 + POSIX Code */

/* Enable POSIX.1-2001 for pthread_rwlock_t and related functions */
#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

/* ========== Tagged Pointers (Multi-Type Immediates) ========== */
/*
 * 3-bit tag scheme for immediate values (no heap allocation):
 *
 * Low 3 bits | Type        | Payload
 * -----------|-------------|------------------
 *    000     | Heap ptr    | 64-bit pointer (aligned)
 *    001     | Integer     | 61-bit signed int
 *    010     | Character   | 21-bit Unicode codepoint
 *    011     | Boolean     | 1-bit (0=false, 1=true)
 */

/* 3-bit tag constants */
#define IMM_TAG_MASK     0x7ULL
#define IMM_TAG_PTR      0x0ULL
#define IMM_TAG_INT      0x1ULL
#define IMM_TAG_CHAR     0x2ULL
#define IMM_TAG_BOOL     0x3ULL

/* Extract tag from value */
#define GET_IMM_TAG(p)   (((uintptr_t)(p)) & IMM_TAG_MASK)

/* Check immediate types */
#define IS_IMMEDIATE(p)      (GET_IMM_TAG(p) != IMM_TAG_PTR)
#define IS_IMMEDIATE_INT(p)  (GET_IMM_TAG(p) == IMM_TAG_INT)
#define IS_IMMEDIATE_CHAR(p) (GET_IMM_TAG(p) == IMM_TAG_CHAR)
#define IS_IMMEDIATE_BOOL(p) (GET_IMM_TAG(p) == IMM_TAG_BOOL)
#define IS_BOXED(p)          (GET_IMM_TAG(p) == IMM_TAG_PTR && (p) != NULL)

/* Immediate Integers */
#define MAKE_INT_IMM(n)      ((Obj*)(((uintptr_t)(n) << 3) | IMM_TAG_INT))
#define INT_IMM_VALUE(p)     ((long)((intptr_t)(p) >> 3))

/* Backward compatibility */
#define MAKE_IMMEDIATE(n)    MAKE_INT_IMM(n)
#define IMMEDIATE_VALUE(p)   INT_IMM_VALUE(p)

/* Immediate Booleans */
#define PURPLE_FALSE         ((Obj*)(((uintptr_t)0 << 3) | IMM_TAG_BOOL))
#define PURPLE_TRUE          ((Obj*)(((uintptr_t)1 << 3) | IMM_TAG_BOOL))

/* Immediate Characters */
#define MAKE_CHAR_IMM(c)     ((Obj*)(((uintptr_t)(c) << 3) | IMM_TAG_CHAR))
#define CHAR_IMM_VALUE(p)    ((long)(((uintptr_t)(p)) >> 3))

/* ========== IPGE: In-Place Generational Evolution ========== */
#define IPGE_MULTIPLIER  0x5851f42D4C957F2DULL
#define IPGE_INCREMENT   0x1442695040888963ULL

static inline uint64_t ipge_evolve(uint64_t gen) {
    return (gen * IPGE_MULTIPLIER) + IPGE_INCREMENT;
}

/* Forward declarations */
typedef struct Obj Obj;
typedef struct GenObj GenObj;
typedef struct BorrowRef BorrowRef;
typedef struct Closure Closure;
void invalidate_weak_refs_for(void* target);
BorrowRef* borrow_create(Obj* obj, const char* source_desc);
void borrow_invalidate_obj(Obj* obj);
Obj* call_closure(Obj* clos, Obj** args, int arg_count);
void closure_release(Closure* c);
void release_user_obj(Obj* x);
void free_channel_obj(Obj* ch_obj);
void scan_user_obj(Obj* obj);
void clear_marks_user_obj(Obj* obj);

/* Reference counting forward declarations */
void inc_ref(Obj* x);
void dec_ref(Obj* x);
void free_obj(Obj* x);

/* Primitive operations forward declarations */
Obj* prim_add(Obj* a, Obj* b);
Obj* prim_sub(Obj* a, Obj* b);
Obj* prim_mul(Obj* a, Obj* b);
Obj* prim_div(Obj* a, Obj* b);
Obj* prim_mod(Obj* a, Obj* b);
Obj* prim_lt(Obj* a, Obj* b);
Obj* prim_gt(Obj* a, Obj* b);
Obj* prim_le(Obj* a, Obj* b);
Obj* prim_ge(Obj* a, Obj* b);
Obj* prim_eq(Obj* a, Obj* b);
Obj* prim_not(Obj* a);
Obj* prim_abs(Obj* a);
Obj* prim_null(Obj* x);
Obj* prim_pair(Obj* x);
Obj* prim_int(Obj* x);
Obj* prim_float(Obj* x);
Obj* prim_char(Obj* x);
Obj* prim_sym(Obj* x);
Obj* obj_car(Obj* p);
Obj* obj_cdr(Obj* p);

/* I/O primitive forward declarations */
Obj* prim_display(Obj* x);
Obj* prim_print(Obj* x);
Obj* prim_newline(void);

/* List operation forward declarations */
Obj* list_append(Obj* a, Obj* b);
Obj* list_filter(Obj* fn, Obj* xs);
Obj* list_reverse(Obj* xs);

/* Type introspection forward declarations */
Obj* ctr_tag(Obj* x);
Obj* ctr_arg(Obj* x, Obj* idx);

/* Character primitive forward declarations */
Obj* char_to_int(Obj* c);
Obj* int_to_char(Obj* n);

/* Float primitive forward declarations */
Obj* int_to_float(Obj* n);
Obj* float_to_int(Obj* f);
Obj* prim_floor(Obj* f);
Obj* prim_ceil(Obj* f);

/* Higher-order function forward declarations */
Obj* prim_apply(Obj* fn, Obj* args);
Obj* prim_compose(Obj* f, Obj* g);

/* Core tags for runtime values */
typedef enum {
    TAG_INT = 1,
    TAG_FLOAT,
    TAG_CHAR,
    TAG_PAIR,
    TAG_SYM,
    TAG_BOX,
    TAG_CLOSURE,
    TAG_CHANNEL,
    TAG_ERROR,
    TAG_ATOM,
    TAG_THREAD
} ObjTag;

#define TAG_USER_BASE 1000

/* Core object type */
typedef struct Obj {
    uint64_t generation;    /* IPGE generation ID for memory safety */
    int mark;               /* Reference count or mark bit */
    int tag;                /* ObjTag */
    int is_pair;            /* 1 if pair, 0 if not */
    int scc_id;             /* SCC identifier for cycle detection (-1 = none) */
    unsigned int scan_tag;  /* Scanner mark (separate from RC) */
    union {
        long i;
        double f;
        struct { struct Obj *a, *b; };
        void* ptr;
    };
} Obj;
/* Size: 40 bytes (down from 48 with gen_obj pointer) */

/* ========== Tagged Pointer Helper Functions ========== */

/* Unboxed integer constructor - returns immediate, no heap! */
static inline Obj* mk_int_unboxed(long i) {
    return MAKE_INT_IMM(i);
}

/* Unboxed boolean constructor */
static inline Obj* mk_bool(int b) {
    return b ? PURPLE_TRUE : PURPLE_FALSE;
}

/* Unboxed character constructor */
static inline Obj* mk_char_unboxed(long c) {
    return MAKE_CHAR_IMM(c);
}

/* Safe integer extraction - works for both boxed and immediate */
static inline long obj_to_int(Obj* p) {
    if (IS_IMMEDIATE_INT(p)) return INT_IMM_VALUE(p);
    if (IS_IMMEDIATE_BOOL(p)) return p == PURPLE_TRUE ? 1 : 0;
    if (IS_IMMEDIATE_CHAR(p)) return CHAR_IMM_VALUE(p);
    return p ? p->i : 0;
}

/* Safe boolean extraction */
static inline int obj_to_bool(Obj* p) {
    if (IS_IMMEDIATE_BOOL(p)) return p == PURPLE_TRUE;
    if (IS_IMMEDIATE_INT(p)) return INT_IMM_VALUE(p) != 0;
    if (p == NULL) return 0;
    return 1;  /* Non-null is truthy */
}

/* Safe character extraction */
static inline long obj_to_char_val(Obj* p) {
    if (IS_IMMEDIATE_CHAR(p)) return CHAR_IMM_VALUE(p);
    if (p && p->tag == TAG_CHAR) return p->i;
    return 0;
}

/* Safe tag extraction - works for all immediate types */
static inline int obj_tag(Obj* p) {
    if (p == NULL) return 0;
    if (IS_IMMEDIATE_INT(p)) return TAG_INT;
    if (IS_IMMEDIATE_CHAR(p)) return TAG_CHAR;
    if (IS_IMMEDIATE_BOOL(p)) return TAG_INT;  /* Booleans are int-like */
    return p->tag;
}

/* Check if value is an integer (boxed or immediate) */
static inline int is_int(Obj* p) {
    if (IS_IMMEDIATE_INT(p) || IS_IMMEDIATE_BOOL(p)) return 1;
    return p && p->tag == TAG_INT;
}

/* Check if value is a character (boxed or immediate) */
static inline int is_char(Obj* p) {
    if (IS_IMMEDIATE_CHAR(p)) return 1;
    return p && p->tag == TAG_CHAR;
}

/* Dynamic Free List */
typedef struct FreeNode {
    Obj* obj;
    struct FreeNode* next;
} FreeNode;

FreeNode* FREE_HEAD = NULL;
int FREE_COUNT = 0;

/* Stack Allocation Pool */
#define STACK_POOL_SIZE 256
Obj STACK_POOL[STACK_POOL_SIZE];
int STACK_PTR = 0;

int is_stack_obj(Obj* x) {
    uintptr_t px = (uintptr_t)x;
    uintptr_t start = (uintptr_t)&STACK_POOL[0];
    uintptr_t end = (uintptr_t)&STACK_POOL[STACK_POOL_SIZE];
    return px >= start && px < end;
}

int is_nil(Obj* x) {
    return x == NULL;
}

/* Internal Weak Reference Support */
/* NOTE: This is internal to the runtime. Users should not use WeakRef directly.
   The compiler automatically detects back-edges and generates appropriate code. */

typedef struct _InternalWeakRef {
    void* target;
    int alive;
} _InternalWeakRef;

typedef struct _InternalWeakRefNode {
    _InternalWeakRef* ref;
    struct _InternalWeakRefNode* next;
} _InternalWeakRefNode;

_InternalWeakRefNode* _WEAK_REF_HEAD = NULL;

static _InternalWeakRef* _mk_weak_ref(void* target) {
    _InternalWeakRef* w = malloc(sizeof(_InternalWeakRef));
    if (!w) return NULL;
    w->target = target;
    w->alive = 1;
    _InternalWeakRefNode* node = malloc(sizeof(_InternalWeakRefNode));
    if (!node) { free(w); return NULL; }
    node->ref = w;
    node->next = _WEAK_REF_HEAD;
    _WEAK_REF_HEAD = node;
    return w;
}

static void* _deref_weak(_InternalWeakRef* w) {
    if (w && w->alive) return w->target;
    return NULL;
}

void _invalidate_weak(_InternalWeakRef* w) {
    if (w) w->alive = 0;
}

void invalidate_weak_refs_for(void* target) {
    _InternalWeakRefNode* n = _WEAK_REF_HEAD;
    while (n) {
        _InternalWeakRef* obj = n->ref;
        if (obj->target == target) {
            _invalidate_weak(obj);
        }
        n = n->next;
    }
}

/* IPGE generation seed - evolves with each allocation */
static uint64_t _ipge_seed = 0x123456789ABCDEF0ULL;

static inline uint64_t _next_generation(void) {
    _ipge_seed = ipge_evolve(_ipge_seed);
    return _ipge_seed;
}

/* Object Constructors */
Obj* mk_int(long i) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->generation = _next_generation();
    x->mark = 1;
    x->tag = TAG_INT;
    x->is_pair = 0;
    x->i = i;
    return x;
}

Obj* mk_float(double f) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->generation = _next_generation();
    x->mark = 1;
    x->tag = TAG_FLOAT;
    x->is_pair = 0;
    x->f = f;
    return x;
}

double get_float(Obj* x) {
    if (!x) return 0.0;
    return x->f;
}

Obj* mk_char(long c) {
    /* Use unboxed for Unicode codepoints */
    if (c >= 0 && c <= 0x10FFFF) {
        return mk_char_unboxed(c);
    }
    /* Fallback to boxed for invalid codepoints */
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->generation = _next_generation();
    x->mark = 1;
    x->tag = TAG_CHAR;
    x->is_pair = 0;
    x->i = c;
    return x;
}

Obj* mk_pair(Obj* a, Obj* b) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->generation = _next_generation();
    x->mark = 1;
    x->tag = TAG_PAIR;
    x->is_pair = 1;
    /* Move semantics: ownership transfers to pair, no inc_ref needed */
    x->a = a;
    x->b = b;
    return x;
}

Obj* mk_sym(const char* s) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->generation = _next_generation();
    x->mark = 1;
    x->tag = TAG_SYM;
    x->is_pair = 0;
    if (s) {
        size_t len = strlen(s);
        char* copy = malloc(len + 1);
        if (!copy) {
            free(x);
            return NULL;
        }
        memcpy(copy, s, len + 1);
        x->ptr = copy;
    } else {
        x->ptr = NULL;
    }
    return x;
}

Obj* mk_box(Obj* v) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->generation = _next_generation();
    x->mark = 1;
    x->tag = TAG_BOX;
    x->is_pair = 0;
    if (v) inc_ref(v);
    x->ptr = v;
    return x;
}

Obj* box_get(Obj* b) {
    if (!b || b->tag != TAG_BOX) return NULL;
    return (Obj*)b->ptr;
}

void box_set(Obj* b, Obj* v) {
    if (!b || b->tag != TAG_BOX) return;
    if (v) inc_ref(v);
    if (b->ptr) dec_ref((Obj*)b->ptr);
    b->ptr = v;
}

Obj* mk_error(const char* msg) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->mark = 1;
    x->scc_id = -1;
    x->is_pair = 0;
    x->scan_tag = 0;
    x->tag = TAG_ERROR;
    x->generation = _next_generation();
    if (msg) {
        size_t len = strlen(msg);
        char* copy = malloc(len + 1);
        if (!copy) {
            free(x);
            return NULL;
        }
        memcpy(copy, msg, len + 1);
        x->ptr = copy;
    } else {
        x->ptr = NULL;
    }
    return x;
}

Obj* mk_int_stack(long i) {
    if (STACK_PTR < STACK_POOL_SIZE) {
        Obj* x = &STACK_POOL[STACK_PTR++];
        x->mark = 0;
        x->scc_id = -1;
        x->is_pair = 0;
        x->scan_tag = 0;
        x->tag = TAG_INT;
        x->generation = _next_generation();
        x->i = i;
        return x;
    }
    return mk_int(i);
}

Obj* mk_float_stack(double f) {
    if (STACK_PTR < STACK_POOL_SIZE) {
        Obj* x = &STACK_POOL[STACK_PTR++];
        x->mark = 0;
        x->scc_id = -1;
        x->is_pair = 0;
        x->scan_tag = 0;
        x->tag = TAG_FLOAT;
        x->generation = _next_generation();
        x->f = f;
        return x;
    }
    return mk_float(f);
}

Obj* mk_char_stack(long c) {
    if (STACK_PTR < STACK_POOL_SIZE) {
        Obj* x = &STACK_POOL[STACK_PTR++];
        x->mark = 0;
        x->scc_id = -1;
        x->is_pair = 0;
        x->scan_tag = 0;
        x->tag = TAG_CHAR;
        x->generation = _next_generation();
        x->i = c;
        return x;
    }
    return mk_char(c);
}

/* Shape-based Deallocation (Ghiya-Hendren analysis) */

void release_children(Obj* x) {
    if (!x) return;
    switch (x->tag) {
    case TAG_PAIR:
        dec_ref(x->a);
        dec_ref(x->b);
        break;
    case TAG_BOX:
        if (x->ptr) dec_ref((Obj*)x->ptr);
        break;
    case TAG_CLOSURE:
        if (x->ptr) closure_release((Closure*)x->ptr);
        break;
    case TAG_SYM:
    case TAG_ERROR:
        if (x->ptr) free(x->ptr);
        break;
    case TAG_CHANNEL:
        if (x->ptr) free_channel_obj(x);
        break;
    default:
        if (x->tag >= TAG_USER_BASE) {
            release_user_obj(x);
        }
        break;
    }
}

/* TREE: Direct free (ASAP) */
void free_tree(Obj* x) {
    if (!x) return;
    if (is_stack_obj(x)) return;
    switch (x->tag) {
    case TAG_PAIR:
        free_tree(x->a);
        free_tree(x->b);
        break;
    case TAG_BOX:
        if (x->ptr) free_tree((Obj*)x->ptr);
        break;
    case TAG_CLOSURE:
        if (x->ptr) closure_release((Closure*)x->ptr);
        break;
    case TAG_SYM:
    case TAG_ERROR:
        if (x->ptr) free(x->ptr);
        break;
    default:
        if (x->tag >= TAG_USER_BASE) {
            release_user_obj(x);
        }
        break;
    }
    borrow_invalidate_obj(x);
    invalidate_weak_refs_for(x);
    free(x);
}

/* DAG: Reference counting */
void dec_ref(Obj* x) {
    if (!x) return;
    /* Immediate integers don't need RC */
    if (IS_IMMEDIATE(x)) return;
    if (is_stack_obj(x)) return;
    if (x->mark < 0) return;
    x->mark--;
    if (x->mark <= 0) {
        release_children(x);
        borrow_invalidate_obj(x);
        invalidate_weak_refs_for(x);
        free(x);
    }
}

void inc_ref(Obj* x) {
    if (!x) return;
    /* Immediate integers don't need RC */
    if (IS_IMMEDIATE(x)) return;
    if (is_stack_obj(x)) return;
    if (x->mark < 0) { x->mark = 1; return; }
    x->mark++;
}

/* RC Optimization: Direct free for proven-unique references (Lobster-style) */
/* When compile-time analysis proves a reference is the only one, skip RC check */
void free_unique(Obj* x) {
    if (!x) return;
    /* Immediate integers don't need freeing */
    if (IS_IMMEDIATE(x)) return;
    if (is_stack_obj(x)) return;
    /* Proven unique at compile time - no RC check needed */
    release_children(x);
    borrow_invalidate_obj(x);
    invalidate_weak_refs_for(x);
    free(x);
}

/* RC Optimization: Borrowed reference - no RC ops needed */
/* For parameters and temporary references that don't transfer ownership */
#define BORROWED_REF(x) (x)  /* No-op marker for documentation */

/* Free list operations */
void free_obj(Obj* x) {
    if (!x) return;
    /* Immediates don't need freeing */
    if (IS_IMMEDIATE(x)) return;
    if (is_stack_obj(x)) return;
    if (x->mark < 0) return;
    x->mark = -1;

    /* IPGE: Evolve generation to invalidate borrowed refs */
    x->generation = ipge_evolve(x->generation);

    FreeNode* n = malloc(sizeof(FreeNode));
    if (!n) {
        release_children(x);
        invalidate_weak_refs_for(x);
        free(x);
        return;
    }
    n->obj = x;
    n->next = FREE_HEAD;
    FREE_HEAD = n;
    FREE_COUNT++;
}

void flush_freelist(void) {
    while (FREE_HEAD) {
        FreeNode* n = FREE_HEAD;
        FREE_HEAD = n->next;
        if (n->obj->mark < 0) {
            release_children(n->obj);
            borrow_invalidate_obj(n->obj);
            invalidate_weak_refs_for(n->obj);
            free(n->obj);
        }
        free(n);
    }
    FREE_COUNT = 0;
}

/* Deferred release for cyclic structures */
void deferred_release(Obj* x) {
    free_obj(x);
}

/* Arena Allocator (Bulk Allocation/Deallocation) */
/* For cyclic data that doesn't escape function scope */
/* Enhanced with external pointer tracking */

#define ARENA_BLOCK_SIZE 4096

typedef struct ArenaBlock {
    char* memory;
    size_t size;
    size_t used;
    struct ArenaBlock* next;
} ArenaBlock;

/* External pointer tracking - for pointers that escape the arena */
typedef struct ArenaExternal {
    void* ptr;
    void (*cleanup)(void*);
    struct ArenaExternal* next;
} ArenaExternal;

typedef struct Arena {
    ArenaBlock* current;
    ArenaBlock* blocks;
    size_t block_size;
    ArenaExternal* externals;
} Arena;

Arena* arena_create(void) {
    Arena* a = malloc(sizeof(Arena));
    if (!a) return NULL;
    a->current = NULL;
    a->blocks = NULL;
    a->block_size = ARENA_BLOCK_SIZE;
    a->externals = NULL;
    return a;
}

static void* arena_alloc(Arena* a, size_t size) {
    if (!a) return NULL;

    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;

    if (!a->current || a->current->used + size > a->current->size) {
        /* Need new block */
        size_t block_size = a->block_size;
        if (size > block_size) block_size = size;

        ArenaBlock* b = malloc(sizeof(ArenaBlock));
        if (!b) return NULL;
        b->memory = malloc(block_size);
        if (!b->memory) {
            free(b);
            return NULL;
        }
        b->size = block_size;
        b->used = 0;
        b->next = a->blocks;
        a->blocks = b;
        a->current = b;
    }

    void* ptr = a->current->memory + a->current->used;
    a->current->used += size;
    return ptr;
}

/* Register an external pointer that must be cleaned up when arena is destroyed */
void arena_register_external(Arena* a, void* ptr, void (*cleanup)(void*)) {
    if (!a || !ptr) return;
    ArenaExternal* e = malloc(sizeof(ArenaExternal));
    if (!e) return;
    e->ptr = ptr;
    e->cleanup = cleanup;
    e->next = a->externals;
    a->externals = e;
}

void arena_destroy(Arena* a) {
    if (!a) return;

    /* Clean up external pointers first */
    ArenaExternal* e = a->externals;
    while (e) {
        ArenaExternal* next = e->next;
        if (e->cleanup) {
            e->cleanup(e->ptr);
        }
        free(e);
        e = next;
    }

    /* Free all blocks */
    ArenaBlock* b = a->blocks;
    while (b) {
        ArenaBlock* next = b->next;
        free(b->memory);
        free(b);
        b = next;
    }

    free(a);
}

/* Reset arena for reuse without destroying */
void arena_reset(Arena* a) {
    if (!a) return;

    /* Clean up externals */
    ArenaExternal* e = a->externals;
    while (e) {
        ArenaExternal* next = e->next;
        if (e->cleanup) {
            e->cleanup(e->ptr);
        }
        free(e);
        e = next;
    }
    a->externals = NULL;

    /* Reset all blocks */
    ArenaBlock* b = a->blocks;
    while (b) {
        b->used = 0;
        b = b->next;
    }
    a->current = a->blocks;
}

Obj* arena_mk_int(Arena* a, long i) {
    Obj* x = arena_alloc(a, sizeof(Obj));
    if (!x) return NULL;
    x->mark = -2;  /* Special mark for arena-allocated */
    x->scc_id = -1;
    x->is_pair = 0;
    x->scan_tag = 0;
    x->tag = TAG_INT;
    x->generation = _next_generation();
    x->i = i;
    return x;
}

Obj* arena_mk_pair(Arena* a, Obj* car, Obj* cdr) {
    Obj* x = arena_alloc(a, sizeof(Obj));
    if (!x) return NULL;
    x->mark = -2;  /* Special mark for arena-allocated */
    x->scc_id = -1;
    x->is_pair = 1;
    x->scan_tag = 0;
    x->tag = TAG_PAIR;
    x->generation = _next_generation();
    x->a = car;
    x->b = cdr;
    return x;
}

/* SCC-Based Reference Counting (ISMM 2024) */
/* For frozen (deeply immutable) cyclic structures */
/* Uses Tarjan's algorithm for O(n) SCC detection */

typedef struct SCC {
    int id;
    Obj** members;
    int member_count;
    int member_capacity;
    int ref_count;      /* Single RC for entire SCC */
    int frozen;         /* 1 if immutable */
    struct SCC* next;
} SCC;

typedef struct SCCRegistry {
    SCC* sccs;
    int next_id;
} SCCRegistry;

SCCRegistry SCC_REGISTRY = {NULL, 0};

/* Tarjan's Algorithm state */
typedef struct TarjanState {
    int* index;
    int* lowlink;
    int* on_stack;
    Obj** stack;
    int stack_top;
    int current_index;
    int capacity;
} TarjanState;

static TarjanState* tarjan_init(int capacity) {
    TarjanState* s = malloc(sizeof(TarjanState));
    if (!s) return NULL;
    s->index = calloc(capacity, sizeof(int));
    s->lowlink = calloc(capacity, sizeof(int));
    s->on_stack = calloc(capacity, sizeof(int));
    s->stack = malloc(capacity * sizeof(Obj*));
    s->stack_top = 0;
    s->current_index = 1;
    s->capacity = capacity;
    if (!s->index || !s->lowlink || !s->on_stack || !s->stack) {
        free(s->index);
        free(s->lowlink);
        free(s->on_stack);
        free(s->stack);
        free(s);
        return NULL;
    }
    return s;
}

void tarjan_free(TarjanState* s) {
    if (!s) return;
    free(s->index);
    free(s->lowlink);
    free(s->on_stack);
    free(s->stack);
    free(s);
}

SCC* create_scc(void) {
    SCC* scc = malloc(sizeof(SCC));
    if (!scc) return NULL;
    scc->id = SCC_REGISTRY.next_id++;
    scc->members = malloc(16 * sizeof(Obj*));
    scc->member_count = 0;
    scc->member_capacity = 16;
    scc->ref_count = 1;
    scc->frozen = 0;
    scc->next = SCC_REGISTRY.sccs;
    SCC_REGISTRY.sccs = scc;
    return scc;
}

void scc_add_member(SCC* scc, Obj* obj) {
    if (!scc || !obj) return;
    if (scc->member_count >= scc->member_capacity) {
        int new_cap = scc->member_capacity * 2;
        Obj** new_members = realloc(scc->members, new_cap * sizeof(Obj*));
        if (!new_members) return;
        scc->members = new_members;
        scc->member_capacity = new_cap;
    }
    scc->members[scc->member_count++] = obj;
    obj->scc_id = scc->id;
}

void freeze_scc(SCC* scc) {
    if (scc) scc->frozen = 1;
}

SCC* find_scc(int id) {
    SCC* scc = SCC_REGISTRY.sccs;
    while (scc) {
        if (scc->id == id) return scc;
        scc = scc->next;
    }
    return NULL;
}

void release_scc(SCC* scc) {
    if (!scc) return;
    scc->ref_count--;
    if (scc->ref_count <= 0 && scc->frozen) {
        /* Free all members */
        for (int i = 0; i < scc->member_count; i++) {
            Obj* obj = scc->members[i];
            if (obj) {
                invalidate_weak_refs_for(obj);
                free(obj);
            }
        }
        free(scc->members);

        /* Remove from registry */
        SCC** pp = &SCC_REGISTRY.sccs;
        while (*pp) {
            if (*pp == scc) {
                *pp = scc->next;
                break;
            }
            pp = &(*pp)->next;
        }
        free(scc);
    }
}

/* Release object considering SCC membership */
void release_with_scc(Obj* obj) {
    if (!obj) return;
    if (obj->scc_id >= 0) {
        SCC* scc = find_scc(obj->scc_id);
        if (scc) {
            release_scc(scc);
            return;
        }
    }
    dec_ref(obj);
}

/* Tarjan's strongconnect for SCC detection */
void tarjan_strongconnect(Obj* v, TarjanState* state,
                                  void (*on_scc)(Obj**, int)) {
    if (!v || !state) return;

    /* Use scan_tag field to store Tarjan index for this node */
    int v_idx = state->current_index++;
    v->scan_tag = (unsigned int)v_idx;
    state->index[v_idx % state->capacity] = v_idx;
    state->lowlink[v_idx % state->capacity] = v_idx;
    state->stack[state->stack_top++] = v;
    state->on_stack[v_idx % state->capacity] = 1;

    /* Visit children */
    if (v->is_pair) {
        Obj* children[] = {v->a, v->b};
        for (int i = 0; i < 2; i++) {
            Obj* w = children[i];
            if (!w) continue;

            int w_idx = (int)w->scan_tag;
            if (w_idx == 0) {
                /* Not visited yet */
                tarjan_strongconnect(w, state, on_scc);
                int w_low = state->lowlink[w->scan_tag % state->capacity];
                if (w_low < state->lowlink[v_idx % state->capacity]) {
                    state->lowlink[v_idx % state->capacity] = w_low;
                }
            } else if (state->on_stack[w_idx % state->capacity]) {
                /* w is on stack */
                if (state->index[w_idx % state->capacity] < state->lowlink[v_idx % state->capacity]) {
                    state->lowlink[v_idx % state->capacity] = state->index[w_idx % state->capacity];
                }
            }
        }
    }

    /* Check if v is root of SCC */
    if (state->lowlink[v_idx % state->capacity] == state->index[v_idx % state->capacity]) {
        Obj* scc_members[256];
        int scc_size = 0;
        Obj* w;
        do {
            w = state->stack[--state->stack_top];
            int w_idx = (int)w->scan_tag;
            state->on_stack[w_idx % state->capacity] = 0;
            if (scc_size < 256) {
                scc_members[scc_size++] = w;
            }
        } while (w != v && state->stack_top > 0);

        if (scc_size > 1 && on_scc) {
            on_scc(scc_members, scc_size);
        }
    }
}

void on_scc_found(Obj** members, int count) {
    SCC* scc = create_scc();
    if (!scc) return;
    for (int i = 0; i < count; i++) {
        scc_add_member(scc, members[i]);
    }
}

/* Detect and freeze SCCs starting from a root object */
void detect_and_freeze_sccs(Obj* root) {
    TarjanState* state = tarjan_init(1024);
    if (!state) return;
    tarjan_strongconnect(root, state, on_scc_found);

    /* Freeze all detected SCCs */
    SCC* scc = SCC_REGISTRY.sccs;
    while (scc) {
        if (!scc->frozen) {
            freeze_scc(scc);
        }
        scc = scc->next;
    }

    tarjan_free(state);
}

/* Deferred Reference Counting */
/* For mutable cyclic structures - bounded O(k) processing per safe point */
/* Based on Deutsch & Bobrow (1976) and CactusRef-style local detection */

typedef struct DeferredDec {
    Obj* obj;
    int count;
    struct DeferredDec* next;
} DeferredDec;

typedef struct DeferredContext {
    DeferredDec* pending;
    int pending_count;
    int batch_size;     /* Max decrements per safe point */
    int total_deferred;
} DeferredContext;

DeferredContext DEFERRED_CTX = {NULL, 0, 32, 0};

/* O(1) deferral with coalescing */
void defer_decrement(Obj* obj) {
    if (!obj) return;
    DEFERRED_CTX.total_deferred++;

    /* Check if already in pending list - coalesce */
    DeferredDec* d = DEFERRED_CTX.pending;
    while (d) {
        if (d->obj == obj) {
            d->count++;
            return;
        }
        d = d->next;
    }

    /* Add new entry */
    DeferredDec* entry = malloc(sizeof(DeferredDec));
    if (!entry) {
        /* Fallback: immediate decrement */
        dec_ref(obj);
        return;
    }
    entry->obj = obj;
    entry->count = 1;
    entry->next = DEFERRED_CTX.pending;
    DEFERRED_CTX.pending = entry;
    DEFERRED_CTX.pending_count++;
}

/* Process up to batch_size decrements - bounded work */
void process_deferred(void) {
    int processed = 0;
    while (DEFERRED_CTX.pending && processed < DEFERRED_CTX.batch_size) {
        DeferredDec* d = DEFERRED_CTX.pending;
        DEFERRED_CTX.pending = d->next;
        DEFERRED_CTX.pending_count--;

        /* Apply decrements */
        while (d->count > 0) {
            dec_ref(d->obj);
            d->count--;
            processed++;
            if (processed >= DEFERRED_CTX.batch_size) break;
        }

        if (d->count > 0) {
            /* Put back for next round */
            d->next = DEFERRED_CTX.pending;
            DEFERRED_CTX.pending = d;
            DEFERRED_CTX.pending_count++;
        } else {
            free(d);
        }
    }
}

/* Check if we should process deferred decrements */
int should_process_deferred(void) {
    return DEFERRED_CTX.pending_count > DEFERRED_CTX.batch_size * 2;
}

/* Flush all pending decrements */
void flush_deferred(void) {
    while (DEFERRED_CTX.pending) {
        DeferredDec* d = DEFERRED_CTX.pending;
        DEFERRED_CTX.pending = d->next;

        while (d->count > 0) {
            dec_ref(d->obj);
            d->count--;
        }
        free(d);
    }
    DEFERRED_CTX.pending_count = 0;
}

/* Safe point: check and maybe process deferred - call at function boundaries */
void safe_point(void) {
    if (should_process_deferred()) {
        process_deferred();
    }
}

/* Set batch size for tuning */
void set_deferred_batch_size(int size) {
    if (size > 0) {
        DEFERRED_CTX.batch_size = size;
    }
}

/* Symmetric Reference Counting (Hybrid Memory Strategy) */
/* Key insight: Treat scope as an object that participates in ownership graph */
/* External refs: From live scopes/roots */
/* Internal refs: Within the object graph */
/* When external_rc drops to 0, the object (or cycle) is orphaned garbage */

typedef struct SymObj SymObj;
typedef struct SymScope SymScope;

struct SymObj {
    int external_rc;      /* References from live scopes */
    int internal_rc;      /* References from other objects */
    SymObj** refs;        /* Objects this object references */
    int ref_count;        /* Number of outgoing references */
    int ref_capacity;
    void* data;           /* Actual data payload (Obj*) */
    int freed;            /* Mark to prevent double-free */
};

struct SymScope {
    SymObj** owned;       /* Objects owned by this scope */
    int owned_count;
    int owned_capacity;
    SymScope* parent;
};

/* Global symmetric RC context */
static struct {
    SymScope* current;
    SymScope** stack;
    int stack_size;
    int stack_capacity;
    int objects_created;
    int cycles_collected;
} SYM_CTX = {NULL, NULL, 0, 0, 0, 0};

/* Forward declarations */
void sym_check_free(SymObj* obj);

SymObj* sym_obj_new(void* data) {
    SymObj* obj = malloc(sizeof(SymObj));
    if (!obj) return NULL;
    obj->external_rc = 0;
    obj->internal_rc = 0;
    obj->refs = NULL;
    obj->ref_count = 0;
    obj->ref_capacity = 0;
    obj->data = data;
    obj->freed = 0;
    return obj;
}

void sym_obj_add_ref(SymObj* obj, SymObj* target) {
    if (!obj || !target) return;
    if (obj->ref_count >= obj->ref_capacity) {
        int new_cap = obj->ref_capacity == 0 ? 8 : obj->ref_capacity * 2;
        SymObj** new_refs = realloc(obj->refs, new_cap * sizeof(SymObj*));
        if (!new_refs) return;
        obj->refs = new_refs;
        obj->ref_capacity = new_cap;
    }
    obj->refs[obj->ref_count++] = target;
}

SymScope* sym_scope_new(SymScope* parent) {
    SymScope* scope = malloc(sizeof(SymScope));
    if (!scope) return NULL;
    scope->owned = NULL;
    scope->owned_count = 0;
    scope->owned_capacity = 0;
    scope->parent = parent;
    return scope;
}

void sym_scope_own(SymScope* scope, SymObj* obj) {
    if (!scope || !obj || obj->freed) return;
    if (scope->owned_count >= scope->owned_capacity) {
        int new_cap = scope->owned_capacity == 0 ? 8 : scope->owned_capacity * 2;
        SymObj** new_owned = realloc(scope->owned, new_cap * sizeof(SymObj*));
        if (!new_owned) return;
        scope->owned = new_owned;
        scope->owned_capacity = new_cap;
    }
    obj->external_rc++;
    scope->owned[scope->owned_count++] = obj;
}

void sym_dec_external(SymObj* obj) {
    if (!obj || obj->freed) return;
    obj->external_rc--;
    sym_check_free(obj);
}

void sym_dec_internal(SymObj* obj) {
    if (!obj || obj->freed) return;
    obj->internal_rc--;
    sym_check_free(obj);
}

void sym_check_free(SymObj* obj) {
    if (!obj || obj->freed) return;
    /* Object is garbage when it has no external references */
    if (obj->external_rc <= 0) {
        obj->freed = 1;
        /* Cascade: decrement internal refs for all objects we reference */
        for (int i = 0; i < obj->ref_count; i++) {
            sym_dec_internal(obj->refs[i]);
        }
        /* Free the data (Obj*) if present */
        if (obj->data) {
            dec_ref((Obj*)obj->data);
        }
        free(obj->refs);
        free(obj);
    }
}

void sym_scope_release(SymScope* scope) {
    if (!scope) return;
    for (int i = 0; i < scope->owned_count; i++) {
        sym_dec_external(scope->owned[i]);
    }
    scope->owned_count = 0;
}

void sym_scope_free(SymScope* scope) {
    if (!scope) return;
    free(scope->owned);
    free(scope);
}

/* Context operations */
void sym_init(void) {
    if (SYM_CTX.current) return;  /* Already initialized */
    SYM_CTX.current = sym_scope_new(NULL);
    SYM_CTX.stack = malloc(8 * sizeof(SymScope*));
    SYM_CTX.stack[0] = SYM_CTX.current;
    SYM_CTX.stack_size = 1;
    SYM_CTX.stack_capacity = 8;
}

SymScope* sym_enter_scope(void) {
    if (!SYM_CTX.current) sym_init();
    SymScope* scope = sym_scope_new(SYM_CTX.current);
    if (!scope) return NULL;
    if (SYM_CTX.stack_size >= SYM_CTX.stack_capacity) {
        int new_cap = SYM_CTX.stack_capacity * 2;
        SymScope** new_stack = realloc(SYM_CTX.stack, new_cap * sizeof(SymScope*));
        if (!new_stack) { sym_scope_free(scope); return NULL; }
        SYM_CTX.stack = new_stack;
        SYM_CTX.stack_capacity = new_cap;
    }
    SYM_CTX.stack[SYM_CTX.stack_size++] = scope;
    SYM_CTX.current = scope;
    return scope;
}

void sym_exit_scope(void) {
    if (SYM_CTX.stack_size <= 1) return;  /* Don't exit global */
    SymScope* scope = SYM_CTX.stack[--SYM_CTX.stack_size];
    SYM_CTX.current = SYM_CTX.stack[SYM_CTX.stack_size - 1];
    /* Count cycles for stats */
    for (int i = 0; i < scope->owned_count; i++) {
        SymObj* obj = scope->owned[i];
        if (obj && !obj->freed && obj->internal_rc > 0 && obj->external_rc == 1) {
            SYM_CTX.cycles_collected++;
        }
    }
    sym_scope_release(scope);
    sym_scope_free(scope);
}

/* Allocate object in current scope */
SymObj* sym_alloc(Obj* data) {
    if (!SYM_CTX.current) sym_init();
    SymObj* obj = sym_obj_new(data);
    if (!obj) return NULL;
    sym_scope_own(SYM_CTX.current, obj);
    SYM_CTX.objects_created++;
    return obj;
}

/* Create internal reference (from one object to another) */
void sym_link(SymObj* from, SymObj* to) {
    if (!from || !to || to->freed) return;
    to->internal_rc++;
    sym_obj_add_ref(from, to);
}

/* Get the Obj* from a SymObj* */
Obj* sym_get_data(SymObj* obj) {
    return obj ? (Obj*)obj->data : NULL;
}


/* ========== Region References (v0.5.0) ========== */
/* Vale/Ada/SPARK-style scope hierarchy validation */
/* O(1) CanReference check via depth comparison */

typedef uint64_t RegionID;
typedef uint32_t RegionDepth;

typedef struct Region Region;
typedef struct RegionObj RegionObj;
typedef struct RegionRef RegionRef;
typedef struct RegionContext RegionContext;

struct Region {
    RegionID id;
    RegionDepth depth;
    Region* parent;
    Region** children;
    int child_count;
    int child_capacity;
    RegionObj** objects;
    int object_count;
    int object_capacity;
    int closed;
};

struct RegionObj {
    Region* region;
    void* data;
    void (*destructor)(void*);
};

struct RegionRef {
    RegionObj* target;
    Region* source_region;
};

struct RegionContext {
    Region* root;
    Region* current;
    uint64_t next_id;
};

/* Global context (can also use explicit context) */
static RegionContext* g_region_ctx = NULL;

static Region* region_new(Region* parent, uint64_t* next_id) {
    Region* r = calloc(1, sizeof(Region));
    if (!r) return NULL;
    r->id = (*next_id)++;
    r->depth = parent ? parent->depth + 1 : 0;
    r->parent = parent;
    r->closed = 0;
    return r;
}

static RegionContext* region_context_new(void) {
    RegionContext* ctx = calloc(1, sizeof(RegionContext));
    if (!ctx) return NULL;
    ctx->next_id = 1;
    ctx->root = region_new(NULL, &ctx->next_id);
    ctx->current = ctx->root;
    return ctx;
}

void region_init(void) {
    if (!g_region_ctx) {
        g_region_ctx = region_context_new();
    }
}

static Region* region_enter(void) {
    region_init();
    Region* child = region_new(g_region_ctx->current, &g_region_ctx->next_id);
    if (!child) return NULL;

    Region* parent = g_region_ctx->current;
    if (parent->child_count >= parent->child_capacity) {
        int new_cap = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        Region** new_children = realloc(parent->children, new_cap * sizeof(Region*));
        if (!new_children) { free(child); return NULL; }
        parent->children = new_children;
        parent->child_capacity = new_cap;
    }
    parent->children[parent->child_count++] = child;
    g_region_ctx->current = child;
    return child;
}

int region_exit(void) {
    if (!g_region_ctx || !g_region_ctx->current) return -1;
    if (g_region_ctx->current == g_region_ctx->root) return -1;  /* Cannot exit root */

    Region* exiting = g_region_ctx->current;
    exiting->closed = 1;

    /* Invalidate all objects in this region */
    for (int i = 0; i < exiting->object_count; i++) {
        RegionObj* obj = exiting->objects[i];
        if (obj) {
            if (obj->destructor && obj->data) {
                obj->destructor(obj->data);
            }
            obj->region = NULL;  /* Mark as invalid */
        }
    }
    g_region_ctx->current = exiting->parent;
    return 0;
}

static RegionObj* region_alloc(void* data, void (*destructor)(void*)) {
    region_init();
    RegionObj* obj = calloc(1, sizeof(RegionObj));
    if (!obj) return NULL;
    obj->region = g_region_ctx->current;
    obj->data = data;
    obj->destructor = destructor;

    Region* r = g_region_ctx->current;
    if (r->object_count >= r->object_capacity) {
        int new_cap = r->object_capacity == 0 ? 8 : r->object_capacity * 2;
        RegionObj** new_objs = realloc(r->objects, new_cap * sizeof(RegionObj*));
        if (!new_objs) { free(obj); return NULL; }
        r->objects = new_objs;
        r->object_capacity = new_cap;
    }
    r->objects[r->object_count++] = obj;
    return obj;
}

/* O(1) check: inner can reference outer (depth >= target depth) */
int region_can_reference(RegionObj* source, RegionObj* target) {
    if (!source || !target) return 0;
    if (!source->region || !target->region) return 0;  /* Invalid objects */
    return source->region->depth >= target->region->depth;
}

static RegionRef* region_create_ref(RegionObj* source, RegionObj* target) {
    if (!region_can_reference(source, target)) {
        fprintf(stderr, "region: scope violation - inner cannot hold ref to outer\n");
        return NULL;
    }
    RegionRef* ref = calloc(1, sizeof(RegionRef));
    if (!ref) return NULL;
    ref->target = target;
    ref->source_region = source->region;
    return ref;
}

int region_ref_is_valid(RegionRef* ref) {
    return ref && ref->target && ref->target->region != NULL;
}

static void* region_deref(RegionRef* ref) {
    if (!region_ref_is_valid(ref)) {
        fprintf(stderr, "region: use-after-free or scope violation\n");
        return NULL;
    }
    return ref->target->data;
}


/* ========== Random Generational References (v0.5.0) ========== */
/* Vale-style use-after-free detection */
/* Thread-safe via pthread_rwlock (C99 + POSIX) */

#include <time.h>

typedef uint64_t Generation;

typedef struct GenObj GenObj;
typedef struct BorrowRef BorrowRef;
typedef struct GenClosure GenClosure;
typedef struct LegacyGenContext LegacyGenContext;

struct GenObj {
    Generation generation;
    void* data;
    void (*destructor)(void*);
    int freed;
    pthread_rwlock_t rwlock;
};

struct BorrowRef {
    GenObj* target;        /* Legacy GenObj system (not used with IPGE) */
    Generation remembered_gen;
    const char* source_desc;
    Obj* ipge_target;      /* IPGE: Actual Obj* for generation comparison */
};

/* Closure with capture validation */
struct GenClosure {
    BorrowRef** captures;
    int capture_count;
    void* (*func)(void* ctx);
    void* ctx;
};

struct LegacyGenContext {
    GenObj** objects;
    int object_count;
    int object_capacity;
    pthread_mutex_t mutex;
};

static LegacyGenContext* g_legacy_ctx = NULL;

/* Fast xorshift64 PRNG for generation IDs */
static Generation legacy_random_gen(void) {
    static uint64_t state = 0;
    static pthread_mutex_t prng_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&prng_mutex);
    if (state == 0) {
        state = (uint64_t)time(NULL) ^ 0x9e3779b97f4a7c15ULL;
    }
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    Generation result = state ? state : 1;  /* Never return 0 */
    pthread_mutex_unlock(&prng_mutex);
    return result;
}

static LegacyGenContext* legacy_context_new(void) {
    LegacyGenContext* ctx = calloc(1, sizeof(LegacyGenContext));
    if (ctx) {
        pthread_mutex_init(&ctx->mutex, NULL);
    }
    return ctx;
}

void legacy_init(void) {
    if (!g_legacy_ctx) {
        g_legacy_ctx = legacy_context_new();
    }
}

static GenObj* legacy_alloc(LegacyGenContext* ctx, void* data, void (*destructor)(void*)) {
    GenObj* obj = calloc(1, sizeof(GenObj));
    if (!obj) return NULL;
    obj->generation = legacy_random_gen();
    obj->data = data;
    obj->destructor = destructor;
    obj->freed = 0;
    pthread_rwlock_init(&obj->rwlock, NULL);

    if (ctx) {
        pthread_mutex_lock(&ctx->mutex);
        if (ctx->object_count >= ctx->object_capacity) {
            int new_cap = ctx->object_capacity == 0 ? 16 : ctx->object_capacity * 2;
            GenObj** new_objs = realloc(ctx->objects, new_cap * sizeof(GenObj*));
            if (new_objs) {
                ctx->objects = new_objs;
                ctx->object_capacity = new_cap;
            }
        }
        if (ctx->object_count < ctx->object_capacity) {
            ctx->objects[ctx->object_count++] = obj;
        }
        pthread_mutex_unlock(&ctx->mutex);
    }
    return obj;
}

void legacy_free(GenObj* obj) {
    if (!obj) return;
    pthread_rwlock_wrlock(&obj->rwlock);
    if (obj->freed) {
        pthread_rwlock_unlock(&obj->rwlock);
        return;
    }
    obj->generation = 0;  /* Invalidate ALL references instantly */
    obj->freed = 1;
    if (obj->destructor && obj->data) {
        obj->destructor(obj->data);
    }
    obj->data = NULL;
    pthread_rwlock_unlock(&obj->rwlock);
    pthread_rwlock_destroy(&obj->rwlock);
}

void borrow_release(BorrowRef* ref) {
    if (ref) free(ref);
}

BorrowRef* legacy_create(GenObj* obj, const char* source_desc) {
    if (!obj) return NULL;
    pthread_rwlock_rdlock(&obj->rwlock);
    if (obj->freed || obj->generation == 0) {
        pthread_rwlock_unlock(&obj->rwlock);
        return NULL;
    }
    BorrowRef* ref = calloc(1, sizeof(BorrowRef));
    if (!ref) {
        pthread_rwlock_unlock(&obj->rwlock);
        return NULL;
    }
    ref->target = obj;
    ref->remembered_gen = obj->generation;
    ref->source_desc = source_desc;
    pthread_rwlock_unlock(&obj->rwlock);
    return ref;
}

/* O(1) validity check - IPGE generation comparison */
int borrow_is_valid(BorrowRef* ref) {
    if (!ref) return 0;
    /* IPGE mode: use ipge_target */
    if (ref->ipge_target) {
        /* Compare remembered generation with current generation */
        return ref->remembered_gen == ref->ipge_target->generation &&
               ref->ipge_target->generation != 0;
    }
    /* Legacy GenObj mode */
    if (!ref->target) return 0;
    pthread_rwlock_rdlock(&ref->target->rwlock);
    int valid = ref->remembered_gen == ref->target->generation &&
                ref->target->generation != 0;
    pthread_rwlock_unlock(&ref->target->rwlock);
    return valid;
}

static void* borrow_deref(BorrowRef* ref) {
    if (!ref || !ref->target) {
        fprintf(stderr, "borrow: null reference\n");
        return NULL;
    }
    pthread_rwlock_rdlock(&ref->target->rwlock);
    if (ref->remembered_gen != ref->target->generation) {
        pthread_rwlock_unlock(&ref->target->rwlock);
        fprintf(stderr, "borrow: use-after-free detected [created at: %s]\n",
                ref->source_desc ? ref->source_desc : "unknown");
        return NULL;
    }
    void* data = ref->target->data;
    pthread_rwlock_unlock(&ref->target->rwlock);
    return data;
}

/* IPGE-based borrowed reference creation */
BorrowRef* borrow_create(Obj* obj, const char* source_desc) {
    if (!obj || IS_IMMEDIATE(obj)) return NULL;

    /* Create a BorrowRef that stores the IPGE generation snapshot */
    BorrowRef* ref = calloc(1, sizeof(BorrowRef));
    if (!ref) return NULL;

    /* IPGE mode: store actual Obj* and snapshot its generation */
    ref->ipge_target = obj;
    ref->remembered_gen = obj->generation;
    ref->source_desc = source_desc;
    ref->target = NULL;  /* Not using legacy GenObj mode */
    return ref;
}

void borrow_invalidate_obj(Obj* obj) {
    if (!obj || IS_IMMEDIATE(obj)) return;
    /* IPGE: Evolve generation to invalidate all borrowed refs */
    obj->generation = ipge_evolve(obj->generation);
}

/* Closure support for safe lambda captures */
static GenClosure* genclosure_new(BorrowRef** captures, int count, void* (*func)(void*), void* ctx) {
    GenClosure* c = calloc(1, sizeof(GenClosure));
    if (!c) return NULL;
    c->captures = captures;
    c->capture_count = count;
    c->func = func;
    c->ctx = ctx;
    return c;
}

int genclosure_validate(GenClosure* c) {
    if (!c) return 0;
    for (int i = 0; i < c->capture_count; i++) {
        if (!borrow_is_valid(c->captures[i])) {
            return 0;
        }
    }
    return 1;
}

static void* genclosure_call(GenClosure* c) {
    if (!genclosure_validate(c)) {
        fprintf(stderr, "genclosure: invalid capture detected\n");
        return NULL;
    }
    return c->func ? c->func(c->ctx) : NULL;
}


/* ========== Closure Runtime ========== */
typedef Obj* (*ClosureFn)(Obj** captures, Obj** args, int arg_count);

struct Closure {
    ClosureFn fn;
    Obj** captures;
    BorrowRef** capture_refs;
    int capture_count;
    int arity;
};

Obj* mk_closure(ClosureFn fn, Obj** captures, BorrowRef** refs, int count, int arity) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->mark = 1;
    x->scc_id = -1;
    x->is_pair = 0;
    x->scan_tag = 0;
    x->tag = TAG_CLOSURE;
    x->generation = _next_generation();

    Closure* c = calloc(1, sizeof(Closure));
    if (!c) {
        free(x);
        return NULL;
    }
    c->fn = fn;
    c->capture_count = count;
    c->arity = arity;

    /* Copy captures array - the passed array may be stack-allocated */
    if (count > 0 && captures) {
        c->captures = malloc(count * sizeof(Obj*));
        if (!c->captures) {
            free(c);
            free(x);
            return NULL;
        }
        memcpy(c->captures, captures, count * sizeof(Obj*));

        if (refs) {
            c->capture_refs = malloc(count * sizeof(BorrowRef*));
            if (c->capture_refs) {
                memcpy(c->capture_refs, refs, count * sizeof(BorrowRef*));
            }
        } else {
            c->capture_refs = NULL;
        }
    } else {
        c->captures = NULL;
        c->capture_refs = NULL;
    }

    for (int i = 0; i < count; i++) {
        if (c->captures && c->captures[i]) inc_ref(c->captures[i]);
    }

    x->ptr = c;
    return x;
}

void closure_release(Closure* c) {
    if (!c) return;
    for (int i = 0; i < c->capture_count; i++) {
        if (c->captures && c->captures[i]) {
            dec_ref(c->captures[i]);
        }
        if (c->capture_refs && c->capture_refs[i]) {
            borrow_release(c->capture_refs[i]);
        }
    }
    free(c->captures);
    free(c->capture_refs);
    free(c);
}

int closure_validate(Closure* c) {
    if (!c || !c->capture_refs) return 1;
    for (int i = 0; i < c->capture_count; i++) {
        if (c->capture_refs[i] && !borrow_is_valid(c->capture_refs[i])) {
            return 0;
        }
    }
    return 1;
}

Obj* call_closure(Obj* clos, Obj** args, int arg_count) {
    if (!clos || clos->tag != TAG_CLOSURE) {
        fprintf(stderr, "call_closure: not a closure\n");
        return NULL;
    }
    Closure* c = (Closure*)clos->ptr;
    if (!c) return NULL;
    if (c->arity >= 0 && arg_count != c->arity) {
        fprintf(stderr, "call_closure: arity mismatch (expected %d, got %d)\n", c->arity, arg_count);
        return NULL;
    }
    if (!closure_validate(c)) {
        fprintf(stderr, "call_closure: invalid capture detected\n");
        return NULL;
    }
    return c->fn ? c->fn(c->captures, args, arg_count) : NULL;
}


/* ========== Constraint References (v0.5.0) ========== */
/* Assertion-based safety for complex patterns */
/* Thread-safe via pthread mutex (C99 + POSIX) */

/* Enable debug mode with -DCONSTRAINT_DEBUG */
#ifndef CONSTRAINT_DEBUG
#define CONSTRAINT_DEBUG 0
#endif

#define MAX_CONSTRAINT_SOURCES 16

typedef struct ConstraintObj {
    void* data;
    void (*destructor)(void*);
    const char* owner;
    int constraint_count;
    int freed;
    pthread_mutex_t mutex;
#if CONSTRAINT_DEBUG
    const char* sources[MAX_CONSTRAINT_SOURCES];
    int source_count;
#endif
} ConstraintObj;

typedef struct ConstraintRef {
    ConstraintObj* target;
    const char* source;
    int released;
    pthread_mutex_t mutex;
} ConstraintRef;

static ConstraintObj* constraint_alloc(void* data, void (*destructor)(void*), const char* owner) {
    ConstraintObj* obj = calloc(1, sizeof(ConstraintObj));
    if (!obj) return NULL;
    obj->data = data;
    obj->destructor = destructor;
    obj->owner = owner;
    obj->constraint_count = 0;
    obj->freed = 0;
    pthread_mutex_init(&obj->mutex, NULL);
#if CONSTRAINT_DEBUG
    obj->source_count = 0;
#endif
    return obj;
}

static ConstraintRef* constraint_add(ConstraintObj* obj, const char* source) {
    if (!obj) return NULL;
    pthread_mutex_lock(&obj->mutex);
    if (obj->freed) {
        pthread_mutex_unlock(&obj->mutex);
        return NULL;
    }
    ConstraintRef* ref = calloc(1, sizeof(ConstraintRef));
    if (!ref) {
        pthread_mutex_unlock(&obj->mutex);
        return NULL;
    }
    ref->target = obj;
    ref->source = source;
    ref->released = 0;
    pthread_mutex_init(&ref->mutex, NULL);
    obj->constraint_count++;
#if CONSTRAINT_DEBUG
    if (obj->source_count < MAX_CONSTRAINT_SOURCES) {
        obj->sources[obj->source_count++] = source;
    }
#endif
    pthread_mutex_unlock(&obj->mutex);
    return ref;
}

/* O(1) release with mutex protection */
int constraint_release(ConstraintRef* ref) {
    if (!ref || !ref->target) return -1;
    pthread_mutex_lock(&ref->mutex);
    if (ref->released) {
        pthread_mutex_unlock(&ref->mutex);
        return -1;  /* Already released */
    }
    ref->released = 1;
    pthread_mutex_unlock(&ref->mutex);

    pthread_mutex_lock(&ref->target->mutex);
    ref->target->constraint_count--;
    pthread_mutex_unlock(&ref->target->mutex);
    return 0;
}

int constraint_free(ConstraintObj* obj) {
    if (!obj) return -1;
    pthread_mutex_lock(&obj->mutex);
    if (obj->freed) {
        pthread_mutex_unlock(&obj->mutex);
        fprintf(stderr, "constraint: double free [owner: %s]\n", obj->owner ? obj->owner : "unknown");
        return -1;
    }
    if (obj->constraint_count > 0) {
        pthread_mutex_unlock(&obj->mutex);
        fprintf(stderr, "constraint violation: cannot free [owner: %s] with %d active constraints\n",
                obj->owner ? obj->owner : "unknown", obj->constraint_count);
#ifdef CONSTRAINT_ASSERT
        abort();
#endif
        return -1;
    }
    obj->freed = 1;
    if (obj->destructor && obj->data) {
        obj->destructor(obj->data);
    }
    obj->data = NULL;
    pthread_mutex_unlock(&obj->mutex);
    pthread_mutex_destroy(&obj->mutex);
    return 0;
}

int constraint_is_valid(ConstraintRef* ref) {
    if (!ref || !ref->target) return 0;
    pthread_mutex_lock(&ref->mutex);
    int rel = ref->released;
    pthread_mutex_unlock(&ref->mutex);
    if (rel) return 0;

    pthread_mutex_lock(&ref->target->mutex);
    int freed = ref->target->freed;
    pthread_mutex_unlock(&ref->target->mutex);
    return !freed;
}

static void* constraint_deref(ConstraintRef* ref) {
    if (!constraint_is_valid(ref)) {
        fprintf(stderr, "constraint: invalid dereference\n");
        return NULL;
    }
    return ref->target->data;
}

/* Arithmetic Operations - with unboxed integer support */

/* Check if boxed value is a float (immediates are never float) */
int num_is_float(Obj* x) {
    if (!x || IS_IMMEDIATE(x)) return 0;
    return x->tag == TAG_FLOAT;
}

/* Convert to double - handles both boxed and immediate */
double num_to_double(Obj* x) {
    if (!x) return 0.0;
    if (IS_IMMEDIATE(x)) return (double)IMMEDIATE_VALUE(x);
    if (x->tag == TAG_FLOAT) return x->f;
    return (double)x->i;
}

/* Fast path: both args are immediate integers -> immediate result */
Obj* add(Obj* a, Obj* b) {
    /* Fast path: both immediate integers */
    if (IS_IMMEDIATE(a) && IS_IMMEDIATE(b)) {
        return mk_int_unboxed(IMMEDIATE_VALUE(a) + IMMEDIATE_VALUE(b));
    }
    /* Handle NULL */
    if (!a && !b) return mk_int_unboxed(0);
    if (!a) return b;
    if (!b) return a;
    /* Float path */
    if (num_is_float(a) || num_is_float(b)) {
        return mk_float(num_to_double(a) + num_to_double(b));
    }
    /* Mixed: one immediate, one boxed int */
    return mk_int_unboxed(obj_to_int(a) + obj_to_int(b));
}

Obj* sub(Obj* a, Obj* b) {
    if (IS_IMMEDIATE(a) && IS_IMMEDIATE(b)) {
        return mk_int_unboxed(IMMEDIATE_VALUE(a) - IMMEDIATE_VALUE(b));
    }
    if (!a && !b) return mk_int_unboxed(0);
    if (!a) return mk_int_unboxed(-obj_to_int(b));
    if (!b) return a;
    if (num_is_float(a) || num_is_float(b)) {
        return mk_float(num_to_double(a) - num_to_double(b));
    }
    return mk_int_unboxed(obj_to_int(a) - obj_to_int(b));
}

Obj* mul(Obj* a, Obj* b) {
    if (IS_IMMEDIATE(a) && IS_IMMEDIATE(b)) {
        return mk_int_unboxed(IMMEDIATE_VALUE(a) * IMMEDIATE_VALUE(b));
    }
    if (!a || !b) return mk_int_unboxed(0);
    if (num_is_float(a) || num_is_float(b)) {
        return mk_float(num_to_double(a) * num_to_double(b));
    }
    return mk_int_unboxed(obj_to_int(a) * obj_to_int(b));
}

Obj* div_op(Obj* a, Obj* b) {
    if (IS_IMMEDIATE(a) && IS_IMMEDIATE(b)) {
        long bv = IMMEDIATE_VALUE(b);
        if (bv == 0) return mk_int_unboxed(0);
        return mk_int_unboxed(IMMEDIATE_VALUE(a) / bv);
    }
    if (!a || !b) return mk_int_unboxed(0);
    if (num_is_float(a) || num_is_float(b)) {
        double denom = num_to_double(b);
        if (denom == 0.0) return mk_float(0.0);
        return mk_float(num_to_double(a) / denom);
    }
    long bv = obj_to_int(b);
    if (bv == 0) return mk_int_unboxed(0);
    return mk_int_unboxed(obj_to_int(a) / bv);
}

Obj* mod_op(Obj* a, Obj* b) {
    if (IS_IMMEDIATE(a) && IS_IMMEDIATE(b)) {
        long bv = IMMEDIATE_VALUE(b);
        if (bv == 0) return mk_int_unboxed(0);
        return mk_int_unboxed(IMMEDIATE_VALUE(a) % bv);
    }
    long bv = obj_to_int(b);
    if (!a || !b || bv == 0) return mk_int_unboxed(0);
    return mk_int_unboxed(obj_to_int(a) % bv);
}

/* Comparison Operations - with unboxed integer support */
Obj* eq_op(Obj* a, Obj* b) {
    /* Fast path: both immediate */
    if (IS_IMMEDIATE(a) && IS_IMMEDIATE(b)) {
        return mk_int_unboxed(a == b ? 1 : 0);
    }
    if (!a && !b) return mk_int_unboxed(1);
    if (!a || !b) return mk_int_unboxed(0);
    if (num_is_float(a) || num_is_float(b)) {
        return mk_int_unboxed(num_to_double(a) == num_to_double(b) ? 1 : 0);
    }
    return mk_int_unboxed(obj_to_int(a) == obj_to_int(b) ? 1 : 0);
}

Obj* lt_op(Obj* a, Obj* b) {
    if (IS_IMMEDIATE(a) && IS_IMMEDIATE(b)) {
        return mk_int_unboxed(IMMEDIATE_VALUE(a) < IMMEDIATE_VALUE(b) ? 1 : 0);
    }
    if (!a || !b) return mk_int_unboxed(0);
    if (num_is_float(a) || num_is_float(b)) {
        return mk_int_unboxed(num_to_double(a) < num_to_double(b) ? 1 : 0);
    }
    return mk_int_unboxed(obj_to_int(a) < obj_to_int(b) ? 1 : 0);
}

Obj* gt_op(Obj* a, Obj* b) {
    if (IS_IMMEDIATE(a) && IS_IMMEDIATE(b)) {
        return mk_int_unboxed(IMMEDIATE_VALUE(a) > IMMEDIATE_VALUE(b) ? 1 : 0);
    }
    if (!a || !b) return mk_int_unboxed(0);
    if (num_is_float(a) || num_is_float(b)) {
        return mk_int_unboxed(num_to_double(a) > num_to_double(b) ? 1 : 0);
    }
    return mk_int_unboxed(obj_to_int(a) > obj_to_int(b) ? 1 : 0);
}

Obj* le_op(Obj* a, Obj* b) {
    if (IS_IMMEDIATE(a) && IS_IMMEDIATE(b)) {
        return mk_int_unboxed(IMMEDIATE_VALUE(a) <= IMMEDIATE_VALUE(b) ? 1 : 0);
    }
    if (!a || !b) return mk_int_unboxed(0);
    if (num_is_float(a) || num_is_float(b)) {
        return mk_int_unboxed(num_to_double(a) <= num_to_double(b) ? 1 : 0);
    }
    return mk_int_unboxed(obj_to_int(a) <= obj_to_int(b) ? 1 : 0);
}

Obj* ge_op(Obj* a, Obj* b) {
    if (IS_IMMEDIATE(a) && IS_IMMEDIATE(b)) {
        return mk_int_unboxed(IMMEDIATE_VALUE(a) >= IMMEDIATE_VALUE(b) ? 1 : 0);
    }
    if (!a || !b) return mk_int_unboxed(0);
    if (num_is_float(a) || num_is_float(b)) {
        return mk_int_unboxed(num_to_double(a) >= num_to_double(b) ? 1 : 0);
    }
    return mk_int_unboxed(obj_to_int(a) >= obj_to_int(b) ? 1 : 0);
}

Obj* not_op(Obj* a) {
    if (!a) return mk_int_unboxed(1);
    if (IS_IMMEDIATE(a)) {
        return mk_int_unboxed(IMMEDIATE_VALUE(a) == 0 ? 1 : 0);
    }
    return mk_int_unboxed(a->i == 0 ? 1 : 0);
}

int is_truthy(Obj* x) {
    if (!x) return 0;
    /* Fast path: immediate integer */
    if (IS_IMMEDIATE(x)) {
        return IMMEDIATE_VALUE(x) != 0;
    }
    switch (x->tag) {
    case TAG_INT:
        return x->i != 0;
    case TAG_FLOAT:
        return x->f != 0.0;
    case TAG_CHAR:
        return x->i != 0;
    default:
        return 1;
    }
}

/* Primitive aliases for compiler */
Obj* prim_add(Obj* a, Obj* b) { return add(a, b); }
Obj* prim_sub(Obj* a, Obj* b) { return sub(a, b); }
Obj* prim_mul(Obj* a, Obj* b) { return mul(a, b); }
Obj* prim_div(Obj* a, Obj* b) { return div_op(a, b); }
Obj* prim_mod(Obj* a, Obj* b) { return mod_op(a, b); }
Obj* prim_lt(Obj* a, Obj* b) { return lt_op(a, b); }
Obj* prim_gt(Obj* a, Obj* b) { return gt_op(a, b); }
Obj* prim_le(Obj* a, Obj* b) { return le_op(a, b); }
Obj* prim_ge(Obj* a, Obj* b) { return ge_op(a, b); }
Obj* prim_eq(Obj* a, Obj* b) { return eq_op(a, b); }
Obj* prim_not(Obj* a) { return not_op(a); }
Obj* prim_abs(Obj* a) {
    if (!a) return mk_int_unboxed(0);
    if (IS_IMMEDIATE(a)) {
        long v = IMMEDIATE_VALUE(a);
        return mk_int_unboxed(v < 0 ? -v : v);
    }
    if (a->tag == TAG_FLOAT) return mk_float(a->f < 0 ? -a->f : a->f);
    return mk_int_unboxed(a->i < 0 ? -a->i : a->i);
}

/* Type predicate wrappers - return Obj* for uniformity */
Obj* prim_null(Obj* x) { return mk_int(x == NULL ? 1 : 0); }
Obj* prim_pair(Obj* x) { return mk_int(x && x->tag == TAG_PAIR ? 1 : 0); }
Obj* prim_int(Obj* x) { return mk_int(x && x->tag == TAG_INT ? 1 : 0); }
Obj* prim_float(Obj* x) { return mk_int(x && x->tag == TAG_FLOAT ? 1 : 0); }
Obj* prim_char(Obj* x) { return mk_int(x && x->tag == TAG_CHAR ? 1 : 0); }
Obj* prim_sym(Obj* x) { return mk_int(x && x->tag == TAG_SYM ? 1 : 0); }

/* I/O Primitives */
void print_obj(Obj* x);  /* forward declaration */

/* Check if a list is a string (all chars) */
int is_string_list(Obj* xs) {
    while (xs && xs->tag == TAG_PAIR) {
        if (!xs->a || xs->a->tag != TAG_CHAR) return 0;
        xs = xs->b;
    }
    return xs == NULL; /* Must be proper list */
}

/* Print a string list as a quoted string */
void print_string(Obj* xs) {
    while (xs && xs->tag == TAG_PAIR) {
        if (xs->a && xs->a->tag == TAG_CHAR) {
            printf("%c", (char)xs->a->i);
        }
        xs = xs->b;
    }
}

void print_list(Obj* xs) {
    /* Check if this is a string (list of chars) */
    if (is_string_list(xs)) {
        print_string(xs);
        return;
    }
    printf("(");
    int first = 1;
    while (xs && xs->tag == TAG_PAIR) {
        if (!first) printf(" ");
        first = 0;
        print_obj(xs->a);
        xs = xs->b;
    }
    if (xs) {
        printf(" . ");
        print_obj(xs);
    }
    printf(")");
}

void print_obj(Obj* x) {
    if (!x) {
        printf("()");
        return;
    }
    switch (x->tag) {
    case TAG_INT:
        printf("%ld", x->i);
        break;
    case TAG_FLOAT:
        printf("%g", x->f);
        break;
    case TAG_CHAR:
        printf("%c", (char)x->i);
        break;
    case TAG_SYM:
        printf("%s", x->ptr ? (char*)x->ptr : "nil");
        break;
    case TAG_PAIR:
        print_list(x);
        break;
    case TAG_CLOSURE:
        printf("#<closure>");
        break;
    case TAG_BOX:
        printf("#<box>");
        break;
    case TAG_CHANNEL:
        printf("#<channel>");
        break;
    default:
        printf("#<object:%d>", x->tag);
        break;
    }
}

Obj* prim_display(Obj* x) {
    print_obj(x);
    return NULL;
}

Obj* prim_print(Obj* x) {
    print_obj(x);
    printf("\n");
    return NULL;
}

Obj* prim_newline(void) {
    printf("\n");
    return NULL;
}

/* Type introspection */
Obj* ctr_tag(Obj* x) {
    if (!x) return mk_sym("nil");
    switch (x->tag) {
    case TAG_INT: return mk_sym("int");
    case TAG_FLOAT: return mk_sym("float");
    case TAG_CHAR: return mk_sym("char");
    case TAG_SYM: return mk_sym("sym");
    case TAG_PAIR: return mk_sym("cell");
    case TAG_BOX: return mk_sym("box");
    case TAG_CLOSURE: return mk_sym("closure");
    case TAG_CHANNEL: return mk_sym("channel");
    case TAG_ATOM: return mk_sym("atom");
    case TAG_THREAD: return mk_sym("thread");
    default:
        if (x->tag >= TAG_USER_BASE) return mk_sym("user");
        return mk_sym("unknown");
    }
}

Obj* ctr_arg(Obj* x, Obj* idx) {
    if (!x || !idx) return NULL;
    long i = idx->i;
    switch (x->tag) {
    case TAG_PAIR:
        if (i == 0) return x->a;
        if (i == 1) return x->b;
        return NULL;
    case TAG_BOX:
        if (i == 0) return (Obj*)x->ptr;
        return NULL;
    default:
        return NULL;
    }
}

/* Character primitives */
Obj* char_to_int(Obj* c) {
    if (!c || c->tag != TAG_CHAR) return mk_int(0);
    return mk_int(c->i);
}

Obj* int_to_char(Obj* n) {
    if (!n) return mk_char(0);
    return mk_char((char)(n->tag == TAG_INT ? n->i : (long)n->f));
}

/* Float primitives */
Obj* int_to_float(Obj* n) {
    if (!n) return mk_float(0.0);
    return mk_float((double)(n->tag == TAG_INT ? n->i : n->f));
}

Obj* float_to_int(Obj* f) {
    if (!f) return mk_int(0);
    return mk_int((long)(f->tag == TAG_FLOAT ? f->f : f->i));
}

Obj* prim_floor(Obj* f) {
    if (!f) return mk_int(0);
    if (f->tag == TAG_FLOAT) {
        double v = f->f;
        return mk_float(v >= 0 ? (long)v : (long)v - (v != (long)v ? 1 : 0));
    }
    return mk_int(f->i);
}

Obj* prim_ceil(Obj* f) {
    if (!f) return mk_int(0);
    if (f->tag == TAG_FLOAT) {
        double v = f->f;
        return mk_float(v >= 0 ? (long)v + (v != (long)v ? 1 : 0) : (long)v);
    }
    return mk_int(f->i);
}

/* Higher-order function primitives */
Obj* prim_apply(Obj* fn, Obj* args) {
    if (!fn) return NULL;
    /* Count args */
    int n = 0;
    Obj* p = args;
    while (p && p->tag == TAG_PAIR) { n++; p = p->b; }

    /* Build args array */
    Obj** arr = n > 0 ? malloc(n * sizeof(Obj*)) : NULL;
    p = args;
    for (int i = 0; i < n; i++) {
        arr[i] = p->a;
        p = p->b;
    }

    Obj* result = call_closure(fn, arr, n);
    if (arr) free(arr);
    return result;
}

/* Compose: (compose f g) returns a function that applies g then f */
Obj* compose_wrapper(Obj** captures, Obj** args, int n);

Obj* prim_compose(Obj* f, Obj* g) {
    if (!f || !g) return NULL;
    Obj** caps = malloc(2 * sizeof(Obj*));
    caps[0] = f; inc_ref(f);
    caps[1] = g; inc_ref(g);
    return mk_closure(compose_wrapper, caps, NULL, 2, 1);
}

Obj* compose_wrapper(Obj** captures, Obj** args, int n) {
    if (n < 1 || !captures) return NULL;
    Obj* f = captures[0];
    Obj* g = captures[1];
    /* Apply g first */
    Obj* intermediate = call_closure(g, args, n);
    /* Then apply f */
    Obj* final_args[1] = { intermediate };
    Obj* result = call_closure(f, final_args, 1);
    dec_ref(intermediate);
    return result;
}

/* List Operations */
Obj* car(Obj* p) {
    if (!p || p->tag != TAG_PAIR) return NULL;
    return p->a;
}

Obj* cdr(Obj* p) {
    if (!p || p->tag != TAG_PAIR) return NULL;
    return p->b;
}

/* Aliases for compiler - return owned references */
Obj* obj_car(Obj* p) {
    Obj* r = car(p);
    if (r) inc_ref(r);
    return r;
}
Obj* obj_cdr(Obj* p) {
    Obj* r = cdr(p);
    if (r) inc_ref(r);
    return r;
}

Obj* list_length(Obj* xs) {
    long n = 0;
    while (xs && xs->tag == TAG_PAIR) {
        n++;
        xs = xs->b;
    }
    return mk_int(n);
}

Obj* list_map(Obj* fn, Obj* xs) {
    if (!fn) return NULL;
    Obj* head = NULL;
    Obj* tail = NULL;
    while (xs && xs->tag == TAG_PAIR) {
        Obj* args[1];
        args[0] = xs->a;
        Obj* val = call_closure(fn, args, 1);
        Obj* node = mk_pair(val, NULL);
        if (!head) {
            head = node;
        } else {
            tail->b = node;
        }
        tail = node;
        xs = xs->b;
    }
    return head;
}

Obj* list_fold(Obj* fn, Obj* init, Obj* xs) {
    if (!fn) return init;
    Obj* acc = init;
    while (xs && xs->tag == TAG_PAIR) {
        Obj* args[2];
        args[0] = acc;
        args[1] = xs->a;
        acc = call_closure(fn, args, 2);
        xs = xs->b;
    }
    return acc;
}

Obj* list_append(Obj* a, Obj* b) {
    if (!a || a->tag != TAG_PAIR) return b;
    /* Build a copy of list a, then append b */
    Obj* head = NULL;
    Obj* tail = NULL;
    while (a && a->tag == TAG_PAIR) {
        Obj* node = mk_pair(a->a, NULL);
        if (node->a) inc_ref(node->a);
        if (!head) {
            head = node;
        } else {
            tail->b = node;
        }
        tail = node;
        a = a->b;
    }
    if (tail) {
        tail->b = b;
        if (b) inc_ref(b);
    } else {
        head = b;
        if (b) inc_ref(b);
    }
    return head;
}

Obj* list_filter(Obj* fn, Obj* xs) {
    if (!fn) return NULL;
    Obj* head = NULL;
    Obj* tail = NULL;
    while (xs && xs->tag == TAG_PAIR) {
        Obj* args[1];
        args[0] = xs->a;
        Obj* keep = call_closure(fn, args, 1);
        /* Non-NULL and non-zero means keep */
        int should_keep = keep && (keep->tag != TAG_INT || keep->i != 0);
        if (keep) dec_ref(keep);
        if (should_keep) {
            Obj* node = mk_pair(xs->a, NULL);
            if (xs->a) inc_ref(xs->a);
            if (!head) {
                head = node;
            } else {
                tail->b = node;
            }
            tail = node;
        }
        xs = xs->b;
    }
    return head;
}

Obj* list_reverse(Obj* xs) {
    Obj* result = NULL;
    while (xs && xs->tag == TAG_PAIR) {
        Obj* node = mk_pair(xs->a, result);
        if (xs->a) inc_ref(xs->a);
        result = node;
        xs = xs->b;
    }
    return result;
}

Obj* list_foldr(Obj* fn, Obj* init, Obj* xs) {
    /* foldr f init (x:xs) = f x (foldr f init xs) */
    if (!fn) return init;
    if (!xs || xs->tag != TAG_PAIR) return init;
    /* Recursive approach - build stack then apply */
    Obj* reversed = list_reverse(xs);
    Obj* acc = init;
    if (init) inc_ref(init);
    Obj* p = reversed;
    while (p && p->tag == TAG_PAIR) {
        Obj* args[2];
        args[0] = p->a;
        args[1] = acc;
        Obj* new_acc = call_closure(fn, args, 2);
        dec_ref(acc);
        acc = new_acc;
        p = p->b;
    }
    dec_ref(reversed);
    return acc;
}

/* Generic Scanners (debug/verification only) */
void scan_obj(Obj* x) {
    if (!x || x->scan_tag) return;
    x->scan_tag = 1;
    switch (x->tag) {
    case TAG_PAIR:
        scan_obj(x->a);
        scan_obj(x->b);
        break;
    case TAG_BOX:
        scan_obj((Obj*)x->ptr);
        break;
    case TAG_CLOSURE: {
        Closure* c = (Closure*)x->ptr;
        if (c && c->captures) {
            for (int i = 0; i < c->capture_count; i++) {
                scan_obj(c->captures[i]);
            }
        }
        break;
    }
    default:
        if (x->tag >= TAG_USER_BASE) {
            scan_user_obj(x);
        }
        break;
    }
}

void clear_marks_obj(Obj* x) {
    if (!x || !x->scan_tag) return;
    x->scan_tag = 0;
    switch (x->tag) {
    case TAG_PAIR:
        clear_marks_obj(x->a);
        clear_marks_obj(x->b);
        break;
    case TAG_BOX:
        clear_marks_obj((Obj*)x->ptr);
        break;
    case TAG_CLOSURE: {
        Closure* c = (Closure*)x->ptr;
        if (c && c->captures) {
            for (int i = 0; i < c->capture_count; i++) {
                clear_marks_obj(c->captures[i]);
            }
        }
        break;
    }
    default:
        if (x->tag >= TAG_USER_BASE) {
            clear_marks_user_obj(x);
        }
        break;
    }
}

/* Perceus Reuse Analysis Runtime */
Obj* try_reuse(Obj* old, size_t size) {
    if (old && old->mark == 1) {
        /* Reusing: release children if this was a pair */
        if (old->is_pair) {
            if (old->a) dec_ref(old->a);
            if (old->b) dec_ref(old->b);
            old->a = NULL;
            old->b = NULL;
        }
        return old;
    }
    if (old) dec_ref(old);
    return malloc(size);
}

Obj* reuse_as_int(Obj* old, long value) {
    Obj* obj = try_reuse(old, sizeof(Obj));
    if (!obj) return NULL;
    obj->mark = 1;
    obj->scc_id = -1;
    obj->is_pair = 0;
    obj->scan_tag = 0;
    obj->tag = TAG_INT;
    obj->generation = _next_generation();
    obj->i = value;
    return obj;
}

Obj* reuse_as_pair(Obj* old, Obj* a, Obj* b) {
    Obj* obj = try_reuse(old, sizeof(Obj));
    if (!obj) return NULL;
    obj->mark = 1;
    obj->scc_id = -1;
    obj->is_pair = 1;
    obj->scan_tag = 0;
    obj->tag = TAG_PAIR;
    obj->generation = _next_generation();
    obj->a = a;
    obj->b = b;
    return obj;
}

/* ========== Concurrency Runtime ========== */
/* Thread-safe ownership management with message passing */

#include <pthread.h>
#include <stdbool.h>

/* === Atomic Reference Counting for Shared Objects === */

/* Atomic increment */
static inline void atomic_inc_ref(Obj* obj) {
    if (obj) {
        __atomic_add_fetch(&obj->mark, 1, __ATOMIC_SEQ_CST);
    }
}

/* Atomic decrement with potential free */
static inline void atomic_dec_ref(Obj* obj) {
    if (obj) {
        if (__atomic_sub_fetch(&obj->mark, 1, __ATOMIC_SEQ_CST) == 0) {
            free_obj(obj);
        }
    }
}

/* Try to acquire unique ownership (for in-place updates) */
static inline bool try_acquire_unique(Obj* obj) {
    if (!obj) return false;
    int expected = 1;
    return __atomic_compare_exchange_n(&obj->mark, &expected, 1,
        false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

/* === Channel Operations with Ownership Transfer === */

typedef struct Channel Channel;
struct Channel {
    Obj** buffer;       /* Ring buffer for values */
    int capacity;       /* Buffer size (0 = unbuffered) */
    int count;          /* Current number of items */
    int read_pos;       /* Read position */
    int write_pos;      /* Write position */
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    bool closed;
};

/* Create a channel */
Obj* make_channel(int capacity) {
    Channel* ch = malloc(sizeof(Channel));
    if (!ch) return NULL;

    ch->capacity = capacity > 0 ? capacity : 1;
    ch->buffer = malloc(sizeof(Obj*) * ch->capacity);
    ch->count = 0;
    ch->read_pos = 0;
    ch->write_pos = 0;
    ch->closed = false;

    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);

    Obj* obj = malloc(sizeof(Obj));
    if (!obj) {
        free(ch->buffer);
        free(ch);
        return NULL;
    }
    obj->mark = 1;
    obj->scc_id = -1;
    obj->is_pair = 0;
    obj->scan_tag = 0;
    obj->tag = TAG_CHANNEL;
    obj->generation = _next_generation();
    obj->ptr = ch;

    return obj;
}

static Channel* channel_payload(Obj* ch_obj) {
    if (!ch_obj || ch_obj->tag != TAG_CHANNEL) return NULL;
    return (Channel*)ch_obj->ptr;
}

/* Send value through channel (TRANSFERS OWNERSHIP) */
/* After send, caller should NOT use or free the value */
static bool channel_send(Obj* ch_obj, Obj* value) {
    Channel* ch = channel_payload(ch_obj);
    if (!ch || ch->closed) return false;

    pthread_mutex_lock(&ch->lock);

    /* Wait for space */
    while (ch->count >= ch->capacity && !ch->closed) {
        pthread_cond_wait(&ch->not_full, &ch->lock);
    }

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return false;
    }

    /* Transfer ownership: no inc_ref needed, sender gives up value */
    ch->buffer[ch->write_pos] = value;
    ch->write_pos = (ch->write_pos + 1) % ch->capacity;
    ch->count++;

    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->lock);

    return true;
}

/* Receive value from channel (RECEIVES OWNERSHIP) */
/* Caller becomes owner, must free when done */
Obj* channel_recv(Obj* ch_obj) {
    Channel* ch = channel_payload(ch_obj);
    if (!ch) return NULL;

    pthread_mutex_lock(&ch->lock);

    /* Wait for data */
    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait(&ch->not_empty, &ch->lock);
    }

    if (ch->count == 0) {
        /* Channel closed and empty */
        pthread_mutex_unlock(&ch->lock);
        return NULL;
    }

    /* Transfer ownership: receiver now owns the value */
    Obj* value = ch->buffer[ch->read_pos];
    ch->read_pos = (ch->read_pos + 1) % ch->capacity;
    ch->count--;

    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->lock);

    return value;  /* Caller owns this */
}

/* Close a channel */
void channel_close(Obj* ch_obj) {
    Channel* ch = channel_payload(ch_obj);
    if (!ch) return;

    pthread_mutex_lock(&ch->lock);
    ch->closed = true;
    pthread_cond_broadcast(&ch->not_empty);
    pthread_cond_broadcast(&ch->not_full);
    pthread_mutex_unlock(&ch->lock);
}

/* Free a channel */
void free_channel_obj(Obj* ch_obj) {
    Channel* ch = channel_payload(ch_obj);
    if (!ch) return;

    /* Free any remaining values (ownership cleanup) */
    while (ch->count > 0) {
        Obj* val = ch->buffer[ch->read_pos];
        if (val) dec_ref(val);
        ch->read_pos = (ch->read_pos + 1) % ch->capacity;
        ch->count--;
    }

    free(ch->buffer);
    pthread_mutex_destroy(&ch->lock);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
    free(ch);
    if (ch_obj) {
        ch_obj->ptr = NULL;
    }
}

/* === Goroutine Spawning === */

typedef struct GoroutineArg GoroutineArg;
struct GoroutineArg {
    Obj* closure;       /* The closure to run */
    Obj** captured;     /* Captured variables (with inc_ref'd ownership) */
    int captured_count;
};

/* Thread entry point */
static void* goroutine_entry(void* arg) {
    GoroutineArg* ga = (GoroutineArg*)arg;

    /* Call the closure */
    if (ga->closure) {
        call_closure(ga->closure, NULL, 0);
        dec_ref(ga->closure);
    }

    /* Release captured variables */
    for (int i = 0; i < ga->captured_count; i++) {
        if (ga->captured[i]) {
            atomic_dec_ref(ga->captured[i]);
        }
    }

    free(ga->captured);
    free(ga);
    return NULL;
}

/* Spawn a goroutine */
void spawn_goroutine(Obj* closure, Obj** captured, int count) {
    GoroutineArg* arg = malloc(sizeof(GoroutineArg));
    if (!arg) return;

    /* Transfer ownership of closure to goroutine */
    arg->closure = closure;
    inc_ref(closure);

    /* Copy and increment captured variables (they become shared) */
    arg->captured_count = count;
    arg->captured = malloc(sizeof(Obj*) * count);
    for (int i = 0; i < count; i++) {
        arg->captured[i] = captured[i];
        if (captured[i]) {
            atomic_inc_ref(captured[i]);
        }
    }

    pthread_t thread;
    pthread_create(&thread, NULL, goroutine_entry, arg);
    pthread_detach(thread);  /* Don't wait for completion */
}

/* === Atom (Atomic Reference) Operations === */

typedef struct Atom Atom;
struct Atom {
    Obj* value;
    pthread_mutex_t lock;
};

/* Create an atom */
Obj* make_atom(Obj* initial) {
    Atom* a = malloc(sizeof(Atom));
    if (!a) return NULL;

    a->value = initial;
    if (initial) inc_ref(initial);
    pthread_mutex_init(&a->lock, NULL);

    Obj* obj = malloc(sizeof(Obj));
    if (!obj) {
        if (initial) dec_ref(initial);
        free(a);
        return NULL;
    }
    obj->mark = 1;
    obj->scc_id = -1;
    obj->is_pair = 0;
    obj->scan_tag = 0;
    obj->tag = TAG_ATOM;
    obj->generation = _next_generation();
    obj->ptr = a;

    return obj;
}

static Atom* atom_payload(Obj* atom_obj) {
    if (!atom_obj || atom_obj->tag != TAG_ATOM) return NULL;
    return (Atom*)atom_obj->ptr;
}

/* Dereference atom (read current value) */
Obj* atom_deref(Obj* atom_obj) {
    Atom* a = atom_payload(atom_obj);
    if (!a) return NULL;

    pthread_mutex_lock(&a->lock);
    Obj* val = a->value;
    if (val) inc_ref(val);
    pthread_mutex_unlock(&a->lock);

    return val;
}

/* Reset atom to new value */
Obj* atom_reset(Obj* atom_obj, Obj* new_val) {
    Atom* a = atom_payload(atom_obj);
    if (!a) return NULL;

    pthread_mutex_lock(&a->lock);
    Obj* old = a->value;
    a->value = new_val;
    if (new_val) inc_ref(new_val);
    if (old) dec_ref(old);
    pthread_mutex_unlock(&a->lock);

    if (new_val) inc_ref(new_val);
    return new_val;
}

/* Swap atom value using function */
Obj* atom_swap(Obj* atom_obj, Obj* fn) {
    Atom* a = atom_payload(atom_obj);
    if (!a || !fn) return NULL;

    pthread_mutex_lock(&a->lock);
    Obj* old = a->value;

    /* Call function with current value */
    Obj* args[1] = { old };
    Obj* new_val = call_closure(fn, args, 1);

    a->value = new_val;
    if (old) dec_ref(old);

    pthread_mutex_unlock(&a->lock);

    if (new_val) inc_ref(new_val);
    return new_val;
}

/* Compare-and-set */
Obj* atom_cas(Obj* atom_obj, Obj* expected, Obj* new_val) {
    Atom* a = atom_payload(atom_obj);
    if (!a) return mk_int(0);

    pthread_mutex_lock(&a->lock);
    bool success = (a->value == expected);
    if (success) {
        Obj* old = a->value;
        a->value = new_val;
        if (new_val) inc_ref(new_val);
        if (old) dec_ref(old);
    }
    pthread_mutex_unlock(&a->lock);

    return mk_int(success ? 1 : 0);
}

/* Free atom */
void free_atom_obj(Obj* atom_obj) {
    Atom* a = atom_payload(atom_obj);
    if (!a) return;

    if (a->value) dec_ref(a->value);
    pthread_mutex_destroy(&a->lock);
    free(a);
    if (atom_obj) atom_obj->ptr = NULL;
}

/* === Thread Operations (with join support) === */

typedef struct ThreadHandle ThreadHandle;
struct ThreadHandle {
    pthread_t thread;
    Obj* result;
    bool done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

typedef struct ThreadArg ThreadArg;
struct ThreadArg {
    Obj* closure;
    ThreadHandle* handle;
};

/* Thread entry point */
static void* thread_entry(void* arg) {
    ThreadArg* ta = (ThreadArg*)arg;

    /* Call the closure */
    Obj* result = NULL;
    if (ta->closure) {
        result = call_closure(ta->closure, NULL, 0);
        dec_ref(ta->closure);
    }

    /* Store result and signal completion */
    pthread_mutex_lock(&ta->handle->lock);
    ta->handle->result = result;
    ta->handle->done = true;
    pthread_cond_signal(&ta->handle->cond);
    pthread_mutex_unlock(&ta->handle->lock);

    free(ta);
    return NULL;
}

/* Spawn a thread (returns handle for joining) */
Obj* spawn_thread(Obj* closure) {
    ThreadHandle* h = malloc(sizeof(ThreadHandle));
    if (!h) return NULL;

    h->result = NULL;
    h->done = false;
    pthread_mutex_init(&h->lock, NULL);
    pthread_cond_init(&h->cond, NULL);

    ThreadArg* arg = malloc(sizeof(ThreadArg));
    if (!arg) {
        free(h);
        return NULL;
    }

    arg->closure = closure;
    if (closure) inc_ref(closure);
    arg->handle = h;

    pthread_create(&h->thread, NULL, thread_entry, arg);

    /* Wrap handle in Obj */
    Obj* obj = malloc(sizeof(Obj));
    if (!obj) return NULL;
    obj->mark = 1;
    obj->scc_id = -1;
    obj->is_pair = 0;
    obj->scan_tag = 0;
    obj->tag = TAG_THREAD;
    obj->generation = _next_generation();
    obj->ptr = h;

    return obj;
}

static ThreadHandle* thread_payload(Obj* thread_obj) {
    if (!thread_obj || thread_obj->tag != TAG_THREAD) return NULL;
    return (ThreadHandle*)thread_obj->ptr;
}

/* Join thread and get result */
Obj* thread_join(Obj* thread_obj) {
    ThreadHandle* h = thread_payload(thread_obj);
    if (!h) return NULL;

    pthread_mutex_lock(&h->lock);
    while (!h->done) {
        pthread_cond_wait(&h->cond, &h->lock);
    }
    Obj* result = h->result;
    if (result) inc_ref(result);
    pthread_mutex_unlock(&h->lock);

    return result;
}

/* Free thread handle */
void free_thread_obj(Obj* thread_obj) {
    ThreadHandle* h = thread_payload(thread_obj);
    if (!h) return;

    pthread_join(h->thread, NULL);
    if (h->result) dec_ref(h->result);
    pthread_mutex_destroy(&h->lock);
    pthread_cond_destroy(&h->cond);
    free(h);
    if (thread_obj) thread_obj->ptr = NULL;
}

/* ========== Destination-Passing Style Runtime ========== */
/* Pre-allocate destination and pass it down */

/* Map with destination passing - writes result into dest */
void map_into(Obj** dest, Obj* (*f)(Obj*), Obj* xs) {
    if (!xs || !dest) return;

    Obj** current = dest;
    while (xs && xs->is_pair) {
        Obj* mapped = f(xs->a);

        /* Allocate directly into destination chain */
        *current = mk_pair(mapped, NULL);
        current = &((*current)->b);

        xs = xs->b;
    }
    *current = NULL;  /* Terminate list */
}

/* Filter with destination passing */
void filter_into(Obj** dest, int (*pred)(Obj*), Obj* xs) {
    if (!xs || !dest) return;

    Obj** current = dest;
    while (xs && xs->is_pair) {
        if (pred(xs->a)) {
            *current = mk_pair(xs->a, NULL);
            inc_ref(xs->a);
            current = &((*current)->b);
        }
        xs = xs->b;
    }
    *current = NULL;
}

/* Append with destination passing - avoids intermediate allocations */
void append_into(Obj** dest, Obj* xs, Obj* ys) {
    if (!dest) return;

    Obj** current = dest;

    /* Copy first list */
    while (xs && xs->is_pair) {
        *current = mk_pair(xs->a, NULL);
        inc_ref(xs->a);
        current = &((*current)->b);
        xs = xs->b;
    }

    /* Append second list (can share structure) */
    *current = ys;
    if (ys) inc_ref(ys);
}

/* Type-Aware Scanner for List */
/* Note: ASAP uses compile-time free injection, not runtime GC */
void scan_List(Obj* x) {
    if (!x || x->scan_tag) return;
    x->scan_tag = 1;
    if (x->is_pair) {
        scan_List(x->a);
        scan_List(x->b);
    }
}

void clear_marks_List(Obj* x) {
    if (!x || !x->scan_tag) return;
    x->scan_tag = 0;
    if (x->is_pair) {
        clear_marks_List(x->a);
        clear_marks_List(x->b);
    }
}

/* Type-Aware Release Functions */
/* Automatically skip weak fields (back-edges) to prevent double-free */

void release_user_obj(Obj* obj) { (void)obj; }

/* Type Constructors */

/* Field Accessors */
/* Getters for weak fields do not increment reference count */
/* Setters for strong fields manage reference counts automatically */

void scan_user_obj(Obj* obj) { (void)obj; }
void clear_marks_user_obj(Obj* obj) { (void)obj; }

/* ========== Exception Handling Runtime ========== */
/* ASAP-compatible exception handling with deterministic cleanup */
/* Uses setjmp/longjmp for non-local control flow */

#include <setjmp.h>

/* Exception context for stack unwinding */
typedef struct ExceptionContext ExceptionContext;
struct ExceptionContext {
    jmp_buf jump_buffer;
    Obj* exception_value;
    ExceptionContext* parent;
    void** cleanup_stack;
    int cleanup_count;
    int cleanup_capacity;
};

/* Thread-local exception context stack */
static __thread ExceptionContext* g_exception_ctx = NULL;

/* Push a new exception context (entering try block) */
static ExceptionContext* exception_push(void) {
    ExceptionContext* ctx = malloc(sizeof(ExceptionContext));
    if (!ctx) return NULL;
    ctx->exception_value = NULL;
    ctx->parent = g_exception_ctx;
    ctx->cleanup_stack = NULL;
    ctx->cleanup_count = 0;
    ctx->cleanup_capacity = 0;
    g_exception_ctx = ctx;
    return ctx;
}

/* Pop exception context (exiting try block normally) */
void exception_pop(void) {
    if (!g_exception_ctx) return;
    ExceptionContext* ctx = g_exception_ctx;
    g_exception_ctx = ctx->parent;
    free(ctx->cleanup_stack);
    free(ctx);
}

/* Register a value for cleanup during unwinding */
void exception_register_cleanup(void* ptr) {
    if (!g_exception_ctx) return;
    ExceptionContext* ctx = g_exception_ctx;
    if (ctx->cleanup_count >= ctx->cleanup_capacity) {
        int new_cap = ctx->cleanup_capacity == 0 ? 8 : ctx->cleanup_capacity * 2;
        void** new_stack = realloc(ctx->cleanup_stack, new_cap * sizeof(void*));
        if (!new_stack) return;
        ctx->cleanup_stack = new_stack;
        ctx->cleanup_capacity = new_cap;
    }
    ctx->cleanup_stack[ctx->cleanup_count++] = ptr;
}

/* Unregister a value (it was freed normally) */
void exception_unregister_cleanup(void* ptr) {
    if (!g_exception_ctx) return;
    ExceptionContext* ctx = g_exception_ctx;
    for (int i = ctx->cleanup_count - 1; i >= 0; i--) {
        if (ctx->cleanup_stack[i] == ptr) {
            /* Remove by shifting */
            for (int j = i; j < ctx->cleanup_count - 1; j++) {
                ctx->cleanup_stack[j] = ctx->cleanup_stack[j + 1];
            }
            ctx->cleanup_count--;
            return;
        }
    }
}

/* Perform cleanup during unwinding */
void exception_cleanup(ExceptionContext* ctx) {
    if (!ctx) return;
    /* Free in reverse order (LIFO) */
    for (int i = ctx->cleanup_count - 1; i >= 0; i--) {
        if (ctx->cleanup_stack[i]) {
            dec_ref((Obj*)ctx->cleanup_stack[i]);
        }
    }
    ctx->cleanup_count = 0;
}

/* Throw an exception */
void exception_throw(Obj* value) {
    if (!g_exception_ctx) {
        /* No handler - print and abort */
        fprintf(stderr, "Uncaught exception: ");
        if (value && value->tag == TAG_ERROR && value->ptr) {
            fprintf(stderr, "%s\n", (char*)value->ptr);
        } else {
            fprintf(stderr, "<unknown>\n");
        }
        abort();
    }

    ExceptionContext* ctx = g_exception_ctx;
    ctx->exception_value = value;
    if (value) inc_ref(value);

    /* Cleanup current context */
    exception_cleanup(ctx);

    /* Jump to handler */
    longjmp(ctx->jump_buffer, 1);
}

/* Get the current exception value */
Obj* exception_get_value(void) {
    if (!g_exception_ctx) return NULL;
    return g_exception_ctx->exception_value;
}

/* Macros for try/catch */
#define TRY_BEGIN() do { \
    ExceptionContext* _exc_ctx = exception_push(); \
    if (_exc_ctx && setjmp(_exc_ctx->jump_buffer) == 0) {

#define TRY_CATCH(var) \
    exception_pop(); \
    } else { \
    Obj* var = exception_get_value(); \
    exception_pop();

#define TRY_END() \
    } \
} while(0)

/* Register allocation for cleanup */
#define REGISTER_CLEANUP(ptr) exception_register_cleanup((void*)(ptr))

/* Unregister after normal free */
#define UNREGISTER_CLEANUP(ptr) exception_unregister_cleanup((void*)(ptr))

/* Throw macro */
#define THROW(value) exception_throw((Obj*)(value))


