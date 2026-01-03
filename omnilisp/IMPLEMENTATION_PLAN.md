# OmniLisp Implementation Plan

## Overview

This document details the complete implementation plan for replacing Purple with OmniLisp.
The C runtime remains unchanged. The Go compiler frontend is rewritten 100%.

---

## Type System: Julia-Style Optional Types

**OmniLisp uses Julia's approach to types: types are OPTIONAL everywhere.**

### Type Annotations Are Always Optional

```lisp
;; No types - fully inferred (like Julia's duck typing)
(define (add x y)
  (+ x y))

;; Partial types - annotate what you want
(define (add [x {Int}] y)
  (+ x y))

;; Full types - for documentation or dispatch
(define (add [x {Int}] [y {Int}]) {Int}
  (+ x y))
```

### Key Principles (from Julia)

1. **Types don't affect runtime behavior** - they guide dispatch and documentation
2. **Multiple dispatch on all arguments** - not just first argument
3. **Parametric types** - `{Array T}`, `{Option T}`, `{Result T E}`
4. **Abstract type hierarchy** - for organizing dispatch
5. **No class-based OOP** - structs + multiple dispatch

### Syntax Forms

| Form | Meaning |
|------|---------|
| `x` | Untyped parameter |
| `[x {Int}]` | Typed parameter |
| `[x 10]` | Parameter with default |
| `[x {Int} 10]` | Typed parameter with default |
| `{Int}` after params | Return type annotation |

---

## Architecture: What Changes

| Component | Change Level | Description |
|-----------|--------------|-------------|
| **src/runtime/reader/pika.c** | New Implementation | Pika Parser: New bracket semantics, dot notation, string interpolation (C Implementation) |
| **pkg/ast/value.go** | 80% Rewrite | New types: Array, Dict, Tuple, Struct, Enum, TypeAnnotation |
| **pkg/eval/eval.go** | 95% Rewrite | New special forms, multiple dispatch, match semantics |
| **pkg/eval/primitives.go** | 60% Rewrite | New primitive operations |
| **pkg/codegen/codegen.go** | 50% Modify | Code generation for new constructs |
| **pkg/analysis/*.go** | 30% Modify | Adapt analysis for new AST |
| **runtime/** | 0% | Unchanged - C runtime stays exactly the same |

---

## Phase 1: Parser Implementation (src/runtime/reader/pika.c)

We will implement the Pika parser directly in C to support the "Tower of Interpreters" model (Level 0 parsing).

### 1.1 C Tokenizer

```c
typedef enum {
    TOK_LPAREN,   // (
    TOK_RPAREN,   // )
    TOK_LBRACKET, // [
    TOK_RBRACKET, // ]
    TOK_LBRACE,   // {
    TOK_RBRACE,   // }
    TOK_HASHBRACE,// #{
    TOK_DOT,      // .
    TOK_STRING,   // "..."
    TOK_INT,      // 123
    TOK_FLOAT,    // 123.456
    TOK_SYMBOL,   // name
    TOK_KEYWORD,  // :key
    TOK_EOF
} TokenType;

// Tokenizer state
typedef struct {
    const char* input;
    size_t pos;
    size_t len;
} Lexer;
```

### 1.2 Parser Changes (C)

The parser will construct `OmniValue` objects directly using the runtime's object system.

| Feature | Description |
|---------|-------------|
| `parse_list` | `()` -> List or Function Call |
| `parse_array` | `[]` -> Array Object |
| `parse_type` | `{}` -> Type Object |
| `parse_dict` | `#{}` -> Dictionary Object |
| `parse_dot` | `obj.field` -> Accessor form |

### 1.3 Dot Notation Parsing (C)

```c
// Logic to transform tokens into 'get' forms:
// obj.field     -> (get obj 'field)
// obj.(expr)    -> (get obj expr)
```

### 1.4 String Interpolation (C)

The C parser must handle `$` inside strings and generate `(string-concat ...)` forms.

### 1.5 Files to Create/Modify

```
src/runtime/reader/pika.c       # Core parser logic
src/runtime/reader/pika.h       # Public API
src/runtime/reader/token.h      # Token definitions
src/runtime/include/omnilisp.h  # Expose omni_read()
```

---

## Phase 2: AST Rewrite (pkg/ast/value.go)

### 2.1 New Value Tags

```go
const (
    // Existing
    TInt Tag = iota
    TSym
    TCell
    TNil
    TPrim
    TMenv
    TCode
    TLambda
    TRecLambda
    TError
    TChar
    TFloat
    TBox
    TCont
    TChan
    TGreenChan
    TAtom
    TThread
    TProcess
    TUserType

    // New for OmniLisp
    TArray       // Mutable array [1 2 3]
    TDict        // Dictionary #{:a 1}
    TTuple       // Immutable tuple (tuple 1 2 3)
    TNamedTuple  // Named tuple (named-tuple [x 1] [y 2])
    TNothing     // Unit value (distinct from nil)
    TTypeAnnot   // Type annotation {Int}
    TStruct      // Struct instance
    TEnum        // Enum variant
    TInterface   // Interface/protocol
    TMethod      // Method (dispatch table entry)
    TGeneric     // Generic function
)
```

### 2.2 Value Struct Extensions

```go
type Value struct {
    // ... existing fields ...

    // TArray
    ArrayData []*Value

    // TDict
    DictData map[*Value]*Value  // Key-value pairs

    // TTuple, TNamedTuple
    TupleData   []*Value
    TupleNames  []string  // For named tuples

    // TStruct, TEnum
    TypeName    string
    TypeFields  map[string]*Value
    EnumTag     string  // For enum variants

    // TTypeAnnot
    TypeParams  []*Value  // For parametric types {Array Int}

    // TGeneric, TMethod
    MethodTable map[string]*Value  // Type signature -> method
}
```

### 2.3 Files to Modify

```
pkg/ast/value.go      # Add new tags and constructors
pkg/ast/types.go      # New file for type system structures
```

---

## Phase 3: Evaluator Rewrite (pkg/eval/eval.go)

### 3.1 New Special Forms

| Form | Description |
|------|-------------|
| `define` | Unified binding (values, functions, types, macros) |
| `let` | With `:seq`, `:rec`, named-let modifiers |
| `match` | Pattern matching (special form, not macro) |
| `if` | Binary conditional |
| `lambda` | Anonymous function |
| `set!` | Mutation |
| `begin` | Sequencing |
| `quote` | Quoting |
| `prompt` / `control` | Delimited continuations |
| `spawn` | Green thread |
| `channel` | Create channel |

### 3.2 Match as Special Form

```go
// (match value
//   [pattern1 result1]
//   [pattern2 :when guard result2]
//   [else default])

func evalMatch(expr, menv *Value) *Value {
    value := Eval(expr.Cdr.Car, menv)  // Evaluate scrutinee
    branches := expr.Cdr.Cdr           // Branch list

    for branch := branches; !IsNil(branch); branch = branch.Cdr {
        // branch.Car is [pattern result] or [pattern :when guard result]
        branchData := branch.Car  // Array value
        pattern := branchData.ArrayData[0]

        if len(branchData.ArrayData) == 4 {
            // Has guard: [pattern :when guard result]
            guard := branchData.ArrayData[2]
            result := branchData.ArrayData[3]
            // ...
        } else {
            // Simple: [pattern result]
            result := branchData.ArrayData[1]
            // ...
        }
    }
}
```

### 3.3 Pattern Matching Implementation

```go
// Pattern types to support
type PatternKind int
const (
    PatWildcard  // _
    PatVariable  // x (binds value)
    PatLiteral   // 1, "str", :sym
    PatArray     // [p1 p2 ...]
    PatRest      // [head .. tail]
    PatList      // (list p1 p2)
    PatCons      // (cons h t)
    PatTuple     // (tuple p1 p2)
    PatStruct    // (TypeName p1 p2)
    PatOr        // (or p1 p2)
    PatAnd       // (and p1 p2)
    PatNot       // (not p)
    PatSatisfies // (satisfies pred)
    PatAs        // (as pattern name)
)

func matchPattern(pattern, value *Value, bindings map[string]*Value) bool {
    // ...
}
```

### 3.4 Multiple Dispatch

```go
// Generic function registry
type GenericFn struct {
    Name    string
    Methods []*Method
}

type Method struct {
    TypeSig   []string  // Parameter types
    Impl      *Value    // Lambda implementation
}

// Dispatch algorithm
func dispatchGeneric(gf *GenericFn, args []*Value) *Value {
    // 1. Get runtime types of all arguments
    types := make([]string, len(args))
    for i, arg := range args {
        types[i] = typeOf(arg)
    }

    // 2. Find applicable methods
    applicable := findApplicable(gf.Methods, types)

    // 3. Sort by specificity
    sorted := sortBySpecificity(applicable)

    // 4. Call most specific
    return callMethod(sorted[0], args)
}
```

### 3.5 Type System

```go
// Type hierarchy
type TypeHierarchy struct {
    Abstracts map[string]*AbstractType
    Concretes map[string]*ConcreteType
    Parametrics map[string]*ParametricType
}

type AbstractType struct {
    Name   string
    Parent string  // Parent abstract type
}

type ConcreteType struct {
    Name   string
    Parent string  // Parent abstract or concrete
    Fields []TypeField
    Mutable bool
}

// Check subtype relationship
func isSubtype(child, parent string) bool {
    // ...
}
```

### 3.6 Files to Modify

```
pkg/eval/eval.go        # Major rewrite
pkg/eval/pattern.go     # Pattern matching (expand)
pkg/eval/dispatch.go    # New file for multiple dispatch
pkg/eval/types.go       # New file for type system
pkg/eval/primitives.go  # Update primitives
```

---

## Phase 4: Define Form Implementation

### 4.1 Unified Define Syntax

```go
// (define x 10)                           -> value
// (define (f x y) body)                   -> function
// (define (f [x {Int}] [y {Int}]) {Int} body) -> typed function
// (define {abstract Animal} Any)          -> abstract type
// (define {struct Point Any} [x {Float}] [y {Float}]) -> struct
// (define {enum Option T} (Some [v {T}]) None) -> enum
// (define {interface Drawable} ...)       -> interface
// (define [method area Circle] (c) ...)   -> method
// (define [macro unless] (cond body) ...) -> macro

func evalDefine(args, menv *Value) *Value {
    first := args.Car

    switch {
    case IsSym(first):
        // (define name value)
        return defineValue(first, args.Cdr.Car, menv)

    case IsCell(first):
        // (define (name params...) body)
        return defineFunction(first, args.Cdr, menv)

    case IsTypeLit(first):
        // (define {abstract/struct/enum/interface ...} ...)
        return defineType(first, args.Cdr, menv)

    case IsArray(first) && first.ArrayData[0].Str == "method":
        // (define [method name Type] ...)
        return defineMethod(first, args.Cdr, menv)

    case IsArray(first) && first.ArrayData[0].Str == "macro":
        // (define [macro name] ...)
        return defineMacro(first, args.Cdr, menv)
    }
}
```

---

## Phase 5: Let Form Implementation

### 5.1 Let Modifiers

```go
// (let [x 1 y 2] body)           -> parallel binding
// (let :seq [x 1 y (+ x 1)] body) -> sequential
// (let :rec [even? ... odd? ...] body) -> recursive
// (let loop [i 0 acc 0] body)    -> named let (loop form)
// (let :seq loop [i 0] body)     -> combined

func evalLet(args, menv *Value) *Value {
    // Parse modifiers
    modifiers := parseLetModifiers(args)
    bindings := modifiers.Bindings  // Array value
    body := modifiers.Body
    name := modifiers.Name  // For named let

    switch modifiers.Mode {
    case LetParallel:
        return evalLetParallel(bindings, body, menv)
    case LetSequential:
        return evalLetSeq(bindings, body, menv)
    case LetRecursive:
        return evalLetRec(bindings, body, menv)
    }
}
```

---

## Phase 6: Hygienic Macros

### 6.1 Syntax Objects

```go
// Syntax object wraps datum with lexical context
type SyntaxObject struct {
    Datum   *Value
    Context *MacroContext
}

type MacroContext struct {
    DefinitionEnv *Value  // Where macro was defined
    UseEnv        *Value  // Where macro is used
    Marks         []int   // Hygiene marks
}
```

### 6.2 Macro Expansion

```go
// (define [macro unless] (cond body)
//   #'(if (not ~cond) ~body nothing))

// #' = syntax quote (preserves lexical context)
// ~  = unquote (evaluate and insert)
// ~@ = unquote-splicing

func expandMacro(macro *Macro, args []*Value, useMenv *Value) *Value {
    // 1. Bind parameters to arguments (as syntax objects)
    // 2. Evaluate transformer body in macro's definition env
    // 3. Return expanded syntax
}
```

---

## Phase 7: Module System

### 7.1 Module Structure

```go
type Module struct {
    Name    string
    Exports []string
    Bindings map[string]*Value
    Imports  []*Import
}

type Import struct {
    Module string
    Only   []string  // :only
    Except []string  // :except
    As     string    // :as (prefix)
    Refer  []string  // :refer (bring into scope)
}
```

### 7.2 Module Forms

```lisp
(module MyModule
  (export f1 f2 MyType)

  (import [OtherModule :only (helper)])

  (define (f1 x) ...)
  (define (f2 y) ...))

(import MyModule)
(import [MyModule :as M])
(import [MyModule :only (f1)])
```

---

## Phase 8: Concurrency

### 8.1 Green Threads

```go
// Existing scheduler can be reused
// Just update API to match OmniLisp

// (spawn thunk)    -> process
// (yield)          -> cooperative yield
// (park)           -> suspend
// (unpark p value) -> resume
```

### 8.2 Channels

```go
// (channel)    -> unbuffered channel
// (channel n)  -> buffered channel
// (send ch v)  -> send (blocks if full)
// (recv ch)    -> receive (blocks if empty)
// (close ch)   -> close channel
```

---

## Phase 9: Code Generation Updates

### 9.1 Array Code Gen

```c
// [1 2 3] becomes:
OmniArray* arr = omni_array_new(3);
omni_array_set(arr, 0, mk_int(1));
omni_array_set(arr, 1, mk_int(2));
omni_array_set(arr, 2, mk_int(3));
```

### 9.2 Struct Code Gen

```c
// (Point 10.0 20.0) becomes:
Obj* point = mk_struct("Point", 2);
struct_set_field(point, 0, mk_float(10.0));
struct_set_field(point, 1, mk_float(20.0));
```

### 9.3 Match Code Gen

```c
// Pattern matching becomes switch/if chains
switch (get_type(value)) {
    case TYPE_INT:
        if (get_int(value) == 0) { ... }
        break;
    case TYPE_STRUCT:
        if (strcmp(get_struct_name(value), "Point") == 0) {
            Obj* x = struct_get_field(value, 0);
            Obj* y = struct_get_field(value, 1);
            ...
        }
        break;
}
```

---

## Implementation Order

### Phase 1: Parser Foundation (C) ✅
- [ ] Implement tokenizer in C (`src/runtime/reader/`)
- [ ] Implement array literal parsing `[]`
- [ ] Implement type literal parsing `{}`
- [ ] Implement dict literal parsing `#{}`
- [ ] Implement dot notation `obj.field`
- [ ] Implement string interpolation
- [ ] Bind `omni_read` to runtime

### Phase 2: AST Extensions ✅ COMPLETE
- [x] Add new Value tags (TArray, TDict, TTuple, etc.)
- [x] Implement constructors and predicates
- [x] Add type annotation structures
- [x] AST tests

### Phase 3: Evaluator Primitives ✅ COMPLETE
- [x] Array operations (make-array, array-ref, etc.)
- [x] Dict operations (make-dict, dict-ref, etc.)
- [x] Tuple operations
- [x] Keyword operations
- [x] Nothing operations
- [x] Generic get for dot notation

### Phase 4: Pattern Matching ✅ COMPLETE
- [x] Array patterns `[a b .. rest]`
- [x] Dict patterns `#{:key pat}`
- [x] Predicate patterns `(? pred)`
- [x] Guards `:when`
- [x] OmniLisp branch syntax `[pattern result]`

### Phase 5: Multiple Dispatch ✅ COMPLETE
- [x] Type registry (abstract, concrete, parametric)
- [x] Subtype checking
- [x] Multiple dispatch implementation
- [x] Method definition and lookup
- [x] `(define (f [x {Type}]) ...)` syntax

### Phase 6: Module System ✅ COMPLETE
- [x] `(module Name (export ...) body)`
- [x] `(import Module)`
- [x] `:as`, `:only`, `:except`, `:refer` modifiers
- [x] Qualified name lookup

### Phase 7: Hygienic Macros ✅ COMPLETE
- [x] `(define [macro name] (params) body)`
- [x] Syntax objects with lexical context
- [x] `#'` / `syntax-quote`
- [x] `~` / `unquote`
- [x] `~@` / `unquote-splicing`

### Phase 8: Let Modifiers ✅ COMPLETE
- [x] Array-style bindings `[x 1 y 2]`
- [x] `:seq` modifier (sequential binding)
- [x] `:rec` modifier (recursive binding)
- [x] Named let `(let loop [i 0] ...)`

### Phase 9: Code Generation ✅ COMPLETE
- [x] Update codegen for new AST
- [x] Array/Dict/Tuple code generation
- [x] Keyword/Nothing code generation
- [x] Type literal code generation

### Phase 10: Tower Semantics ✅ COMPLETE (inherited from Purple)
- [x] `lift` - value to code
- [x] `run` - execute code at base
- [x] `EM` - escape to meta
- [x] `shift` - go up n levels
- [x] Handler system (get-meta, set-meta!, with-handlers)

### Pending
- [ ] Struct/Enum code generation
- [ ] Error messages polish
- [ ] Documentation
- [ ] Performance tuning
- [ ] Full test suite

---

## File Change Summary

### Complete Rewrites
- `pkg/eval/eval.go` - New evaluator core

### New Implementations (C)
- `src/runtime/reader/pika.c` - C Parser (Pika)
- `src/runtime/reader/pika.h` - C Parser Headers

### Major Modifications
- `pkg/ast/value.go` - New types
- `pkg/eval/primitives.go` - New operations
- `pkg/codegen/codegen.go` - New constructs

### New Files
- `pkg/eval/dispatch.go` - Multiple dispatch
- `pkg/eval/types.go` - Type system
- `pkg/eval/module.go` - Module system
- `pkg/eval/macro.go` - Hygienic macros
- `pkg/ast/types.go` - Type structures

### Unchanged
- `runtime/src/*.c` - C runtime (no changes)
- `runtime/tests/*.c` - Runtime tests (no changes)

---

## Verification Checklist

### Parser (C)
- [ ] `()` parses as forms
- [ ] `[]` parses as arrays/bindings
- [ ] `{}` parses as type forms
- [ ] `#{}` parses as dicts
- [ ] `:symbol` shorthand works
- [ ] `$interpolation` works
- [ ] `obj.field` parses correctly
- [ ] `.field` creates accessor lambda

### Evaluator ✅
- [x] `define` handles all cases
- [x] `let` with `:seq`, `:rec` works
- [x] Named `let` (loop) works
- [x] `match` with patterns works
- [x] Guards (`:when`) work
- [x] Multiple dispatch works
- [x] Type hierarchy respects

### Types (Partial)
- [x] Abstract types define hierarchy
- [ ] Structs instantiate correctly
- [ ] Enums with data work
- [x] Parametric types work (basic)
- [x] Optional type annotations work

### Code Gen ✅
- [x] Arrays generate correct C
- [x] Dicts generate correct C
- [x] Tuples generate correct C
- [ ] Structs generate correct C
- [ ] Match generates efficient code
- [x] ASAP memory management preserved

### Tower Semantics ✅
- [x] `lift` works
- [x] `run` works
- [x] `EM` works
- [x] Handler system works
- [x] `with-handlers` scoping works
