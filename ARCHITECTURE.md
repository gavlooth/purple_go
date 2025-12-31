# Architecture

## Core Idea
Purple Go is a **stage-polymorphic evaluator**: the same evaluator interprets concrete values and compiles lifted values to C. Memory management is **ASAP first** (compile-time free insertion), and every runtime optimization sits **on top of** that baseline. The system explicitly forbids stop-the-world GC.

## Layers (High-Level)

### 1) Evaluator / Codegen
- `Eval` returns either concrete values or `Code` values.
- Codegen emits a standalone ANSI C99 runtime plus the compiled expression.
- Stage-polymorphic: same code path handles both interpretation and compilation.

### 2) ASAP (Base Engine)
- Compile-time liveness + shape analysis decides *where* and *how* to free.
- Produces `free_tree`, `dec_ref`, `deferred_release`, etc.
- Variables captured by closures are NOT freed in parent scope.

### 3) Optimizations on Top of ASAP
- **Shape Analysis**: TREE/DAG/CYCLIC strategies (Ghiya-Hendren).
- **DAG RC**: `inc_ref`/`dec_ref` for shared acyclic graphs.
- **SCC RC** (frozen cycles): Tarjan on a *local* subgraph; no global pause.
- **Deferred RC** (mutable cycles): bounded O(k) work at safe points.
- **Weak Refs**: explicit invalidation on free.
- **Perceus Reuse**: reuse eligible objects (no global scans).
- **Arena Scopes**: bulk allocation/free for cyclic data that does not escape.

## Engine Constraints (Non-Negotiable)
- **No stop-the-world GC**: no global traversal or global pauses.
- **Locality**: work is per-object, per-scope, or per-subgraph.
- **Bounded Work**: deferred RC is capped per safe point; SCC computation is local to frozen graphs.
- **Explicit Registration**: external references are explicitly registered (e.g., arena externals).

## What "ASAP First" Means
- The compiler must remain correct **with only ASAP**.
- All other engines are optional accelerators or correctness patches for specific shapes.
- If an optimization fails or is disabled, the code should still be memory-safe under ASAP.

## Revision: Relaxed ASAP with Arenas + Weak Edges

Relax the "ASAP First" rule slightly while keeping the **no stop-the-world** guarantee:

### Key Changes
1. **Embrace bulk deallocation via arenas**
   - Arenas allow cyclic structures without per-object tracking
   - O(1) deallocation of entire arena at scope exit
   - No lifetime inference needed for arena-allocated objects

2. **Allow cycles without lifetime inference**
   - Weak edges break ownership cycles at compile time
   - Back-edge detection via type analysis (no programmer annotation)
   - Arena scopes handle complex cyclic graphs

3. **Preserve static determinism**
   - All deallocation points are still known at compile time
   - No runtime cycle detection or tracing
   - Bounded, predictable memory behavior

### Why This Works
- **Arenas + weak edges** provide a practical escape hatch for cycles
- **Static analysis** still drives most decisions (shape, escape, liveness)
- **No global pauses**: arena free is O(1), weak invalidation is O(weak_refs)
- **Graceful degradation**: falls back to arena if shape analysis is uncertain

### Trade-offs
| Pure ASAP | Relaxed ASAP |
|-----------|--------------|
| Per-object free injection | Arena bulk free for cycles |
| Requires acyclic or inferred lifetimes | Allows arbitrary cycles in arenas |
| Zero runtime overhead | Small arena bookkeeping |
| May reject valid programs | Accepts more programs |

## Hybrid Memory Strategy (v0.4.0)

The compiler now uses a **hybrid strategy** that selects the optimal memory management technique based on static analysis:

```
┌─────────────────────────────────────────────────────────────────┐
│                    COMPILE-TIME ANALYSIS                         │
│                                                                  │
│  Shape Analysis ──► TREE / DAG / CYCLIC                         │
│  Back-Edge Detection ──► cycles_broken: true/false              │
│  Escape Analysis ──► local / arg / global                       │
│  RC Optimization ──► unique / alias / borrowed                  │
└─────────────────────────────────────────────────────────────────┘
                              │
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
       TREE/DAG          CYCLIC+broken      CYCLIC+unbroken
          │                   │                   │
          ▼                   ▼                   ▼
   Pure ASAP/RC          dec_ref           Symmetric RC
   (free_tree/dec_ref)   (weak edges)      (scope-as-object)
```

### Strategy Selection

| Shape | Cycle Status | Frozen | Strategy | Function |
|-------|--------------|--------|----------|----------|
| TREE | - | - | ASAP | `free_tree()` |
| DAG | - | - | Standard RC | `dec_ref()` |
| CYCLIC | Broken | - | Standard RC | `dec_ref()` |
| CYCLIC | Unbroken | Yes | SCC-based RC | `scc_release()` |
| CYCLIC | Unbroken | No | **Symmetric RC** | `sym_exit_scope()` |
| Unknown | - | - | Symmetric RC | `sym_exit_scope()` |

### Why Symmetric RC for Unbroken Cycles?

Previously, unbroken cycles used **arena allocation**. Symmetric RC is now preferred:

| Aspect | Arena | Symmetric RC |
|--------|-------|--------------|
| Peak memory | High (holds until scope) | Low (immediate free) |
| Long scopes | Everything held | Freed as orphaned |
| Cycle collection | At scope exit only | Immediate when orphaned |
| Memory pattern | O(scope_lifetime) | O(object_lifetime) |

Arena remains available as **opt-in** for batch allocation scenarios.

### RC Optimization Layer

On top of strategy selection, the **RC Optimization** layer eliminates redundant operations:

| Optimization | Description | Elimination |
|--------------|-------------|-------------|
| Uniqueness | Proven single ref | Skip RC check → `free_unique()` |
| Aliasing | Multiple vars, same object | Only one does RC |
| Borrowing | Parameters, temps | Zero RC operations |

Typical elimination rate: **~75%** of RC operations.

## Implemented Algorithms

### Escape Analysis
- **Classification**: `ESCAPE_NONE` (local), `ESCAPE_ARG` (escapes via argument), `ESCAPE_GLOBAL` (escapes to return/closure)
- **Capture Tracking**: Variables captured by lambdas are marked; ownership transfers to closure
- **Reference**: Aiken et al., "Better Static Memory Management" (PLDI 1995)

### Shape Analysis (Ghiya-Hendren)
- **TREE**: No sharing, no cycles → direct free (`free_tree`)
- **DAG**: Sharing but acyclic → reference counting (`dec_ref`)
- **CYCLIC**: May have cycles → SCC-based RC or deferred RC
- **Lattice**: TREE < DAG < CYCLIC (join = max)
- **Reference**: Ghiya & Hendren, "Is it a tree, a DAG, or a cyclic graph?" (POPL 1996)

### Liveness Analysis
- Track last use of each variable
- Free at last-use point, not just scope-end (Non-Lexical Lifetimes)
- **Reference**: Rust Borrow Checker / Polonius

### SCC-Based Reference Counting (ISMM 2024)
- Tarjan's algorithm to find strongly connected components
- Single reference count per SCC (not per-object)
- Only for frozen (immutable) cyclic structures
- O(n) in SCC size, not heap size
- **Reference**: ISMM 2024 approach for deeply immutable cycles

### Deferred Reference Counting
- Bounded processing at safe points
- O(k) work per safe point, where k = batch_size
- Fallback for mutable cyclic structures
- **Reference**: CactusRef-style local cycle detection

### Arena Allocation
- Bulk allocation from memory blocks
- O(1) deallocation of entire arena
- For cyclic data that doesn't escape function scope
- **Reference**: Tofte & Talpin, Region-Based Memory Management

### Perceus Reuse Analysis
- Pair deallocations with allocations of same size
- Enables in-place updates ("Functional But In-Place")
- **Reference**: Reinking et al., "Perceus: Garbage Free Reference Counting with Reuse" (PLDI 2021)

### RC Optimization (Lobster-style)
- Compile-time elimination of ~75-95% of reference counting operations
- **Uniqueness Analysis**: Prove when a reference is the only one → use `free_unique()` directly
- **Alias Tracking**: Track when multiple variables point to same object → elide redundant RC ops
- **Borrow Tracking**: Parameters without ownership transfer → no RC operations
- **Implementation**: `pkg/analysis/rcopt.go`
- **Reference**: Lobster language (https://aardappel.github.io/lobster/memory_management.html)

### Symmetric Reference Counting (Hybrid Strategy)
- **Default for unbroken cycles** - more memory efficient than arenas
- **Key insight**: Treat scope as an object that participates in ownership graph
- **External refs**: From live scopes/roots
- **Internal refs**: Within the object graph
- **When external_rc drops to 0**, the cycle is orphaned → freed immediately
- **O(1) deterministic** cycle collection without global GC
- **Implementation**: `pkg/memory/symmetric.go`
- **Generated runtime**: `sym_enter_scope()`, `sym_exit_scope()`, `sym_alloc()`, `sym_link()`

## Package Structure

```
purple_go/
├── main.go                    # CLI entry point
├── pkg/
│   ├── ast/
│   │   └── value.go           # Core Value type (tagged union)
│   ├── parser/
│   │   └── parser.go          # S-expression parser
│   ├── eval/
│   │   ├── eval.go            # Stage-polymorphic evaluator + deftype
│   │   ├── env.go             # Environment handling
│   │   └── primitives.go      # Built-in primitives
│   ├── analysis/
│   │   ├── escape.go          # Escape analysis
│   │   ├── shape.go           # Shape analysis + CyclicFreeStrategy
│   │   ├── liveness.go        # Liveness analysis
│   │   └── ownership.go       # Constructor-level ownership tracking
│   ├── codegen/
│   │   ├── codegen.go         # C99 code generation
│   │   ├── runtime.go         # Runtime header generation (integrated)
│   │   ├── types.go           # Type registry + back-edge detection
│   │   ├── arena.go           # Arena code generation
│   │   └── weak_edge.go       # Weak edge detection
│   └── memory/
│       ├── asap.go            # ASAP CLEAN phase
│       ├── scc.go             # SCC-based RC (Tarjan) - standalone
│       ├── deferred.go        # Deferred RC - standalone
│       └── arena.go           # Arena allocator - standalone
└── test/
    ├── deftype_test.go        # Type system tests
    └── backedge_integration_test.go  # Back-edge detection tests
```

### Integration Status

| Component | Location | Status |
|-----------|----------|--------|
| Back-edge detection | `codegen/types.go` | **Integrated** - auto-detects weak fields |
| Type-aware release | `codegen/runtime.go` | **Integrated** - skips weak fields |
| Ownership tracking | `analysis/ownership.go` | **Integrated** - field-strength aware |
| Shape routing | `analysis/shape.go` | **Integrated** - CycleStatus enum |
| Arena runtime | `codegen/runtime.go` | **Integrated** - with externals support |
| SCC runtime | `codegen/runtime.go` | **Integrated** - Tarjan's algorithm |
| Deferred RC | `codegen/runtime.go` | **Integrated** - bounded processing |

### Runtime Features

The `pkg/codegen/runtime.go` now includes all memory management strategies:

1. **Arena with externals**:
   - `arena_create()`, `arena_destroy()`, `arena_reset()`
   - `arena_register_external()` for tracking pointers that escape arena
   - Separate memory pointer per block for robust management

2. **SCC-based RC (ISMM 2024)**:
   - Full Tarjan's algorithm for O(n) SCC detection
   - `detect_and_freeze_sccs()` for automatic SCC detection
   - `release_with_scc()` for SCC-aware release
   - Single reference count per SCC for frozen cycles

3. **Deferred RC**:
   - `defer_decrement()` with O(1) coalescing
   - `process_deferred()` for bounded O(k) work per safe point
   - `safe_point()` for automatic processing at function boundaries
   - `flush_deferred()` for cleanup at program exit

4. **Perceus Reuse**:
   - `try_reuse()`, `reuse_as_int()`, `reuse_as_pair()`
   - Enables "Functional But In-Place" (FBIP) programming

The standalone implementations in `pkg/memory/` are kept for reference and testing.

## Reference Counting Techniques

### Standard Reference Counting
Basic reference counting with `inc_ref`/`dec_ref`. Used for DAG-shaped data.
- **Pros**: Immediate reclamation, no pauses, simple
- **Cons**: Cannot handle cycles, overhead per pointer operation
- **Reference**: Collins, G. E. "A method for overlapping and erasure of lists" (CACM 1960)

### Deferred Reference Counting
Delays decrements to batch them at safe points, reducing overhead.
- Bounded O(k) work per safe point
- Avoids immediate cascade of decrements
- **Reference**: Deutsch & Bobrow, "An Efficient, Incremental, Automatic Garbage Collector" (CACM 1976)

### Weighted Reference Counting
Assigns weights to references; splitting a reference divides the weight.
- Enables efficient copying without updating original
- **Reference**: Watson & Watson, "An Efficient Garbage Collection Scheme for Parallel Computer Architectures" (1987)

### Typed Reference Counting (Back-Edge Detection)
Uses type information to identify which fields can form back-edges (cycles).
- Compiler automatically makes back-edge fields weak
- No programmer annotation needed
- **Reference**: Bacon & Rajan, "Concurrent Cycle Collection in Reference Counted Systems" (ECOOP 2001)
- **Reference**: Lins, "Cyclic Reference Counting by Typed Reference Fields" (Science of Computer Programming 2012)

### Trial Deletion / Local Mark-Scan
Performs local reachability analysis when dropping potential cycle members.
- O(cycle_size) not O(heap_size)
- Deterministic, happens at drop point
- **Reference**: Christopher, "Reference count garbage collection" (Software Practice & Experience 1984)
- **Reference**: Lins, "Cyclic Reference Counting with Lazy Mark-Scan" (Information Processing Letters 1992)

### SCC-Based Reference Counting
Groups strongly connected components into single reference count units.
- Uses Tarjan's algorithm for SCC detection
- Single RC for entire frozen cycle
- **Reference**: Paz et al., "An Efficient On-the-Fly Cycle Collection" (TOPLAS 2007)

### CactusRef-Style Cycle Collection
Local cycle detection without global scanning.
- Tracks internal vs external reference counts
- Orphaned cycles detected and freed immediately
- **Reference**: https://github.com/artichoke/cactusref

### Perceus: Reuse Analysis
Pairs deallocations with allocations for in-place reuse.
- "Functional But In-Place" (FBIP) programming
- Eliminates allocation when object is about to be freed
- **Reference**: Reinking et al., "Perceus: Garbage Free Reference Counting with Reuse" (PLDI 2021)
  - https://dl.acm.org/doi/10.1145/3453483.3454032

### Lobster-Style Compile-Time RC
Compile-time analysis determines where RC operations are needed.
- Many RC operations eliminated statically
- Similar to ASAP philosophy
- **Reference**: https://aardappel.github.io/lobster/memory_management.html

## Key References

### Memory Management (General)
1. **ASAP**: Proust, "As Static As Possible memory management" (2017)
   - https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-908.pdf
2. **Practical Static Memory**: Corbyn, "Practical Static Memory Management" (2020)
   - https://nathancorbyn.com/pdf/practical_static_memory_management.pdf

### Shape and Escape Analysis
3. **Shape Analysis**: Ghiya & Hendren, "Is it a tree, a DAG, or a cyclic graph?" (POPL 1996)
   - https://dl.acm.org/doi/10.1145/237721.237724
4. **Static Memory Management**: Aiken et al., "Better Static Memory Management" (PLDI 1995)
   - https://theory.stanford.edu/~aiken/publications/papers/pldi95.pdf

### Reference Counting
5. **Perceus**: Reinking et al., "Garbage Free Reference Counting with Reuse" (PLDI 2021) - **Distinguished Paper**
   - https://dl.acm.org/doi/10.1145/3453483.3454032
6. **Concurrent Cycles**: Bacon & Rajan, "Concurrent Cycle Collection in Reference Counted Systems" (ECOOP 2001)
   - https://dl.acm.org/doi/10.1007/3-540-45337-7_12
7. **Typed Reference Counting**: Lins, "Cyclic Reference Counting by Typed Reference Fields" (2012)
   - https://www.sciencedirect.com/science/article/abs/pii/S1477842411000285

### Region-Based Memory
8. **Region-Based Memory**: Tofte & Talpin, "Region-Based Memory Management" (Information & Computation 1997)
   - https://dl.acm.org/doi/10.1006/inco.1997.2673
9. **MLKit**: Elsman et al., "The MLKit Compiler"
   - https://elsman.com/mlkit/

### Optimization Techniques
10. **Destination-Passing Style**: Shaikhha et al., "Destination-passing style for efficient memory management" (FHPC 2017)
    - https://dl.acm.org/doi/10.1145/3122948.3122949
11. **MemFix**: Lee et al., "MemFix: Static Analysis-Based Repair of Memory Leaks" (ESEC/FSE 2018)
    - https://dl.acm.org/doi/10.1145/3236024.3236079

### Stage-Polymorphism
12. **Collapsing Towers**: Amin & Rompf, "Collapsing Towers of Interpreters" (POPL 2018)
    - https://dl.acm.org/doi/10.1145/3158140

## Generated C99 Output

The compiler generates ANSI C99 code with:
- `Obj` tagged union for runtime values
- `mk_int()`, `mk_pair()` constructors
- `free_tree()`, `dec_ref()`, `deferred_release()` memory management
- Stack allocation pool for short-lived objects
- Weak reference support for breaking cycles
- SCC registry for frozen cyclic structures
- Deferred decrement queue with bounded processing
- Arena allocator for bulk allocation/deallocation
