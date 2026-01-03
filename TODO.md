# Purple Go TODO

## Priority: Eliminate Weak Reference Exposure to Users

**Goal**: Users should never need to use `WeakRef` directly. The compiler should automatically detect back-edges in cyclic data structures and handle them internally.

### Current State

The Go implementation has partial infrastructure:
- `TypeRegistry` with `OwnershipEdge` and `IsBackEdge` field ✅
- `AnalyzeBackEdges()` using DFS cycle detection ✅
- `markFieldWeak()` to mark back-edge fields ✅
- `WeakRef` runtime support ✅

**Gap**: This infrastructure isn't connected to actual user-defined types or codegen. The type registry only has hardcoded defaults (`Pair`, `List`, `Tree`).

---

### Phase 1: Type Definition Syntax & Registry Population

**Files**: `pkg/parser/parser.go`, `pkg/eval/eval.go`, `pkg/codegen/types.go`

1. Add `deftype` form to the language:
   ```scheme
   (deftype Node
     (value int)
     (next Node)
     (prev Node))   ; compiler will infer this is a back-edge
   ```

2. During parsing/eval, populate `TypeRegistry` from `deftype` forms

3. Wire up `BuildOwnershipGraph()` and `AnalyzeBackEdges()` after all types are registered

**Acceptance**: User-defined types appear in registry with correct field info

---

### Phase 2: Back-Edge Heuristics

**Files**: `pkg/codegen/types.go`

Enhance `AnalyzeBackEdges()` with naming heuristics:

| Field Pattern | Inference |
|---------------|-----------|
| `parent`, `owner`, `container` | Likely back-edge (points to ancestor) |
| `prev`, `back`, `up` | Likely back-edge (reverse direction) |
| Second field of same type | Candidate for back-edge |

Algorithm:
```
for each type T:
  for each field F where F.Type could form cycle with T:
    if F.name in BACK_EDGE_HINTS:
      mark F as weak (high confidence)
    else if already have owning field of same type:
      mark F as weak (medium confidence)
    else:
      use DFS cycle detection (current approach)
```

**Acceptance**: `Node.prev` auto-detected as weak without DFS

---

### Phase 3: Codegen Integration

**Files**: `pkg/codegen/codegen.go`, `pkg/codegen/runtime.go`

1. Generate field accessors that respect strength:
   ```c
   // Strong field - normal access
   Obj* get_next(Node* n) { return n->next; }

   // Weak field - no inc_ref, just read
   Obj* get_prev(Node* n) { return n->prev; }  // no ownership
   ```

2. Generate release functions that skip weak fields:
   ```c
   void release_Node(Node* n) {
       dec_ref(n->next);   // strong - decrement
       // n->prev skipped  // weak - don't decrement
       free(n);
   }
   ```

3. Remove `mk_weak_ref` from public API (keep internal)

**Acceptance**: Doubly-linked list works without user-visible WeakRef

---

### Phase 4: Shape-Aware Back-Edge Routing

**Files**: `pkg/analysis/shape.go`, `pkg/codegen/codegen.go`

Integrate shape analysis with back-edge detection:

```
Shape Analysis Result → Memory Strategy
─────────────────────────────────────────
TREE                 → free_tree (no back-edges possible)
DAG                  → dec_ref (no cycles)
CYCLIC + frozen      → SCC-based RC
CYCLIC + mutable     → auto-weak back-edges + dec_ref
CYCLIC + unknown     → arena allocation (fallback)
```

**Acceptance**: `letrec` cycles handled without user annotation

---

### Phase 5: Constructor-Level Ownership Tracking

**Files**: `pkg/analysis/ownership.go` (new), `pkg/codegen/codegen.go`

Track ownership at construction sites, not just types:

```scheme
(let ((a (mk-node 1 nil nil)))      ; a owns node
  (let ((b (mk-node 2 a nil)))      ; b.next owns a? No - a already owned
    (set! (node-prev a) b)          ; back-edge: a.prev -> b (non-owning)
    ...))
```

The `set!` to `prev` is automatically non-owning because:
1. `prev` field detected as back-edge pattern
2. `b` is already owned by enclosing scope

**Acceptance**: Circular references via `set!` handled automatically

---

### Phase 6: Testing & Validation

**Files**: `pkg/codegen/backedge_test.go` (new)

Test cases:
1. Doubly-linked list (prev is weak)
2. Tree with parent pointers (parent is weak)
3. Graph with arbitrary edges (arena fallback)
4. Closure capturing cyclic data
5. `letrec` mutual recursion

Validation:
- No memory leaks (valgrind)
- No use-after-free
- No double-free
- Deterministic behavior

---

## Backlog: Memory Architecture Enhancements (Post‑11)

These are ASAP‑compatible enhancements (no language restrictions, no STW GC).
See `ROADMAP.md` Phase 9 and `docs/UNIFIED_OPTIMIZATION_PLAN.md` for sketches.

1) Linear/offset regions for serialization & FFI  
   - Search terms: `RegionContext`, `region_enter`, `region_alloc`, `region_ref_deref`
2) Pluggable region backends (IRegion‑style vtable)  
   - Search terms: `region_alloc`, `arena_alloc`, `free_tree`, `dec_ref`
3) Weak ref control blocks (merge‑friendly)  
   - Search terms: `Weak`, `weak`, `invalidate_weak`, `BorrowRef`
4) Transmigration / isolation on region escape  
   - Search terms: `ShapeInfo`, `ESCAPE_`, `scan_`, `release_children`
5) External handle indexing (FFI + determinism)  
   - Search terms: `BorrowRef`, `ipge_`, `generation`, `Handle`, `tag`

References (Vale docs):
- `Vale/docs/LinearRegion.md`
- `Vale/docs/IRegion.md`
- `Vale/docs/WeakRef.md`
- `Vale/docs/regions/Transmigration.md`
- `Vale/docs/PerfectReplayability.md`

---

## Secondary: Port Missing Features from purple_c_scratch

### Destination-Passing Style (DPS)

**Reference**: purple_c_scratch/src/analysis/dps.c

Enables stack allocation of return values:
```scheme
;; Current: heap allocates result
(define (map f xs) ...)

;; DPS: caller provides destination
(define (map-into! dest f xs) ...)
```

**Files to create**: `pkg/analysis/dps.go`, `pkg/codegen/dps.go`

---

### Exception Handling (Landing Pads)

**Reference**: purple_c_scratch/src/memory/exception.h

Generate cleanup metadata for stack unwinding:
```c
// At each try point, track live allocations
// Generate landing pads that free live objects on unwind
```

**Files to create**: `pkg/codegen/exception.go`

**Depends on**: `try`/`catch` forms (already implemented)

---

### Concurrency (Ownership Transfer)

**Reference**: purple_c_scratch/src/memory/concurrent.h

Ownership classes:
```go
const (
    OwnLocal       // Thread-local, pure ASAP
    OwnTransferred // Ownership moves via channel
    OwnShared      // Atomic RC required
    OwnImmutable   // No sync needed
)
```

**Files to create**: `pkg/analysis/concurrent.go`, `pkg/codegen/concurrent.go`

**Depends on**: Adding channel/thread primitives to language

---

### Interprocedural Analysis (Function Summaries)

Summarize each function's memory behavior:
```
process : (xs: List @borrowed) -> List @fresh
  consumes: none
  escapes: return value
  allocates: O(n)
```

**Files to create**: `pkg/analysis/summary.go`

**Benefits**:
- Cross-function ownership tracking
- Better escape analysis
- Enables more ASAP optimizations

---

### Perceus Reuse Analysis

**Reference**: Reinking et al., PLDI 2021

Pair `free` with subsequent `alloc` of same size:
```c
// Before
free_obj(x);
y = mk_int(42);

// After (reuse x's memory)
y = reuse_as_int(x, 42);
```

**Files to create**: `pkg/analysis/reuse.go`, `pkg/codegen/reuse.go`

---

## Comparison: purple_c_scratch vs purple_go

| Feature | purple_c_scratch | purple_go | Priority |
|---------|------------------|-----------|----------|
| Type registry | Full ownership graph | Partial (hardcoded) | **HIGH** |
| Back-edge inference | Auto from types | Infrastructure only | **HIGH** |
| WeakRef exposure | Internal only | User-visible | **HIGH** |
| DPS | Implemented | Missing | Medium |
| Exception cleanup | Implemented | Missing | Medium |
| Concurrency | Ownership transfer | Missing | Medium |
| Interprocedural | Partial | Missing | Medium |
| Perceus reuse | Planned | Missing | Low |
| Shape analysis | Full | Full | ✅ Done |
| Escape analysis | Full | Full | ✅ Done |
| Liveness analysis | Full | Full | ✅ Done |
| SCC-based RC | Full | Full | ✅ Done |
| Deferred RC | Full | Full | ✅ Done |
| Arena allocation | Full | Full | ✅ Done |

---

## Implementation Order

```
Phase 1: deftype + registry population     [Week 1]
    ↓
Phase 2: Back-edge heuristics              [Week 1]
    ↓
Phase 3: Codegen integration               [Week 2]
    ↓
Phase 4: Shape-aware routing               [Week 2]
    ↓
Phase 5: Constructor ownership             [Week 3]
    ↓
Phase 6: Testing                           [Week 3]
    ↓
DPS / Exception / Concurrency              [Future]
```

---

# Missing Features from Original Purple (HVM4)

Comparison between our Go implementation and the original Purple at `/home/heefoo/Documents/code/purple`.

## Legend
- ✅ Implemented in purple_go
- ❌ Missing from purple_go
- 🔶 Partially implemented

---

## Core Language Features

### Data Types
| Feature | Status | Notes |
|---------|--------|-------|
| Numbers (integers) | ✅ | `TInt` |
| Symbols | ✅ | `TSym` |
| Pairs/Cons cells | ✅ | `TCell` |
| Nil | ✅ | `TNil` |
| Characters (`#\a`, `#\newline`) | ✅ | `TChar` with named chars |
| Strings (as char lists) | ✅ | Quoted lists of `TChar` |
| Closures | ✅ | `TLambda` |
| Code values | ✅ | `TCode` |
| Error values | ✅ | `TError` with message |

### Special Forms
| Feature | Status | Notes |
|---------|--------|-------|
| `lambda` | ✅ | Basic lambda |
| `lambda self` (recursive) | ✅ | `(lambda self (x) body)` for self-reference |
| `let` | ✅ | Single binding |
| `letrec` | ✅ | Recursive bindings |
| `if` | ✅ | Conditional |
| `quote` | ✅ | Quote expression |
| `and` / `or` | ✅ | Short-circuit logic |
| `do` (sequencing) | ✅ | `(do e1 e2 ... en)` returns last |
| `match` | ✅ | Full pattern matching |

### Pattern Matching (✅ Implemented)
| Feature | Description |
|---------|-------------|
| Wildcard `_` | Matches anything, binds nothing |
| Variable patterns | `x` matches and binds |
| Literal patterns | `(42)` matches specific value |
| Constructor patterns | `(CON a b)` destructures |
| Nested patterns | `(CON (CON a b) c)` |
| Or-patterns | `(or pat1 pat2)` alternatives |
| As-patterns | `(x @ pat)` bind whole and parts |
| List patterns | `(list a b . rest)` |
| Guards | `:when condition` |

---

## Stage-Polymorphic Evaluation

| Feature | Status | Notes |
|---------|--------|-------|
| `lift` | ✅ | Quote value as code |
| `run` | ✅ | `(run code)` execute code at base level |
| `code` / `quote` | ✅ | Quote as AST |
| Compile mode | ✅ | Full 9-handler tower support |

---

## Meta-Level / Reflective Features (✅ Implemented)

| Feature | Status | Description |
|---------|--------|-------------|
| `EM` | ✅ | Execute at parent meta-level |
| `shift` | ✅ | `(shift n expr)` Go up n levels and evaluate |
| `clambda` | ✅ | Compile lambda under current semantics |
| `meta-level` | ✅ | `(meta-level)` Get current tower level |
| `get-meta` | ✅ | `(get-meta 'name)` Fetch handler by name |
| `set-meta!` | ✅ | `(set-meta! 'name fn)` Install custom handler |
| `with-menv` | ✅ | `(with-menv menv body)` Evaluate with custom menv |
| `with-handlers` | ✅ | `(with-handlers ((name fn) ...) body)` |
| `default-handler` | ✅ | `(default-handler 'name arg)` Delegate to default |

### Handler Customization (✅ 9-Handler Table)
- `lit` handler - numeric literal evaluation
- `var` handler - variable lookup
- `lam` handler - lambda creation
- `app` handler - function application
- `if` handler - conditional
- `lft` handler - lift operation
- `run` handler - code execution
- `em` handler - meta-level jump
- `clam` handler - compiled lambda

---

## Primitives

### Arithmetic
| Feature | Status |
|---------|--------|
| `+`, `-`, `*`, `/`, `%` | ✅ |

### Comparison
| Feature | Status |
|---------|--------|
| `=`, `<`, `>`, `<=`, `>=` | ✅ |
| `not` | ✅ |

### List Operations
| Feature | Status | Notes |
|---------|--------|-------|
| `cons` | ✅ | |
| `car` / `fst` | ✅ | |
| `cdr` / `snd` | ✅ | |
| `null?` | ✅ | |
| `map` | ✅ | Higher-order |
| `filter` | ✅ | Higher-order |
| `fold` / `foldr` | ✅ | Right fold |
| `foldl` | ✅ | Left fold |
| `length` | ✅ | |
| `append` | ✅ | |
| `reverse` | ✅ | |
| `apply` | ✅ | Apply fn to arg list |

### Function Combinators (✅ Implemented)
| Feature | Description |
|---------|-------------|
| `compose` | `(compose f g)` → f ∘ g |
| `flip` | Swap argument order |

---

## Introspection

| Feature | Status | Description |
|---------|--------|-------------|
| `ctr-tag` | ❌ | Extract constructor name |
| `ctr-arg` | ❌ | Extract constructor argument by index |
| `reify-env` | ❌ | Return current environment as value |
| `gensym` | ✅ | Generate unique symbol |
| `eval` | ✅ | Evaluate code at runtime |
| `sym-eq?` | ✅ | Symbol equality check |
| `trace` | ✅ | Trace value during evaluation |

---

## Error Handling (✅ Implemented)

| Feature | Status | Description |
|---------|--------|-------------|
| `error` | ✅ | Raise error with message |
| `try` | ✅ | Catch errors with handler |
| `assert` | ✅ | Conditional error |
| `default-handler` | ❌ | Delegate to default |

---

## I/O and FFI

| Feature | Status | Description |
|---------|--------|-------------|
| `(ffi "func" args)` | ✅ | Call external C function |
| `(ffi-declare ...)` | ✅ | Declare external function |
| `ffi "puts"` | ✅ | Write string to stdout |
| `ffi "putchar"` | ✅ | Write single character |
| `ffi "getchar"` | ✅ | Read single character |
| `ffi "exit"` | ✅ | Exit with code |
| `trace` | ✅ | Evaluate and trace value |
| Tensor/libtorch | ❌ | See LIBTORCH_PLAN.md |

---

## Macro System

| Feature | Status | Description |
|---------|--------|-------------|
| Quasiquote `` ` `` | ✅ | Quote with evaluation |
| Unquote `,` | ✅ | Evaluate in quasiquote |
| Unquote-splicing `,@` | ✅ | Splice list |
| `defmacro` | ✅ | Define macro with transformer |
| `mcall` | ✅ | Call macro by name |
| `macroexpand` | ✅ | Expand without eval |

---

## Implementation Details

### Variable Representation
| Feature | Status | Notes |
|---------|--------|-------|
| Named variables | ✅ | Current approach |
| De Bruijn indices | ❌ | Original uses indices |

### Memory Management
| Feature | Status | Notes |
|---------|--------|-------|
| ASAP free insertion | ✅ | Compile-time |
| Shape analysis | ✅ | TREE/DAG/CYCLIC |
| Arena allocation | ✅ | For cyclic data |
| Weak edges | ✅ | Break ownership cycles |
| HVM4 interaction nets | ❌ | Original uses HVM4 |

---

## Priority Order for Implementation

### High Priority (Core Language) - ✅ COMPLETE
1. ✅ **Pattern matching** - fundamental for idiomatic code
2. ✅ **Recursive lambda** - `(lambda self ...)` for cleaner recursion
3. ✅ **Error handling** - `error`, `try`, `assert`
4. ✅ **List operations** - `map`, `filter`, `fold`, etc.

### Medium Priority (Staging) - ✅ COMPLETE
5. ✅ **`run` form** - execute code at base level
6. ✅ **Meta-level operations** - EM, shift, clambda, meta-level
7. ✅ **Handler customization** - 9-handler table with get/set-meta!, with-handlers

### Lower Priority (Convenience) - ✅ COMPLETE
8. ✅ **Quasiquote** - template syntax
9. ✅ **Macro system** - syntactic abstraction (defmacro, mcall, macroexpand)
10. ✅ **FFI/I/O** - practical programs
11. ✅ **Characters/strings** - text handling
12. ✅ **Introspection** - gensym, eval, sym-eq?, trace
13. ✅ **JIT execution** - Runtime C code execution via GCC

---

## References

- Original Purple: `/home/heefoo/Documents/code/purple`
- "Collapsing Towers of Interpreters" (Amin & Rompf, POPL 2018)
- HVM4: Higher Order Virtual Machine
