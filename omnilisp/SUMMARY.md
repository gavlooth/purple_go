# Omnilisp: Summary of Design

Omnilisp is a modern Lisp dialect that synthesizes the best features of Scheme, Common Lisp, Julia, and Clojure. This repo currently ships a small C compiler/runtime subset; the rest is the target design.

## Implemented (C Compiler)
*   **Core syntax:** lists `(...)`, quote `'x`, comments `; ...`
*   **Special forms:** `define`, `lambda`/`fn`, `let`, `let*`, `if`, `do`/`begin`
*   **Bindings:** list-style `(let ((x 1) (y 2)) ...)` and array-style `(let [x 1 y 2] ...)`
*   **Primitives:** `+ - * / %`, `< > <= >= =`, `cons car cdr null?`, `display print newline`
*   **Truthiness:** empty list and numeric zero are false; everything else is truthy

## Planned Design (Not Yet Implemented)
### Syntax & Aesthetics (planned)
*   **Pure S-Expressions:** Uses `()` for forms, `[]` for bindings/arrays, `{}` for types, and `#{}` for dicts.
*   **Dot Access:** `obj.field.subfield` reader macro for clean, nested property access.
*   **No Redundancy:** Only Symbols (no separate Keyword type). `:key` is sugar for `'key`.

### Type System (planned, Julia-inspired)
*   **Multiple Dispatch:** Functions are generic by default. Methods are dispatched on all arguments.
*   **Specificity Rules:** Julia-style method specificity with explicit ambiguity errors.
*   **Hierarchy:** Abstract types for organization, Concrete structs for data.
*   **Parametricity:** `{Vector Int}` for clear, typed containers.
*   **Immutability:** Structs are immutable by default; `{mutable ...}` for imperative needs.
*   **Named Args:** Passed as immutable named tuples; `&` separates positional from named `:key value` pairs and supports defaults.
*   **Default Params:** Positional defaults supported via `[name default]` or `[name Type default]`.

### Control & Error Handling (planned)
*   **Delimited Continuations:** Native `prompt` and `control` primitives.
*   **Condition System:** Resumable errors (restarts) built on continuations. No stack-unwinding `try/catch`.
*   **TCO:** Fully supported via trampolining.
*   **Green Threads:** Cooperative processes and channels built on continuations.

### Power Tools (planned)
*   **Hygienic Macros:** Racket-style `syntax-case` for safe, powerful code generation.
*   **Pattern Matching:** Optima-style, extensible, and compiled for performance.
*   **Iterators & Loops:** Explicit `iterate(iter, state)` protocol with `for`/`foreach` macros.
*   **Arrays & Sequences:** Arrays are the primary mutable sequence; vectors are immutable by default. Tuples/lists are planned first, with other collections later. Sequence ops work on any iterator and `collect` builds vectors (`iter->list`/`iter->vector` conversions).
*   **Piping:** `|>` operator for readable data flows.

### Module System (planned)
*   **Explicit:** One-file-per-module. No implicit `include`.
*   **Controlled:** `(import [Mod :as M :refer (f)])` for namespace sanity.
