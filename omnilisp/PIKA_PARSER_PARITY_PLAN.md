# Pika Parser Parity Plan (C, Paper-Accurate + Java-Complete)

## Goals & Constraints
- [ ] Be a standalone C99 library (no runtime/refcount dependencies)
- [ ] Follow the paper’s algorithm as the base semantics and complexity model
- [ ] Feature parity with the Java implementation (100%+), then add clearly optional extras
- [ ] Thread-safe: no mutable globals; parser state owned per parse
- [ ] Deterministic: no GC, no hidden background threads

## Phase 0 — Inventory & Spec Alignment
- [ ] Read the paper algorithm section and extract the formal evaluation rules
- [ ] Read Java implementation (public API + internal evaluation engine) and list features
- [ ] Build a feature matrix: **Paper vs Java vs Current C**
- [ ] Document grammar syntax/DSL supported by Java (operators, precedence, labels, etc.)
- [ ] Define “must-implement” behaviors (error reporting, left recursion, associativity)
- [ ] Identify Java-only behaviors that go beyond the paper (plan to support or justify)

## Phase 1 — Library Boundary & API Design
- [ ] Create standalone module layout: `pika/` with public header and private sources
- [ ] Define opaque handles:
  - [ ] `PikaGrammar*` (immutable after build)
  - [ ] `PikaParser*` or `PikaState*` (per-parse)
  - [ ] `PikaNode*` (parse tree / CST)
- [ ] Define public APIs:
  - [ ] Grammar creation / rule builders (terminal, range, seq, alt, rep, etc.)
  - [ ] Grammar finalize / validate
  - [ ] Parse function returning result + error diagnostics
  - [ ] Parse tree traversal, span extraction, rule-name access
- [ ] Ensure ABI stability: avoid exposing internal structs
- [ ] Provide compile-time options (e.g., disable parse tree caching)

## Phase 2 — Grammar IR & Builder
- [ ] Replace ad-hoc `rules[]` array with a grammar builder
- [ ] Implement expression nodes (immutable IR):
  - [ ] Terminal literal
  - [ ] Range / char class
  - [ ] Any
  - [ ] Sequence
  - [ ] Ordered choice
  - [ ] Zero/one-or-more, optional
  - [ ] And/Not lookahead
  - [ ] Rule reference
- [ ] Add rule metadata:
  - [ ] Rule name
  - [ ] Rule precedence / associativity (if Java supports)
  - [ ] Labels / capture names
- [ ] Add grammar validation:
  - [ ] Undefined rule references
  - [ ] Left-recursive cycles allowed by algorithm but validated for consistency
  - [ ] Empty-string cycles that would cause non-termination
- [ ] Provide grammar serialization/debug dump for testing

## Phase 3 — Core Algorithm (Paper-Accurate)
- [ ] Implement right-to-left dynamic programming table
- [ ] Implement fixpoint iteration exactly as in the paper
- [ ] Ensure monotonic growth of match length and convergence criteria
- [ ] Handle left recursion per paper (no backtracking explosion)
- [ ] Correctly propagate semantic values after fixpoint convergence
- [ ] Separate *match computation* from *value construction* to avoid stale AST
- [ ] Provide hooks for semantics/actions without embedding runtime types

## Phase 4 — Java Parity Features
- [ ] Parse tree output compatible with Java node types:
  - [ ] Rule name / label
  - [ ] Span (start, end)
  - [ ] Children list with deterministic ordering
- [ ] Error reporting parity:
  - [ ] Farthest failure position
  - [ ] Expected terminals (set)
  - [ ] Contextual rule stack (if Java provides)
- [ ] Precedence & associativity (if Java uses operator rules)
- [ ] Grammar annotations or “leaf” rules (if Java supports)
- [ ] Whitespace/comment skipping strategies (if provided by Java helper API)
- [ ] Rule inlining / memoization behavior aligned with Java

## Phase 5 — C Library Ergonomics
- [ ] Provide a small C builder DSL for rules (for static grammars)
- [ ] Provide an optional text grammar loader if Java exposes grammar-by-string
- [ ] Provide clean error objects and destruction routines
- [ ] Ensure memory ownership rules are explicit in docs

## Phase 6 — Test Suite Parity + Cross-Validation
- [ ] Port Java test cases where feasible (unit and integration)
- [ ] Add paper examples as golden tests
- [ ] Add negative tests for error reporting
- [ ] Add left-recursion stress tests
- [ ] Add large-input regression tests for time/memory
- [ ] Add randomized grammar tests (fuzzing) for stability
- [ ] Add corpus cross-check: same grammar+input in Java and C must match

## Phase 7 — Performance & Memory Targets
- [ ] Implement O(n * rules) parse table allocation; support custom allocators
- [ ] Add memory usage estimation API (table size, node count)
- [ ] Optimize hot paths: terminal compare, range checks, seq/alt loops
- [ ] Provide optional parse-tree caching mode vs lightweight matches
- [ ] Benchmarks vs Java parser on representative inputs

## Phase 8 — “Beyond Java” Enhancements (Optional)
- [ ] Incremental parsing mode (reuse table for edits)
- [ ] Streaming input mode (chunked buffers)
- [ ] Debug visualization of match table
- [ ] Parse-tree pruning / selective capture
- [ ] Deterministic error recovery (best-effort partial AST)

## Phase 9 — Integration (Runtime Adapter Only)
- [ ] Build adapter layer in runtime to map Pika parse nodes → Value
- [ ] Ensure adapter is optional; core library remains standalone
- [ ] Add runtime tests that use the adapter but keep parser tests independent

## Deliverables
- [ ] `pika/` standalone C library with docs and examples
- [ ] Compatibility report: Java vs C (features + deviations)
- [ ] Full unit/integration test suite with CI targets
- [ ] Benchmarks and memory report

