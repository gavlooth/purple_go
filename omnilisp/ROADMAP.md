# Omnilisp Implementation Roadmap

This roadmap outlines the steps to build the Omnilisp language using the C-based `omniruntime`.

## Phase 0: Runtime Core (C)
**Goal:** Establish the object model and memory management.
*   [ ] **Object Model (`types.c`):**
    *   Define `OmniValue` (Tagged union or pointer).
    *   Implement Primitives: `Nothing` (singleton), `Boolean`, `Int`, `Float`.
    *   Implement `String` (using `dstring` util).
    *   Implement `Symbol` (interned).
*   [ ] **Collections:**
    *   `Array` (Mutable, dynamic).
    *   `List` (Persistent/Linked).
    *   `Tuple` (Immutable, fixed).
    *   Ensure empty collections are distinct from `nothing`.
*   [ ] **Memory Management:**
    *   Verify `arena`, `region`, and `concurrent` allocators.
    *   Implement GC interface or RefCounting (if not using pure Arena/Region).

## Phase 1: The Reader (Lexer & Parser)
**Goal:** Convert source text into AST (S-expressions) within the C runtime (`src/runtime/reader`).
*   [ ] **Tokenizer (C):**
    *   Handle `(` `)` `[` `]` `{` `}` `#{` `}`.
    *   Handle `..` and `...` (Rest/Variadic).
    *   Handle `.` as dot-operator (distinguish from number decimal).
    *   Handle string interpolation syntax `"$var"`.
*   [ ] **Parser:**
    *   Parse `[]` as Array literals (or binding/pattern specs depending on context).
    *   Parse `{}` as Type forms.
    *   Parse `obj.field` and `.field` syntax into access forms.
    *   Parse `obj.(expr)` dynamic access.

## Phase 2: The Evaluator (Interpreter)
**Goal:** Execute the AST.
*   [ ] **Environments:** Implement scopes using `hashmap`.
*   [ ] **Special Forms:**
    *   `define` (Global bindings).
    *   `let`: Support `:seq`, `:rec`, and Triplet syntax `[name {Type} val]`.
    *   `if`: Implement Lisp-style truthiness (only `false`/`nothing` are falsy).
    *   `set!`: Mutation.
    *   `quote`: Handling literals.
*   [ ] **Pattern Matching (`match`):**
    *   Implement as a special form.
    *   Support `[]` branches.
    *   Support `else` as sugar for `_`.
    *   Support guards (`:when`).
*   [ ] **Function Calls:**
    *   Implement `lambda`.
    *   Implement Trampoline for Tail Call Optimization (TCO).

## Phase 3: The Object System
**Goal:** Implement Types, Structs, and Multiple Dispatch.
*   [ ] **Type Registry:**
    *   Abstract Types (`define {abstract ...}`).
    *   Concrete Structs (`define {struct ...}`).
    *   Interfaces (`define {interface ...}` with `{Self}`).
*   [ ] **Generic Functions:**
    *   Registry of Generic Functions.
    *   Method definition `(define (name args...) ...)` (Overloads).
*   [ ] **Dispatch Logic:**
    *   Implement specificity sorting.
    *   Runtime dispatch based on argument types.

## Phase 4: Sequences & Iteration
*   [ ] **Iterators:** Define the `iterate` protocol.
*   [ ] **Looping:** Implement `foreach` and `for` (comprehension) macros.
*   [ ] **Ranges:** Implement `start:end` syntax for slicing.

## Phase 5: Standard Library & Macros
*   [ ] **Macros:** Implement hygienic macros (`syntax`, `#'`, `~`).
*   [ ] **Core Lib:** `map`, `filter`, `reduce` implemented in Omnilisp.
*   [ ] **Modules:** `module` and `import`.
