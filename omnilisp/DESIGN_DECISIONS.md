# OmniLisp Open Design Decisions
Tag: Syntax Guide

This document lists **remaining** design decisions that need resolution.
For resolved decisions, see the Decision Log at the bottom.

---

## Open Decisions

### 1. Struct Construction Syntax

**Context:** How are structs instantiated?

```lisp
(define {struct Point}
  [x {Float}]
  [y {Float}])
```

| Option | Syntax | Notes |
|--------|--------|-------|
| A | `(Point 10.0 20.0)` | Positional only |
| B | `(Point & :x 10.0 :y 20.0)` | Named with & separator |
| C | `(Point #{:x 10.0 :y 20.0})` | Dict-like |
| D | `(Point 10.0 20.0)` or `(Point :x 10.0 :y 20.0)` | Both allowed |
| E | `{Point :x 10.0 :y 20.0}` | Record literal syntax |

**Additional questions:**
- Partial construction with defaults?
- Update syntax: `(with-fields p :x 99)` to copy with changes?

```
[ ] Option A - Positional only
[x] Option B - Named with &
[ ] Option C - Dict-like
[ ] Option D - Both positional and named
[ ] Option E - Record literal {Type ...}

Notes:
Decision: Positional constructors by default; named fields use `&`.
Mixed allowed: positional before `&`, named after.
Example: `(Point 10.0 & :y 20.0)`. Unknown/duplicate keys are errors.

```

---

### 2. Parametric Struct + Parent Type

**Context:** How to specify both type parameters AND parent type?

```lisp
(define {struct Point}         ; Point extends Any by default
  [x {Float}] [y {Float}])

(define {struct [Pair T]}      ; Pair<T>, parent unclear
  [first {T}] [second {T}])
```

| Option | Syntax | Notes |
|--------|--------|-------|
| A | `{struct Pair T Any}` | Parent always last |
| B | `{struct Pair T :extends Any}` | Explicit keyword |
| C | `{struct Pair T} :extends Any` | Outside braces |
| D | `{struct Pair T} (:parent Any) [...]` | In body |

**Additional questions:**
- Can parametric types have non-Any parents?
- Variance: covariant, contravariant, invariant?

```
[ ] Option A - Parent always last position
[ ] Option B - :extends keyword inside braces
[ ] Option C - :extends outside braces
[ ] Option D - :parent in body

Notes:
Decision: Use tuple-style header inside braces:
`(define {struct [Pair T] :extends Any} ...)`
Everything inside `{}` is type context; parent defaults to `Any`.
Parametric types may extend non-`Any` parents.

```

---

### 3. Struct Mutability

**Context:** How should struct mutability be controlled?

| Option | Syntax | Notes |
|--------|--------|-------|
| A | `{struct ...}` immutable, `{mutable ...}` mutable | Explicit keyword |
| B | All mutable, use `(freeze obj)` | Runtime freeze |
| C | `[field :mutable {T}]` per-field | Fine-grained |

```
[ ] Option A - Explicit mutable keyword
[ ] Option B - All mutable, explicit freeze
[x] Option C - Per-field mutability

Notes:
Decision: Field modifier `:mutable` in field spec:
`[hp :mutable {Int}]` (defaults may follow).
`:mutable` only valid in struct field specs.
`{mutable ...}` remains as sugar for “all fields mutable.”

```

---

### 4. Enum Namespace

**Context:** How are enum variants accessed?

```lisp
(define {enum Color} Red Green Blue)
```

| Option | Syntax | Notes |
|--------|--------|-------|
| A | `Color.Red` | Always qualified |
| B | `Red` | Always flat |
| C | `Red` if unique, `Color.Red` if ambiguous | Smart |

```
[ ] Option A - Always qualified (Rust-style)
[ ] Option B - Always flat (Haskell-style)
[x] Option C - Smart qualification

Notes:
Decision: Unqualified if unique in scope; require `Type.Variant` when ambiguous.

```

---

### 5. Pattern Predicate Evaluation

**Context:** How does `(satisfies pred)` work?

```lisp
(match x
  [(satisfies even?) "even"]
  [(satisfies (lambda (n) (> n 10))) "big"])
```

| Option | Behavior | Notes |
|--------|----------|-------|
| A | `pred` must be known function | Static only |
| B | `pred` evaluated at match time | Dynamic lookup |
| C | Any expression returning predicate | Inline lambdas OK |

```
[ ] Option A - Static only (named functions)
[ ] Option B - Dynamic lookup
[x] Option C - Inline expressions allowed

Notes:
Decision: Any expression returning a predicate is allowed; evaluate per match branch.

```

---

### 6. Quote and Brackets

**Context:** How does quote interact with `[]`, `{}`, `#{}`?

```lisp
'(1 2 3)    ; -> list
'[1 2 3]    ; -> ???
'#{:a 1}    ; -> ???
```

| Option | `'(...)` | `'[...]` | `'#{...}` |
|--------|----------|----------|-----------|
| A | list | list | list |
| B | quoted list | quoted array | quoted dict |
| C | list | ERROR | ERROR |
| D | syntax object | quoted array | quoted dict |

```
[ ] Option A - All become lists
[x] Option B - Preserve structure types
[ ] Option C - Only () quotable
[ ] Option D - () special for macros

Notes:
Decision: Quote preserves structure: `'(...)` list, `'[...]` array, `'#{...}` dict.

```

---

### 7. Channel Closed Semantics

**Context:** What happens when sending to a closed channel?

```lisp
(define ch (channel))
(close ch)
(send ch value)  ; ???
```

| Option | Behavior | Notes |
|--------|----------|-------|
| A | Runtime error | Fail-fast |
| B | Returns `false` | Must check |
| C | Silent drop | Dangerous |

**Additional questions:**
- `(closed? ch)` predicate?
- Select/alt for multiple channels?

```
[ ] Option A - Error on send to closed
[x] Option B - Return false
[ ] Option C - Silent drop

Notes:
Decision: `(send ch v)` returns `false` if closed; otherwise `true`.
`(recv ch)` from closed returns `nothing` (already defined).

```

---

### 8. Lambda Shorthand

**Context:** Should there be a shorter lambda syntax?

```lisp
(lambda (x) (+ x 1))         ; Current
```

| Option | Single Arg | Multi Arg | Notes |
|--------|------------|-----------|-------|
| A | No shorthand | - | Status quo |
| B | `#(+ % 1)` | `#(* %1 %2)` | Clojure-style |
| C | `\(+ _ 1)` | `\(* _1 _2)` | Underscore placeholders |
| D | `(fn (x) ...)` | `(fn (x y) ...)` | Just shorter name |
| E | `(-> x (+ x 1))` | `(-> (x y) (* x y))` | Arrow syntax |

```
[ ] Option A - No shorthand
[ ] Option B - Clojure-style #(% ...)
[ ] Option C - Underscore placeholders
[ ] Option D - fn alias
[x] Option E - Arrow syntax

Notes:
Decision: `(-> x (+ x 1))` / `(-> (x y) (* x y))` as lambda shorthand.
Pipeline already uses `|>`, so `->` is reserved for lambdas.

```

---

### 10. For/Foreach Multiple Bindings

**Context:** What does `(for [x xs y ys] ...)` mean?

| Option | Behavior | Example Result |
|--------|----------|----------------|
| A | Nested (Cartesian product) | All pairs |
| B | Parallel (zip) | Paired by index |
| C | Explicit `:nested` vs `:zip` | User chooses |

```lisp
;; Option A (nested): [x xs y ys] -> for each x, for each y
(for [x [1 2] y [a b]] (tuple x y))
;; -> [(1 a) (1 b) (2 a) (2 b)]

;; Option B (zip): [x xs y ys] -> zip x and y
(for [x [1 2] y [a b]] (tuple x y))
;; -> [(1 a) (2 b)]
```

```
[x] Option A - Nested (Cartesian product)
[ ] Option B - Parallel (zip)
[ ] Option C - Explicit modifier required

Notes:
Decision: Nested semantics for both `for` and `foreach`. Add `:zip` later if needed.

```

---

### 11. Self in Methods

**Context:** How is the receiver accessed in method bodies?

| Option | Syntax | Notes |
|--------|--------|-------|
| A | `(define [method f T] (self arg) ...)` | Explicit first param |
| B | `(define [method f T] (arg) self.x ...)` | Implicit self binding |
| C | `(define [method f T] (arg) @x ...)` | @ for self access |

```
[x] Option A - Explicit self parameter
[ ] Option B - Implicit self binding
[ ] Option C - @ shorthand for self.

Notes:
Decision: Explicit `self` parameter in method definitions.

```

---

### 12. Nil vs Nothing

**Context:** Should `nil` exist separately from `nothing`?

| Concept | Current | Notes |
|---------|---------|-------|
| `nil` | Empty list / Scheme nil | Falsy |
| `nothing` | Unit/void value | Falsy |
| `[]` | Empty array | Truthy? |
| `(list)` | Empty list | = nil? |

| Option | Approach |
|--------|----------|
| A | Keep nil, nothing, empty as separate |
| B | Remove nil, use (list) for empty list |
| C | Unify nil and nothing into single concept |

```
[ ] Option A - Keep all three concepts
[x] Option B - Remove nil keyword
[ ] Option C - Unify nil and nothing

Notes:
Decision: No `nil`; empty list is `(list)`. `nothing` remains distinct.

```

---

### 13. Error Handling Strategy

**Context:** What's the primary error handling mechanism?

| Option | Style | Notes |
|--------|-------|-------|
| A | `{Result T E}` types | Rust-style, explicit |
| B | Conditions/restarts | CL-style, powerful |
| C | Try/catch exceptions | Traditional |
| D | Result + panic hybrid | Rust-style |

```
[ ] Option A - Result types only
[ ] Option B - Conditions/restarts (CL-style)
[ ] Option C - Exceptions (try/catch)
[x] Option D - Hybrid Result + panic

Notes:
Decision: Follow `DESIGN.md`: `error` + single `restart-case` (Phase-1).
`Result` is available but not required; full condition system deferred.

```

---

### 14. Named Arguments

**Context:** How to pass keyword arguments?

| Option | Syntax | Notes |
|--------|--------|-------|
| A | `(f x & :color :red)` | & separator |
| B | `(f x :color :red)` | Keywords inline |
| C | `(f x #{:color :red})` | Trailing map |
| D | No named arguments | Rely on positional + defaults |

```
[x] Option A - Ampersand separator
[ ] Option B - Keywords inline
[ ] Option C - Trailing map
[ ] Option D - No named arguments

Notes:
Decision: `&` separates positional from named args in definitions and calls.

```

---

## Already Resolved

| # | Topic | Decision | Date |
|---|-------|----------|------|
| 1 | Parameter syntax | Use `{Type}` wrapper: `[x {Int}]` | 2026-01-02 |
| 2 | Let type annotations | Triplet: `[x {Int} 1]` | 2026-01-02 |
| 3 | Truthiness | Lisp-style: `false`/`nothing` falsy, else truthy | 2026-01-02 |
| 4 | Named Let + Modifiers | Modifier first: `(let :seq loop [...] ...)` | 2026-01-02 |
| 5 | Variadic syntax | `..` primary, `...` alias | 2026-01-02 |
| 6 | Multi-arity | Overloads via multiple dispatch | 2026-01-02 |
| 7 | Return type position | After params: `(f [x {Int}]) {Int}` | 2026-01-02 |
| 8 | Interface vs Abstract | Both supported, interfaces explicit | 2026-01-02 |
| 9 | Interface signatures | `{Self}` placeholder | 2026-01-02 |
| 10 | Method calls | UFCS: `(obj.f args)` = `(f obj args)` | 2026-01-02 |
| 11 | Match else | Supported as sugar for `_` | 2026-01-02 |
| 12 | Interface implementation | Julia-style implicit (just define methods) | 2026-01-03 |
| 13 | Module privacy | Export list | 2026-01-03 |
| 14 | Equality | Protocol-based `=` with `identical?` for identity | 2026-01-03 |
| 15 | Concurrency | Green threads + channels (current impl) | 2026-01-03 |
| 16 | Struct construction | Positional + named after `&` | 2026-01-03 |
| 17 | Parametric struct parent | `{struct [Name params] :extends Parent}` | 2026-01-03 |
| 18 | Struct mutability | Per-field `:mutable`; `{mutable ...}` sugar | 2026-01-03 |
| 19 | Enum namespace | Smart qualification (only when ambiguous) | 2026-01-03 |
| 20 | Pattern predicate eval | Any expression returning predicate | 2026-01-03 |
| 21 | Quote + brackets | Preserve structure types | 2026-01-03 |
| 22 | Channel closed send | `(send)` returns `false` if closed | 2026-01-03 |
| 23 | Lambda shorthand | Arrow `(-> ...)` | 2026-01-03 |
| 24 | Begin semantics | Empty returns `nothing`, no scope | 2026-01-03 |
| 25 | For/foreach bindings | Nested (Cartesian) | 2026-01-03 |
| 26 | Self in methods | Explicit `self` parameter | 2026-01-03 |
| 27 | Nil vs nothing | No `nil`; keep `nothing` distinct | 2026-01-03 |
| 28 | Error handling | `error` + single `restart-case` (Phase-1) | 2026-01-03 |
| 29 | Named arguments | `&` separator | 2026-01-03 |

---

## Summary Checklist

**Must Decide:**
- [x] 1. Struct Construction
- [x] 2. Parametric Struct Parent
- [x] 3. Struct Mutability
- [x] 4. Enum Namespace
- [x] 13. Error Handling

**Should Decide:**
- [x] 5. Pattern Predicate Eval
- [x] 6. Quote and Brackets
- [x] 11. Self in Methods
- [x] 12. Nil vs Nothing

**Can Defer:**
- [x] 7. Channel Closed Semantics
- [x] 8. Lambda Shorthand
- [x] 10. For/Foreach Bindings
- [x] 14. Named Arguments
