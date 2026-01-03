# Future Optimizations & Validation Testing

## Overview

This document outlines future work for Purple Go's ASAP memory management system.
All items are optional enhancements - the core system is complete and functional.

---

## Part 1: Future Optimizations

### O.1 Compile-Time RC Elimination (High Impact)

**Source**: Lobster language, Perceus (PLDI 2021)

**Problem**: Many `inc_ref`/`dec_ref` pairs are provably unnecessary.

**Current State**: `pkg/analysis/rcopt.go` tracks uniqueness but doesn't eliminate all redundant ops.

**Proposed Enhancement**:
```
Before:  inc_ref(x); use(x); dec_ref(x);
After:   use(x);  // Proven unique, skip RC entirely
```

| Task | Description | Effort |
|------|-------------|--------|
| O.1.1 | Extend uniqueness analysis to handle more patterns | Medium |
| O.1.2 | Track borrowed references through function calls | Medium |
| O.1.3 | Eliminate inc/dec pairs for provably-unique paths | Small |
| O.1.4 | Add statistics: "X% of RC operations eliminated" | Small |

**Expected Impact**: 30-50% reduction in RC operations for typical code.

---

### O.2 Active Reuse Transformation (High Impact)

**Source**: Perceus FBIP (Functional But In-Place)

**Problem**: Reuse analysis exists but codegen doesn't actively transform code.

**Current State**: `ReuseAnalyzer` finds candidates, `GenerateAllocation()` helper exists.

**Proposed Enhancement**:
```scheme
;; Before (two allocations)
(let ((x (cons 1 2)))
  (let ((y (cons 3 4)))  ;; x is dead here
    y))

;; After (reuse x's memory for y)
(let ((x (cons 1 2)))
  (let ((y (reuse_as_pair x 3 4)))
    y))
```

| Task | Description | Effort |
|------|-------------|--------|
| O.2.1 | Integrate `ReuseAnalyzer` into `GenerateLet()` | Medium |
| O.2.2 | Track allocation sizes at compile time | Small |
| O.2.3 | Generate `reuse_as_*` calls instead of `mk_*` + `free` | Medium |
| O.2.4 | Handle partial reuse (larger block → smaller allocation) | Medium |

**Expected Impact**: Near-zero allocation for many functional patterns.

---

### O.3 DPS Code Generation (Medium Impact)

**Source**: Destination-Passing Style (FHPC 2017)

**Problem**: List-returning functions allocate intermediate results.

**Current State**: DPS runtime exists (`map_into`, `filter_into`) but not auto-generated.

**Proposed Enhancement**:
```c
// Before: allocates result list
Obj* result = map(f, xs);

// After: caller provides destination
Obj* result;
map_into(&result, f, xs);  // No intermediate allocation
```

| Task | Description | Effort |
|------|-------------|--------|
| O.3.1 | Identify functions returning fresh allocations | Small |
| O.3.2 | Generate `_dps` variants for eligible functions | Large |
| O.3.3 | Transform call sites to use DPS when beneficial | Large |
| O.3.4 | Handle nested DPS (e.g., `map(f, filter(g, xs))`) | Large |

**Expected Impact**: Constant memory for pipelines like `map(f, filter(g, xs))`.

---

### O.4 Region Inference (Medium Impact)

**Source**: MLKit, Tofte-Talpin

**Problem**: Per-object free has overhead; related objects could share lifetime.

**Proposed Enhancement**:
```c
// Before: individual frees
free(a); free(b); free(c);

// After: region-based
Region* r = region_create();
a = region_alloc(r, ...);
b = region_alloc(r, ...);
c = region_alloc(r, ...);
region_destroy(r);  // One operation frees all
```

| Task | Description | Effort |
|------|-------------|--------|
| O.4.1 | Infer region parameters for functions | Large |
| O.4.2 | Group allocations with same lifetime into regions | Large |
| O.4.3 | Generate region allocation/deallocation | Medium |
| O.4.4 | Optimize: merge small regions, inline single-object regions | Medium |

**Expected Impact**: Reduced free() overhead for related allocations.

---

### O.5 Interprocedural Ownership Propagation (Medium Impact)

**Source**: SOTER (ownership transfer inference)

**Problem**: Summaries exist but aren't used to optimize callers.

**Current State**: `SummaryRegistry` has param/return ownership.

**Proposed Enhancement**:
```scheme
;; If we know `process` consumes its argument:
(let ((x (alloc)))
  (process x)    ;; x consumed by callee
  ;; No free needed here - callee freed it
  )
```

| Task | Description | Effort |
|------|-------------|--------|
| O.5.1 | Query summaries in `GenerateLet()` cleanup | Medium |
| O.5.2 | Skip caller-side free for consumed parameters | Small |
| O.5.3 | Skip inc_ref for parameters callee will borrow | Small |
| O.5.4 | Infer summaries for recursive functions | Large |

**Expected Impact**: Fewer redundant RC ops at call boundaries.

---

### O.6 Non-Lexical Lifetimes (Low Impact)

**Source**: Rust/Polonius

**Problem**: Variables freed at scope end, not at last use.

**Current State**: Liveness analysis exists in `pkg/analysis/liveness.go`.

**Proposed Enhancement**:
```scheme
(let ((x (alloc)))
  (use x)        ;; Last use of x
  (long-computation)  ;; x could be freed here, not at scope end
  result)
```

| Task | Description | Effort |
|------|-------------|--------|
| O.6.1 | Use liveness info to free at last-use point | Medium |
| O.6.2 | Handle control flow (different free points per branch) | Large |
| O.6.3 | Ensure correctness with exceptions | Medium |

**Expected Impact**: Earlier memory reclamation, reduced peak memory.

---

### O.7 Stack Allocation for Non-Escaping Values (Low Impact)

**Source**: Escape analysis literature

**Problem**: Non-escaping values still heap-allocated.

**Current State**: Escape analysis exists, stack pool exists.

**Proposed Enhancement**:
```c
// Before
Obj* x = mk_int(42);  // Heap allocation
use(x);
dec_ref(x);

// After (proven non-escaping)
Obj x_storage;
Obj* x = init_int_stack(&x_storage, 42);  // Stack allocation
use(x);
// No free needed - automatic on scope exit
```

| Task | Description | Effort |
|------|-------------|--------|
| O.7.1 | Use escape analysis to identify stack candidates | Small |
| O.7.2 | Generate stack allocation for `ESCAPE_NONE` | Medium |
| O.7.3 | Handle aggregates (pairs, closures) on stack | Medium |

**Expected Impact**: Zero heap allocation for many local variables.

---

## Part 2: Validation Testing

### V.1 Memory Safety (Critical)

**Objective**: Prove generated C code has no memory errors.

| Test | Tool | Description |
|------|------|-------------|
| V.1.1 | Valgrind | `valgrind --leak-check=full --error-exitcode=1` |
| V.1.2 | ASan | Compile with `-fsanitize=address` |
| V.1.3 | MSan | Compile with `-fsanitize=memory` |
| V.1.4 | UBSan | Compile with `-fsanitize=undefined` |

**Test Cases**:
```scheme
;; Leak test: all allocations freed
(let ((x (cons 1 2)))
  (car x))

;; Double-free test: no double frees
(let ((x (cons 1 2)))
  (let ((y x))  ;; Alias
    (car y)))

;; Use-after-free test: no dangling pointers
(let ((x (cons 1 2)))
  (let ((y (car x)))
    ;; x freed here
    y))  ;; y should still be valid

;; Cycle test: weak edges prevent leaks
(deftype Node (value int) (next Node) (prev Node :weak))
(let ((a (mk-Node 1 nil nil))
      (b (mk-Node 2 a nil)))
  (set! (Node-prev a) b)
  (Node-value a))
```

---

### V.2 Concurrency Safety (Critical)

**Objective**: Prove thread-safe code has no data races.

| Test | Tool | Description |
|------|------|-------------|
| V.2.1 | TSan | Compile with `-fsanitize=thread` |
| V.2.2 | Helgrind | Valgrind's thread error detector |

**Test Cases**:
```scheme
;; Ownership transfer: no race
(let ((ch (make-chan 1))
      (x (cons 1 2)))
  (go (lambda ()
        (let ((y (chan-recv! ch)))
          (display y))))
  (chan-send! ch x)
  ;; x is dead here - transferred to goroutine
  nil)

;; Shared variable: atomic RC
(let ((x (cons 1 2)))
  (go (lambda () (display x)))
  (go (lambda () (display x)))
  x)
```

---

### V.3 Correctness Testing (High Priority)

**Objective**: Compiled code produces same results as interpreter.

| Test | Description |
|------|-------------|
| V.3.1 | Run all 100+ existing tests through JIT |
| V.3.2 | Compare interpreter vs compiled output |
| V.3.3 | Property-based testing (QuickCheck-style) |

**Test Harness**:
```go
func TestCompiledMatchesInterpreter(t *testing.T) {
    tests := loadAllTestCases()
    for _, tc := range tests {
        interpResult := interpret(tc.Code)
        compiledResult := compileAndRun(tc.Code)
        if !equal(interpResult, compiledResult) {
            t.Errorf("%s: interp=%v, compiled=%v",
                tc.Name, interpResult, compiledResult)
        }
    }
}
```

---

### V.4 Performance Benchmarks (Medium Priority)

**Objective**: Measure and track performance.

| Benchmark | Description |
|-----------|-------------|
| V.4.1 | Allocation rate (allocs/sec) |
| V.4.2 | Peak memory usage |
| V.4.3 | RC operation count |
| V.4.4 | Reuse hit rate |
| V.4.5 | Comparison vs manual memory management |

**Benchmark Suite**:
```scheme
;; Allocation-heavy: list operations
(define (bench-list n)
  (fold + 0 (map (lambda (x) (* x x)) (range n))))

;; Cycle-heavy: graph algorithms
(define (bench-graph n)
  (let ((g (make-graph n)))
    (dfs g 0)))

;; Concurrent: producer-consumer
(define (bench-channel n)
  (let ((ch (make-chan 100)))
    (go (producer ch n))
    (consumer ch n)))
```

---

### V.5 Stress Testing (Medium Priority)

**Objective**: Find edge cases and resource limits.

| Test | Description |
|------|-------------|
| V.5.1 | Deep recursion (stack overflow handling) |
| V.5.2 | Large allocations (memory exhaustion) |
| V.5.3 | Many goroutines (thread limits) |
| V.5.4 | Complex cycles (SCC algorithm stress) |
| V.5.5 | Long-running (memory stability over time) |

---

### V.6 Fuzzing (Low Priority)

**Objective**: Find crashes via random input.

| Test | Tool | Description |
|------|------|-------------|
| V.6.1 | go-fuzz | Fuzz the parser |
| V.6.2 | AFL | Fuzz compiled programs |
| V.6.3 | libFuzzer | Fuzz runtime functions |

---

## Priority Order

### Immediate (Before Production Use)
1. **V.1** Memory Safety (Valgrind/ASan)
2. **V.2** Concurrency Safety (TSan)
3. **V.3** Correctness Testing

### Short Term (Next Major Release)
4. **O.1** RC Elimination
5. **O.2** Active Reuse Transformation
6. **V.4** Performance Benchmarks

### Medium Term (Future Releases)
7. **O.5** Interprocedural Ownership
8. **O.3** DPS Code Generation
9. **V.5** Stress Testing

### Long Term (Research)
10. **O.4** Region Inference
11. **O.6** Non-Lexical Lifetimes
12. **O.7** Stack Allocation
13. **V.6** Fuzzing

---

## Part 2: Language & Tooling Goodies (Non-Core)

These items improve developer experience and ecosystem usability. They are optional, but high leverage.

### L.1 Language Server (LSP)
- Go-to-definition, find references, rename
- Hover docs and type info
- Diagnostics surfaced in editors

### L.2 Module System Polish
- Status: basic module system exists (exports/imports/aliases).
- Add versioned imports and clear conflict errors.
- Deterministic module resolution across files/paths.

### L.3 Pattern Matching Upgrades
- Exhaustiveness warnings
- Better error reporting for match failures

### L.4 Macro Hygiene
- Status: hygienic macros exist (gensym + syntax-quote), but mark tracking is simplified.
- Complete syntax object mark propagation for true hygiene.
- Better macro error spans.

### L.5 Deterministic Builds
- Stable output ordering
- Reproducible codegen for caching and CI

### L.6 Developer Tooling
- Auto-formatter and lint rules
- Golden-file test harness
- Fuzz hooks for parser and optimizer passes

### L.7 Profiling & Tracing
- Status: basic `(trace ...)` exists for evaluation.
- Compile-time stats and pass timing.
- Structured runtime tracing hooks and sampling.

---

## Implementation Notes

### Adding a New Optimization

1. Create analysis in `pkg/analysis/`
2. Add test cases in `*_test.go`
3. Wire to `CodeGenerator` in `pkg/codegen/codegen.go`
4. Generate runtime support in `pkg/codegen/runtime.go`
5. Validate with V.1-V.3

### Adding a New Validation Test

1. Create test harness in `test/`
2. Add CI integration (GitHub Actions)
3. Document expected behavior
4. Add to regression suite

---

## References

1. Perceus: Garbage Free Reference Counting with Reuse (PLDI 2021)
2. ASAP: As Static As Possible memory management (Proust 2017)
3. Destination-Passing Style for Efficient Memory Management (FHPC 2017)
4. Region-Based Memory Management (Tofte-Talpin 1997)
5. Lobster Memory Management (aardappel.github.io)
6. SOTER: Inferring Ownership Transfer (UIUC)
7. Rust Borrow Checker / Polonius
