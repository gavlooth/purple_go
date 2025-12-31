# Changelog

All notable changes to Purple Go are documented in this file.

## [Unreleased]

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
