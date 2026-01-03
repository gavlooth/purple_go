# Omnilisp Exhaustive Syntax Guide

This document provides a comprehensive set of examples for all Omnilisp features.

---

## Implemented Syntax (C Compiler)

The current C compiler implements a **small core** of the language. Everything
below this section is the full design spec and may not be wired yet.

### Literals
```lisp
42               ; Integer
foo              ; Symbol
()               ; Empty list
```

### Lists, Quote, and Function Calls
```lisp
(+ 1 2)           ; Function application (prefix)
(cons 1 (cons 2 ()))  ; List construction
'x               ; Quote -> (quote x)
```

### Special Forms
```lisp
(define answer 42)
(define (square x) (* x x))

(lambda (x) (* x x))
(fn (x) (* x x))        ; Alias for lambda

(let ((x 1) (y 2)) (+ x y))   ; List-style bindings
(let [x 1 y 2] (+ x y))       ; Array-style bindings

(if (< 1 2) 10 20)

(do
  (print "hi")
  42)                       ; Returns last expression

(begin 1 2 3)               ; Alias for do
```

### Truthiness
```lisp
; empty list and numeric zero are falsey, everything else is truthy
(if 0 1 2)   ; -> 2
(if () 1 2)  ; -> 2
```

### Built-ins Wired Today
```lisp
; Arithmetic: + - * / %
; Comparison: < > <= >= =
; Lists: cons car cdr null?
; I/O: display print newline
```

### Comments
```lisp
; This is a comment to end-of-line
```

## Planned Syntax (Design Target)

All sections below describe the intended Omnilisp language design and are not yet implemented in the C compiler unless explicitly noted.

## Bracket Semantics

| Bracket | Context | Meaning | Example |
|---------|---------|---------|---------|
| `()` | General | Form / function call | `(+ 1 2)` |
| `()` | In pattern | Constructor pattern | `(Some v)` |
| `[]` | Expression | Array literal | `[1 2 3]` |
| `[]` | In `let` | Binding pairs/triplets | `[x 1]`, `[x {Int} 1]` |
| `[]` | In `match`/`cond` | Branch `[pattern result]` | `[[] "empty"]` |
| `[]` | In pattern | Array pattern | `[a b]`, `[h .. t]` |
| `[]` | In params | Param spec | `[x {Int}]`, `[x 10]`, `[x {Int} 10]` |
| `[]` | In struct | Field definition | `[name {Type}]` |
| `{}` | In `define` | Type-level form | `{struct ...}` |
| `{}` | Type annotation | Type wrapper | `{Int}`, `{Array Int}` |
| `#{}` | Expression | Dict literal | `#{:a 1}` |

---

## 1. Literals & Values

### 1.1 Scalars
```lisp
;; Numbers
42              ; Int
3.14            ; Float
1_000_000       ; With underscores
0xDEADBEEF      ; Hex
0b101010        ; Binary

;; Strings & Characters
"Standard string"
"String with $interpolation and \n escapes"
#\a             ; Character literal
#\newline       ; Special character

;; Symbols & Booleans
'a-symbol       ; Quoted symbol
:shorthand      ; Reader sugar for 'shorthand
true
false
nothing         ; Unit value (void/absence)
```

### 1.2 Collections
```lisp
;; Arrays (Mutable)
[1 2 3 4]
[:a "b" 10.5]
[[1 2] [3 4]]   ; Nested
[]              ; Empty array (NOT nothing)

;; Lists (Persistent)
(list 1 2 3)
(cons 1 (list 2 3))
(list)          ; Empty list (NOT nothing)

;; Tuples (Immutable fixed-width)
(tuple 1 2 "three")
(tuple :ok (tuple 10 20))

;; Named Tuples (Immutable records)
(named-tuple [x 1] [y 2])

;; Dictionaries (Key-Value)
#{:name "Alice" :age 30}
#{}             ; Empty dict (NOT nothing)
```

### 1.3 Nothing vs Empty
```lisp
;; nothing is the unit/void value
(print "hello")     ; -> nothing
(set! x 10)         ; -> nothing

;; Empty collections are distinct values
(nothing? nothing)  ; -> true
(nothing? [])       ; -> false
(nothing? (list))   ; -> false

(empty? [])         ; -> true
(empty? (list))     ; -> true
(null? (list))      ; -> true (list-specific)
```

### 1.4 Parametric Types
```lisp
{Array Int}
{Dict Symbol String}
{Option Int}
{Result String Error}
{Array Float 2}     ; 2D Array
```

---

## 2. Bindings

### 2.1 Define (Top-Level)
```lisp
(define x 10)
(define msg "Hello")
(define items [1 2 3])

;; With metadata
(define ^:private ^:hot version 1.0)
```

### 2.2 Let (Local Bindings)
Bindings are in `[]` as an even number of forms (name-value pairs):

```lisp
;; Basic let (parallel binding)
(let [x 1
      y 2]
  (+ x y))  ; -> 3

;; Sequential let (each sees previous)
(let :seq [x 1
           y (+ x 1)
           z (+ y 1)]
  z)  ; -> 3

;; Recursive let (for mutual recursion)
(let :rec [even? (lambda (n) (if (= n 0) true (odd? (- n 1))))
           odd?  (lambda (n) (if (= n 0) false (even? (- n 1))))]
  (even? 10))  ; -> true

;; Named let (loop form)
(let loop [i 0
           acc 0]
  (if (> i 10)
      acc
      (loop (+ i 1) (+ acc i))))  ; -> 55
```

### 2.3 Destructuring
```lisp
;; In define
(define [a b c] [1 2 3])
(define [first .. rest] [1 2 3 4])

;; In let
(let [[x y] [10 20]]
  (+ x y))  ; -> 30

(let :seq [[a .. bs] [1 2 3 4]
           sum (reduce + 0 bs)]
  sum)  ; -> 9
```

---

## 3. Functions

### 3.1 Basic Functions
```lisp
(define (add x y)
  (+ x y))

;; With type annotations (types wrapped in {})
(define (add [x {Int}] [y {Int}]) {Int}
  (+ x y))
```

### 3.1.1 Lambda Shorthand (Planned)
```lisp
(-> x (+ x 1))         ; Single-arg lambda
(-> (x y) (* x y))     ; Multi-arg lambda
```

### 3.2 Multi-Arity (via Overloads)
```lisp
;; Multi-arity handled via multiple dispatch
(define (greet)
  (greet "Guest"))

(define (greet name)
  (print "Hello, $name"))

(define (greet name title)
  (print "Hello, $title $name"))
```

### 3.3 Default Parameters
```lisp
(define (greet [name "Guest"])
  (print "Hello, $name"))

(define (connect [host {String} "localhost"] [port {Int} 8080])
  ...)
```

### 3.4 Variadic Functions
```lisp
(define (sum .. nums)
  (reduce + 0 nums))

(define (log level .. args)
  (print "[$level] " (join " " args)))
```

### 3.5 Named Arguments
```lisp
;; Definition with named params after &
(define (draw shape & [color :black] [stroke Int 1])
  ...)

;; Call with & separator
(draw circle & :color :red :stroke 2)

;; Also accepts bracket pairs
(draw circle & [:color :red] [:stroke 2])

;; Using defaults
(draw circle)  ; color=:black, stroke=1
```

### 3.6 Partial Application
```lisp
(define plus-5 (partial add 5))
(plus-5 10)  ; -> 15

(map (partial * 2) [1 2 3])  ; -> [2 4 6]
```

---

## 4. Types

### 4.1 Abstract Types
```lisp
(define {abstract Shape} Any)
(define {abstract Polygon} Shape)
(define {abstract Animal} Any)
```

### 4.2 Structs (Immutable)
```lisp
(define {struct Point}
  [x {Float}]
  [y {Float}])

(define p (Point 10.0 20.0))
p.x  ; -> 10.0
```

### 4.3 Mutable Structs
```lisp
(define {struct Player}
  [hp :mutable {Int}]
  [name {String}])

;; Sugar: (define {mutable Player} ...) marks all fields mutable.

(define hero (Player 100 "Arthur"))
(set! hero.hp 95)
```

### 4.4 Parametric Structs
```lisp
(define {struct [Pair T]}
  [first {T}]
  [second {T}])

(define {struct [Entry K V]}
  [key {K}]
  [value {V}])
```

---

## 5. Sum Types (Enums)

### 5.1 Simple Enums
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

### 5.2 Enums with Data
```lisp
(define {enum Option T}
  (Some [value {T}])
  None)

(define {enum Result T E}
  (Ok [value {T}])
  (Err [error {E}]))
```

### 5.3 Using Option/Result
```lisp
(define (find-user id)
  (if (exists? id)
      (Some (get-user id))
      None))

(define (unwrap-or opt default)
  (match opt
    [(Some v) v]
    [None default]))

(define (map-result f result)
  (match result
    [(Ok v) (Ok (f v))]
    [(Err e) (Err e)]))
```

### 5.4 Recursive Enums
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

## 6. Multiple Dispatch

### 6.1 Generic Functions
```lisp
(define (area [s Shape])
  (error "No area for $(type-of s)"))

(define [method area Circle] (c)
  (* 3.14159 c.radius c.radius))

(define [method area Rect] (r)
  (* r.width r.height))
```

### 6.2 Multi-Argument Dispatch
```lisp
(define [method collide Circle Rect] (c r)
  (print "Circle hit Rect"))

(define [method collide Rect Circle] (r c)
  (collide c r))  ; Delegate
```

### 6.3 Interfaces (Protocols)
```lisp
(define {interface Drawable}
  (draw [self {Self}] [canvas {Canvas}])
  (bounds [self {Self}]))

(define [method draw Circle] (self canvas)
  ...)

(define [method bounds Circle] (self)
  (Rect (* 2 self.radius) (* 2 self.radius)))
```

---

## 7. Pattern Matching

`match` is a **special form**. Branches use `[]` for grouping: `[pattern result]`.

### 7.1 Basic Patterns
```lisp
(match x
  [1 "one"]
  ["hello" "greeting"]
  [:ok "symbol ok"]
  [_ "anything"])
```

### 7.2 Logic Patterns
```lisp
(match x
  [(or 1 2 3) "small"]
  [(and n (satisfies even?)) "even number"]
  [(not 0) "non-zero"])
```

### 7.3 Array Patterns
```lisp
(match arr
  [[] "empty"]
  [[x] "singleton"]
  [[a b] "pair"]
  [[first .. rest] "has elements"]
  [else "default"])

;; Even [[] []] is unambiguous:
(match arr
  [[] []])    ; Pattern: [], Result: []
```

### 7.4 List Patterns
```lisp
(match lst
  [(list) "empty list"]
  [(cons h t) "has head"]
  [(list a b c) "exactly three"])
```

### 7.5 Struct & Enum Patterns
```lisp
(match shape
  [(Circle r) (* 3.14 r r)]
  [(Rect w h) (* w h)])

(match result
  [(Ok value) value]
  [(Err msg) (error msg)])
```

### 7.6 Guards
Guards extend the branch to 4 elements: `[pattern :when guard result]`

```lisp
(match n
  [x :when (> x 100) "big"]
  [x :when (> x 10) "medium"]
  [_ "small"])
```

### 7.7 Alias Pattern
```lisp
(match point
  [(as (Point x y) p) :when (= x y)
   (print "diagonal: " p)])
```

---

## 8. Arrays & Sequences

### 8.1 Array Basics
```lisp
[1 2 3 4]
(array 1 2 3)

arr.(0)           ; Access
(set! arr.(0) 99) ; Mutation
(length arr)      ; Length
```

### 8.2 Functional Operations (Return New Array)
```lisp
(map f arr)       ; -> new array
(filter pred arr) ; -> new array
(reverse arr)     ; -> new array
(sort arr)        ; -> new array
(concat a b)      ; -> new array
```

### 8.3 Mutating Operations (In-Place)
```lisp
(map! f arr)      ; Mutates arr, returns nothing
(filter! pred arr)
(reverse! arr)
(sort! arr)
(push! arr elem)  ; Append element
(pop! arr)        ; Remove and return last
```

### 8.4 Aliasing & Copying
```lisp
(define a [1 2 3])
(define b a)          ; Alias (same array)
(set! a.(0) 99)
b.(0)                 ; -> 99

(define c (copy a))   ; Independent copy
(set! a.(0) 0)
c.(0)                 ; -> 99 (unchanged)
```

### 8.5 Slicing
```lisp
arr.[0 2 4]       ; Select indices -> [arr.(0) arr.(2) arr.(4)]
arr.[1:4]         ; Slice range -> new array
arr.[::2]         ; Every 2nd element
```

### 8.6 Lists
```lisp
(list 1 2 3)
(cons 1 (list 2 3))
(car lst)         ; Head
(cdr lst)         ; Tail
(null? lst)       ; Empty check
```

### 8.7 Iteration
```lisp
(foreach [x arr]
  (print x))

(for [x (range 1 10)]
  (* x x))

(for [x items :when (even? x)]
  x)

(collect (range 1 5))     ; -> [1 2 3 4]
(iter->list (range 1 5))  ; -> (list 1 2 3 4)
(iter->array some-list)   ; -> array
```

---

## 9. Access Syntax

### 9.1 Dot Notation
```lisp
obj.field         ; Symbol field access
obj.(expr)        ; Dynamic access (computed key/index)
obj.[indices]     ; Multi-access / slice
```

### 9.2 Functional Accessors
```lisp
.name             ; -> (lambda (it) it.name)
.pos.x            ; -> (lambda (it) it.pos.x)

(map .name users)
(filter .active? items)
(sort-by .age users)
```

### 9.3 Method Calls (UFCS)
```lisp
;; Uniform Function Call Syntax
(obj.draw ctx)         ; -> (draw obj ctx)
(arr.map f)            ; -> (map arr f)
(str.split ",")        ; -> (split str ",")
```

### 9.4 Mutation
```lisp
(set! obj.field value)
(set! arr.(index) value)
```

---

## 10. Control Flow

### 10.1 Conditionals
```lisp
(if condition then-expr else-expr)

;; cond is a macro expanding to match
(cond
  [(< x 0) "negative"]
  [(> x 0) "positive"]
  [else "zero"])

;; Expands to:
(match true
  [_ :when (< x 0) "negative"]
  [_ :when (> x 0) "positive"]
  [_ "zero"])

(when condition
  (side-effect))

(unless condition
  (fallback))
```

### 10.2 Looping
```lisp
;; Named let recursion
(let loop [i 0]
  (when (< i 10)
    (print i)
    (loop (+ i 1))))

;; Foreach
(foreach [item items]
  (process item))

;; For comprehension
(for [x (range 1 10)
      y (range 1 10)
      :when (< x y)]
  (tuple x y))
```

### 10.3 Piping
```lisp
(|> value
    (f arg)           ; -> (f value arg)
    (g arg))          ; -> (g result arg)

;; With placeholder
(|> 10
    (- 100 %))        ; -> (- 100 10) -> 90

;; With accessors
(|> users
    (filter .active?)
    (map .name)
    (sort))
```

---

## 11. Concurrency

### 11.1 Green Threads
```lisp
(define p (spawn (lambda () (compute))))
```

### 11.2 Channels
```lisp
(define ch (channel))     ; Unbuffered
(define ch (channel 10))  ; Buffered

(send ch value)           ; Blocks if full
(recv ch)                 ; Blocks if empty
(close ch)
```

### 11.3 Coordination
```lisp
(yield)                   ; Cooperative yield
(park)                    ; Suspend
(unpark process value)    ; Resume with value
```

---

## 12. Macros

### 12.1 Definition
```lisp
(define [macro unless] (condition body)
  #'(if (not ~condition) ~body nothing))

(define [macro with-timing] (name . body)
  #'(let [start (now)]
      (let [result (begin ~@body)]
        (print ~name " took " (- (now) start) "ms")
        result)))
```

### 12.2 Syntax API
```lisp
#'(...)           ; Syntax quote
~expr             ; Unquote
~@expr            ; Splicing unquote
(gensym)          ; Fresh symbol
(syntax->datum s) ; Extract value
(datum->syntax ctx d) ; Wrap in context
```

---

## 13. Modules

### 13.1 Definition
```lisp
(module MyModule
  (export func1 func2 MyType)

  (define (func1 ...) ...)
  (define (func2 ...) ...)
  (define {struct MyType} ...))
```

### 13.2 Import
```lisp
(import MyModule)                     ; All exports
(import [MyModule :only (func1)])     ; Specific
(import [MyModule :as M])             ; Qualified
(import [MyModule :refer (func1) :as M])
```

---

## 14. Strings

### 14.1 Interpolation
```lisp
"Hello, $name"
"Sum: $(+ x y)"
"Literal: \$100"
```

### 14.2 Prefixes
```lisp
r"regex\d+"           ; Regex
raw"C:\path\file"     ; Raw (no escapes)
b"binary\x00data"     ; Byte array
```

---

## 15. FFI

```lisp
(@ffi "printf" "Value: %d\n" 42)
(@ffi "malloc" size)
(@ffi "free" ptr)
```

---

## 16. Metadata

```lisp
^:private            ; Keyword metadata
^:hot                ; Compiler hint
^"Docstring"         ; Documentation

(define ^:private ^"Internal helper"
  (helper x) ...)
```
