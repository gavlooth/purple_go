# Changelog

All notable changes to Purple Go are documented in this file.

## [Unreleased]

## [0.5.0] - 2025-12-31

### Added
- **Region References** (`pkg/memory/region.go`)
  - Vale/Ada/SPARK-style scope hierarchy validation
  - Key invariant: pointer cannot point to more deeply scoped region
  - Prevents cross-scope dangling references at link time
  - O(1) depth check on reference creation
  - Functions: `EnterRegion()`, `ExitRegion()`, `CreateRef()`, `CanReference()`
  - 10 tests covering scope hierarchy, nested regions, reference validation

- **Random Generational References** (`pkg/memory/genref.go`)
  - Vale-style use-after-free detection
  - Each object has random 64-bit generation number
  - Each reference remembers generation at creation time
  - On deref: if gen mismatch → UAF detected (O(1) check)
  - On free: gen = 0 (invalidates all existing references)
  - Closure capture validation before execution
  - Functions: `Alloc()`, `CreateRef()`, `Deref()`, `IsValid()`, `GenClosure`
  - 13 tests covering UAF detection, closures, validation

- **Constraint References** (`pkg/memory/constraint.go`)
  - Assertion-based safety for complex patterns (graphs, observers, callbacks)
  - Single owner + multiple non-owning "constraint" references
  - On free: ASSERT constraint count is zero
  - Catches dangling references at development time
  - Functions: `AddConstraint()`, `Release()`, `Free()`, `GetStats()`
  - 17 tests covering observer pattern, violations, statistics

- **Tiered Safety Strategy**
  - Simple (90%): Pure ASAP + ad-hoc validation (zero cost)
  - Cross-scope (7%): Region refs (+8 bytes, O(1) check)
  - Closures/callbacks (3%): Random gen refs (+16 bytes, 1 cmp)
  - Debug mode: + Constraint refs (assert on free)

- **Comprehensive Benchmarks** (`pkg/memory/benchmark_test.go`)
  - Region, GenRef, Constraint, and Symmetric RC benchmarks
  - Baseline comparisons (raw pointer vs safety strategies)
  - Realistic workload benchmarks (closure-heavy, observer pattern)

### Changed
- Runtime generator includes Region, GenRef, and Constraint functions
- `GenerateAll()` now calls new safety strategy generators

### Optimized
- **Constraint Release: 13,937× faster** (130 μs → 9.3 ns)
  - Removed O(n) linear search through ConstraintSources
  - Atomic operations for released flag (CAS)
  - Optional debug tracking (`NewConstraintContextDebug()`)
  - Map with unique ID for O(1) deletion in debug mode

### Performance Results

| Operation | Time | vs Raw Pointer |
|-----------|------|----------------|
| Raw pointer deref | 0.13 ns | baseline |
| Region CanReference | 0.32 ns | 2.4× |
| Constraint IsValid | 9.4 ns | 71× |
| GenRef IsValid | 11.2 ns | 85× |
| GenRef Deref | 11.5 ns | 87× |

| Workload | Time | Allocations |
|----------|------|-------------|
| Closure-heavy | 168 ns | 2 |
| Observer pattern | 296 ns | 6 |
| Scoped access | 289 ns | 11 |
| Mixed strategies | 624 ns | 8 |

### References
- Vale Grimoire: https://verdagon.dev/grimoire/grimoire
- Random Generational References: https://verdagon.dev/blog/generational-references
- Ada/SPARK scope rules for pointer safety

## [0.4.0] - 2025-12-31

### Added
- **Symmetric Reference Counting** (`pkg/memory/symmetric.go`)
  - New hybrid memory strategy for unbroken cycles
  - Treats scope as object participating in ownership graph
  - External refs (from scopes) vs Internal refs (from objects)
  - O(1) deterministic cycle collection without global GC
  - More memory efficient than arenas for long-running scopes
  - Functions: `sym_enter_scope()`, `sym_exit_scope()`, `sym_alloc()`, `sym_link()`
  - 11 comprehensive tests covering cycles, chains, nested scopes

- **RC Operation Elimination** (`pkg/analysis/rcopt.go`)
  - Lobster-style compile-time RC optimization
  - ~75% of RC operations eliminated in typical code
  - **Uniqueness Analysis**: Prove single reference → use `free_unique()`
  - **Alias Tracking**: Track variable aliases → elide redundant RC ops
  - **Borrow Tracking**: Parameters without ownership → zero RC operations
  - 15 tests validating optimization strategies

- **Hybrid Memory Strategy Selection**
  - TREE → `free_tree()` (zero overhead)
  - DAG → `dec_ref()` (standard RC)
  - CYCLIC + broken → `dec_ref()` (weak edges)
  - CYCLIC + frozen → `scc_release()` (SCC-based)
  - CYCLIC + unbroken → `symmetric_rc` (NEW default)
  - Arena available as opt-in for batch operations

### Changed
- `CyclicFreeStrategy` now includes `CyclicStrategySymmetric`
- Unbroken cycles default to Symmetric RC instead of Arena
- Runtime generator includes Symmetric RC functions
- CodeGenerator integrates RC optimization context

### References
- Lobster Memory Management: https://aardappel.github.io/lobster/memory_management.html
- Symmetric RC research on scope-as-object patterns

## [0.3.0] - 2025-12-31

### Added
- **Character Literals** (`pkg/ast/value.go`, `pkg/parser/parser.go`)
  - `TChar` tag for character values
  - Syntax: `#\a`, `#\newline`, `#\space`, `#\tab`, `#\return`
  - Primitives: `char?`, `char->int`, `int->char`, `char=?`, `char<?`

- **Strings as Character Lists** (`pkg/parser/parser.go`, `pkg/eval/primitives.go`)
  - `"hello"` parses to quoted list of `TChar` values
  - Primitives: `string?`, `string->list`, `list->string`
  - `string-length`, `string-append`, `string-ref`, `substring`

- **Quasiquote System** (`pkg/eval/eval.go`, `pkg/parser/parser.go`)
  - Backtick quasiquote: `` `(a b c) ``
  - Unquote: `,x` evaluates x inside quasiquote
  - Unquote-splicing: `,@xs` splices list into quasiquote
  - Nested quasiquote support with depth tracking

- **Run Form** (`pkg/eval/eval.go`)
  - `(run code)` executes code at base level

- **Introspection** (`pkg/eval/eval.go`)
  - `eval` - evaluate quoted expressions
  - `gensym` - generate unique symbols
  - `sym-eq?` - symbol equality test
  - `trace` - trace values during evaluation

## [0.2.0] - 2025-12-31

### Added
- **Pattern Matching** (`pkg/eval/pattern.go`)
  - `match` expression: `(match expr (pat body) ...)`
  - Wildcard patterns: `_`
  - Variable patterns: `x`
  - Literal patterns: `42`
  - Cons patterns: `(cons a b)`
  - Or patterns: `(or pat1 pat2)`
  - As patterns: `(@ x pat)`
  - List patterns: `(list a b c)`
  - Guards: `:when condition`

- **Recursive Lambda**
  - `(lambda self (x) body)` for self-reference
  - New `TRecLambda` tag in AST
  - Cleaner recursion without `letrec`

- **Error Handling**
  - `(error msg)` - raise error
  - `(try expr handler)` - catch errors
  - `(assert cond msg)` - conditional error
  - New `TError` tag in AST

- **List Operations (Higher-Order)**
  - `map`, `filter`, `fold`, `foldl`
  - `length`, `append`, `reverse`
  - `apply`, `compose`, `flip`

- **Sequencing**
  - `(do e1 e2 ... en)` - returns last expression

### Changed
- Extended AST with `TRecLambda` and `TError` tags
- Added `SelfName` field to Value for recursive lambdas

## [0.1.1] - 2025-12-31

### Added
- **Arena Allocation** for cyclic/unknown shapes
  - `ArenaCodeGenerator` for arena-scoped code blocks
  - `arena_create()`, `arena_destroy()` for O(1) bulk deallocation
  - `arena_mk_int()`, `arena_mk_pair()` arena-aware constructors
  - Automatic arena fallback when shape analysis returns CYCLIC/UNKNOWN

- **Weak Edge Detection**
  - `DetectWeakEdges()` analyzes type graph for back-edges
  - `GenerateWeakEdgeComment()` documents detected weak edges
  - Breaks ownership cycles at compile time without programmer annotation

- **Comprehensive Codegen Tests**
  - Tests for arena code generation
  - Tests for weak edge detection
  - Tests for runtime function generation
  - End-to-end program generation tests

### Changed
- `GenerateLet()` now uses arena fallback for CYCLIC/UNKNOWN shapes
- `CodeGenerator` includes `ArenaCodeGenerator` and `useArenaFallback` flag
- Runtime includes arena allocator functions

### Fixed
- Variable redeclaration issue in `GenerateProgram()` main function

## [0.1.0] - 2025-12-31

### Added
- **Core AST** (`pkg/ast/value.go`)
  - Tagged union `Value` type with TInt, TSym, TCell, TNil, TPrim, TMenv, TCode, TLambda
  - Constructors and helper functions

- **S-Expression Parser** (`pkg/parser/parser.go`)
  - Iterative parser for integers, symbols, lists, quotes
  - Comment handling

- **Stage-Polymorphic Evaluator** (`pkg/eval/`)
  - Interpret concrete values or compile lifted values to C
  - Default handlers: HLit, HVar, HApp, HLet, HIf
  - Special forms: quote, lift, if, let, letrec, lambda, and, or
  - Built-in primitives: arithmetic, comparison, list operations

- **Analysis** (`pkg/analysis/`)
  - **Escape Analysis**: ESCAPE_NONE, ESCAPE_ARG, ESCAPE_GLOBAL classification
  - **Shape Analysis**: TREE, DAG, CYCLIC shape detection (Ghiya-Hendren)
  - **Liveness Analysis**: Track last use for ASAP free placement
  - Lambda capture detection

- **Code Generation** (`pkg/codegen/`)
  - C99 runtime generation with Obj type
  - Shape-based deallocation: `free_tree`, `dec_ref`, `deferred_release`
  - Weak reference support
  - Perceus reuse analysis runtime
  - Type registry with ownership graph analysis

- **Memory Management** (`pkg/memory/`)
  - ASAP (As Static As Possible) free insertion
  - SCC-based reference counting (Tarjan's algorithm)
  - Deferred reference counting with bounded processing
  - Arena allocator for bulk allocation/deallocation

- **CLI** (`main.go`)
  - REPL mode for interactive evaluation
  - File execution mode
  - Compile mode with `-c` flag for C99 output

- **Documentation**
  - `ARCHITECTURE.md` with implemented algorithms and references
  - `CLAUDE.md` with project instructions

### References
- ASAP: Proust, "As Static As Possible memory management" (2017)
- Shape Analysis: Ghiya & Hendren, "Is it a tree, a DAG, or a cyclic graph?" (POPL 1996)
- Perceus: Reinking et al., "Garbage Free Reference Counting with Reuse" (PLDI 2021)
- Region-Based Memory: Tofte & Talpin (1997)
- Collapsing Towers: Amin & Rompf (POPL 2018)
