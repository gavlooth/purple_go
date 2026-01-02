package codegen

import (
	"fmt"
	"io"
	"strings"

	"purple_go/pkg/analysis"
)

// RuntimeConfig specifies what runtime features to emit based on analysis results.
// The compiler auto-decides - no user flags needed.
type RuntimeConfig struct {
	// Core (always true)
	EmitCore bool // Obj struct, tagged pointers, constructors, free_obj/free_tree

	// Analysis-driven features
	EmitClosures    bool // Lambdas/closures present
	EmitBorrowRef   bool // Closures capture variables (UAF safety)
	EmitPool        bool // Non-escaping temporaries detected
	EmitNaNBox      bool // Float operations present
	EmitPerceus     bool // Reuse opportunities found
	EmitSCC         bool // Frozen cycles detected
	EmitSymmetric   bool // Mutable cycles detected
	EmitConcurrency bool // go/chan primitives used
	EmitDPS         bool // DPS-eligible functions found
	EmitArena       bool // Arena-suitable patterns found
	EmitRegions     bool // Region-based safety needed
	EmitConstraints bool // Debug constraint refs (usually false)
	EmitDeferred    bool // Deferred RC needed (rare)
}

// NewRuntimeConfigFromAnalysis creates a RuntimeConfig based on analysis results.
// This is where the compiler auto-decides what runtime features to emit.
func NewRuntimeConfigFromAnalysis(
	hasClosures bool,
	hasCapturedVars bool,
	hasNonEscapingTemps bool,
	hasFloats bool,
	hasReuseOpportunities bool,
	hasCycles bool,
	hasFrozenCycles bool,
	hasConcurrency bool,
	hasDPSEligible bool,
) *RuntimeConfig {
	return &RuntimeConfig{
		EmitCore:        true, // Always
		EmitClosures:    hasClosures,
		EmitBorrowRef:   hasCapturedVars,
		EmitPool:        hasNonEscapingTemps,
		EmitNaNBox:      hasFloats,
		EmitPerceus:     hasReuseOpportunities,
		EmitSCC:         hasFrozenCycles,
		EmitSymmetric:   hasCycles && !hasFrozenCycles,
		EmitConcurrency: hasConcurrency,
		EmitDPS:         hasDPSEligible,
		EmitArena:       false, // Opt-in only
		EmitRegions:     false, // Superseded by BorrowRef
		EmitConstraints: false, // Debug only
		EmitDeferred:    false, // Contradicts ASAP
	}
}

// DefaultRuntimeConfig returns config with all features enabled (for compatibility)
func DefaultRuntimeConfig() *RuntimeConfig {
	return &RuntimeConfig{
		EmitCore:        true,
		EmitClosures:    true,
		EmitBorrowRef:   true,
		EmitPool:        true,
		EmitNaNBox:      true,
		EmitPerceus:     true,
		EmitSCC:         true,
		EmitSymmetric:   true,
		EmitConcurrency: true,
		EmitDPS:         true,
		EmitArena:       true,
		EmitRegions:     true,
		EmitConstraints: true,
		EmitDeferred:    true,
	}
}

// RuntimeGenerator generates the C99 runtime code
type RuntimeGenerator struct {
	w        io.Writer
	registry *TypeRegistry
	config   *RuntimeConfig
}

// NewRuntimeGenerator creates a new runtime generator with default config (all features)
func NewRuntimeGenerator(w io.Writer, registry *TypeRegistry) *RuntimeGenerator {
	return &RuntimeGenerator{w: w, registry: registry, config: DefaultRuntimeConfig()}
}

// NewRuntimeGeneratorWithConfig creates a runtime generator with specific config
func NewRuntimeGeneratorWithConfig(w io.Writer, registry *TypeRegistry, config *RuntimeConfig) *RuntimeGenerator {
	return &RuntimeGenerator{w: w, registry: registry, config: config}
}

func (g *RuntimeGenerator) emit(format string, args ...interface{}) {
	if len(args) == 0 {
		io.WriteString(g.w, format)
	} else {
		fmt.Fprintf(g.w, format, args...)
	}
}

// emitRaw writes raw C code without any format interpretation
// Use this for C code containing % (modulo, printf formats, etc.)
func (g *RuntimeGenerator) emitRaw(s string) {
	io.WriteString(g.w, s)
}

// GenerateHeader generates the main runtime header
func (g *RuntimeGenerator) GenerateHeader() {
    g.emit(`/* Purple + ASAP C Compiler Output */
/* Primary Strategy: ASAP (compile-time free insertion) */
/* Generated ANSI C99 + POSIX Code */
/* Go is NOT involved in runtime - pure C execution */

/* Enable POSIX.1-2001 for pthread_rwlock_t and related functions */
#define _POSIX_C_SOURCE 200112L

/* Runtime features auto-selected by compiler based on program analysis */
/* No user flags needed - compiler emits only what's required */

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct Obj Obj;
typedef struct GenObj GenObj;
typedef struct BorrowRef BorrowRef;
typedef struct Closure Closure;
void invalidate_weak_refs_for(void* target);
static BorrowRef* borrow_create(Obj* obj, const char* source_desc);
static void borrow_invalidate_obj(Obj* obj);
static Obj* call_closure(Obj* clos, Obj** args, int arg_count);
static void closure_release(Closure* c);
static void release_user_obj(Obj* x);
static void free_channel_obj(Obj* ch_obj);
static void scan_user_obj(Obj* obj);
static void clear_marks_user_obj(Obj* obj);

/* Reference counting forward declarations */
static void inc_ref(Obj* x);
static void dec_ref(Obj* x);
static void free_obj(Obj* x);

/* Primitive operations forward declarations */
static Obj* prim_add(Obj* a, Obj* b);
static Obj* prim_sub(Obj* a, Obj* b);
static Obj* prim_mul(Obj* a, Obj* b);
static Obj* prim_div(Obj* a, Obj* b);
static Obj* prim_mod(Obj* a, Obj* b);
static Obj* prim_lt(Obj* a, Obj* b);
static Obj* prim_gt(Obj* a, Obj* b);
static Obj* prim_le(Obj* a, Obj* b);
static Obj* prim_ge(Obj* a, Obj* b);
static Obj* prim_eq(Obj* a, Obj* b);
static Obj* prim_not(Obj* a);
static Obj* prim_abs(Obj* a);
static Obj* prim_null(Obj* x);
static Obj* prim_pair(Obj* x);
static Obj* prim_int(Obj* x);
static Obj* prim_float(Obj* x);
static Obj* prim_char(Obj* x);
static Obj* prim_sym(Obj* x);
static Obj* obj_car(Obj* p);
static Obj* obj_cdr(Obj* p);

/* I/O primitive forward declarations */
static Obj* prim_display(Obj* x);
static Obj* prim_print(Obj* x);
static Obj* prim_newline(void);

/* List operation forward declarations */
static Obj* list_append(Obj* a, Obj* b);
static Obj* list_filter(Obj* fn, Obj* xs);
static Obj* list_reverse(Obj* xs);

/* Type introspection forward declarations */
static Obj* ctr_tag(Obj* x);
static Obj* ctr_arg(Obj* x, Obj* idx);

/* Character primitive forward declarations */
static Obj* char_to_int(Obj* c);
static Obj* int_to_char(Obj* n);

/* Float primitive forward declarations */
static Obj* int_to_float(Obj* n);
static Obj* float_to_int(Obj* f);
static Obj* prim_floor(Obj* f);
static Obj* prim_ceil(Obj* f);

/* Higher-order function forward declarations */
static Obj* prim_apply(Obj* fn, Obj* args);
static Obj* prim_compose(Obj* f, Obj* g);

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
#define IMM_TAG_MASK     0x7ULL
#define IMM_TAG_PTR      0x0ULL   /* Heap pointer (must be 8-byte aligned) */
#define IMM_TAG_INT      0x1ULL   /* Immediate integer */
#define IMM_TAG_CHAR     0x2ULL   /* Immediate character */
#define IMM_TAG_BOOL     0x3ULL   /* Immediate boolean */

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

#define IS_FALSE(p)          ((p) == PURPLE_FALSE || (p) == NULL)
#define IS_TRUE(p)           ((p) == PURPLE_TRUE)

/* Immediate Characters */
#define MAKE_CHAR_IMM(c)     ((Obj*)(((uintptr_t)(c) << 3) | IMM_TAG_CHAR))
#define CHAR_IMM_VALUE(p)    ((long)(((uintptr_t)(p)) >> 3))

/* Unboxed constructors */
static inline Obj* mk_int_unboxed(long i) { return MAKE_INT_IMM(i); }
static inline Obj* mk_char_unboxed(long c) { return MAKE_CHAR_IMM(c); }
static inline Obj* mk_bool(int b) { return b ? PURPLE_TRUE : PURPLE_FALSE; }

/* IPGE: In-Place Generational Evolution */
#define IPGE_MULTIPLIER  0x5851f42D4C957F2DULL
#define IPGE_INCREMENT   0x1442695040888963ULL

static inline uint64_t ipge_evolve(uint64_t gen) {
    return (gen * IPGE_MULTIPLIER) + IPGE_INCREMENT;
}

static uint64_t _ipge_seed = 0x123456789ABCDEF0ULL;

static inline uint64_t _next_generation(void) {
    _ipge_seed = ipge_evolve(_ipge_seed);
    return _ipge_seed;
}
`)

	if g.registry != nil {
		userTypes := g.registry.GetUserDefinedTypes()
		for i, typeDef := range userTypes {
			g.emit("#define TAG_USER_%s (TAG_USER_BASE + %d)\n", typeDef.Name, i)
		}
	}

	g.emit(`
/* Core object type */
typedef struct Obj {
    uint64_t generation;    /* IPGE generation ID for memory safety */
    int mark;               /* Reference count or mark bit */
    int tag;                /* ObjTag */
    int is_pair;            /* 1 if pair, 0 if not */
    int scc_id;             /* SCC identifier (-1 if not in SCC) */
    unsigned int scan_tag : 31;  /* Scanner mark (separate from RC) */
    unsigned int tethered : 1;   /* Scope tethering bit (Vale-style) */
    union {
        long i;
        double f;
        struct { struct Obj *a, *b; };
        void* ptr;
    };
} Obj;
/* Size: 40 bytes */

/* ========== Scope Tethering (Vale-style) ========== */
/*
 * When borrowing a reference in a scope, mark it "tethered".
 * While tethered, skip generation checks - the object is guaranteed
 * to stay alive for the duration of the scope.
 */

/* Tether an object (on borrow) */
static inline void tether_obj(Obj* obj) {
    if (obj && !IS_IMMEDIATE(obj)) {
        obj->tethered = 1;
    }
}

/* Untether an object (on scope exit) */
static inline void untether_obj(Obj* obj) {
    if (obj && !IS_IMMEDIATE(obj)) {
        obj->tethered = 0;
    }
}

/* Check if object is tethered */
static inline int is_tethered(Obj* obj) {
    if (!obj || IS_IMMEDIATE(obj)) return 0;
    return obj->tethered;
}

/* Fast deref with tether check - skips gen validation if tethered */
static inline Obj* tethered_deref(Obj* obj, uint64_t expected_gen) {
    if (!obj) return NULL;
    if (IS_IMMEDIATE(obj)) return obj;
    if (obj->tethered) return obj;  /* Fast path: skip gen check */
    return (obj->generation == expected_gen) ? obj : NULL;
}

/* Tethered borrowed reference - combines ptr + expected gen */
typedef struct TetheredRef {
    Obj* ptr;
    uint64_t gen;
} TetheredRef;

/* Create tethered reference (tethers the object) */
static inline TetheredRef tether_borrow(Obj* obj) {
    TetheredRef ref = { obj, 0 };
    if (obj && !IS_IMMEDIATE(obj)) {
        ref.gen = obj->generation;
        obj->tethered = 1;
    }
    return ref;
}

/* Release tethered reference (untethers the object) */
static inline void tether_release(TetheredRef ref) {
    if (ref.ptr && !IS_IMMEDIATE(ref.ptr)) {
        ref.ptr->tethered = 0;
    }
}

/* Deref tethered reference - always succeeds while tethered */
static inline Obj* tether_deref(TetheredRef ref) {
    return ref.ptr;  /* No check needed - tethered */
}

/* Safe integer extraction - works for all immediate types */
static inline long obj_to_int(Obj* p) {
    if (IS_IMMEDIATE_INT(p)) return INT_IMM_VALUE(p);
    if (IS_IMMEDIATE_BOOL(p)) return p == PURPLE_TRUE ? 1 : 0;
    if (IS_IMMEDIATE_CHAR(p)) return CHAR_IMM_VALUE(p);
    return p ? p->i : 0;
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
static inline int is_char_val(Obj* p) {
    if (IS_IMMEDIATE_CHAR(p)) return 1;
    return p && p->tag == TAG_CHAR;
}

/* Boolean extraction */
static inline int obj_to_bool(Obj* p) {
    if (IS_IMMEDIATE_BOOL(p)) return p == PURPLE_TRUE;
    if (IS_IMMEDIATE_INT(p)) return INT_IMM_VALUE(p) != 0;
    if (p == NULL) return 0;
    return 1;  /* Non-null boxed values are truthy */
}

/* Character extraction */
static inline long obj_to_char(Obj* p) {
    if (IS_IMMEDIATE_CHAR(p)) return CHAR_IMM_VALUE(p);
    if (p && p->tag == TAG_CHAR) return p->i;
    return 0;
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

static int is_stack_obj(Obj* x) {
    if (!x || IS_IMMEDIATE(x)) return 0;  /* Immediates aren't stack objects */
    uintptr_t px = (uintptr_t)x;
    uintptr_t start = (uintptr_t)&STACK_POOL[0];
    uintptr_t end = (uintptr_t)&STACK_POOL[STACK_POOL_SIZE];
    return px >= start && px < end;
}

static int is_nil(Obj* x) {
    return x == NULL;  /* Immediates are never nil */
}

`)
}

// GenerateConstructors generates object constructors
func (g *RuntimeGenerator) GenerateConstructors() {
	g.emit(`/* Object Constructors */
Obj* mk_int(long i) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->generation = _next_generation();
    x->mark = 1;
    x->tag = TAG_INT;
    x->is_pair = 0;
    x->scc_id = -1;
    x->scan_tag = 0;
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
    x->scc_id = -1;
    x->scan_tag = 0;
    x->f = f;
    return x;
}

double get_float(Obj* x) {
    if (!x || IS_IMMEDIATE(x)) return 0.0;
    return x->f;
}

Obj* mk_char(long c) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->generation = _next_generation();
    x->mark = 1;
    x->tag = TAG_CHAR;
    x->is_pair = 0;
    x->scc_id = -1;
    x->scan_tag = 0;
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
    x->scc_id = -1;
    x->scan_tag = 0;
    /* Move semantics: ownership transfers to pair, no inc_ref needed */
    /* Caller must inc_ref before calling if they want to keep a reference */
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
    x->scc_id = -1;
    x->scan_tag = 0;
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
    x->scc_id = -1;
    x->scan_tag = 0;
    if (v && !IS_IMMEDIATE(v)) inc_ref(v);
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
    x->generation = _next_generation();
    x->mark = 1;
    x->tag = TAG_ERROR;
    x->is_pair = 0;
    x->scc_id = -1;
    x->scan_tag = 0;
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
        x->generation = _next_generation();
        x->mark = 0;
        x->tag = TAG_INT;
        x->is_pair = 0;
        x->scc_id = -1;
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

static void release_children(Obj* x) {
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
    if (!x || IS_IMMEDIATE(x)) return;  /* Immediates can't be freed */
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
    x->generation = ipge_evolve(x->generation);  /* IPGE: invalidate refs */
    invalidate_weak_refs_for(x);
    free(x);
}

/* DAG: Reference counting */
void dec_ref(Obj* x) {
    if (!x || IS_IMMEDIATE(x)) return;  /* Immediates have no refcount */
    if (is_stack_obj(x)) return;
    if (x->mark < 0) return;
    x->mark--;
    if (x->mark <= 0) {
        release_children(x);
        x->generation = ipge_evolve(x->generation);  /* IPGE: invalidate refs */
        invalidate_weak_refs_for(x);
        free(x);
    }
}

void inc_ref(Obj* x) {
    if (!x || IS_IMMEDIATE(x)) return;  /* Immediates have no refcount */
    if (is_stack_obj(x)) return;
    if (x->mark < 0) { x->mark = 1; return; }
    x->mark++;
}

/* RC Optimization: Direct free for proven-unique references (Lobster-style) */
/* When compile-time analysis proves a reference is the only one, skip RC check */
void free_unique(Obj* x) {
    if (!x || IS_IMMEDIATE(x)) return;  /* Immediates can't be freed */
    if (is_stack_obj(x)) return;
    /* Proven unique at compile time - no RC check needed */
    release_children(x);
    x->generation = ipge_evolve(x->generation);  /* IPGE: invalidate refs */
    invalidate_weak_refs_for(x);
    free(x);
}

/* RC Optimization: Borrowed reference - no RC ops needed */
/* For parameters and temporary references that don't transfer ownership */
#define BORROWED_REF(x) (x)  /* No-op marker for documentation */

/* Free list operations */
void free_obj(Obj* x) {
    if (!x || IS_IMMEDIATE(x)) return;  /* Immediates can't be freed */
    if (is_stack_obj(x)) return;
    if (x->mark < 0) return;
    x->mark = -1;
    x->generation = ipge_evolve(x->generation);  /* IPGE: invalidate refs early */
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
            /* IPGE: generation already evolved in free_obj */
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
	g.emitRaw(`/* Arithmetic Operations */
static double num_to_double(Obj* x) {
    if (!x) return 0.0;
    if (IS_IMMEDIATE_INT(x)) return (double)INT_IMM_VALUE(x);
    if (IS_IMMEDIATE_BOOL(x)) return x == PURPLE_TRUE ? 1.0 : 0.0;
    if (IS_IMMEDIATE_CHAR(x)) return (double)CHAR_IMM_VALUE(x);
    if (x->tag == TAG_FLOAT) return x->f;
    return (double)x->i;
}

static int num_is_float(Obj* x) {
    if (!x || IS_IMMEDIATE(x)) return 0;  /* Immediates are never float */
    return x->tag == TAG_FLOAT;
}

Obj* add(Obj* a, Obj* b) {
    if (!a || !b) return mk_int_unboxed(0);
    if (num_is_float(a) || num_is_float(b)) {
        return mk_float(num_to_double(a) + num_to_double(b));
    }
    return mk_int_unboxed(obj_to_int(a) + obj_to_int(b));
}

Obj* sub(Obj* a, Obj* b) {
    if (!a || !b) return mk_int_unboxed(0);
    if (num_is_float(a) || num_is_float(b)) {
        return mk_float(num_to_double(a) - num_to_double(b));
    }
    return mk_int_unboxed(obj_to_int(a) - obj_to_int(b));
}

Obj* mul(Obj* a, Obj* b) {
    if (!a || !b) return mk_int_unboxed(0);
    if (num_is_float(a) || num_is_float(b)) {
        return mk_float(num_to_double(a) * num_to_double(b));
    }
    return mk_int_unboxed(obj_to_int(a) * obj_to_int(b));
}

Obj* div_op(Obj* a, Obj* b) {
    if (!a || !b) return mk_int_unboxed(0);
    if (num_is_float(a) || num_is_float(b)) {
        double denom = num_to_double(b);
        if (denom == 0.0) return mk_float(0.0);
        return mk_float(num_to_double(a) / denom);
    }
    long bi = obj_to_int(b);
    if (bi == 0) return mk_int_unboxed(0);
    return mk_int_unboxed(obj_to_int(a) / bi);
}

Obj* mod_op(Obj* a, Obj* b) {
    long bi = obj_to_int(b);
    if (!a || !b || bi == 0) return mk_int_unboxed(0);
    return mk_int_unboxed(obj_to_int(a) % bi);
}

`)
}

// GenerateComparison generates comparison operations
func (g *RuntimeGenerator) GenerateComparison() {
	g.emitRaw(`/* Comparison Operations */
Obj* eq_op(Obj* a, Obj* b) {
    /* Handle pointer equality first (works for immediates) */
    if (a == b) return PURPLE_TRUE;
    if (!a && !b) return PURPLE_TRUE;
    if (!a || !b) return PURPLE_FALSE;
    if (num_is_float(a) || num_is_float(b)) {
        return mk_bool(num_to_double(a) == num_to_double(b));
    }
    return mk_bool(obj_to_int(a) == obj_to_int(b));
}

Obj* lt_op(Obj* a, Obj* b) {
    if (!a || !b) return PURPLE_FALSE;
    if (num_is_float(a) || num_is_float(b)) {
        return mk_bool(num_to_double(a) < num_to_double(b));
    }
    return mk_bool(obj_to_int(a) < obj_to_int(b));
}

Obj* gt_op(Obj* a, Obj* b) {
    if (!a || !b) return PURPLE_FALSE;
    if (num_is_float(a) || num_is_float(b)) {
        return mk_bool(num_to_double(a) > num_to_double(b));
    }
    return mk_bool(obj_to_int(a) > obj_to_int(b));
}

Obj* le_op(Obj* a, Obj* b) {
    if (!a || !b) return PURPLE_FALSE;
    if (num_is_float(a) || num_is_float(b)) {
        return mk_bool(num_to_double(a) <= num_to_double(b));
    }
    return mk_bool(obj_to_int(a) <= obj_to_int(b));
}

Obj* ge_op(Obj* a, Obj* b) {
    if (!a || !b) return PURPLE_FALSE;
    if (num_is_float(a) || num_is_float(b)) {
        return mk_bool(num_to_double(a) >= num_to_double(b));
    }
    return mk_bool(obj_to_int(a) >= obj_to_int(b));
}

Obj* not_op(Obj* a) {
    if (!a) return PURPLE_TRUE;
    if (IS_IMMEDIATE_BOOL(a)) return a == PURPLE_TRUE ? PURPLE_FALSE : PURPLE_TRUE;
    if (IS_IMMEDIATE_INT(a)) return INT_IMM_VALUE(a) == 0 ? PURPLE_TRUE : PURPLE_FALSE;
    return mk_bool(obj_to_int(a) == 0);
}

static int is_truthy(Obj* x) {
    if (!x) return 0;
    if (IS_IMMEDIATE_BOOL(x)) return x == PURPLE_TRUE;
    if (IS_IMMEDIATE_INT(x)) return INT_IMM_VALUE(x) != 0;
    if (IS_IMMEDIATE_CHAR(x)) return CHAR_IMM_VALUE(x) != 0;
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
static Obj* prim_add(Obj* a, Obj* b) { return add(a, b); }
static Obj* prim_sub(Obj* a, Obj* b) { return sub(a, b); }
static Obj* prim_mul(Obj* a, Obj* b) { return mul(a, b); }
static Obj* prim_div(Obj* a, Obj* b) { return div_op(a, b); }
static Obj* prim_mod(Obj* a, Obj* b) { return mod_op(a, b); }
static Obj* prim_lt(Obj* a, Obj* b) { return lt_op(a, b); }
static Obj* prim_gt(Obj* a, Obj* b) { return gt_op(a, b); }
static Obj* prim_le(Obj* a, Obj* b) { return le_op(a, b); }
static Obj* prim_ge(Obj* a, Obj* b) { return ge_op(a, b); }
static Obj* prim_eq(Obj* a, Obj* b) { return eq_op(a, b); }
static Obj* prim_not(Obj* a) { return not_op(a); }
static Obj* prim_abs(Obj* a) {
    if (!a) return mk_int_unboxed(0);
    if (IS_IMMEDIATE_INT(a)) {
        long v = INT_IMM_VALUE(a);
        return mk_int_unboxed(v < 0 ? -v : v);
    }
    if (IS_IMMEDIATE(a)) return a;  /* Other immediates unchanged */
    if (a->tag == TAG_FLOAT) return mk_float(a->f < 0 ? -a->f : a->f);
    return mk_int_unboxed(a->i < 0 ? -a->i : a->i);
}

/* Type predicate wrappers - return Obj* for uniformity */
static Obj* prim_null(Obj* x) { return mk_bool(x == NULL); }
static Obj* prim_pair(Obj* x) {
    if (!x || IS_IMMEDIATE(x)) return PURPLE_FALSE;
    return mk_bool(x->tag == TAG_PAIR);
}
static Obj* prim_int(Obj* x) {
    if (IS_IMMEDIATE_INT(x) || IS_IMMEDIATE_BOOL(x)) return PURPLE_TRUE;
    return mk_bool(x && x->tag == TAG_INT);
}
static Obj* prim_float(Obj* x) {
    if (!x || IS_IMMEDIATE(x)) return PURPLE_FALSE;
    return mk_bool(x->tag == TAG_FLOAT);
}
static Obj* prim_char(Obj* x) {
    if (IS_IMMEDIATE_CHAR(x)) return PURPLE_TRUE;
    return mk_bool(x && x->tag == TAG_CHAR);
}
static Obj* prim_sym(Obj* x) {
    if (!x || IS_IMMEDIATE(x)) return PURPLE_FALSE;
    return mk_bool(x->tag == TAG_SYM);
}

/* I/O Primitives */
static void print_obj(Obj* x);  /* forward declaration */

/* Check if a list is a string (all chars) */
static int is_string_list(Obj* xs) {
    while (xs && xs->tag == TAG_PAIR) {
        if (!xs->a || xs->a->tag != TAG_CHAR) return 0;
        xs = xs->b;
    }
    return xs == NULL; /* Must be proper list */
}

/* Print a string list as a quoted string */
static void print_string(Obj* xs) {
    while (xs && xs->tag == TAG_PAIR) {
        if (xs->a && xs->a->tag == TAG_CHAR) {
            putchar((char)xs->a->i);
        }
        xs = xs->b;
    }
}

static void print_list(Obj* xs) {
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

static void print_obj(Obj* x) {
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

static Obj* prim_display(Obj* x) {
    print_obj(x);
    return NULL;
}

static Obj* prim_print(Obj* x) {
    print_obj(x);
    printf("\n");
    return NULL;
}

static Obj* prim_newline(void) {
    printf("\n");
    return NULL;
}

/* Type introspection */
static Obj* ctr_tag(Obj* x) {
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

static Obj* ctr_arg(Obj* x, Obj* idx) {
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
static Obj* char_to_int(Obj* c) {
    if (IS_IMMEDIATE_CHAR(c)) return mk_int_unboxed(CHAR_IMM_VALUE(c));
    if (!c || c->tag != TAG_CHAR) return mk_int_unboxed(0);
    return mk_int_unboxed(c->i);
}

static Obj* int_to_char(Obj* n) {
    if (!n) return mk_char_unboxed(0);
    return mk_char_unboxed(obj_to_int(n));
}

/* Float primitives */
static Obj* int_to_float(Obj* n) {
    if (!n) return mk_float(0.0);
    return mk_float(num_to_double(n));
}

static Obj* float_to_int(Obj* f) {
    if (!f) return mk_int_unboxed(0);
    if (IS_IMMEDIATE(f)) return mk_int_unboxed(obj_to_int(f));
    return mk_int_unboxed((long)(f->tag == TAG_FLOAT ? f->f : f->i));
}

static Obj* prim_floor(Obj* f) {
    if (!f) return mk_int_unboxed(0);
    if (IS_IMMEDIATE(f)) return mk_int_unboxed(obj_to_int(f));  /* Already integer */
    if (f->tag == TAG_FLOAT) {
        double v = f->f;
        return mk_float(v >= 0 ? (long)v : (long)v - (v != (long)v ? 1 : 0));
    }
    return mk_int_unboxed(f->i);
}

static Obj* prim_ceil(Obj* f) {
    if (!f) return mk_int_unboxed(0);
    if (IS_IMMEDIATE(f)) return mk_int_unboxed(obj_to_int(f));  /* Already integer */
    if (f->tag == TAG_FLOAT) {
        double v = f->f;
        return mk_float(v >= 0 ? (long)v + (v != (long)v ? 1 : 0) : (long)v);
    }
    return mk_int_unboxed(f->i);
}

/* Higher-order function primitives */
static Obj* prim_apply(Obj* fn, Obj* args) {
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
static Obj* compose_wrapper(Obj** captures, Obj** args, int n);

static Obj* prim_compose(Obj* f, Obj* g) {
    if (!f || !g) return NULL;
    Obj** caps = malloc(2 * sizeof(Obj*));
    caps[0] = f; inc_ref(f);
    caps[1] = g; inc_ref(g);
    return mk_closure(compose_wrapper, caps, NULL, 2, 1);
}

static Obj* compose_wrapper(Obj** captures, Obj** args, int n) {
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

`)
}

// GenerateListRuntime generates list and closure-aware helpers
func (g *RuntimeGenerator) GenerateListRuntime() {
	g.emit(`/* List Operations */
Obj* car(Obj* p) {
    if (!p || p->tag != TAG_PAIR) return NULL;
    return p->a;
}

Obj* cdr(Obj* p) {
    if (!p || p->tag != TAG_PAIR) return NULL;
    return p->b;
}

/* Aliases for compiler - return owned references */
static Obj* obj_car(Obj* p) {
    Obj* r = car(p);
    if (r) inc_ref(r);
    return r;
}
static Obj* obj_cdr(Obj* p) {
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

`)
}

// GenerateScanRuntime generates generic scanner helpers (non-GC)
func (g *RuntimeGenerator) GenerateScanRuntime() {
	g.emit(`/* Generic Scanners (debug/verification only) */
static void scan_obj(Obj* x) {
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

static void clear_marks_obj(Obj* x) {
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

// GenerateUserTypeScanners generates scanner functions for user-defined types
// Scanners skip weak fields to avoid traversing back-edges
func (g *RuntimeGenerator) GenerateUserTypeScanners() {
	if g.registry == nil {
		return
	}

	userTypes := g.registry.GetUserDefinedTypes()
	if len(userTypes) == 0 {
		g.emit("static void scan_user_obj(Obj* obj) { (void)obj; }\n")
		g.emit("static void clear_marks_user_obj(Obj* obj) { (void)obj; }\n\n")
		return
	}

	g.emit(`/* User-Defined Type Scanners */
/* Note: Weak fields are SKIPPED to avoid traversing back-edges */
/* These scanners are for debugging/verification, not GC */

`)

	for _, typeDef := range userTypes {
		// Generate scan function
		g.emit("void scan_%s(Obj* obj) {\n", typeDef.Name)
		g.emit("    if (!obj || obj->tag != TAG_USER_%s) return;\n", typeDef.Name)
		g.emit("    %s* x = (%s*)obj->ptr;\n", typeDef.Name, typeDef.Name)
		g.emit("    if (!x) return;\n")

		for _, field := range typeDef.Fields {
			if field.Strength == FieldWeak {
				g.emit("    /* x->%s: WEAK - skip to avoid back-edge traversal */\n", field.Name)
			} else {
				g.emit("    if (x->%s) scan_obj(x->%s);\n", field.Name, field.Name)
			}
		}

		g.emit("}\n\n")

		// Generate clear_marks function
		g.emit("void clear_marks_%s(Obj* obj) {\n", typeDef.Name)
		g.emit("    if (!obj || obj->tag != TAG_USER_%s) return;\n", typeDef.Name)
		g.emit("    %s* x = (%s*)obj->ptr;\n", typeDef.Name, typeDef.Name)
		g.emit("    if (!x) return;\n")

		for _, field := range typeDef.Fields {
			if field.Strength == FieldWeak {
				g.emit("    /* x->%s: WEAK - skip */\n", field.Name)
			} else {
				g.emit("    if (x->%s) clear_marks_obj(x->%s);\n", field.Name, field.Name)
			}
		}

		g.emit("}\n\n")
	}

	// Dispatcher for user-defined scanners
	g.emit("static void scan_user_obj(Obj* obj) {\n")
	g.emit("    if (!obj) return;\n")
	g.emit("    switch (obj->tag) {\n")
	for _, typeDef := range userTypes {
		g.emit("    case TAG_USER_%s:\n", typeDef.Name)
		g.emit("        scan_%s(obj);\n", typeDef.Name)
		g.emit("        break;\n")
	}
	g.emit("    default:\n")
	g.emit("        break;\n")
	g.emit("    }\n")
	g.emit("}\n\n")

	g.emit("static void clear_marks_user_obj(Obj* obj) {\n")
	g.emit("    if (!obj) return;\n")
	g.emit("    switch (obj->tag) {\n")
	for _, typeDef := range userTypes {
		g.emit("    case TAG_USER_%s:\n", typeDef.Name)
		g.emit("        clear_marks_%s(obj);\n", typeDef.Name)
		g.emit("        break;\n")
	}
	g.emit("    default:\n")
	g.emit("        break;\n")
	g.emit("    }\n")
	g.emit("}\n\n")
}

// GenerateArenaRuntime generates arena allocation runtime with externals support
func (g *RuntimeGenerator) GenerateArenaRuntime() {
	g.emit(`

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
    x->generation = _next_generation();
    x->mark = -2;  /* Special mark for arena-allocated */
    x->tag = TAG_INT;
    x->is_pair = 0;
    x->scc_id = -1;
    x->scan_tag = 0;
    x->i = i;
    return x;
}

Obj* arena_mk_pair(Arena* a, Obj* car, Obj* cdr) {
    Obj* x = arena_alloc(a, sizeof(Obj));
    if (!x) return NULL;
    x->generation = _next_generation();
    x->mark = -2;  /* Special mark for arena-allocated */
    x->tag = TAG_PAIR;
    x->is_pair = 1;
    x->scc_id = -1;
    x->scan_tag = 0;
    x->a = car;
    x->b = cdr;
    return x;
}


`)
}

// GeneratePerceusRuntime generates Perceus reuse analysis runtime
func (g *RuntimeGenerator) GeneratePerceusRuntime() {
	g.emit(`

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
    obj->generation = _next_generation();
    obj->mark = 1;
    obj->tag = TAG_INT;
    obj->is_pair = 0;
    obj->scc_id = -1;
    obj->scan_tag = 0;
    obj->i = value;
    return obj;
}

Obj* reuse_as_pair(Obj* old, Obj* a, Obj* b) {
    Obj* obj = try_reuse(old, sizeof(Obj));
    if (!obj) return NULL;
    obj->generation = _next_generation();
    obj->mark = 1;
    obj->tag = TAG_PAIR;
    obj->is_pair = 1;
    obj->scc_id = -1;
    obj->scan_tag = 0;
    obj->a = a;
    obj->b = b;
    return obj;
}

Obj* reuse_as_box(Obj* old, Obj* value) {
    Obj* obj = try_reuse(old, sizeof(Obj));
    if (!obj) return NULL;
    obj->generation = _next_generation();
    obj->mark = 1;
    obj->tag = TAG_BOX;
    obj->is_pair = 0;
    obj->scc_id = -1;
    obj->scan_tag = 0;
    obj->ptr = value;
    return obj;
}

Obj* reuse_as_closure(Obj* old, ClosureFn fn, Obj** captures, BorrowRef** refs, int count, int arity) {
    Obj* obj = try_reuse(old, sizeof(Obj));
    if (!obj) return NULL;
    obj->generation = _next_generation();
    obj->mark = 1;
    obj->tag = TAG_CLOSURE;
    obj->is_pair = 0;
    obj->scc_id = -1;
    obj->scan_tag = 0;

    Closure* c = malloc(sizeof(Closure));
    if (!c) { free(obj); return NULL; }
    c->fn = fn;
    c->captures = captures;
    c->capture_refs = refs;
    c->capture_count = count;
    c->arity = arity;
    obj->ptr = c;
    return obj;
}

/* Check if object can be reused (unique reference) */
static inline int can_reuse(Obj* obj) {
    if (!obj || IS_IMMEDIATE(obj)) return 0;
    return obj->mark == 1;  /* Reference count of 1 means unique */
}

/* Consume for potential reuse - returns object if reusable, frees otherwise */
static inline Obj* consume_for_reuse(Obj* obj) {
    if (can_reuse(obj)) {
        return obj;  /* Caller can reuse this memory */
    }
    if (obj && !IS_IMMEDIATE(obj)) dec_ref(obj);
    return NULL;
}


`)
}

// GenerateSCCRuntime generates SCC-based reference counting runtime (ISMM 2024)
func (g *RuntimeGenerator) GenerateSCCRuntime() {
	g.emitRaw(`

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
	g.emit(`

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


`)
}

// GenerateSymmetricRuntime generates symmetric reference counting runtime
// This is the preferred method for handling unbroken cycles - more memory efficient than arenas
func (g *RuntimeGenerator) GenerateSymmetricRuntime() {
	g.emit(`

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

	userTypes := g.registry.GetUserDefinedTypes()
	if len(userTypes) == 0 {
		return
	}

	g.emit(`/* User-Defined Types */
/* Generated from deftype declarations with automatic back-edge detection */

`)

	// Phase 1: Forward declarations for mutual recursion
	g.emit("/* Forward declarations */\n")
	for _, typeDef := range userTypes {
		g.emit("typedef struct %s %s;\n", typeDef.Name, typeDef.Name)
	}
	g.emit("\n")

	// Phase 2: Full struct definitions (in definition order)
	for _, typeDef := range userTypes {
		g.emit("/* Type: %s */\n", typeDef.Name)
		g.emit("struct %s {\n", typeDef.Name)

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

		g.emit("};\n\n")
	}
}

// fieldTypeToCType converts a field type to a C type
func (g *RuntimeGenerator) fieldTypeToCType(field TypeField) string {
	// Store all fields as Obj* for uniform runtime representation.
	return "Obj*"
}

// GenerateTypeReleaseFunctions generates release functions for user-defined types
func (g *RuntimeGenerator) GenerateTypeReleaseFunctions() {
	if g.registry == nil {
		return
	}

	userTypes := g.registry.GetUserDefinedTypes()
	g.emit(`/* Type-Aware Release Functions */
/* Automatically skip weak fields (back-edges) to prevent double-free */

`)

	if len(userTypes) == 0 {
		g.emit("static void release_user_obj(Obj* obj) { (void)obj; }\n\n")
		return
	}

	for _, typeDef := range userTypes {
		g.emit("void release_%s(Obj* obj) {\n", typeDef.Name)
		g.emit("    if (!obj) return;\n")
		g.emit("    if (obj->tag != TAG_USER_%s) return;\n", typeDef.Name)
		g.emit("    %s* x = (%s*)obj->ptr;\n", typeDef.Name, typeDef.Name)
		g.emit("    if (!x) return;\n")

		// Process each field
		for _, field := range typeDef.Fields {
			switch field.Strength {
			case FieldWeak:
				// Weak field - DO NOT decrement
				g.emit("    /* x->%s: weak back-edge - skip to prevent cycle */\n", field.Name)
			default:
				// Treat all non-weak fields as strong
				g.emit("    if (x->%s) dec_ref(x->%s); /* strong */\n", field.Name, field.Name)
			}
		}

		g.emit("    free(x);\n")
		g.emit("    obj->ptr = NULL;\n")
		g.emit("}\n\n")
	}

	// Dispatcher for user-defined types
	g.emit("static void release_user_obj(Obj* obj) {\n")
	g.emit("    if (!obj) return;\n")
	g.emit("    switch (obj->tag) {\n")
	for _, typeDef := range userTypes {
		g.emit("    case TAG_USER_%s:\n", typeDef.Name)
		g.emit("        release_%s(obj);\n", typeDef.Name)
		g.emit("        break;\n")
	}
	g.emit("    default:\n")
	g.emit("        break;\n")
	g.emit("    }\n")
	g.emit("}\n\n")
}

// GenerateTypeConstructors generates constructor functions for user-defined types
func (g *RuntimeGenerator) GenerateTypeConstructors() {
	if g.registry == nil {
		return
	}

	g.emit(`/* Type Constructors */

`)

	userTypes := g.registry.GetUserDefinedTypes()
	for _, typeDef := range userTypes {
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

		g.emit("Obj* mk_%s(%s) {\n", typeDef.Name, paramStr)
		g.emit("    %s* x = malloc(sizeof(%s));\n", typeDef.Name, typeDef.Name)
		g.emit("    if (!x) return NULL;\n")

		for _, field := range typeDef.Fields {
			if field.Strength != FieldWeak {
				g.emit("    if (%s) inc_ref(%s); /* strong ref */\n", field.Name, field.Name)
			}
			g.emit("    x->%s = %s;\n", field.Name, field.Name)
		}

		g.emit("    Obj* obj = malloc(sizeof(Obj));\n")
		g.emit("    if (!obj) { free(x); return NULL; }\n")
		g.emit("    obj->generation = _next_generation();\n")
		g.emit("    obj->mark = 1;\n")
		g.emit("    obj->tag = TAG_USER_%s;\n", typeDef.Name)
		g.emit("    obj->is_pair = 0;\n")
		g.emit("    obj->scc_id = -1;\n")
		g.emit("    obj->scan_tag = 0;\n")
		g.emit("    obj->ptr = x;\n")
		g.emit("    return obj;\n")
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

	userTypes := g.registry.GetUserDefinedTypes()
	for _, typeDef := range userTypes {
		for _, field := range typeDef.Fields {
			cType := g.fieldTypeToCType(field)

			// Getter
			if field.Strength == FieldWeak {
				g.emit("/* %s.%s getter - WEAK field, no ref count increment */\n", typeDef.Name, field.Name)
				g.emit("%s get_%s_%s(Obj* obj) {\n", cType, typeDef.Name, field.Name)
				g.emit("    if (!obj || obj->tag != TAG_USER_%s) return NULL;\n", typeDef.Name)
				g.emit("    %s* x = (%s*)obj->ptr;\n", typeDef.Name, typeDef.Name)
				g.emit("    return x ? x->%s : NULL;\n", field.Name)
				g.emit("}\n\n")
			} else {
				g.emit("/* %s.%s getter - STRONG field */\n", typeDef.Name, field.Name)
				g.emit("%s get_%s_%s(Obj* obj) {\n", cType, typeDef.Name, field.Name)
				g.emit("    if (!obj || obj->tag != TAG_USER_%s) return NULL;\n", typeDef.Name)
				g.emit("    %s* x = (%s*)obj->ptr;\n", typeDef.Name, typeDef.Name)
				g.emit("    if (!x) return NULL;\n")
				g.emit("    if (x->%s) inc_ref(x->%s);\n", field.Name, field.Name)
				g.emit("    return x->%s;\n", field.Name)
				g.emit("}\n\n")
			}

			// Setter
			g.emit("/* %s.%s setter */\n", typeDef.Name, field.Name)
			g.emit("void set_%s_%s(Obj* obj, %s value) {\n", typeDef.Name, field.Name, cType)
			g.emit("    if (!obj || obj->tag != TAG_USER_%s) return;\n", typeDef.Name)
			g.emit("    %s* x = (%s*)obj->ptr;\n", typeDef.Name, typeDef.Name)
			g.emit("    if (!x) return;\n")

			if field.Strength != FieldWeak {
				g.emit("    if (value) inc_ref(value); /* new value */\n")
				g.emit("    if (x->%s) dec_ref(x->%s); /* old value */\n", field.Name, field.Name)
			} else {
				g.emit("    /* weak field - no ref count management */\n")
			}

			g.emit("    x->%s = value;\n", field.Name)
			g.emit("}\n\n")
		}
	}
}

// GenerateRegionRuntime generates region reference functions
func (g *RuntimeGenerator) GenerateRegionRuntime() {
	g.emit(`

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

static void region_init(void) {
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

static int region_exit(void) {
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
static int region_can_reference(RegionObj* source, RegionObj* target) {
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

static int region_ref_is_valid(RegionRef* ref) {
    return ref && ref->target && ref->target->region != NULL;
}

static void* region_deref(RegionRef* ref) {
    if (!region_ref_is_valid(ref)) {
        fprintf(stderr, "region: use-after-free or scope violation\n");
        return NULL;
    }
    return ref->target->data;
}


`)
}

// GenerateBorrowRefRuntime generates generational reference functions
func (g *RuntimeGenerator) GenerateBorrowRefRuntime() {
	g.emitRaw(`

/* ========== BorrowRef / IPGE (v0.6.0) ========== */
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
    GenObj* target;
    Generation remembered_gen;
    const char* source_desc;
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

static void legacy_init(void) {
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

static void legacy_free(GenObj* obj) {
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

static void borrow_release(BorrowRef* ref) {
    if (ref) free(ref);
}

static BorrowRef* legacy_create(GenObj* obj, const char* source_desc) {
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

/* IPGE-style BorrowRef: use inline generation from Obj */
typedef struct IPGERef {
    Obj* obj;
    uint64_t remembered_gen;
    const char* source_desc;
} IPGERef;

static BorrowRef* borrow_create_ipge(Obj* obj, uint64_t gen, const char* source_desc) {
    if (!obj) return NULL;
    /* Store IPGE ref in BorrowRef struct */
    BorrowRef* ref = calloc(1, sizeof(BorrowRef) + sizeof(IPGERef));
    if (!ref) return NULL;
    IPGERef* ipge = (IPGERef*)(ref + 1);
    ipge->obj = obj;
    ipge->remembered_gen = gen;
    ipge->source_desc = source_desc;
    ref->target = NULL;  /* Mark as IPGE-style by setting target to NULL */
    ref->remembered_gen = (uint64_t)(uintptr_t)ipge;  /* Store pointer to IPGERef */
    ref->source_desc = source_desc;
    return ref;
}

/* O(1) validity check - just compare generations (with read lock) */
static int borrow_is_valid(BorrowRef* ref) {
    if (!ref) return 0;
    /* Check for IPGE-style ref (target is NULL) */
    if (!ref->target) {
        IPGERef* ipge = (IPGERef*)(uintptr_t)ref->remembered_gen;
        if (!ipge || !ipge->obj) return 0;
        return ipge->remembered_gen == ipge->obj->generation;
    }
    pthread_rwlock_rdlock(&ref->target->rwlock);
    int valid = ref->remembered_gen == ref->target->generation &&
                ref->target->generation != 0;
    pthread_rwlock_unlock(&ref->target->rwlock);
    return valid;
}

static void* borrow_deref(BorrowRef* ref) {
    if (!ref) {
        fprintf(stderr, "borrow: null reference\n");
        return NULL;
    }
    /* Check for IPGE-style ref (target is NULL) */
    if (!ref->target) {
        IPGERef* ipge = (IPGERef*)(uintptr_t)ref->remembered_gen;
        if (!ipge || !ipge->obj) {
            fprintf(stderr, "borrow: null IPGE reference\n");
            return NULL;
        }
        if (ipge->remembered_gen != ipge->obj->generation) {
            fprintf(stderr, "borrow: use-after-free detected [created at: %s]\n",
                    ipge->source_desc ? ipge->source_desc : "unknown");
            return NULL;
        }
        return ipge->obj;
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

/* BorrowRef get: returns validated Obj* (public API wrapper) */
Obj* borrow_get(BorrowRef* ref) {
    return (Obj*)borrow_deref(ref);
}

/* IPGE-style binding: snapshot current generation */
static BorrowRef* borrow_create(Obj* obj, const char* source_desc) {
    if (!obj || IS_IMMEDIATE(obj)) return NULL;
    legacy_init();
    /* IPGE: Use inline generation for validation */
    return borrow_create_ipge(obj, obj->generation, source_desc);
}

static void borrow_invalidate_obj(Obj* obj) {
    /* IPGE: Generation evolves in free functions, no need for explicit invalidation */
    (void)obj;
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

static int genclosure_validate(GenClosure* c) {
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


`)
}

// GenerateClosureRuntime generates closure support for AST->C compilation
func (g *RuntimeGenerator) GenerateClosureRuntime() {
	g.emitRaw(`
/* ========== Closure Runtime ========== */
typedef Obj* (*ClosureFn)(Obj** captures, Obj** args, int arg_count);

struct Closure {
    ClosureFn fn;
    Obj** captures;
    BorrowRef** capture_refs;
    int capture_count;
    int arity;
};

static Obj* mk_closure(ClosureFn fn, Obj** captures, BorrowRef** refs, int count, int arity) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->generation = _next_generation();
    x->mark = 1;
    x->tag = TAG_CLOSURE;
    x->is_pair = 0;
    x->scc_id = -1;
    x->scan_tag = 0;

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

static void closure_release(Closure* c) {
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

static int closure_validate(Closure* c) {
    if (!c || !c->capture_refs) return 1;
    for (int i = 0; i < c->capture_count; i++) {
        if (c->capture_refs[i] && !borrow_is_valid(c->capture_refs[i])) {
            return 0;
        }
    }
    return 1;
}

static Obj* call_closure(Obj* clos, Obj** args, int arg_count) {
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

`)
}

// GenerateConstraintRuntime generates constraint reference functions
func (g *RuntimeGenerator) GenerateConstraintRuntime() {
	g.emitRaw(`

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
static int constraint_release(ConstraintRef* ref) {
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

static int constraint_free(ConstraintObj* obj) {
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

static int constraint_is_valid(ConstraintRef* ref) {
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


`)
}

// GenerateAll generates the complete runtime with ALL features.
// The runtime is a library for REPL/JIT - all features must be available.
// Optimization happens at codegen (which functions to CALL), not here.
func (g *RuntimeGenerator) GenerateAll() {
	// Core ASAP
	g.GenerateHeader()
	g.GenerateWeakRefs()
	g.GenerateConstructors()
	g.GenerateMemoryManagement()
	g.GenerateArithmetic()
	g.GenerateComparison()
	g.GenerateListRuntime()
	g.GenerateScanRuntime()
	g.GenerateScanner("List", true)

	// User types
	g.GenerateUserTypes()
	g.GenerateTypeReleaseFunctions()
	g.GenerateTypeConstructors()
	g.GenerateFieldAccessors()
	g.GenerateUserTypeScanners()

	// All runtime features - always available
	g.GenerateArenaRuntime()
	g.GeneratePoolRuntime()
	g.GenerateNaNBoxingRuntime()
	g.GenerateSCCRuntime()
	g.GenerateDeferredRuntime()
	g.GenerateSymmetricRuntime()
	g.GenerateRegionRuntime()
	g.GenerateBorrowRefRuntime()
	g.GenerateClosureRuntime()
	g.GenerateConstraintRuntime()
	g.GeneratePerceusRuntime()
	g.GenerateConcurrencyRuntime()
	g.GenerateDPSRuntime()
	g.GenerateExceptionRuntime()
}

// GeneratePoolRuntime generates thread-local pool allocator for temporary values
func (g *RuntimeGenerator) GeneratePoolRuntime() {
	g.emitRaw(`

/* ========== Pool Allocator (Bump Allocation) ========== */
/* Thread-local pool for non-escaping temporary allocations */
/* O(1) allocation, bulk deallocation at scope exit */

#define POOL_BLOCK_SIZE 4096

typedef struct PoolBlock {
    char* memory;
    size_t size;
    size_t used;
    struct PoolBlock* next;
} PoolBlock;

typedef struct Pool {
    PoolBlock* current;
    PoolBlock* blocks;
    size_t block_size;
    int alloc_count;
} Pool;

/* Thread-local pool for current function scope */
static __thread Pool* _current_pool = NULL;
static __thread int _pool_depth = 0;

Pool* pool_create(void) {
    Pool* p = malloc(sizeof(Pool));
    if (!p) return NULL;
    p->current = NULL;
    p->blocks = NULL;
    p->block_size = POOL_BLOCK_SIZE;
    p->alloc_count = 0;
    return p;
}

void* pool_alloc(Pool* p, size_t size) {
    if (!p) return NULL;

    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;

    if (!p->current || p->current->used + size > p->current->size) {
        /* Need new block */
        size_t block_size = p->block_size;
        if (size > block_size) block_size = size;

        PoolBlock* b = malloc(sizeof(PoolBlock));
        if (!b) return NULL;
        b->memory = malloc(block_size);
        if (!b->memory) {
            free(b);
            return NULL;
        }
        b->size = block_size;
        b->used = 0;
        b->next = p->blocks;
        p->blocks = b;
        p->current = b;
    }

    void* ptr = p->current->memory + p->current->used;
    p->current->used += size;
    p->alloc_count++;
    return ptr;
}

/* Pool-allocated object makers */
Obj* pool_mk_int(Pool* p, long value) {
    Obj* x = pool_alloc(p, sizeof(Obj));
    if (!x) return mk_int(value);  /* Fallback to heap */
    x->generation = _next_generation();
    x->mark = -2;  /* Special mark: pool-allocated */
    x->tag = TAG_INT;
    x->is_pair = 0;
    x->scc_id = -1;
    x->scan_tag = 0;
    x->i = value;
    return x;
}

Obj* pool_mk_pair(Pool* p, Obj* a, Obj* b) {
    Obj* x = pool_alloc(p, sizeof(Obj));
    if (!x) return mk_pair(a, b);  /* Fallback to heap */
    x->generation = _next_generation();
    x->mark = -2;
    x->tag = TAG_PAIR;
    x->is_pair = 1;
    x->scc_id = -1;
    x->scan_tag = 0;
    x->a = a;
    x->b = b;
    return x;
}

Obj* pool_mk_box(Pool* p, Obj* value) {
    Obj* x = pool_alloc(p, sizeof(Obj));
    if (!x) return mk_box(value);
    x->generation = _next_generation();
    x->mark = -2;
    x->tag = TAG_BOX;
    x->is_pair = 0;
    x->scc_id = -1;
    x->scan_tag = 0;
    x->ptr = value;
    return x;
}

void pool_destroy(Pool* p) {
    if (!p) return;

    PoolBlock* b = p->blocks;
    while (b) {
        PoolBlock* next = b->next;
        free(b->memory);
        free(b);
        b = next;
    }

    free(p);
}

void pool_reset(Pool* p) {
    if (!p) return;

    PoolBlock* b = p->blocks;
    while (b) {
        b->used = 0;
        b = b->next;
    }
    p->current = p->blocks;
    p->alloc_count = 0;
}

/* Check if object is pool-allocated */
static inline int is_pool_allocated(Obj* obj) {
    return obj && !IS_IMMEDIATE(obj) && obj->mark == -2;
}

/* Deep copy pool-allocated value to heap (for escaping values) */
Obj* pool_escape_to_heap(Obj* obj) {
    if (!obj || IS_IMMEDIATE(obj)) return obj;
    if (!is_pool_allocated(obj)) return obj;  /* Already on heap */

    if (obj->is_pair) {
        Obj* a_heap = pool_escape_to_heap(obj->a);
        Obj* b_heap = pool_escape_to_heap(obj->b);
        return mk_pair(a_heap, b_heap);
    } else if (obj->tag == TAG_INT) {
        return mk_int(obj->i);
    } else if (obj->tag == TAG_FLOAT) {
        return mk_float(obj->f);
    } else if (obj->tag == TAG_BOX) {
        Obj* val_heap = pool_escape_to_heap((Obj*)obj->ptr);
        return mk_box(val_heap);
    }

    /* Unknown type - return as-is */
    return obj;
}

/* Pool scope management */
void pool_enter_scope(void) {
    if (_pool_depth == 0) {
        _current_pool = pool_create();
    }
    _pool_depth++;
}

void pool_exit_scope(void) {
    if (_pool_depth > 0) {
        _pool_depth--;
        if (_pool_depth == 0 && _current_pool) {
            pool_destroy(_current_pool);
            _current_pool = NULL;
        }
    }
}

Pool* pool_current(void) {
    return _current_pool;
}


`)
}

// GenerateExceptionRuntime generates exception handling support
func (g *RuntimeGenerator) GenerateExceptionRuntime() {
	excGen := NewExceptionCodeGenerator(g.w, g.registry)
	excGen.GenerateExceptionRuntime()
}

// GenerateConcurrencyRuntime generates thread-safe runtime for goroutines and channels
func (g *RuntimeGenerator) GenerateConcurrencyRuntime() {
	ctx := analysis.NewConcurrencyContext()
	gen := analysis.NewConcurrencyCodeGenerator(ctx)
	g.emit(`

%s

`, gen.GenerateConcurrencyRuntime())
}

// GenerateDPSRuntime generates Destination-Passing Style runtime
func (g *RuntimeGenerator) GenerateDPSRuntime() {
	dps := analysis.NewDPSOptimizer()
	g.emit(`

%s

`, dps.GenerateDPSRuntime())
}

// GenerateNaNBoxingRuntime generates NaN-boxing for unboxed floats
func (g *RuntimeGenerator) GenerateNaNBoxingRuntime() {
	g.emitRaw(`

/* ========== NaN-Boxing for Unboxed Floats ========== */
/* Uses IEEE 754 quiet NaN payload bits to store pointers */
/* Enables unboxed floats while still supporting object pointers */

/*
 * IEEE 754 Double Layout:
 *   Quiet NaN:  0 11111111111 1xxx...xxx (51 payload bits)
 *   We use:     0x7FF8_0000_0000_0000 as NaN prefix
 *   Payload:    48 bits for pointer (enough for x86-64 canonical addresses)
 *
 * Value encoding:
 *   - Normal floats: any bit pattern that isn't our NaN-boxed format
 *   - Tagged ints/bools/chars: already use low bits (3-bit tag)
 *   - Object pointers: NaN-boxed with our prefix
 */

/* NaN-boxing constants */
#define NANBOX_PREFIX     0x7FF8000000000000ULL  /* Quiet NaN prefix */
#define NANBOX_MASK       0xFFFF000000000000ULL  /* High 16 bits */
#define NANBOX_PTR_MASK   0x0000FFFFFFFFFFFFULL  /* Low 48 bits for pointer */

/* Check if a raw 64-bit value is a NaN-boxed pointer */
#define IS_NANBOXED_PTR(bits)  (((bits) & NANBOX_MASK) == NANBOX_PREFIX)

/* Extract pointer from NaN-boxed value */
#define NANBOX_TO_PTR(bits)    ((Obj*)((uintptr_t)((bits) & NANBOX_PTR_MASK)))

/* Create NaN-boxed pointer from Obj* */
#define PTR_TO_NANBOX(p)       (NANBOX_PREFIX | ((uint64_t)(uintptr_t)(p) & NANBOX_PTR_MASK))

/* Universal value type that can hold float or NaN-boxed pointer */
typedef union {
    double f;           /* IEEE 754 double */
    uint64_t bits;      /* Raw bits for NaN-boxing */
} NanBoxedValue;

/* Check if value is a real float (not NaN-boxed or immediate) */
static inline int nanbox_is_float(NanBoxedValue v) {
    /* If it's our NaN-box prefix, it's a pointer */
    if (IS_NANBOXED_PTR(v.bits)) return 0;
    /* If it's a tagged immediate, it's not a float */
    if (IS_IMMEDIATE(v.bits)) return 0;
    /* Otherwise it's a float */
    return 1;
}

/* Check if value is a NaN-boxed object pointer */
static inline int nanbox_is_ptr(NanBoxedValue v) {
    return IS_NANBOXED_PTR(v.bits);
}

/* Get float value (caller must verify nanbox_is_float first) */
static inline double nanbox_to_float(NanBoxedValue v) {
    return v.f;
}

/* Get object pointer from NaN-boxed value */
static inline Obj* nanbox_to_obj(NanBoxedValue v) {
    if (IS_NANBOXED_PTR(v.bits)) {
        return NANBOX_TO_PTR(v.bits);
    }
    if (IS_IMMEDIATE(v.bits)) {
        return (Obj*)v.bits;  /* Tagged immediate */
    }
    return NULL;  /* It's a float, not an object */
}

/* Create NaN-boxed value from float */
static inline NanBoxedValue nanbox_from_float(double f) {
    NanBoxedValue v;
    v.f = f;
    return v;
}

/* Create NaN-boxed value from object pointer */
static inline NanBoxedValue nanbox_from_obj(Obj* obj) {
    NanBoxedValue v;
    if (!obj) {
        v.bits = 0;
    } else if (IS_IMMEDIATE((uint64_t)(uintptr_t)obj)) {
        v.bits = (uint64_t)(uintptr_t)obj;  /* Keep tagged immediate */
    } else {
        v.bits = PTR_TO_NANBOX(obj);  /* NaN-box the pointer */
    }
    return v;
}

/* Arithmetic on NaN-boxed floats */
static inline NanBoxedValue nanbox_add(NanBoxedValue a, NanBoxedValue b) {
    if (nanbox_is_float(a) && nanbox_is_float(b)) {
        return nanbox_from_float(a.f + b.f);
    }
    /* Handle mixed types - fall back to heap */
    return nanbox_from_float(0.0);
}

static inline NanBoxedValue nanbox_sub(NanBoxedValue a, NanBoxedValue b) {
    if (nanbox_is_float(a) && nanbox_is_float(b)) {
        return nanbox_from_float(a.f - b.f);
    }
    return nanbox_from_float(0.0);
}

static inline NanBoxedValue nanbox_mul(NanBoxedValue a, NanBoxedValue b) {
    if (nanbox_is_float(a) && nanbox_is_float(b)) {
        return nanbox_from_float(a.f * b.f);
    }
    return nanbox_from_float(0.0);
}

static inline NanBoxedValue nanbox_div(NanBoxedValue a, NanBoxedValue b) {
    if (nanbox_is_float(a) && nanbox_is_float(b) && b.f != 0.0) {
        return nanbox_from_float(a.f / b.f);
    }
    return nanbox_from_float(0.0);
}

/* Comparison on NaN-boxed values */
static inline int nanbox_eq(NanBoxedValue a, NanBoxedValue b) {
    if (nanbox_is_float(a) && nanbox_is_float(b)) {
        return a.f == b.f;
    }
    if (nanbox_is_ptr(a) && nanbox_is_ptr(b)) {
        return nanbox_to_obj(a) == nanbox_to_obj(b);
    }
    return 0;
}

static inline int nanbox_lt(NanBoxedValue a, NanBoxedValue b) {
    if (nanbox_is_float(a) && nanbox_is_float(b)) {
        return a.f < b.f;
    }
    return 0;
}

/* Print NaN-boxed value for debugging */
static inline void nanbox_print(NanBoxedValue v) {
    if (nanbox_is_float(v)) {
        printf("%g", v.f);
    } else if (nanbox_is_ptr(v)) {
        Obj* obj = nanbox_to_obj(v);
        print_obj(obj);
    } else {
        printf("<nanbox:0x%llx>", (unsigned long long)v.bits);
    }
}



`)
}

// GenerateRuntime generates the complete C99 runtime to a string
func GenerateRuntime(registry *TypeRegistry) string {
	var sb strings.Builder
	gen := NewRuntimeGenerator(&sb, registry)
	gen.GenerateAll()
	return sb.String()
}
