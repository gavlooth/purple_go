# Omnilisp Design Specification
Tag: Syntax Guide

## 1. Core Philosophy

### 1.1 Paradigm
Multi-paradigm (Functional + Imperative) with a bias toward functional idioms.

### 1.2 Type System: Julia-Style Optional Types

**OmniLisp's type system is exactly like Julia's:**
- **Types are ALWAYS optional** - you can omit them everywhere
- **Multiple dispatch on ALL arguments** - not just the first
- **No class-based OOP** - structs + multiple dispatch instead
- **Gradual typing** - add types for documentation, dispatch, or optimization

#### Type Annotation Syntax

Types are written in curly braces `{}` and are always optional:

```lisp
;; Fully untyped (like dynamic Python/JavaScript)
(define (add x y)
  (+ x y))

;; Partially typed (type just what matters)
(define (add [x {Int}] y)
  (+ x y))

;; Fully typed (explicit types everywhere)
(define (add [x {Int}] [y {Int}]) {Int}
  (+ x y))
```

#### Parameter Forms

| Syntax | Meaning |
|--------|---------|
| `x` | Untyped parameter |
| `[x {Int}]` | Typed parameter |
| `[x 10]` | Parameter with default value |
| `[x {Int} 10]` | Typed parameter with default |

#### Return Type

Place type annotation after parameters to specify return type:

```lisp
(define (square [x {Float}]) {Float}
  (* x x))
```

#### Why Julia-Style?

1. **Flexibility** - Write quick scripts without types, add them later for robustness
2. **Performance** - Compiler can optimize when types are known
3. **Dispatch** - Multiple dispatch enables clean extensibility without inheritance
4. **Interop** - Untyped code seamlessly calls typed code and vice versa

### 1.3 Syntax
Scheme-style S-expressions with extended bracket semantics:
*   `()` for forms, function calls, and constructor patterns.
*   `[]` for data-level grouping (arrays, match branches, parameter specs, struct fields).
*   `{}` for type-level forms only (types, interfaces, parametrics).
*   `#{}` for dictionary literals.

### 1.3 Bracket Semantics Summary

| Bracket | Context | Meaning | Example |
|---------|---------|---------|---------|
| `()` | General | Form / function call | `(+ 1 2)` |
| `()` | In pattern | Constructor pattern | `(Some v)`, `(Point x y)` |
| `[]` | Expression | Array literal | `[1 2 3]` |
| `[]` | In `let` | Binding pairs/triplets | `[x 1]`, `[x {Int} 1]` |
| `[]` | In `match`/`cond` | Branch `[pattern result]` | `[[] "empty"]` |
| `[]` | In pattern | Array pattern | `[a b]`, `[h .. t]` |
| `[]` | In params | Param spec | `[x {Int}]`, `[x 10]`, `[x {Int} 10]` |
| `[]` | In struct | Field definition | `[name {Type}]` |
| `{}` | In `define` | Type-level form | `{struct ...}`, `{enum ...}` |
| `{}` | Type annotation | Type wrapper | `{Int}`, `{Array Int}`, `{Self}` |
| `#{}` | Expression | Dict literal | `#{:a 1 :b 2}` |

### 1.4 Collections
*   **Arrays:** Primary mutable sequence. `[]` literals produce mutable arrays.
*   **Lists:** Secondary persistent linked lists.
*   **Tuples:** Fixed-size immutable records for multiple returns.
*   **Dicts:** Key-value maps using `#{}` syntax (deferred to later phases).

### 1.5 Object Model
Multiple Dispatch (Julia-style). No class-based OOP.

### 1.6 Control Flow
Delimited Continuations (`prompt`/`control`), Trampolined Tail Calls.

### 1.7 Current Implemented Subset (C Compiler)
The current C compiler/runtime implements a **small core** of the language. The rest
of this document describes the intended language design.

**Implemented syntax & forms**
- Lists `(...)`
- Quote `'x` -> `(quote x)`
- Special forms: `define`, `lambda` / `fn`, `let`, `let*`, `if`, `do` / `begin`
- Function application in prefix form
- Comments start with `;` and run to end-of-line

**Binding forms**
- List-style: `(let ((x 1) (y 2)) ...)`
- Array-style: `(let [x 1 y 2] ...)` (used as a binding container)

**Literals**
- Integers (decimal)
- Symbols
- Empty list `()`

**Primitives currently wired**
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `<`, `>`, `<=`, `>=`, `=`
- Lists: `cons`, `car`, `cdr`, `null?`
- I/O: `display`, `print`, `newline`

**Truthiness**
- Empty list and numeric zero are false; everything else is truthy.

---

## Planned Design (Not Yet Implemented in C Compiler)

The sections below describe the intended Omnilisp language design and are not yet implemented in the C compiler unless explicitly noted.

## 2. Values & Nothing

### 2.1 The `nothing` Value
`nothing` is Omnilisp's **unit value** representing absence, void, or "no meaningful result." It is the return value of side-effecting operations.

```lisp
(print "hello")  ; -> nothing
(set! x 10)      ; -> nothing
```

### 2.2 Empty Collections (Not `nothing`)
Empty collections are **distinct values**, not `nothing`:

```lisp
[]               ; Empty array (not nothing)
(list)           ; Empty list (not nothing)
(tuple)          ; Empty tuple (not nothing)
#{}              ; Empty dict (not nothing)

;; Explicit checks
(empty? [])      ; -> true
(nothing? [])    ; -> false
(nothing? nothing) ; -> true
```

### 2.3 Optional Values
For optional/nullable semantics, use explicit wrapper types (see Section 6 on Sum Types):

```lisp
(define {enum Option T}
  (Some [value T])
  None)

;; Usage
(define (find-user id)
  (if (exists? id)
      (Some (get-user id))
      None))
```

---

## 3. Bindings & Definitions

### 3.1 Universal `define`
Omnilisp unifies all top-level bindings under `define`:

```lisp
;; Values
(define x 10)
(define msg "Hello")

;; Functions
(define (add x y) (+ x y))

;; With type annotations
(define (add [x {Int}] [y {Int}]) {Int}
  (+ x y))
```

### 3.2 Local Bindings (`let`)
Omnilisp provides a unified `let` form with Clojure-style binding syntax. Bindings are in `[]` as an even number of forms (name-value pairs).

#### Basic Let (Parallel Binding)
All bindings are evaluated in the enclosing environment, then bound simultaneously. Supports triplets for type annotations:

```lisp
(let [x {Int} 1
      y {Int} 2]
  (+ x y))  ; -> 3
```

#### Sequential Let (`let*` behavior with `:seq`)
Use `:seq` to enable sequential binding where each binding sees previous ones:

```lisp
(let :seq [x 1
           y (+ x 1)
           z (+ y 1)]
  z)  ; -> 3
```

#### Recursive Let (`letrec` behavior with `:rec`)
...

#### Named Let (Loop Form)
A `let` with a name before the bindings creates a recursive loop. Can be combined with modifiers:

```lisp
(let :seq loop [i {Int} 0
                acc {Int} 0]
  (if (> i 10)
      acc
      (loop (+ i 1) (+ acc i))))  ; -> 55
```

### 3.3 Destructuring in Bindings
Both `define` and `let` support pattern destructuring:

```lisp
(define [a b c] [1 2 3])
(define [x .. rest] (some-function))

(let [[first .. rest] [1 2 3 4]]
  first)  ; -> 1

(let :seq [[a .. bs] [1 2 3 4]
           sum (reduce + 0 bs)]
  sum)  ; -> 9
```

### 3.4 Multi-Arity Functions
Functions with multiple arities are defined by overloading the function name. Arity dispatch is handled by the multiple dispatch system.

```lisp
(define (greet)
  (greet "Guest"))

(define (greet name)
  (print "Hello, $name"))

(define (greet name title)
  (print "Hello, $title $name"))
```

### 3.5 Default Parameters
Use `[name default]` or `[name {Type} default]` for defaults:

```lisp
(define (greet [name "Guest"])
  (print "Hello, $name"))

(define (connect [host {String} "localhost"] [port {Int} 8080])
  ...)
```

### 3.6 Variadic Functions

```lisp
(define (sum .. nums)
  (reduce + 0 nums))

(define (format template .. args)
  ...)
```

### 3.7 Named Arguments
The symbol `&` separates positional from named parameters:

```lisp
;; Definition
(define (draw shape & [color :black] [stroke Int 1])
  ...)

;; Call-site: & starts named argument section
(draw circle & :color :red :stroke 2)

;; Expands to:
(draw circle (named-tuple [color :red] [stroke 2]))
```

Semantics:
*   Unknown named keys are an error unless `& opts` captures the rest.
*   Duplicate named keys are an error.
*   Named arguments do not affect arity or dispatch.

---

## 4. Control Structures

### 4.1 The Core Primitive: `match` (Special Form)

`match` is a **special form** (not a macro). The evaluator handles it directly.

Branches use `[]` for grouping - consistent with `[]` being "data-level grouping":

```lisp
(match value
  [pattern1 result1]
  [pattern2 result2]
  [else default])
```

Each branch is a `[pattern result]` pair. The special form knows:
- Element 0 = pattern
- Element 1 = result (or `:when` guard result)
- `else` is a valid wildcard pattern.
**No ambiguity:** Even `[[] []]` is clear - it's a branch where pattern is `[]` and result is `[]`.

```lisp
(match arr
  [[] "empty"]           ; Pattern: [], Result: "empty"
  [[a] a]                ; Pattern: [a], Result: a
  [[a b] (+ a b)]        ; Pattern: [a b], Result: (+ a b)
  [[] []]                ; Pattern: [], Result: [] - perfectly clear!
  [_ "default"])
```

### 4.2 Guards

Guards extend the branch to 4 elements: `[pattern :when guard result]`

```lisp
(match n
  [x :when (> x 100) "big"]
  [x :when (> x 10) "medium"]
  [_ "small"])
```

Branch structure:
- 2 elements: `[pattern result]`
- 4 elements: `[pattern :when guard-expr result]`

### 4.3 Standard Conditionals

#### `if`
```lisp
(if condition then-expr else-expr)

;; Expands to (Lisp-style truthiness: false/nothing are falsy, all else truthy):
(match condition
  [(or false nothing) else-expr]
  [_ then-expr])
```

#### `cond` (Macro)
`cond` is a macro that expands to `match`:

```lisp
(cond
  [condition1 result1]
  [condition2 result2]
  [else default])

;; Expands to:
(match true
  [_ :when condition1 result1]
  [_ :when condition2 result2]
  [_ default])
```

The `else` clause becomes a wildcard `_` without a guard.

#### `when` / `unless`
```lisp
(when condition body ...)
;; -> (if condition (begin body ...) nothing)

(unless condition body ...)
;; -> (if condition nothing (begin body ...))
```

### 4.4 Pattern Matching

#### Pattern Primitives
*   **Wildcard:** `_` matches any value, no binding.
*   **Variable:** `name` matches any value, binds it.
*   **Literal:** `1`, `"string"`, `:symbol` matches exact value.

#### Logic Patterns
```lisp
(or p1 p2 ...)   ; Match if any pattern matches
(and p1 p2 ...)  ; Match if all patterns match
(not p)          ; Match if pattern doesn't match
```

#### Constructor Patterns
```lisp
[]                       ; Empty array
[p1 p2 ..]               ; Array with elements
[h .. t]                 ; Array head/tail (rest pattern)

(list)                   ; Empty list
(list p1 p2 ..)          ; List with elements
(cons head tail)         ; Cons pattern

(tuple p1 p2 ..)         ; Tuple pattern
(named-tuple [k1 p1] ..) ; Named tuple pattern

(TypeName p1 p2 ..)      ; Struct/enum constructor pattern
```

#### Predicates
```lisp
(satisfies pred)         ; Match if (pred value) is true
```

#### Rest Patterns
```lisp
[first .. rest]          ; Bind head and tail of array
[a b .. _]               ; Match at least 2 elements, ignore rest
```

#### Alias Pattern
```lisp
(as pattern name)        ; Match pattern and bind whole value to name
```

### 4.5 Iterators & Looping

#### Protocol
```lisp
(iterate collection)       ; -> (tuple value state) or nothing
(iterate collection state) ; -> (tuple value state) or nothing
```

#### Looping Macros
```lisp
;; Side effects
(foreach [x (range 1 10)]
  (print x))

;; Comprehension (builds array)
(for [x (range 1 10)]
  (* x x))

;; With filter
(for [x (range 1 100) :when (even? x)]
  x)
```

### 4.6 Delimited Continuations

```lisp
(prompt body ...)          ; Establish delimiter
(control k body ...)       ; Capture continuation up to prompt, bind to k
```

### 4.7 Condition System (Basic)
Phase-1 provides minimal error handling:

```lisp
(error "message")          ; Raise non-resumable error
(error Type "message")     ; Raise typed error

;; Basic restart (single)
(restart-case expr
  (restart-name (args) handler-body))
```

Full CL-style conditions with `signal`, `handler-bind`, and multi-restart negotiation are deferred.

---

## 5. Type System

### 5.1 Abstract Types
Define nodes in the type hierarchy for dispatch:

```lisp
(define {abstract Animal} Any)
(define {abstract Mammal} Animal)
(define {abstract Bird} Animal)
```

### 5.2 Concrete Structs
Structs are **immutable by default**:

```lisp
(define {struct Point}
  [x {Float}]
  [y {Float}])

(define {struct Circle :extends Shape}
  [center {Point}]
  [radius {Float}])
```

#### Mutable Structs
```lisp
(define {struct Player}
  [hp :mutable {Int}]
  [name {String}]
  [pos {Point}])

;; Sugar: (define {mutable Player} ...) marks all fields mutable.

(define hero (Player 100 "Arthur" (Point 0 0)))
(set! hero.hp 95)
```

### 5.3 Parametric Types
Use `{}` for type parameters:

```lisp
{Array Int}              ; Array of integers
{Dict Symbol String}     ; Dictionary
{Array Float 2}          ; 2D array
{Option Int}             ; Optional integer
```

#### Defining Parametric Types
```lisp
(define {struct [Pair T]}
  [first {T}]
  [second {T}])

(define {struct [Entry K V]}
  [key {K}]
  [value {V}])
```

---

## 6. Sum Types (Enums)

Omnilisp supports algebraic data types via `enum`:

### 6.1 Basic Enums
```lisp
(define {enum Color}
  Red
  Green
  Blue)

(match color
  [Red "stop"]
  [Green "go"]
  [Blue "wait"])
```

### 6.2 Enums with Data
```lisp
(define {enum Option T}
  (Some [value {T}])
  None)

(define {enum Result T E}
  (Ok [value {T}])
  (Err [error {E}]))

(define {enum List T}
  (Cons [head {T}] [tail {List T}])
  Nil)
```

### 6.3 Pattern Matching on Enums
```lisp
(define (unwrap-or opt default)
  (match opt
    [(Some v) v]
    [None default]))

(define (map-result f result)
  (match result
    [(Ok v) (Ok (f v))]
    [(Err e) (Err e)]))
```

### 6.4 Recursive Enums
```lisp
(define {enum Expr}
  (Lit [value {Int}])
  (Add [left {Expr}] [right {Expr}])
  (Mul [left {Expr}] [right {Expr}]))

(define (eval expr)
  (match expr
    [(Lit n) n]
    [(Add l r) (+ (eval l) (eval r))]
    [(Mul l r) (* (eval l) (eval r))]))
```

---

## 7. Multiple Dispatch

### 7.1 Generic Functions
```lisp
;; Declare generic with fallback
(define (area [s Shape])
  (error "No area method for $(type-of s)"))

;; Implement for specific types
(define [method area Circle] (c)
  (* 3.14159 c.radius c.radius))

(define [method area Rect] (r)
  (* r.width r.height))
```

### 7.2 Multi-Argument Dispatch
```lisp
(define [method collide Circle Circle] (a b)
  ...)

(define [method collide Circle Rect] (c r)
  ...)

(define [method collide Rect Circle] (r c)
  (collide c r))  ; Delegate to other method
```

### 7.3 Specificity Rules
*   Method A is more specific than B if all parameter types in A are subtypes of corresponding types in B.
*   If no unique most-specific method exists, an ambiguity error is raised.
*   Parametric types are invariant.

### 7.4 Interfaces (Protocols)
Interfaces define explicit contracts for generic functions. `{Self}` is used as a placeholder for the implementing type.

```lisp
(define {interface Drawable}
  "Protocol for renderable objects."
  (draw [self {Self}] [ctx {Context}])
  (bounds [self {Self}]))

;; Implementation via methods
(define [method draw Circle] (self ctx)
  ...)

(define [method bounds Circle] (self)
  (Rect (* 2 self.radius) (* 2 self.radius)))
```

---

## 8. Arrays & Sequences

### 8.1 Array Basics
Arrays are mutable, contiguous sequences:

```lisp
[1 2 3 4]                ; Array literal
(array 1 2 3)            ; Constructor form

;; Access
arr.(0)                  ; First element
arr.(n)                  ; nth element (0-indexed)

;; Mutation
(set! arr.(0) 99)
```

### 8.2 Mutation Semantics

**Principle:** Sequence functions return **new arrays** by default. Use `!`-suffixed variants for in-place mutation.

```lisp
;; Functional (return new array)
(map f arr)              ; -> new array
(filter pred arr)        ; -> new array
(reverse arr)            ; -> new array

;; Imperative (mutate in place, return nothing)
(map! f arr)             ; Mutates arr, returns nothing
(filter! pred arr)       ; Mutates arr, returns nothing
(reverse! arr)           ; Mutates arr, returns nothing
(sort! arr)              ; Mutates arr, returns nothing
```

### 8.3 Aliasing & Copying
Arrays are reference types. Assignment creates aliases:

```lisp
(define a [1 2 3])
(define b a)             ; b is an alias to a
(set! a.(0) 99)
b.(0)                    ; -> 99 (same array)

;; Explicit copy
(define c (copy a))      ; c is independent
(set! a.(0) 0)
c.(0)                    ; -> 99 (unchanged)
```

### 8.4 Slicing & Projection
```lisp
arr.[0 2 4]              ; Select indices -> new array [arr.(0) arr.(2) arr.(4)]
arr.[1:3]                ; Slice range -> new array

;; Slices are copies, not views
(define slice arr.[0:2])
(set! arr.(0) 999)
slice.(0)                ; Unchanged (slice is a copy)
```

### 8.5 Lists (Persistent)
```lisp
(list 1 2 3)             ; Linked list
(cons 1 (list 2 3))      ; Prepend
(car lst)                ; Head
(cdr lst)                ; Tail

;; Empty list
(list)                   ; -> empty list (NOT nothing)
(null? (list))           ; -> true
(null? [])               ; -> false (use empty? for arrays)
```

### 8.6 Tuples
Fixed-size immutable sequences for multiple returns:

```lisp
(tuple 1 2 3)
(values a b c)           ; Sugar for tuple in return position

;; Destructuring
(define (tuple x y) (min-max 10 20))

;; Access
tup.(0)                  ; First element
```

### 8.7 Iteration Protocol
All sequences implement `iterate`:

```lisp
(collect (range 1 5))    ; -> [1 2 3 4]
(iter->list (range 1 5)) ; -> (list 1 2 3 4)
(iter->array some-list)  ; -> array from list
```

---

## 9. Access Syntax

### 9.1 Dot Notation
```lisp
obj.field                ; -> (get obj 'field)
obj.(expr)               ; -> (get obj expr) - dynamic access
obj.[indices]            ; -> multi-access / slice
```

### 9.2 Functional Accessors
A leading dot creates a getter function:

```lisp
.name                    ; -> (lambda (it) it.name)
.pos.x                   ; -> (lambda (it) it.pos.x)
.items.(0)               ; -> (lambda (it) it.items.(0))

;; Usage
(map .name users)        ; Extract names from users
(filter .active? items)  ; Filter by predicate field
```

### 9.3 Method Calls (UFCS)
Uniform Function Call Syntax: `(obj.method args)` expands to `(method obj args)`.

```lisp
(obj.draw ctx)             ; -> (draw obj ctx)
(arr.map f)                ; -> (map arr f)
(str.split ",")            ; -> (split str ",")
```

### 9.4 Mutation
```lisp
(set! obj.field value)
(set! obj.(index) value)
(set! (.field obj) value)  ; Equivalent
```

---

## 10. Macros

### 10.1 Hygienic Macros
```lisp
(define [macro unless] (condition body)
  #'(if (not ~condition) ~body nothing))

(define [macro with-log] (msg . body)
  #'(begin
      (print "Start: " ~msg)
      (let [result (begin ~@body)]
        (print "End: " ~msg)
        result)))
```

### 10.2 Core API
*   `syntax` / `#'` - Create syntax object
*   `syntax->datum` - Extract value from syntax
*   `datum->syntax` - Wrap value in syntax context
*   `gensym` / `fresh` - Generate unique symbol
*   `~` - Unquote in syntax template
*   `~@` - Splicing unquote

---

## 11. Piping & Composition

### 11.1 Pipe Operator
```lisp
(|> value
    (f arg1)           ; -> (f value arg1)
    (g arg2))          ; -> (g result arg2)
```

### 11.2 Placeholder
Use `%` to control placement:

```lisp
(|> 10
    (- 100 %))         ; -> (- 100 10) -> 90
```

### 11.3 With Functional Accessors
```lisp
(|> users
    (filter .active?)
    (map .name)
    (sort))
```

---

## 12. Concurrency

### 12.1 Green Threads
```lisp
(define p (spawn (lambda () (compute-something))))
```

### 12.2 Channels
```lisp
(define ch (channel))        ; Unbuffered
(define ch (channel 10))     ; Buffered (capacity 10)

(send ch value)              ; Send (blocks if full)
(recv ch)                    ; Receive (blocks if empty)
(close ch)                   ; Close channel

;; Receive from closed channel returns nothing
```

### 12.3 Park / Unpark
```lisp
(park)                       ; Suspend current thread
(unpark process value)       ; Resume process with value
```

### 12.4 Yield
```lisp
(yield)                      ; Cooperative yield to scheduler
```

---

## 13. Modules

### 13.1 Definition
```lisp
(module MyModule
  (export function1 function2 TypeName)

  (define (function1 ...) ...)
  (define (function2 ...) ...)
  (define {struct TypeName} ...))
```

### 13.2 Import
```lisp
(import MyModule)                    ; Import all exports
(import [MyModule :only (f1 f2)])    ; Import specific
(import [MyModule :as M])            ; Qualified import
(import [MyModule :refer (f1) :as M]) ; Mixed
```

---

## 14. Strings

### 14.1 Interpolation
```lisp
"Hello, $name"
"Sum: $(+ x y)"
"Escape: \$100"
```

### 14.2 String Prefixes
```lisp
r"regex\d+"              ; Regex (Pika-powered)
raw"C:\path\file"        ; Raw string (no escapes)
b"binary\x00data"        ; Byte array
```

---

## 15. Foreign Function Interface

```lisp
(@ffi "function_name" arg1 arg2 ...)

;; Examples
(@ffi "printf" "Value: %d\n" 42)
(@ffi "malloc" size)
```

Type marshalling is automatic between Omnilisp and native types.

---

## 16. Metadata

```lisp
^metadata object
^:keyword object

;; Examples
(define ^:private ^:hot internal-fn ...)
(define ^"Docstring here" (my-func x) ...)
```

Metadata is stored separately and does not affect identity or equality.

---

## 17. Special Forms vs Macros

### Special Forms (handled by evaluator)
*   `match` - Pattern matching
*   `if` - Binary conditional
*   `define` - Bindings
*   `let` - Local bindings
*   `lambda` - Function creation
*   `set!` - Mutation
*   `begin` - Sequencing
*   `quote` / `'` - Quoting
*   `prompt` / `control` - Continuations

### Macros (expand to special forms)
*   `cond` - Expands to `match` with guards
*   `when` / `unless` - Expand to `if`
*   `for` / `foreach` - Expand to iterator calls
*   `|>` - Expand to nested calls
*   User-defined macros via `define [macro ...]`

---

## 18. Phase-1 Scope

### Include
*   Reader: `()`, `[]`, `{}`, `#{}`, dot access, `:sym` sugar, core literals
*   Unified `define`: values, functions, methods, macros, types
*   `let` with `:seq` and `:rec` modifiers
*   Type system: abstract types, structs, parametric types, enums
*   Multiple dispatch with specificity rules
*   `match` as special form with `[]` branches; `cond` as macro expanding to `match`
*   Pattern matching: all core patterns, guards, no user-defined patterns
*   Basic hygienic macros
*   Iterator protocol + `foreach`/`for`
*   Module system
*   Basic error handling (`error` + single restart)
*   Trampolined TCO
*   Cooperative concurrency: `spawn`, `yield`, channels
*   Mutable arrays, persistent lists, tuples

### Defer
*   Extensible patterns (`define [pattern ...]`)
*   Full condition system (multi-restart, `handler-bind`)
*   Dicts and sets (basic `#{}` syntax available, full API later)
*   Advanced FFI (callbacks, structs)

---

## 19. Tower Semantics (Meta-Level Programming)

OmniLisp supports **tower of interpreters** semantics based on the "Collapsing Towers of Interpreters" paper. This enables multi-stage programming where code can be generated, inspected, and executed at different meta-levels.

### 19.1 Core Concepts

The tower consists of multiple levels:
- **Level 0**: Base runtime (C code execution)
- **Level 1**: Interpreted evaluation (default)
- **Level N**: Higher meta-levels for code generation

Each level has its own **handlers** that define evaluation semantics.

### 19.2 Tower Forms

| Form | Description |
|------|-------------|
| `(lift expr)` | Lift value to code representation (go up one level) |
| `(run code)` | Execute code at base level (go down to level 0) |
| `(EM expr)` | Escape to Meta - evaluate at parent level |
| `(shift n expr)` | Go up n levels and evaluate |
| `(meta-level)` | Get current tower level as integer |

### 19.3 Lifting and Running

```lisp
;; Lift a value to its code representation
(lift 42)           ; -> Code representing mk_int(42)
(lift (+ 1 2))      ; -> Evaluates to 3, then lifts to mk_int(3)

;; Run code at base level
(run (lift 42))     ; -> Execute the C code, get 42 back

;; Multi-stage: generate optimized code
(define (power-gen n)
  (if (= n 0)
      (lift 1)
      `(* x ~(power-gen (- n 1)))))

(power-gen 3)       ; -> (* x (* x (* x 1)))
```

### 19.4 Handler System

Handlers define the semantics of each evaluation phase. OmniLisp uses `[]` brackets for handler specifications:

| Handler | Purpose |
|---------|---------|
| `h-lit` | Literal handling |
| `h-var` | Variable lookup |
| `h-lam` | Lambda creation |
| `h-app` | Function application |
| `h-if` | Conditional evaluation |
| `h-lft` | Lift operation |
| `h-run` | Run operation |
| `h-em` | Escape to meta |
| `h-clam` | Compile lambda |

### 19.5 Handler Manipulation

```lisp
;; Get a handler
(get-meta 'h-app)            ; -> current app handler

;; Set a handler (returns new menv)
(set-meta! 'h-app my-handler)

;; Scoped handler changes
(with-handlers [[h-lit custom-lit]
                [h-app custom-app]]
  body ...)
```

### 19.6 Custom Evaluation Semantics

Define custom handlers to modify evaluation:

```lisp
;; Tracing evaluator - log every application
(define (tracing-app expr menv)
  (let [fn (car expr)
        args (cdr expr)]
    (print "Calling: " fn " with " args)
    (default-handler 'h-app expr)))  ; Delegate to default

(with-handlers [[h-app tracing-app]]
  (+ 1 2 3))
;; Prints: Calling: + with (1 2 3)
;; Returns: 6
```

### 19.7 Code Generation with Tower

The tower enables **staged compilation**:

```lisp
;; Stage 0: Define a code generator
(define (gen-loop n body-gen)
  #'(let loop [i 0]
      (if (< i ~(lift n))
          (begin
            ~(body-gen 'i)
            (loop (+ i 1)))
          nothing)))

;; Stage 1: Generate specialized code
(gen-loop 10 (lambda (i) #'(print ~i)))
;; -> Expands to optimized loop code

;; Stage 2: Execute
(run (gen-loop 10 (lambda (i) #'(print ~i))))
```

### 19.8 Escape to Meta (EM)

`EM` evaluates an expression at the parent meta-level:

```lisp
;; Inside a meta-level computation
(EM (+ 1 2))        ; Evaluate (+ 1 2) at parent level

;; Useful for accessing outer bindings
(let [x 10]
  (with-handlers [[h-var custom-var]]
    (EM x)))        ; Access x using parent's h-var, not custom-var
```

### 19.9 Compile Lambda (clambda)

`clambda` compiles a lambda under current handler semantics:

```lisp
(clambda (x) (* x x))   ; Compile with current handlers

;; In a lifted context, produces code for a function
(with-handlers [[h-lit lift-handler]
                [h-app gen-app]]
  (clambda (x) (* x x)))
;; -> Generates C code for the function
```

### 19.10 Integration with Macros

Tower semantics work with hygienic macros:

```lisp
(define [macro staged-power] (n)
  (if (= n 0)
      #'1
      #'(* x ~(staged-power (- n 1)))))

;; At compile time, generates specialized multiplication chain
(define (power5 x)
  (staged-power 5))
;; Expands to: (* x (* x (* x (* x (* x 1)))))
```

---

## 20. Implementation Status (C Compiler)

### Implemented
- Parser: integers, symbols, lists `(...)`, arrays `[...]` (used for `let` bindings), quote `'`
- Special forms: `define`, `lambda` / `fn`, `let`, `let*`, `if`, `do` / `begin`
- Primitives: `+`, `-`, `*`, `/`, `%`, `<`, `>`, `<=`, `>=`, `=`, `cons`, `car`, `cdr`, `null?`
- I/O: `display`, `print`, `newline`
- ASAP analysis pass integrated into codegen for compile-time `free_obj` insertion

### Not Yet Wired in the C Compiler
- Strings, floats, chars, dicts, tuples, type literals
- Pattern matching, macros, modules, multiple dispatch
- Concurrency/FFI surface syntax and tower semantics
