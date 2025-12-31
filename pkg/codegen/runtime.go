package codegen

import (
	"fmt"
	"io"
	"strings"
)

// RuntimeGenerator generates the C99 runtime code
type RuntimeGenerator struct {
	w        io.Writer
	registry *TypeRegistry
}

// NewRuntimeGenerator creates a new runtime generator
func NewRuntimeGenerator(w io.Writer, registry *TypeRegistry) *RuntimeGenerator {
	return &RuntimeGenerator{w: w, registry: registry}
}

func (g *RuntimeGenerator) emit(format string, args ...interface{}) {
	fmt.Fprintf(g.w, format, args...)
}

// GenerateHeader generates the main runtime header
func (g *RuntimeGenerator) GenerateHeader() {
	g.emit(`/* Purple + ASAP C Compiler Output */
/* Primary Strategy: ASAP + ISMM 2024 (Deeply Immutable Cycles) */
/* Generated ANSI C99 Code */

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

/* Forward declarations */
void invalidate_weak_refs_for(void* target);

/* Core object type */
typedef struct Obj {
    int mark;           /* Reference count or mark bit */
    int scc_id;         /* SCC identifier (-1 if not in SCC) */
    int is_pair;        /* 1 if pair, 0 if int */
    unsigned int scan_tag;  /* Scanner mark (separate from RC) */
    union {
        long i;
        struct { struct Obj *a, *b; };
    };
} Obj;

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

static int is_stack_obj(Obj* x) {
    uintptr_t px = (uintptr_t)x;
    uintptr_t start = (uintptr_t)&STACK_POOL[0];
    uintptr_t end = (uintptr_t)&STACK_POOL[STACK_POOL_SIZE];
    return px >= start && px < end;
}

static int is_nil(Obj* x) {
    return x == NULL;
}

`)
}

// GenerateConstructors generates object constructors
func (g *RuntimeGenerator) GenerateConstructors() {
	g.emit(`/* Object Constructors */
Obj* mk_int(long i) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->mark = 1;
    x->scc_id = -1;
    x->is_pair = 0;
    x->scan_tag = 0;
    x->i = i;
    return x;
}

Obj* mk_float(double f) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->mark = 1;
    x->scc_id = -1;
    x->is_pair = 0;
    x->scan_tag = 0;
    /* Store float in the same space as int (union) */
    *((double*)&x->i) = f;
    return x;
}

double get_float(Obj* x) {
    if (!x) return 0.0;
    return *((double*)&x->i);
}

Obj* mk_pair(Obj* a, Obj* b) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->mark = 1;
    x->scc_id = -1;
    x->is_pair = 1;
    x->scan_tag = 0;
    x->a = a;
    x->b = b;
    return x;
}

Obj* mk_sym(const char* s) {
    /* For now, symbols are not supported at runtime */
    return NULL;
}

Obj* mk_int_stack(long i) {
    if (STACK_PTR < STACK_POOL_SIZE) {
        Obj* x = &STACK_POOL[STACK_PTR++];
        x->mark = 0;
        x->scc_id = -1;
        x->is_pair = 0;
        x->scan_tag = 0;
        x->i = i;
        return x;
    }
    return mk_int(i);
}

`)
}

// GenerateMemoryManagement generates memory management functions
func (g *RuntimeGenerator) GenerateMemoryManagement() {
	g.emit(`/* Shape-based Deallocation (Ghiya-Hendren analysis) */

/* TREE: Direct free (ASAP) */
void free_tree(Obj* x) {
    if (!x) return;
    if (is_stack_obj(x)) return;
    if (x->is_pair) {
        free_tree(x->a);
        free_tree(x->b);
    }
    invalidate_weak_refs_for(x);
    free(x);
}

/* DAG: Reference counting */
void dec_ref(Obj* x) {
    if (!x) return;
    if (is_stack_obj(x)) return;
    if (x->mark < 0) return;
    x->mark--;
    if (x->mark <= 0) {
        if (x->is_pair) {
            dec_ref(x->a);
            dec_ref(x->b);
        }
        invalidate_weak_refs_for(x);
        free(x);
    }
}

void inc_ref(Obj* x) {
    if (!x) return;
    if (is_stack_obj(x)) return;
    if (x->mark < 0) { x->mark = 1; return; }
    x->mark++;
}

/* RC Optimization: Direct free for proven-unique references (Lobster-style) */
/* When compile-time analysis proves a reference is the only one, skip RC check */
void free_unique(Obj* x) {
    if (!x) return;
    if (is_stack_obj(x)) return;
    /* Proven unique at compile time - no RC check needed */
    if (x->is_pair) {
        /* Children might not be unique, use dec_ref for safety */
        dec_ref(x->a);
        dec_ref(x->b);
    }
    invalidate_weak_refs_for(x);
    free(x);
}

/* RC Optimization: Borrowed reference - no RC ops needed */
/* For parameters and temporary references that don't transfer ownership */
#define BORROWED_REF(x) (x)  /* No-op marker for documentation */

/* Free list operations */
void free_obj(Obj* x) {
    if (!x) return;
    if (is_stack_obj(x)) return;
    if (x->mark < 0) return;
    x->mark = -1;
    FreeNode* n = malloc(sizeof(FreeNode));
    if (!n) {
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

`)
}

// GenerateArithmetic generates arithmetic operations
func (g *RuntimeGenerator) GenerateArithmetic() {
	g.emit(`/* Arithmetic Operations */
Obj* add(Obj* a, Obj* b) {
    if (!a || !b) return mk_int(0);
    return mk_int(a->i + b->i);
}

Obj* sub(Obj* a, Obj* b) {
    if (!a || !b) return mk_int(0);
    return mk_int(a->i - b->i);
}

Obj* mul(Obj* a, Obj* b) {
    if (!a || !b) return mk_int(0);
    return mk_int(a->i * b->i);
}

Obj* div_op(Obj* a, Obj* b) {
    if (!a || !b || b->i == 0) return mk_int(0);
    return mk_int(a->i / b->i);
}

Obj* mod_op(Obj* a, Obj* b) {
    if (!a || !b || b->i == 0) return mk_int(0);
    return mk_int(a->i %% b->i);
}

`)
}

// GenerateComparison generates comparison operations
func (g *RuntimeGenerator) GenerateComparison() {
	g.emit(`/* Comparison Operations */
Obj* eq_op(Obj* a, Obj* b) {
    if (!a && !b) return mk_int(1);
    if (!a || !b) return mk_int(0);
    return mk_int(a->i == b->i ? 1 : 0);
}

Obj* lt_op(Obj* a, Obj* b) {
    if (!a || !b) return mk_int(0);
    return mk_int(a->i < b->i ? 1 : 0);
}

Obj* gt_op(Obj* a, Obj* b) {
    if (!a || !b) return mk_int(0);
    return mk_int(a->i > b->i ? 1 : 0);
}

Obj* le_op(Obj* a, Obj* b) {
    if (!a || !b) return mk_int(0);
    return mk_int(a->i <= b->i ? 1 : 0);
}

Obj* ge_op(Obj* a, Obj* b) {
    if (!a || !b) return mk_int(0);
    return mk_int(a->i >= b->i ? 1 : 0);
}

Obj* not_op(Obj* a) {
    if (!a) return mk_int(1);
    return mk_int(a->i == 0 ? 1 : 0);
}

`)
}

// GenerateWeakRefs generates weak reference support
// NOTE: WeakRef is INTERNAL to the runtime - users should not use it directly.
// The compiler automatically handles back-edges through type analysis.
func (g *RuntimeGenerator) GenerateWeakRefs() {
	g.emit(`/* Internal Weak Reference Support */
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

static void _invalidate_weak(_InternalWeakRef* w) {
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

`)
}

// GenerateScanner generates scanner functions for a type
func (g *RuntimeGenerator) GenerateScanner(typeName string, isList bool) {
	g.emit(`/* Type-Aware Scanner for %s */
/* Note: ASAP uses compile-time free injection, not runtime GC */
void scan_%s(Obj* x) {
    if (!x || x->scan_tag) return;
    x->scan_tag = 1;
`, typeName, typeName)

	if isList {
		g.emit(`    if (x->is_pair) {
        scan_%s(x->a);
        scan_%s(x->b);
    }
`, typeName, typeName)
	}

	g.emit(`}

void clear_marks_%s(Obj* x) {
    if (!x || !x->scan_tag) return;
    x->scan_tag = 0;
`, typeName)

	if isList {
		g.emit(`    if (x->is_pair) {
        clear_marks_%s(x->a);
        clear_marks_%s(x->b);
    }
`, typeName, typeName)
	}

	g.emit(`}

`)
}

// GenerateArenaRuntime generates arena allocation runtime with externals support
func (g *RuntimeGenerator) GenerateArenaRuntime() {
	g.emit(`/* Arena Allocator (Bulk Allocation/Deallocation) */
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
    x->a = car;
    x->b = cdr;
    return x;
}

`)
}

// GeneratePerceusRuntime generates Perceus reuse analysis runtime
func (g *RuntimeGenerator) GeneratePerceusRuntime() {
	g.emit(`/* Perceus Reuse Analysis Runtime */
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
    obj->a = a;
    obj->b = b;
    return obj;
}

`)
}

// GenerateSCCRuntime generates SCC-based reference counting runtime (ISMM 2024)
func (g *RuntimeGenerator) GenerateSCCRuntime() {
	g.emit(`/* SCC-Based Reference Counting (ISMM 2024) */
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

static void tarjan_free(TarjanState* s) {
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
static void tarjan_strongconnect(Obj* v, TarjanState* state,
                                  void (*on_scc)(Obj**, int)) {
    if (!v || !state) return;

    /* Use scan_tag field to store Tarjan index for this node */
    int v_idx = state->current_index++;
    v->scan_tag = (unsigned int)v_idx;
    state->index[v_idx %% state->capacity] = v_idx;
    state->lowlink[v_idx %% state->capacity] = v_idx;
    state->stack[state->stack_top++] = v;
    state->on_stack[v_idx %% state->capacity] = 1;

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
                int w_low = state->lowlink[w->scan_tag %% state->capacity];
                if (w_low < state->lowlink[v_idx %% state->capacity]) {
                    state->lowlink[v_idx %% state->capacity] = w_low;
                }
            } else if (state->on_stack[w_idx %% state->capacity]) {
                /* w is on stack */
                if (state->index[w_idx %% state->capacity] < state->lowlink[v_idx %% state->capacity]) {
                    state->lowlink[v_idx %% state->capacity] = state->index[w_idx %% state->capacity];
                }
            }
        }
    }

    /* Check if v is root of SCC */
    if (state->lowlink[v_idx %% state->capacity] == state->index[v_idx %% state->capacity]) {
        Obj* scc_members[256];
        int scc_size = 0;
        Obj* w;
        do {
            w = state->stack[--state->stack_top];
            int w_idx = (int)w->scan_tag;
            state->on_stack[w_idx %% state->capacity] = 0;
            if (scc_size < 256) {
                scc_members[scc_size++] = w;
            }
        } while (w != v && state->stack_top > 0);

        if (scc_size > 1 && on_scc) {
            on_scc(scc_members, scc_size);
        }
    }
}

static void on_scc_found(Obj** members, int count) {
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

`)
}

// GenerateDeferredRuntime generates deferred reference counting runtime
func (g *RuntimeGenerator) GenerateDeferredRuntime() {
	g.emit(`/* Deferred Reference Counting */
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

`)
}

// GenerateSymmetricRuntime generates symmetric reference counting runtime
// This is the preferred method for handling unbroken cycles - more memory efficient than arenas
func (g *RuntimeGenerator) GenerateSymmetricRuntime() {
	g.emit(`/* Symmetric Reference Counting (Hybrid Memory Strategy) */
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
static void sym_check_free(SymObj* obj);

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

static void sym_obj_add_ref(SymObj* obj, SymObj* target) {
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

static void sym_check_free(SymObj* obj) {
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

`)
}

// GenerateUserTypes generates struct definitions for user-defined types
func (g *RuntimeGenerator) GenerateUserTypes() {
	if g.registry == nil {
		return
	}

	g.emit(`/* User-Defined Types */
/* Generated from deftype declarations with automatic back-edge detection */

`)

	for name, typeDef := range g.registry.Types {
		// Skip built-in types
		if name == "Pair" || name == "List" || name == "Tree" || name == "Obj" {
			continue
		}

		g.emit("/* Type: %s */\n", name)
		g.emit("typedef struct %s {\n", name)

		for _, field := range typeDef.Fields {
			cType := g.fieldTypeToCType(field)
			strengthComment := ""
			switch field.Strength {
			case FieldWeak:
				strengthComment = " /* WEAK - auto-detected back-edge */"
			case FieldStrong:
				strengthComment = " /* STRONG */"
			}
			g.emit("    %s %s;%s\n", cType, field.Name, strengthComment)
		}

		g.emit("} %s;\n\n", name)
	}
}

// fieldTypeToCType converts a field type to a C type
func (g *RuntimeGenerator) fieldTypeToCType(field TypeField) string {
	if !field.IsScannable {
		// Primitive type
		switch field.Type {
		case "int", "i32":
			return "int"
		case "long", "i64":
			return "long"
		case "float", "f32":
			return "float"
		case "double", "f64":
			return "double"
		case "bool", "boolean":
			return "int"
		case "char":
			return "char"
		default:
			return "int"
		}
	}
	// Pointer type
	return fmt.Sprintf("struct %s*", field.Type)
}

// GenerateTypeReleaseFunctions generates release functions for user-defined types
func (g *RuntimeGenerator) GenerateTypeReleaseFunctions() {
	if g.registry == nil {
		return
	}

	g.emit(`/* Type-Aware Release Functions */
/* Automatically skip weak fields (back-edges) to prevent double-free */

`)

	for name, typeDef := range g.registry.Types {
		// Skip built-in types
		if name == "Pair" || name == "List" || name == "Tree" || name == "Obj" {
			continue
		}

		g.emit("void release_%s(%s* x) {\n", name, name)
		g.emit("    if (!x) return;\n")

		// Process each field
		for _, field := range typeDef.Fields {
			if !field.IsScannable {
				continue // Skip primitive fields
			}

			switch field.Strength {
			case FieldStrong:
				// Strong field - decrement reference
				g.emit("    if (x->%s) dec_ref((Obj*)x->%s); /* strong */\n", field.Name, field.Name)
			case FieldWeak:
				// Weak field - DO NOT decrement, just comment
				g.emit("    /* x->%s: weak back-edge - skip to prevent cycle */\n", field.Name)
			}
		}

		g.emit("    invalidate_weak_refs_for(x);\n")
		g.emit("    free(x);\n")
		g.emit("}\n\n")
	}
}

// GenerateTypeConstructors generates constructor functions for user-defined types
func (g *RuntimeGenerator) GenerateTypeConstructors() {
	if g.registry == nil {
		return
	}

	g.emit(`/* Type Constructors */

`)

	for name, typeDef := range g.registry.Types {
		// Skip built-in types
		if name == "Pair" || name == "List" || name == "Tree" || name == "Obj" {
			continue
		}

		// Generate parameter list
		var params []string
		for _, field := range typeDef.Fields {
			cType := g.fieldTypeToCType(field)
			params = append(params, fmt.Sprintf("%s %s", cType, field.Name))
		}
		paramStr := strings.Join(params, ", ")
		if paramStr == "" {
			paramStr = "void"
		}

		g.emit("%s* mk_%s(%s) {\n", name, name, paramStr)
		g.emit("    %s* x = malloc(sizeof(%s));\n", name, name)
		g.emit("    if (!x) return NULL;\n")

		for _, field := range typeDef.Fields {
			if field.IsScannable && field.Strength == FieldStrong {
				g.emit("    if (%s) inc_ref((Obj*)%s); /* strong ref */\n", field.Name, field.Name)
			}
			g.emit("    x->%s = %s;\n", field.Name, field.Name)
		}

		g.emit("    return x;\n")
		g.emit("}\n\n")
	}
}

// GenerateFieldAccessors generates getter/setter functions that respect field strength
func (g *RuntimeGenerator) GenerateFieldAccessors() {
	if g.registry == nil {
		return
	}

	g.emit(`/* Field Accessors */
/* Getters for weak fields do not increment reference count */
/* Setters for strong fields manage reference counts automatically */

`)

	for name, typeDef := range g.registry.Types {
		// Skip built-in types
		if name == "Pair" || name == "List" || name == "Tree" || name == "Obj" {
			continue
		}

		for _, field := range typeDef.Fields {
			if !field.IsScannable {
				continue // Skip primitive fields for now
			}

			cType := g.fieldTypeToCType(field)

			// Getter
			if field.Strength == FieldWeak {
				g.emit("/* %s.%s getter - WEAK field, no ref count increment */\n", name, field.Name)
				g.emit("%s get_%s_%s(%s* x) {\n", cType, name, field.Name, name)
				g.emit("    return x ? x->%s : NULL;\n", field.Name)
				g.emit("}\n\n")
			} else {
				g.emit("/* %s.%s getter - STRONG field */\n", name, field.Name)
				g.emit("%s get_%s_%s(%s* x) {\n", cType, name, field.Name, name)
				g.emit("    if (!x) return NULL;\n")
				g.emit("    if (x->%s) inc_ref((Obj*)x->%s);\n", field.Name, field.Name)
				g.emit("    return x->%s;\n", field.Name)
				g.emit("}\n\n")
			}

			// Setter
			g.emit("/* %s.%s setter */\n", name, field.Name)
			g.emit("void set_%s_%s(%s* x, %s value) {\n", name, field.Name, name, cType)
			g.emit("    if (!x) return;\n")

			if field.Strength == FieldStrong {
				g.emit("    if (value) inc_ref((Obj*)value); /* new value */\n")
				g.emit("    if (x->%s) dec_ref((Obj*)x->%s); /* old value */\n", field.Name, field.Name)
			} else {
				g.emit("    /* weak field - no ref count management */\n")
			}

			g.emit("    x->%s = value;\n", field.Name)
			g.emit("}\n\n")
		}
	}
}

// GenerateAll generates the complete runtime
func (g *RuntimeGenerator) GenerateAll() {
	g.GenerateHeader()
	g.GenerateWeakRefs()
	g.GenerateConstructors()
	g.GenerateMemoryManagement()
	g.GenerateArenaRuntime()
	g.GenerateSCCRuntime()
	g.GenerateDeferredRuntime()
	g.GenerateSymmetricRuntime()
	g.GenerateArithmetic()
	g.GenerateComparison()
	g.GeneratePerceusRuntime()
	g.GenerateScanner("List", true)
	g.GenerateUserTypes()
	g.GenerateTypeReleaseFunctions()
	g.GenerateTypeConstructors()
	g.GenerateFieldAccessors()
}

// GenerateRuntime generates the complete C99 runtime to a string
func GenerateRuntime(registry *TypeRegistry) string {
	var sb strings.Builder
	gen := NewRuntimeGenerator(&sb, registry)
	gen.GenerateAll()
	return sb.String()
}
