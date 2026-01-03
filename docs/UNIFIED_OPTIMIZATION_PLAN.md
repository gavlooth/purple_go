# Unified Optimization Plan: Vale + Lobster + Perceus

## Design Principle: No Language Restrictions

All optimizations are **inferred automatically** or enabled via optional hints. Existing Purple code works unchanged - the compiler just generates faster code when it can prove safety.

## Reclamation vs Safety (Do Not Confuse)

**Reclamation (frees memory):**
- ASAP free insertion (baseline)
- Reference counting (DAG/shared)
- SCC / symmetric RC (cycles)
- Arenas / regions (bulk free)
- Perceus reuse (free+alloc → reuse)

**Safety (detects stale refs only):**
- GenRef / IPGE / tagged handles
- Region refs / constraint refs
- Borrow/tether checks

Safety layers can be disabled without breaking correctness. Reclamation layers cannot.

## Strategy Selection Matrix (Reclamation)

| Shape | Cycle Status | Mutable? | Escape | Strategy | Runtime ops |
|-------|--------------|----------|--------|----------|-------------|
| Tree | N/A | N/A | Any | ASAP | `free_tree` |
| DAG | N/A | N/A | Any | RC | `inc_ref` / `dec_ref` |
| Cyclic | Broken (weak edges) | Any | Any | RC | `dec_ref` |
| Cyclic | Unbroken | Frozen | Any | SCC RC | `scc_release` |
| Cyclic | Unbroken | Mutable | Any | Symmetric RC | `sym_exit_scope` |
| Unknown | Unknown | Unknown | Any | Symmetric RC | `sym_exit_scope` |
| Any | N/A | N/A | Local-only | Arena/Region | `arena_*` / `region_*` |

Notes:
- Arenas are preferred for **non-escaping** complex cyclic structures.
- Symmetric RC is the safe fallback when shape is unknown.
- Weak edges can turn cyclic → DAG, enabling plain RC.

## GenRef / IPGE Soundness Requirements

Generational validation is **unsound** unless the generation field remains readable
after logical free. You must use **one** of these:

1) **Stable slot pool** (recommended)
2) **Quarantine allocator** (delay actual free)
3) **Indirection table** (stable metadata)

Reading `obj->generation` after `free(obj)` is undefined behavior in C.

## Region-Driven RC Elision (From Regions + RC Benchmarks)

This is a compile-time RC reduction strategy inspired by the Vale
Regions+RC cellular automata benchmark (local repo:
`/home/heefoo/Documents/code/RegionsRCCellularAutomataBenchmark`).
The key idea is **region-local borrowing**: if we can prove a value never
escapes its region, we can skip inc/dec entirely for all inner uses.

### Techniques to Implement

1) **Region-aware RC Elision**
   - If a value is proven region-local and immutable within a region scope,
     treat all references as borrowed → **no inc/dec** in inner loops.
   - Only emit RC ops when a value crosses a region boundary or becomes shared.

2) **Per-Region External Refcount**
   - Track the **number of escaping references** per region instead of per-object.
   - When a region object escapes, increment region externals.
   - When externals drop to 0, free the entire region in O(1).

3) **Borrow/Tether at Loop Entry**
   - Insert a single borrow/tether at loop or block entry.
   - Use fast-path deref inside hot loops (no repeated gen/RC checks).

4) **Escape-Driven Routing**
   - Combine escape + ownership + shape to route:
     - Region-local → no RC
     - Shared/escaping → RC
     - Complex local cycles → arena/region

5) **Reuse + Regions**
   - If unique and region-local, reuse in place instead of free+alloc
     (Perceus-style FBIP within region).

### Expected Impact

On RC-heavy, region-friendly code, this can eliminate the majority of RC ops
and shrink RC overhead substantially. Gains are workload-dependent but can
be large in nested-array or cellular-automata-style loops.

## Optimization Status (Documented)

Status is **documented** (may lag implementation). See runtime and compiler sources
for ground truth when in doubt.

| Optimization | Status | Evidence | Notes |
|--------------|--------|----------|-------|
| ASAP free insertion | Partial (C codegen) | `csrc/codegen/codegen.c` | Emits `free_obj` at analysis positions |
| Liveness / last-use | Partial | `csrc/analysis/analysis.c` | Free points computed, limited ownership logic |
| Escape / stack alloc | Planned | `csrc/analysis/analysis.h` | API exists, no codegen routing |
| Capture tracking | Partial | `csrc/analysis/analysis.c` | Captured vars marked; ownership integration minimal |
| Shape analysis | Planned (stub) | `csrc/analysis/analysis.c` | TODO in `omni_analyze_shape` |
| Back-edge weak refs | Runtime ready | `runtime/src/runtime.c` | Weak refs implemented; compile-time detection TBD |
| RC (DAG) | Runtime ready | `runtime/src/runtime.c` | `inc_ref` / `dec_ref` |
| SCC RC | Runtime ready | `runtime/src/runtime.c` | `scc_release` |
| Symmetric RC | Runtime ready | `runtime/src/runtime.c` | `sym_exit_scope` |
| Deferred RC | Runtime ready | `runtime/src/runtime.c` | `deferred_release` |
| Arenas / regions | Runtime ready | `runtime/src/runtime.c` | `arena_*` / region refs |
| Perceus reuse | Planned (analysis) | `csrc/analysis/analysis.c` | `omni_analyze_reuse` TODO |
| RC elision (borrow/unique) | Partial | `runtime/src/runtime.c` | Runtime helpers exist; analysis wiring limited |
| Region-aware RC elision | Planned | `runtime/src/runtime.c`, `csrc/analysis` | Needs compiler routing + region proof |
| Region external refcount | Planned | `runtime/src/runtime.c` | Requires per-region externals + escape tracking |
| GenRef / IPGE | Partial (unsound w/ malloc) | `runtime/src/runtime.c`, `runtime/include/purple.h` | Requires stable slots/quarantine |
| Region / constraint refs | Runtime ready | `runtime/src/runtime.c` | Safety-only (no reclaim) |
| Concurrency ownership | Planned | `csrc/analysis/analysis.h` | No full compiler routing yet |

## TODO: Pending Optimizations (Not Yet Implemented)

Use this as the checklist for remaining work. These are **not fully implemented**
or not wired into codegen yet.

1) **Escape‑aware stack allocation** (route local allocations to stack/arena)
2) **Full liveness‑driven free insertion** (last‑use frees in all paths)
3) **Ownership‑driven codegen** (borrow/consume/owned routing everywhere)
4) **Shape analysis + weak back‑edge routing** (compile‑time cycle breaking)
5) **Perceus reuse analysis** (free+alloc → reuse in codegen)
6) **Region‑aware RC elision** (skip inc/dec for region‑local borrows)
7) **Per‑region external refcount** (bulk free when externals reach 0)
8) **Borrow/tether loop insertion** (single check, hot‑loop fast path)
9) **Interprocedural summaries for ownership/escape** (caller‑callee RC routing)
10) **Concurrency ownership inference** (message‑passing transfer rules)
11) **GenRef/IPGE soundness fix** (stable slots/quarantine/indirection)

## Implementation Map (For New Contributors)

This section is intended for technically adept contributors who are new to the
codebase. It tells you **where to start**, **what to search for**, and **which
runtime hooks exist**.

### Core Code Locations

- **Codegen entry**: `csrc/codegen/codegen.c`
  - Search for: `omni_codegen_emit_frees`, `omni_codegen_generate_program`,
    `free_obj`, `dec_ref`
- **Analysis passes**: `csrc/analysis/analysis.c`, `csrc/analysis/analysis.h`
  - Search for: `omni_analyze_*`, `VarUsage`, `EscapeInfo`, `OwnerInfo`,
    `ShapeInfo`, `ReuseCandidate`
- **Runtime**: `runtime/src/runtime.c`
  - Search for: `inc_ref`, `dec_ref`, `free_obj`, `arena_`, `region_`,
    `scc_`, `sym_`, `borrow_`
- **Runtime public header**: `runtime/include/purple.h`
  - Search for: `BorrowedRef`, `ipge_`, `tether_`
- **Tests**: `runtime/tests/*`
  - Search for: `test_*` and `BorrowRef`, `arena`, `scc`, `deferred`

### Optimization → Implementation Guide

Each item below gives a minimal “map” for implementation:
**entry point**, **analysis data**, **runtime hooks**, and **search terms**.

1) **Escape‑aware stack allocation**
   - Entry point: `csrc/codegen/codegen.c` (allocation emission)
   - Analysis: `EscapeInfo` in `csrc/analysis/analysis.c`
   - Runtime: add/route to stack alloc helpers (search `mk_*` in `runtime/src/runtime.c`)
   - Search terms: `omni_analyze_escape`, `ESCAPE_NONE`, `mk_int`, `mk_pair`

2) **Full liveness‑driven free insertion**
   - Entry point: `omni_codegen_emit_frees` in `csrc/codegen/codegen.c`
   - Analysis: `VarUsage` + `OwnerInfo` + `omni_get_frees_at`
   - Runtime: `free_obj`, `dec_ref`
   - Search terms: `last_use`, `free_pos`, `omni_get_frees_at`

3) **Ownership‑driven codegen**
   - Entry point: call sites that emit `inc_ref/dec_ref` and let‑binding cleanup
   - Analysis: `OwnerInfo` (`OWNER_LOCAL`, `OWNER_BORROWED`, `OWNER_TRANSFERRED`)
   - Runtime: `inc_ref`, `dec_ref`, `free_unique`
   - Search terms: `OWNER_`, `must_free`, `ownership`

4) **Shape analysis + weak back‑edge routing**
   - Entry point: type/def handling + allocation routing
   - Analysis: `ShapeInfo`, `omni_analyze_shape`
   - Runtime: weak refs (`invalidate_weak_refs_for`), `dec_ref`, `scc_release`, `sym_exit_scope`
   - Search terms: `SHAPE_`, `back_edge`, `weak`

5) **Perceus reuse analysis**
   - Entry point: allocation emission in codegen (free+alloc → reuse)
   - Analysis: `ReuseCandidate`, `omni_analyze_reuse` (currently TODO)
   - Runtime: `reuse_as_*`, `can_reuse`, `consume_for_reuse`
   - Search terms: `reuse_as_`, `ReuseCandidate`, `Perceus`

6) **Region‑aware RC elision**
   - Entry point: inc/dec emission paths; add “region‑local” fast path
   - Analysis: escape + ownership + region boundaries
   - Runtime: `region_enter`, `region_exit`, `region_alloc`, `region_can_reference`
   - Search terms: `region_`, `region_ref`, `region_can_reference`

7) **Per‑region external refcount**
   - Entry point: places where values escape a region
   - Analysis: escape/ownership + region graph
   - Runtime: extend `Region`/`RegionObj` with `external_rc`, add `region_register_external`
   - Search terms: `RegionObj`, `region_exit`, `external`

8) **Borrow/tether loop insertion**
   - Entry point: loop/let codegen for borrowed vars
   - Analysis: ownership + purity/borrow classification
   - Runtime: `tether`, `untether`, `deref_tethered` in `runtime/include/purple.h`
   - Search terms: `tether`, `borrow_ref`, `deref_borrowed`

9) **Interprocedural summaries (ownership/escape)**
   - Entry point: function call codegen
   - Analysis: function summaries (add to `AnalysisContext`)
   - Runtime: no change required (compile‑time only)
   - Search terms: `summary`, `call`, `ownership`, `escape`

10) **Concurrency ownership inference**
   - Entry point: channel/spawn codegen (message passing)
   - Analysis: ownership transfer at send/recv
   - Runtime: `channel_*`, `spawn_*`
   - Search terms: `channel`, `spawn`, `ownership`

11) **GenRef/IPGE soundness fix**
   - Entry point: all BorrowedRef/BorrowRef access paths
   - Analysis: decide which allocations need gen‑safety
   - Runtime: implement one of:
     - stable slot pool
     - quarantine allocator
     - indirection table
   - Search terms: `BorrowRef`, `BorrowedRef`, `generation`, `ipge_`

### Suggested Workflow

1) Read `runtime/src/runtime.c` around the function names above.
2) Search `csrc/codegen/codegen.c` for emission sites of `inc_ref/dec_ref/free_obj`.
3) Follow analysis types in `csrc/analysis/analysis.h` to see what data exists.
4) Add a small runtime test in `runtime/tests/` for each new optimization.

### Example Transformations (Before / After)

These are **illustrative**; exact codegen formatting may differ.

1) **Liveness free insertion**
```c
// Before: free at scope end
Obj* x = mk_pair(a, b);
use(x);
/* ... */
free_obj(x);
```
```c
// After: free at last use
Obj* x = mk_pair(a, b);
use(x);
free_obj(x);
/* ... */
```

2) **Escape‑aware stack allocation**
```c
// Before
Obj* t = mk_int(42);
use(t);
dec_ref(t);
```
```c
// After (ESCAPE_NONE)
Obj* t = mk_int_stack(42);
use(t);
/* no free */
```

3) **Borrowed/ownership‑driven RC elision**
```c
// Before
inc_ref(arg);
use(arg);
dec_ref(arg);
```
```c
// After (borrowed)
use(arg);  /* no inc/dec */
```

4) **Perceus reuse**
```c
// Before
free_obj(x);
Obj* y = mk_pair(a, b);
```
```c
// After
Obj* y = reuse_as_pair(x, a, b);
```

5) **Region‑aware RC elision**
```c
// Before
inc_ref(v);
use(v);
dec_ref(v);
```
```c
// After (region‑local borrow)
use(v);  /* no RC inside region */
```

6) **Borrow/tether in hot loops**
```c
// Before
for (...) {
    Obj* x = deref_borrowed(r);
    use(x);
}
```
```c
// After
Obj* x = tether_borrowed(r);  /* once */
for (...) {
    use(x);  /* fast path */
}
untether(x);
```

7) **Shape‑driven routing (weak edges / cycles)**
```c
// DAG
dec_ref(obj);
```
```c
// Cyclic + unbroken + mutable
sym_exit_scope(scope_obj);
```

## Glossary + References (Quick Orientation)

**ASAP**: Compile‑time free insertion; no tracing GC (Proust 2017).
**RC**: Reference counting for shared DAGs; `inc_ref` / `dec_ref`.
**SCC RC**: Release frozen cycles via local SCC computation.
**Symmetric RC**: Track scope‑as‑object for mutable cycles; free when orphaned.
**Perceus**: Reuse analysis; free+alloc → in‑place reuse (PLDI 2021).
**DPS**: Destination‑passing style; write into caller‑provided memory.
**Regions**: Lexical lifetime zones; bulk deallocation; O(1) checks (Tofte‑Talpin/MLKit).
**GenRef/IPGE**: Generational refs for UAF detection; requires stable slot/quarantine.
**Weak ref / back‑edge**: Compile‑time cycle breaking by weakening back‑edges.
**Escape analysis**: Classify local/arg/global to choose allocation strategy.
**Liveness**: Find last use to free early.
**Shape analysis**: Tree / DAG / cyclic classification (Ghiya‑Hendren).

**Paper/keyword search terms:**
ASAP (Proust 2017), Perceus (Reinking PLDI 2021), Shape Analysis (Ghiya‑Hendren),
Region Inference (Tofte‑Talpin, MLKit), Destination‑Passing Style (FHPC 2017),
Symmetric RC / SCC RC, Vale Regions / Zero‑Cost Borrowing.

## GenRef/IPGE Runtime Soundness Audit (2026-01-03)

Findings:
- **IPGE/BorrowedRef reads `obj->generation` directly** from heap objects that are
  freed with `malloc/free` (`free_obj` → `flush_freelist`). After `free`, any
  generation check is **undefined behavior** unless memory is kept alive.
- **No stable slot pool/quarantine** exists for `Obj` allocations. The freelist
  delays frees but does not guarantee safety after a flush.
- **Legacy GenObj path** uses a separate object with locks and does not free the
  GenObj itself (safe for reads but leaks unless reclaimed separately).
- **Thread safety**: IPGE path has no locking; concurrent free/check can race.

Actions to make IPGE sound:
1) Allocate IPGE-managed objects from a **stable slot pool**, or
2) Add a **quarantine allocator** that delays `free`, or
3) Use an **indirection table** for generation metadata, or
4) Constrain borrow refs so they **cannot outlive** the freelist flush point.

## Optimization Layers (Unified)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         COMPILE-TIME ANALYSIS                            │
├─────────────────────────────────────────────────────────────────────────┤
│  1. Purity Analysis     → Which expressions are read-only?              │
│  2. Ownership Analysis  → Borrowed / Consumed / Owned (Lobster)         │
│  3. Uniqueness Analysis → Is this reference provably unique?            │
│  4. Escape Analysis     → Local / Arg / Global                          │
│  5. Shape Analysis      → Tree / DAG / Cyclic                           │
│  6. Reuse Analysis      → Can we reuse this allocation? (Perceus)       │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         OPTIMIZATION SELECTION                           │
├─────────────────────────────────────────────────────────────────────────┤
│  Pure + Borrowed    → Zero-cost access (no checks, no RC)               │
│  Pure + Owned       → Tethered access (skip repeated gen checks)        │
│  Unique + Local     → Direct free (no RC check)                         │
│  Consumed           → Ownership transfer (no inc_ref)                   │
│  Reuse Match        → In-place reuse (no alloc/free)                    │
│  Pool-eligible      → Bump allocation (O(1) alloc)                      │
│  Default            → Full safety (IPGE + RC)                           │
└─────────────────────────────────────────────────────────────────────────┘
```

## Phase 1: Purity Analysis (Vale-inspired)

### Concept
Automatically detect expressions that don't mutate captured state. These get zero-cost access.

### Detection Rules
```
PURE if:
  - Literal values (int, char, string, etc.)
  - Variable references (read-only)
  - Pure function calls (known pure primitives)
  - let/if/lambda bodies where all sub-expressions are pure
  - No set!, box-set!, or other mutation

IMPURE if:
  - Contains set!, box-set!, channel operations
  - Calls unknown/impure functions
  - Contains side effects (I/O, etc.)
```

### Implementation
```go
// pkg/analysis/purity.go
type PurityAnalyzer struct {
    KnownPure map[string]bool  // Built-in pure functions
}

func (a *PurityAnalyzer) IsPure(expr *ast.Value, env *PurityEnv) bool {
    if expr == nil || ast.IsNil(expr) {
        return true
    }

    switch expr.Tag {
    case ast.TInt, ast.TFloat, ast.TChar, ast.TSym:
        return true  // Literals are pure

    case ast.TCell:
        if ast.IsSym(expr.Car) {
            op := expr.Car.Str

            // Mutation operations are impure
            if op == "set!" || op == "box-set!" ||
               op == "chan-send!" || op == "atom-reset!" {
                return false
            }

            // Check if known pure function
            if a.KnownPure[op] {
                return a.AllArgsPure(expr.Cdr, env)
            }

            // let/if/lambda - check body
            if op == "let" || op == "if" || op == "lambda" {
                return a.AnalyzeSpecialForm(op, expr.Cdr, env)
            }
        }
        // Unknown call - conservatively impure
        return false
    }
    return false
}
```

### Codegen Impact
```go
func (g *CodeGenerator) GenerateAccess(varName string) string {
    if g.purityCtx.IsPureContext() && g.purityCtx.IsReadOnly(varName) {
        // Zero-cost: no IPGE check, no RC
        return varName
    }
    // Normal access with safety
    return g.GenerateSafeAccess(varName)
}
```

## Phase 2: Scope Tethering (Vale-inspired)

### Concept
When borrowing a reference in a scope, mark it "tethered" to skip repeated generation checks.

### Runtime Addition
```c
// Add to Obj struct (uses padding, no size increase)
typedef struct Obj {
    uint64_t generation;
    int mark;
    int tag;
    int is_pair;
    int scc_id;
    unsigned int scan_tag : 31;
    unsigned int tethered : 1;  // NEW: single bit flag
    union { ... };
} Obj;

// Tether operations
static inline void tether(Obj* obj) {
    if (obj && !IS_IMMEDIATE(obj)) obj->tethered = 1;
}

static inline void untether(Obj* obj) {
    if (obj && !IS_IMMEDIATE(obj)) obj->tethered = 0;
}

// Fast deref - skips gen check if tethered
static inline Obj* deref_tethered(Obj* obj, uint64_t expected_gen) {
    if (!obj) return NULL;
    if (IS_IMMEDIATE(obj)) return obj;
    if (obj->tethered) return obj;  // FAST PATH
    return (obj->generation == expected_gen) ? obj : NULL;
}
```

### Automatic Application
Compiler tethers at scope entry for borrowed references:
```c
// Generated for: (let ((x (get-item list))) (use x) (use x) (use x))
{
    Obj* x = get_item(list);
    tether(x);  // Inserted by compiler

    use(x);  // No gen check - tethered
    use(x);  // No gen check - tethered
    use(x);  // No gen check - tethered

    untether(x);  // Inserted by compiler
}
```

## Phase 3: Lobster Ownership Modes (Enhanced)

### Ownership Classification
```go
type OwnershipMode int
const (
    OwnershipOwned    OwnershipMode = iota  // Caller owns, must free
    OwnershipBorrowed                        // Temporary access, no RC
    OwnershipConsumed                        // Ownership transfers to callee
)
```

### Inference Rules
```
BORROWED if:
  - Parameter used read-only in function body
  - Let binding in pure context
  - Loop variable

CONSUMED if:
  - Parameter stored in data structure
  - Parameter returned from function
  - Parameter passed to consuming function

OWNED if:
  - Fresh allocation
  - Return value from function
  - Result of copy operation
```

### Codegen Impact
```go
func (g *CodeGenerator) GenerateCall(fn string, args []*ast.Value) string {
    var argStrs []string
    for i, arg := range args {
        mode := g.ownershipCtx.GetParamMode(fn, i)
        argStr := g.Generate(arg)

        switch mode {
        case OwnershipBorrowed:
            // No RC operations
            argStrs = append(argStrs, argStr)
        case OwnershipConsumed:
            // No inc_ref (ownership transfers)
            argStrs = append(argStrs, argStr)
            g.ownershipCtx.MarkTransferred(arg)
        case OwnershipOwned:
            // Normal RC
            argStrs = append(argStrs, argStr)
        }
    }
    return fmt.Sprintf("%s(%s)", fn, strings.Join(argStrs, ", "))
}
```

## Phase 4: Perceus Reuse Analysis

### Concept
When freeing an object, check if a same-sized allocation follows. If so, reuse the memory.

### Pattern Detection
```
REUSE CANDIDATE if:
  - dec_ref(x) followed by mk_*(...)
  - sizeof(x) == sizeof(new allocation)
  - x is provably dead after this point

Example:
  (let ((old (cons 1 2)))
    (let ((new (cons 3 4)))  ;; Can reuse old's memory
      new))
```

### Implementation
```go
// pkg/analysis/reuse.go
type ReuseAnalyzer struct {
    PendingFrees map[string]*FreeInfo  // Variables about to be freed
}

type FreeInfo struct {
    VarName  string
    TypeSize int
    Line     int
}

func (a *ReuseAnalyzer) FindReuseCandidate(allocType string, size int) *FreeInfo {
    for _, info := range a.PendingFrees {
        if info.TypeSize == size {
            delete(a.PendingFrees, info.VarName)
            return info
        }
    }
    return nil
}
```

### Codegen
```c
// Without reuse:
dec_ref(old);
Obj* new = mk_pair(mk_int(3), mk_int(4));

// With reuse:
Obj* new = reuse_as_pair(old, mk_int(3), mk_int(4));
// old's memory reused, no free/malloc
```

## Phase 5: Pool Allocation (Automatic)

### Eligibility Detection
```
POOL-ELIGIBLE if:
  - Allocated in let binding
  - Does not escape function (ESCAPE_NONE)
  - In pure or known-bounded context
  - Not captured by closure
```

### Implementation
```c
// Thread-local pool for temporary allocations
static __thread PurePool* _temp_pool = NULL;

static Obj* pool_mk_pair(Obj* a, Obj* b) {
    if (_temp_pool) {
        Obj* x = pure_pool_alloc(_temp_pool, sizeof(Obj));
        if (x) {
            x->generation = _next_generation();
            x->mark = -3;  // Special mark: pool-allocated
            x->tag = TAG_PAIR;
            x->is_pair = 1;
            x->a = a;
            x->b = b;
            return x;
        }
    }
    return mk_pair(a, b);  // Fallback to heap
}
```

### Scope Management
```c
// Generated for functions with pool-eligible allocations
Obj* process_list(Obj* items) {
    PurePool* _saved_pool = _temp_pool;
    _temp_pool = pure_pool_create(4096);

    // ... function body using pool_mk_* ...
    Obj* result = ...;

    // Move result to heap if escaping
    if (result && result->mark == -3) {
        result = deep_copy_to_heap(result);
    }

    pure_pool_destroy(_temp_pool);
    _temp_pool = _saved_pool;
    return result;
}
```

## Phase 6: NaN-Boxing for Floats

### Concept
Use NaN payload bits to store pointers, enabling unboxed floats.

### IEEE 754 Double Layout
```
Quiet NaN:  0 11111111111 1xxx...xxx (51 payload bits)
Signaling:  0 11111111111 0xxx...xxx

We use: 0x7FF8_0000_0000_0000 as NaN prefix
Payload: 48 bits for pointer (enough for current x86-64)
```

### Implementation
```c
#define NAN_BOXING_ENABLED 1

#if NAN_BOXING_ENABLED

#define NANBOX_PREFIX    0x7FF8000000000000ULL
#define NANBOX_MASK      0xFFFF000000000000ULL
#define NANBOX_PTR_MASK  0x0000FFFFFFFFFFFFULL

// Check if value is a boxed pointer
#define IS_NANBOXED_PTR(v)  (((v) & NANBOX_MASK) == NANBOX_PREFIX)

// Extract pointer from NaN-boxed value
#define NANBOX_TO_PTR(v)    ((Obj*)((v) & NANBOX_PTR_MASK))

// Box a pointer as NaN
#define PTR_TO_NANBOX(p)    (NANBOX_PREFIX | ((uint64_t)(p) & NANBOX_PTR_MASK))

// Universal value type
typedef union {
    double f;
    uint64_t bits;
} Value;

static inline int is_float(Value v) {
    // A real float won't have our NaN prefix (statistically impossible)
    return !IS_NANBOXED_PTR(v.bits) && !IS_IMMEDIATE(v.bits);
}

static inline double value_to_float(Value v) {
    return v.f;
}

static inline Obj* value_to_obj(Value v) {
    if (IS_NANBOXED_PTR(v.bits)) return NANBOX_TO_PTR(v.bits);
    if (IS_IMMEDIATE(v.bits)) return (Obj*)v.bits;
    return NULL;  // It's a float, not an object
}

#endif
```

## Combined Optimization Table

| Analysis Result | Optimization Applied | RC Cost | Check Cost | Alloc Cost |
|-----------------|---------------------|---------|------------|------------|
| Pure + Borrowed | Zero-cost access | 0 | 0 | N/A |
| Pure + Owned | Tethered access | 0 | 0 (after 1st) | N/A |
| Borrowed param | Skip RC | 0 | 1 | N/A |
| Consumed param | Transfer ownership | 0 | 1 | N/A |
| Unique + Local | Direct free | 0 | 0 | N/A |
| Reuse match | In-place reuse | 0 | 0 | 0 |
| Pool-eligible | Bump allocation | 0 | 0 | O(1) |
| Default | Full safety | 2 | 1 | O(1) |

## Implementation Order

| Phase | Feature | Complexity | Impact | Dependencies |
|-------|---------|------------|--------|--------------|
| 1a | Purity Analysis | Medium | Very High | None |
| 1b | Scope Tethering | Low | High | Runtime change |
| 2a | Ownership Modes | Medium | High | Purity |
| 2b | Perceus Reuse | Medium | Medium | Ownership |
| 3a | Pool Allocation | Medium | Medium | Escape analysis |
| 3b | NaN-Boxing | Low | Medium | None |

## Expected Results

### RC Operation Reduction
- Current: ~75% eliminated (Lobster-style)
- After Phase 1-2: ~90% eliminated
- After Phase 3: ~95% eliminated

### Check Reduction
- Current: 1 IPGE check per deref
- After Tethering: ~0.1 checks per deref (amortized)
- After Purity: 0 checks in pure contexts

### Allocation Speedup
- Current: malloc/free per object (~50ns)
- After Pool: bump allocation (~5ns)
- After Reuse: 0 allocation for reused objects

### Overall Hot-Path Improvement
Conservative estimate: **2-5× speedup** for typical functional code
Optimistic (pure-heavy): **5-10× speedup**

## Backward Compatibility

All optimizations are transparent:
- No new syntax required
- Existing code works unchanged
- Compiler infers everything
- Falls back to safe defaults when analysis fails

Optional hints (future):
```scheme
;; Hint that function is pure (helps analysis)
(define (sum xs) ^pure
  (fold + 0 xs))

;; Hint that parameter is consumed
(define (push-all target items) ^(consumed target)
  ...)
```

These hints are **optional** - just help the compiler when inference is insufficient.
