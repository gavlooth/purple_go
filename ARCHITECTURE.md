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
│   │   ├── eval.go            # Stage-polymorphic evaluator
│   │   ├── env.go             # Environment handling
│   │   └── primitives.go      # Built-in primitives
│   ├── analysis/
│   │   ├── escape.go          # Escape analysis
│   │   ├── shape.go           # Shape analysis
│   │   └── liveness.go        # Liveness analysis
│   ├── codegen/
│   │   ├── codegen.go         # C99 code generation
│   │   ├── runtime.go         # Runtime header generation
│   │   └── types.go           # Type registry
│   └── memory/
│       ├── asap.go            # ASAP CLEAN phase
│       ├── scc.go             # SCC-based RC (Tarjan)
│       ├── deferred.go        # Deferred RC
│       └── arena.go           # Arena allocator
└── examples/
    └── demo.purple            # Example programs
```

## Key References

1. **ASAP**: Proust, "As Static As Possible memory management" (2017)
2. **Shape Analysis**: Ghiya & Hendren, "Is it a tree, a DAG, or a cyclic graph?" (POPL 1996)
3. **Perceus**: Reinking et al., "Garbage Free Reference Counting with Reuse" (PLDI 2021)
4. **Region-Based Memory**: Tofte & Talpin, "Region-Based Memory Management" (1997)
5. **Static Memory Management**: Aiken et al., "Better Static Memory Management" (PLDI 1995)
6. **MemFix**: Lee et al., "Static Analysis-Based Repair of Memory Leaks" (ESEC/FSE 2018)
7. **Collapsing Towers**: Amin & Rompf, "Collapsing Towers of Interpreters" (POPL 2018)

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
