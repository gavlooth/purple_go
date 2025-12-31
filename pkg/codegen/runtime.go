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
func (g *RuntimeGenerator) GenerateWeakRefs() {
	g.emit(`/* Weak Reference Support */
typedef struct WeakRef {
    void* target;
    int alive;
} WeakRef;

typedef struct WeakRefNode {
    WeakRef* ref;
    struct WeakRefNode* next;
} WeakRefNode;

WeakRefNode* WEAK_REF_HEAD = NULL;

WeakRef* mk_weak_ref(void* target) {
    WeakRef* w = malloc(sizeof(WeakRef));
    if (!w) return NULL;
    w->target = target;
    w->alive = 1;
    WeakRefNode* node = malloc(sizeof(WeakRefNode));
    if (!node) { free(w); return NULL; }
    node->ref = w;
    node->next = WEAK_REF_HEAD;
    WEAK_REF_HEAD = node;
    return w;
}

void* deref_weak(WeakRef* w) {
    if (w && w->alive) return w->target;
    return NULL;
}

void invalidate_weak(WeakRef* w) {
    if (w) w->alive = 0;
}

void invalidate_weak_refs_for(void* target) {
    WeakRefNode* n = WEAK_REF_HEAD;
    while (n) {
        WeakRef* obj = n->ref;
        if (obj->target == target) {
            invalidate_weak(obj);
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

// GenerateArenaRuntime generates arena allocation runtime
func (g *RuntimeGenerator) GenerateArenaRuntime() {
	g.emit(`/* Arena Allocator (Bulk Allocation/Deallocation) */
/* For cyclic data that doesn't escape function scope */

#define ARENA_BLOCK_SIZE 4096

typedef struct ArenaBlock {
    char data[ARENA_BLOCK_SIZE];
    size_t used;
    struct ArenaBlock* next;
} ArenaBlock;

typedef struct Arena {
    ArenaBlock* head;
    ArenaBlock* current;
} Arena;

static ArenaBlock* arena_new_block(void) {
    ArenaBlock* block = malloc(sizeof(ArenaBlock));
    if (!block) return NULL;
    block->used = 0;
    block->next = NULL;
    return block;
}

Arena* arena_create(void) {
    Arena* arena = malloc(sizeof(Arena));
    if (!arena) return NULL;
    arena->head = arena_new_block();
    arena->current = arena->head;
    return arena;
}

void arena_destroy(Arena* arena) {
    if (!arena) return;
    ArenaBlock* block = arena->head;
    while (block) {
        ArenaBlock* next = block->next;
        free(block);
        block = next;
    }
    free(arena);
}

static void* arena_alloc(Arena* arena, size_t size) {
    if (!arena || !arena->current) return NULL;

    /* Align to 8 bytes */
    size = (size + 7) & ~7;

    if (arena->current->used + size > ARENA_BLOCK_SIZE) {
        ArenaBlock* new_block = arena_new_block();
        if (!new_block) return NULL;
        arena->current->next = new_block;
        arena->current = new_block;
    }

    void* ptr = &arena->current->data[arena->current->used];
    arena->current->used += size;
    return ptr;
}

Obj* arena_mk_int(Arena* arena, long i) {
    Obj* x = arena_alloc(arena, sizeof(Obj));
    if (!x) return NULL;
    x->mark = -2;  /* Special mark for arena-allocated */
    x->scc_id = -1;
    x->is_pair = 0;
    x->scan_tag = 0;
    x->i = i;
    return x;
}

Obj* arena_mk_pair(Arena* arena, Obj* a, Obj* b) {
    Obj* x = arena_alloc(arena, sizeof(Obj));
    if (!x) return NULL;
    x->mark = -2;  /* Special mark for arena-allocated */
    x->scc_id = -1;
    x->is_pair = 1;
    x->scan_tag = 0;
    x->a = a;
    x->b = b;
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

// GenerateAll generates the complete runtime
func (g *RuntimeGenerator) GenerateAll() {
	g.GenerateHeader()
	g.GenerateWeakRefs()
	g.GenerateConstructors()
	g.GenerateMemoryManagement()
	g.GenerateArenaRuntime()
	g.GenerateArithmetic()
	g.GenerateComparison()
	g.GeneratePerceusRuntime()
	g.GenerateScanner("List", true)
}

// GenerateRuntime generates the complete C99 runtime to a string
func GenerateRuntime(registry *TypeRegistry) string {
	var sb strings.Builder
	gen := NewRuntimeGenerator(&sb, registry)
	gen.GenerateAll()
	return sb.String()
}
