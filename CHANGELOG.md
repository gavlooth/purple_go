# Changelog

All notable changes to Purple Go are documented in this file.

## [Unreleased]

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
