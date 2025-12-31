# Missing Features from Original Purple (HVM4)

Comparison between our Go implementation and the original Purple at `/home/heefoo/Documents/code/purple`.

## Legend
- ✅ Implemented in purple_go
- ❌ Missing from purple_go
- 🔶 Partially implemented

---

## Core Language Features

### Data Types
| Feature | Status | Notes |
|---------|--------|-------|
| Numbers (integers) | ✅ | `TInt` |
| Symbols | ✅ | `TSym` |
| Pairs/Cons cells | ✅ | `TCell` |
| Nil | ✅ | `TNil` |
| Characters (`#\a`, `#\newline`) | ❌ | Original supports char literals |
| Strings (as char lists) | ❌ | Original represents strings as cons lists |
| Closures | ✅ | `TLambda` |
| Code values | ✅ | `TCode` |
| Error values | ❌ | `#Err{msg}` in original |

### Special Forms
| Feature | Status | Notes |
|---------|--------|-------|
| `lambda` | ✅ | Basic lambda |
| `lambda self` (recursive) | ❌ | `(lambda self (x) body)` for self-reference |
| `let` | ✅ | Single binding |
| `letrec` | ✅ | Recursive bindings |
| `if` | ✅ | Conditional |
| `quote` | ✅ | Quote expression |
| `and` / `or` | ✅ | Short-circuit logic |
| `do` (sequencing) | ❌ | `(do e1 e2 ... en)` returns last |
| `match` | ❌ | Full pattern matching |

### Pattern Matching (❌ All Missing)
| Feature | Description |
|---------|-------------|
| Wildcard `_` | Matches anything, binds nothing |
| Variable patterns | `x` matches and binds |
| Literal patterns | `(42)` matches specific value |
| Constructor patterns | `(CON a b)` destructures |
| Nested patterns | `(CON (CON a b) c)` |
| Or-patterns | `(or pat1 pat2)` alternatives |
| As-patterns | `(x @ pat)` bind whole and parts |
| List patterns | `(list a b . rest)` |
| Guards | `:when condition` |

---

## Stage-Polymorphic Evaluation

| Feature | Status | Notes |
|---------|--------|-------|
| `lift` | ✅ | Quote value as code |
| `run` | ❌ | `(run base code)` execute at base level |
| `code` / `quote` | ✅ | Quote as AST |
| Compile mode | 🔶 | Basic support, not full tower |

---

## Meta-Level / Reflective Features (❌ All Missing)

| Feature | Description |
|---------|-------------|
| `EM` | Execute at parent meta-level |
| `shift` | Go up n levels and evaluate |
| `clambda` | Compile lambda under current semantics |
| `meta-level` | Get current tower level |
| `get-meta` | Fetch handler by name |
| `set-meta!` | Install custom handler |
| `with-menv` | Evaluate in specific meta-environment |
| `with-handlers` | Set multiple handlers at once |

### Handler Customization (❌ All Missing)
- `lit` handler - numeric literal evaluation
- `var` handler - variable lookup
- `lam` handler - lambda creation
- `app` handler - function application
- `if` handler - conditional
- `lft` handler - lift operation
- `run` handler - code execution
- `em` handler - meta-level jump
- `clam` handler - compiled lambda

---

## Primitives

### Arithmetic
| Feature | Status |
|---------|--------|
| `+`, `-`, `*`, `/`, `%` | ✅ |

### Comparison
| Feature | Status |
|---------|--------|
| `=`, `<`, `>`, `<=`, `>=` | ✅ |
| `not` | ✅ |

### List Operations
| Feature | Status | Notes |
|---------|--------|-------|
| `cons` | ✅ | |
| `car` / `fst` | ✅ | |
| `cdr` / `snd` | ✅ | |
| `null?` | ✅ | |
| `map` | ❌ | Higher-order |
| `filter` | ❌ | Higher-order |
| `fold` / `foldr` | ❌ | Right fold |
| `foldl` | ❌ | Left fold |
| `length` | ❌ | |
| `append` | ❌ | |
| `reverse` | ❌ | |
| `apply` | ❌ | Apply fn to arg list |

### Function Combinators (❌ All Missing)
| Feature | Description |
|---------|-------------|
| `compose` | `(compose f g)` → f ∘ g |
| `flip` | Swap argument order |

---

## Introspection (❌ All Missing)

| Feature | Description |
|---------|-------------|
| `ctr-tag` | Extract constructor name |
| `ctr-arg` | Extract constructor argument by index |
| `reify-env` | Return current environment as value |
| `gensym` | Generate unique symbol |
| `eval` | Evaluate code at runtime |
| `sym-eq?` | Symbol equality check |

---

## Error Handling (❌ All Missing)

| Feature | Description |
|---------|-------------|
| `error` | Raise error with message |
| `try` | Catch errors with handler |
| `assert` | Conditional error |
| `default-handler` | Delegate to default |

---

## I/O and FFI (❌ All Missing)

| Feature | Description |
|---------|-------------|
| `ffi "puts"` | Write string to stdout |
| `ffi "putchar"` | Write single character |
| `ffi "getchar"` | Read single character |
| `ffi "exit"` | Exit with code |
| `ffi "newline"` | Write newline |
| `trace` | Evaluate and trace value |

---

## Macro System (❌ All Missing)

| Feature | Description |
|---------|-------------|
| Quasiquote `` ` `` | Quote with evaluation |
| Unquote `,` | Evaluate in quasiquote |
| Unquote-splicing `,@` | Splice list |
| `defmacro` | Define macro |
| `mcall` | Call macro |
| `macroexpand` | Expand without eval |

---

## Implementation Details

### Variable Representation
| Feature | Status | Notes |
|---------|--------|-------|
| Named variables | ✅ | Current approach |
| De Bruijn indices | ❌ | Original uses indices |

### Memory Management
| Feature | Status | Notes |
|---------|--------|-------|
| ASAP free insertion | ✅ | Compile-time |
| Shape analysis | ✅ | TREE/DAG/CYCLIC |
| Arena allocation | ✅ | For cyclic data |
| Weak edges | ✅ | Break ownership cycles |
| HVM4 interaction nets | ❌ | Original uses HVM4 |

---

## Priority Order for Implementation

### High Priority (Core Language)
1. **Pattern matching** - fundamental for idiomatic code
2. **Recursive lambda** - `(lambda self ...)` for cleaner recursion
3. **Error handling** - `error`, `try`, `assert`
4. **List operations** - `map`, `filter`, `fold`, etc.

### Medium Priority (Staging)
5. **`run` form** - execute code at base level
6. **Meta-level operations** - EM, shift, clambda
7. **Handler customization** - user-defined semantics

### Lower Priority (Convenience)
8. **Quasiquote** - template syntax
9. **Macro system** - syntactic abstraction
10. **FFI/I/O** - practical programs
11. **Characters/strings** - text handling
12. **Introspection** - metaprogramming

---

## References

- Original Purple: `/home/heefoo/Documents/code/purple`
- "Collapsing Towers of Interpreters" (Amin & Rompf, POPL 2018)
- HVM4: Higher Order Virtual Machine
