# Omnilisp Design Decisions
Tag: Syntax Guide

This document catalogs open design questions requiring resolution before implementation.

---

## 1. Parameter Syntax

### Current State
```lisp
(define (foo [x 10] [y Int] [z String "hello"])
  ...)
```

**Decision: How to Distinguish Type vs Default?**

**Problem:** `[x 10]` vs `[y Int]` - parser needs semantic info to know `Int` is a type and `10` is a value.

**Selected Option: {Type} wrapper**
Type annotations are wrapped in `{}` to distinguish them from default values.

| Syntax | Meaning |
|--------|---------|
| `[x {Int}]` | Name `x`, type `Int` |
| `[x 10]` | Name `x`, default value `10` |
| `[x {Int} 10]` | Name `x`, type `Int`, default `10` |

**Decision:** Use `{}` for types in parameters. Consistent with `{}` being the "type-level" bracket.

---

## 2. Let Binding Type Annotations

**Decision: How to Type-Annotate Let Bindings?**

**Selected Option: Triplet syntax**
Consistent with function parameters, let bindings can be triplets.

| Syntax | Meaning |
|--------|---------|
| `[x 1]` | Pair: Name and value |
| `[x {Int} 1]` | Triplet: Name, type, and value |

**Decision:** Use the same triplet pattern as function parameters. The parser distinguishes triplets by the presence of the `{}` type wrapper.

---

## 3. Truthiness Semantics
...
---

## 4. Named Let + Modifiers

**Decision: Can These Combine?**

**Selected Option: B (Modifier before name)**
The modifier (like `:seq` or `:rec`) comes before the loop name.

**Example:**
`(let :seq loop [x 1 y (+ x 1)] ...)`

**Decision:** Option B. Clearer grammar and separation of concerns.

---

## 5. Rest/Variadic Syntax

**Decision: Unify or Distinguish `.` Usages?**

**Selected Option: Use `..` (and `...`)**
Use `..` as the primary rest/variadic marker. `...` is supported as an alias to avoid visual confusion errors.

| Context | Syntax |
|---------|--------|
| Rest Pattern | `[h .. t]` (or `[h ... t]`) |
| Variadic Params | `(define (sum .. nums) ...)` |
| Field Access | `obj.field` |

**Decision:** Option B modified. `..` is cleaner, but `...` is allowed. Range operations use `:` or explicit `range` to avoid conflict.

---

## 6. Multi-Arity Function Syntax

**Decision: Better Multi-Arity Syntax?**

**Selected Option: E (Overloads)**
Multi-arity is handled via multiple dispatch. A function can be defined multiple times with different signatures.

**Example:**
```lisp
(define (greet) (greet "Guest"))
(define (greet name) (print "Hello, $name"))
```

**Decision:** Option E. Arity dispatch is just a specific case of multiple dispatch. This unifies the language model and simplifies `define`.

---

## 7. Return Type Annotations

**Decision: Add Return Type Annotations?**

**Selected Option: Type after params (in `{}`)**
Return types are placed after the argument list, wrapped in `{}`.

**Example:**
```lisp
(define (add [x {Int}] [y {Int}]) {Int}
  (+ x y))
```

**Decision:** Option F. Consistent with `{}` as type annotation. No special arrow syntax needed.

---

## 8. Interface vs Abstract Types

**Decision: What's the Relationship?**

**Selected Option: A (Abstract + Interface)**
Omnilisp supports both.
*   **Abstract Types:** Nodes in the single-inheritance type hierarchy (for code sharing/structure).
*   **Interfaces:** Multi-implementable behavior contracts.

**Decision:** Interfaces are explicit contracts. They provide safety and tooling benefits over informal duck typing.

---

## 9. Interface Method Signatures

**Decision: What Does `[self Canvas]` Mean?**

**Selected Option: Typed Params with `{Self}`**
To describe the contract explicitly in a generic function world, we use `{Self}` as a placeholder for the implementing type.

**Example:**
```lisp
(define {interface Drawable}
  (draw [subject {Self}] [ctx {Context}]))
```

**Decision:** Option B modified. Use `{Self}` to identify the receiver(s).

---

## 10. Method Call Syntax

**Decision: How to Call Methods with Arguments?**

**Selected Option: E (UFCS) + Dot Sugar**
Methods are just generic functions. `(method obj args)` is the standard form.
The dot syntax `(obj.method args)` is supported as sugar for calling a method found on an object.

**Decision:** Uniform Function Call Syntax. `(obj.method args)` expands to `(method obj args)`.

---

## 11. Struct Construction

### Current State
```lisp
(define {struct Point Any}
  [x Float]
  [y Float])

(Point 10.0 20.0)  ; Positional construction
```

### Decision: Support Named/Keyword Construction?

**Options:**

| Option | Syntax | Notes |
|--------|--------|-------|
| A. Positional only | `(Point 10.0 20.0)` | Simple, order-dependent |
| B. Named with & | `(Point & :x 10.0 :y 20.0)` | Reuses named args syntax |
| C. Named with #{} | `(Point #{:x 10.0 :y 20.0})` | Dict-like |
| D. Both allowed | `(Point 10.0 20.0)` or `(Point :x 10.0 :y 20.0)` | Flexible |
| E. Record literal | `{Point :x 10.0 :y 20.0}` | Special syntax |

**Considerations:**
- Partial construction (some fields, others default)?
- Update syntax: `(Point p :x 99)` to copy with changes?
- Interaction with pattern matching

**Decision needed:** ____________________

---

## 12. Parametric Struct + Parent Type

### Current State
```lisp
(define {struct Point Any}     ; Point extends Any
  [x Float] [y Float])

(define {struct Pair T}        ; Pair<T>, parent unclear
  [first T] [second T])
```

### Decision: How to Specify Both Type Params and Parent?

**Options:**

| Option | Syntax | Notes |
|--------|--------|-------|
| A. Parent always last | `(define {struct Pair T Any} ...)` | T is param, Any is parent |
| B. Parent with keyword | `(define {struct Pair T :extends Any} ...)` | Explicit |
| C. Separate inheritance | `(define {struct Pair T} :extends Any ...)` | Outside braces |
| D. Parent in body | `(define {struct Pair T} (:parent Any) [first T] ...)` | In field list |

**Questions:**
- Can parametric types have non-Any parents?
- Variance: covariant, contravariant, invariant?

**Decision needed:** ____________________

---

## 13. Pattern Predicate Evaluation

### Current State
```lisp
(satisfies pred)  ; Match if (pred value) is true
```

### Decision: When/How is Predicate Evaluated?

**Questions:**
1. Is `pred` evaluated once at pattern compile time, or each match?
2. Can `pred` be a local variable?
3. Can `pred` be an arbitrary expression?

**Options:**

| Option | Behavior | Example |
|--------|----------|---------|
| A. Static only | `pred` must be a known function | `(satisfies even?)` only |
| B. Dynamic lookup | `pred` evaluated each match | `(let [p even?] (match x [(satisfies p) ...]))` |
| C. Inline expression | Any expr returning predicate | `(satisfies (lambda (x) (> x 10)))` |

**Considerations:**
- Performance implications
- Hygiene in macros
- Error messages

**Decision needed:** ____________________

---

## 14. `else` in Match

**Decision: Add `else` to Match?**

**Selected Option: D (`else` is sugar for `_`)**
`else` can be used in the pattern position of a `match` branch.

**Example:**
```lisp
(match x
  [1 "one"]
  [else "other"])
```

**Decision:** Support `else`. It reads better than `_` for default cases and provides consistency with `cond`.

---

## 15. Quote and Brackets

### Current State
```lisp
'symbol           ; Quoted symbol
:symbol           ; Sugar for 'symbol
```

### Decision: How Does Quote Interact with [], {}, #{}?

**Questions:**
```lisp
'(1 2 3)    ; -> list? quoted form?
'[1 2 3]    ; -> quoted array? vector?
'#{:a 1}    ; -> quoted dict?
#'(...)     ; -> syntax object (for macros)
```

**Options:**

| Option | `'(...)` | `'[...]` | `'#{...}` |
|--------|----------|----------|-----------|
| A. All become lists | list | list | list |
| B. Preserve structure | quoted list | quoted array | quoted dict |
| C. Only () quotable | list | ERROR | ERROR |
| D. () is special | syntax object | quoted array | quoted dict |

**Considerations:**
- Macro manipulation needs
- Eval/read symmetry
- Runtime representation of quoted forms

**Decision needed:** ____________________

---

## 16. Channel Semantics

### Current State
```lisp
(send ch value)   ; Blocks if full
(recv ch)         ; Blocks if empty
(close ch)
;; Receive from closed = nothing
```

### Decision: Send to Closed Channel?

**Options:**

| Option | Behavior | Notes |
|--------|----------|-------|
| A. Error | Runtime error on send to closed | Fail-fast |
| B. Return false | `(send ch v)` returns `false` if closed | Must check |
| C. Silent drop | Value discarded | Dangerous |
| D. Block forever | Never returns | Probably bad |

**Additional questions:**
- Can you check if channel is closed? `(closed? ch)`
- Multiple receivers - fair scheduling?
- Select/alt for multiple channels?

**Decision needed:** ____________________

---

## 17. Lambda Shorthand

### Current State
```lisp
(lambda (x) (+ x 1))
(lambda (x y) (* x y))
```

### Decision: Add Lambda Shorthand?

**Options:**

| Option | Single Arg | Multi Arg | Notes |
|--------|------------|-----------|-------|
| A. No shorthand | `(lambda (x) ...)` | `(lambda (x y) ...)` | Status quo |
| B. Clojure-style | `#(+ % 1)` | `#(* %1 %2)` | `%` placeholders |
| C. Backslash | `\(+ _ 1)` | `\(* _1 _2)` | Haskell-ish |
| D. Arrow | `(-> x (+ x 1))` | `(-> (x y) (* x y))` | ES6-ish |
| E. Fn alias | `(fn (x) ...)` | `(fn (x y) ...)` | Just shorter name |

**Considerations:**
- Nesting lambdas with placeholders
- Readability
- Conflict with other syntax

**Decision needed:** ____________________

---

## 18. `begin` Semantics

### Current State
Listed as special form but undocumented.

### Decision: Define `begin` Precisely

**Questions:**
1. What does `(begin)` (empty) return?
2. Are all expressions evaluated?
3. Is it just sequencing or does it create a scope?

**Options:**

| Option | Empty | Return | Scope |
|--------|-------|--------|-------|
| A. Sequence only | `nothing` | Last expr | No new scope |
| B. Implicit scope | `nothing` | Last expr | New scope |
| C. Error if empty | ERROR | Last expr | No new scope |
| D. Returns all | `nothing` | Tuple of all | No new scope |

**Decision needed:** ____________________

---

## 19. For/Foreach Distinction

### Current State
```lisp
(foreach [x items] (print x))   ; Side effects, returns nothing?
(for [x items] (* x x))         ; Comprehension, returns array
```

### Decision: Clarify Semantics

**Questions:**
1. What does `foreach` return?
2. Can `for` have multiple bindings? (Nested vs flat?)
3. What if `for` body returns `nothing`?

**Options for multiple bindings in `for`:**

| Option | Syntax | Result |
|--------|--------|--------|
| A. Nested (Cartesian) | `(for [x xs y ys] (tuple x y))` | All pairs |
| B. Parallel (zip) | `(for [x xs y ys] (tuple x y))` | Paired elements |
| C. Explicit choice | `(for :nested [x xs y ys] ...)` vs `(for :zip ...)` | User decides |

**Decision needed:** ____________________

---

## Summary: Priority Order

### Must Decide Before Implementation
1. Parameter type vs default syntax (#1)
2. Truthiness semantics (#3)
3. Rest/variadic `.` syntax (#5)
4. Interface method signatures (#9)

### Should Decide Soon
5. Let binding type annotations (#2)
6. Named let + modifiers (#4)
7. Multi-arity syntax (#6)
8. Abstract vs Interface relationship (#8)

### Can Defer
9. Return type annotations (#7)
10. Method call syntax (#10)
11. Struct construction options (#11)
12. Lambda shorthand (#17)
13. Channel closed semantics (#16)

---

## Decision Log

| # | Topic | Decision | Date | Notes |
|---|-------|----------|------|-------|
| 1 | Param syntax | Use `{Type}` for annotations | 2026-01-02 | Unambiguous and consistent with bracket semantics. |
| 2 | Let type annotations | Triplet syntax `[x {T} v]` | 2026-01-02 | Consistent with function params. |
| 3 | Truthiness | Option B (Lisp-style) | 2026-01-02 | Falsy = `false`, `nothing`. Everything else truthy. |
| 4 | Named Let + Modifiers | Option B: `:seq loop` | 2026-01-02 | Modifier precedes name. |
| 5 | Rest Syntax | `..` / `...` | 2026-01-02 | `..` is primary, `...` alias. No dot conflict. |
| 6 | Multi-Arity | Overloads | 2026-01-02 | Handled via Multiple Dispatch. |
| 7 | Return Types | After params `{T}` | 2026-01-02 | Consistent with type wrapper syntax. |
| 8 | Interfaces | Explicit | 2026-01-02 | Restored for robustness/safety. |
| 9 | Interface Sigs | `{Self}` | 2026-01-02 | Placeholder for implementing type. |
| 10 | Method Calls | UFCS `(obj.f args)` | 2026-01-02 | Maps to `(f obj args)`. |
| 14 | Match else | Supported | 2026-01-02 | Sugar for `_` pattern. |
