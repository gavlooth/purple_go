# Purple Runtime Test Plan - 100% Coverage

## Test Categories

### 1. Object Constructors (11 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `mk_int` | positive, negative, zero, LONG_MAX, LONG_MIN | malloc failure |
| `mk_float` | positive, negative, zero, infinity, NaN | malloc failure |
| `mk_char` | ASCII, extended chars, 0, 255 | malloc failure |
| `mk_pair` | normal, NULL car, NULL cdr, both NULL | malloc failure |
| `mk_sym` | normal string, empty string, NULL | malloc failure, string copy failure |
| `mk_box` | normal value, NULL value | malloc failure |
| `mk_error` | normal message, empty, NULL | malloc failure |
| `mk_int_stack` | normal, pool exhaustion fallback | pool overflow |
| `mk_closure` | normal, no captures, no args | malloc failure |
| `arena_mk_int` | normal | NULL arena |
| `arena_mk_pair` | normal | NULL arena |

### 2. Memory Management (15 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `inc_ref` | normal, NULL, stack obj, negative mark | multiple increments |
| `dec_ref` | normal, to zero, NULL, stack obj | double decrement |
| `free_obj` | normal, NULL, stack obj, already freed | re-free |
| `free_tree` | single, nested, deep tree | cyclic (should handle) |
| `free_unique` | normal, NULL, stack obj | |
| `release_children` | pair, box, closure, sym, error, channel, user | NULL children |
| `flush_freelist` | empty, single, multiple | |
| `deferred_release` | normal | |
| `defer_decrement` | normal, coalesce same obj | malloc failure |
| `process_deferred` | partial batch, full batch | |
| `flush_deferred` | empty, with items | |
| `safe_point` | below threshold, above threshold | |
| `is_stack_obj` | stack obj, heap obj, NULL | boundary cases |
| `is_nil` | NULL, non-NULL | |

### 3. Box Operations (2 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `box_get` | normal, empty box | NULL, wrong tag |
| `box_set` | normal, set NULL, overwrite | NULL box, wrong tag |

### 4. Pair/List Operations (10 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `obj_car` | normal pair | NULL, non-pair |
| `obj_cdr` | normal pair | NULL, non-pair |
| `list_length` | empty, single, multiple | non-list |
| `list_append` | both non-empty, first empty, second empty | both empty |
| `list_reverse` | empty, single, multiple | |
| `list_map` | empty, single, multiple | NULL fn |
| `list_filter` | keep all, keep none, keep some | NULL fn |
| `list_fold` | empty, single, multiple | NULL fn |
| `list_foldr` | empty, single, multiple | NULL fn |

### 5. Arithmetic Primitives (6 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `prim_add` | int+int, float+float, int+float | overflow, NULL |
| `prim_sub` | normal | underflow, NULL |
| `prim_mul` | normal | overflow, NULL |
| `prim_div` | normal, float division | divide by zero, NULL |
| `prim_mod` | normal, negative | mod by zero, NULL |
| `prim_abs` | positive, negative, zero | LONG_MIN |

### 6. Comparison Primitives (6 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `prim_lt` | true, false, equal | NULL, type mismatch |
| `prim_gt` | true, false, equal | NULL, type mismatch |
| `prim_le` | true, false, equal | NULL, type mismatch |
| `prim_ge` | true, false, equal | NULL, type mismatch |
| `prim_eq` | same, different, same ref | NULL, type mismatch |
| `prim_not` | truthy, falsy | NULL |

### 7. Type Predicates (6 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `prim_null` | NULL, non-NULL | |
| `prim_pair` | pair, non-pair | NULL |
| `prim_int` | int, non-int | NULL |
| `prim_float` | float, non-float | NULL |
| `prim_char` | char, non-char | NULL |
| `prim_sym` | sym, non-sym | NULL |

### 8. Type Introspection (2 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `ctr_tag` | each tag type | NULL, user types |
| `ctr_arg` | pair car, pair cdr, box | NULL, invalid idx |

### 9. I/O Primitives (3 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `prim_display` | int, float, char, string, pair | NULL, nested |
| `prim_print` | int, float, char, string, pair | NULL, nested |
| `prim_newline` | normal | |

### 10. Character/String Primitives (2 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `char_to_int` | normal char | NULL, non-char |
| `int_to_char` | valid range, 0, 255 | NULL, non-int, out of range |

### 11. Float Primitives (4 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `int_to_float` | normal | NULL, non-int |
| `float_to_int` | normal, truncation | NULL, non-float, overflow |
| `prim_floor` | positive, negative, whole | NULL, non-float |
| `prim_ceil` | positive, negative, whole | NULL, non-float |

### 12. Closure Operations (3 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `mk_closure` | no captures, with captures | malloc failure |
| `call_closure` | 0 args, 1 arg, multiple args | NULL closure, wrong arity |
| `closure_release` | normal | NULL |

### 13. Arena Allocator (5 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `arena_create` | normal | malloc failure |
| `arena_destroy` | empty, with objects, with externals | NULL |
| `arena_reset` | empty, with objects | NULL |
| `arena_alloc` | small, large, multiple blocks | NULL arena, 0 size |
| `arena_register_external` | normal | NULL arena, NULL ptr |

### 14. SCC Detection (8 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `create_scc` | normal | malloc failure |
| `scc_add_member` | normal, capacity expansion | NULL scc, NULL obj |
| `freeze_scc` | normal | NULL |
| `find_scc` | found, not found | empty registry |
| `release_scc` | refcount > 0, refcount = 0 | NULL |
| `release_with_scc` | in scc, not in scc | NULL |
| `detect_and_freeze_sccs` | simple cycle, complex | NULL root |
| `tarjan_strongconnect` | single node, tree, dag, cycle | deep recursion |

### 15. Generational References (5 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `genref_from_obj` | normal | NULL obj |
| `genref_release` | normal | NULL, double release |
| `genref_get` | valid, invalidated | NULL |
| `genref_invalidate_obj` | normal | NULL |

### 16. Weak References (3 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `_mk_weak_ref` | normal | malloc failure |
| `_deref_weak` | alive, dead | NULL |
| `invalidate_weak_refs_for` | found, not found | NULL target |

### 17. Symmetric RC (8 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `sym_enter_scope` | normal, nested | |
| `sym_exit_scope` | normal, with orphans | exit without enter |
| `sym_alloc` | normal | NULL |
| `sym_add_ref` | normal | NULL src/tgt |
| `sym_inc_external` | normal | NULL |
| `sym_dec_external` | to zero, above zero | NULL |

### 18. Channels (5 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `make_channel` | buffered, unbuffered | malloc failure, 0 capacity |
| `channel_send` | normal, buffer full, closed | NULL channel, NULL value |
| `channel_recv` | normal, empty wait, closed | NULL channel |
| `channel_close` | normal, already closed | NULL |
| `free_channel_obj` | empty, with values | NULL |

### 19. Atoms (5 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `make_atom` | with initial, NULL initial | malloc failure |
| `atom_deref` | normal | NULL atom |
| `atom_reset` | normal | NULL atom, NULL value |
| `atom_swap` | normal | NULL atom, NULL fn |
| `atom_cas` | success, failure | NULL |

### 20. Threads (3 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `spawn_thread` | normal | NULL closure |
| `thread_join` | immediate, delayed | NULL handle |
| `spawn_goroutine` | normal, with captures | NULL closure |

### 21. Higher-Order Functions (2 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `prim_apply` | normal | NULL fn, NULL args |
| `prim_compose` | normal | NULL f, NULL g |

### 22. Truthiness (1 function)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `is_truthy` | NULL, 0, non-zero, pair | |

## Stress Tests

1. **Deep recursion** - Stack overflow handling
2. **Large allocations** - Memory pressure
3. **Rapid alloc/free cycles** - Fragmentation
4. **Concurrent channel ops** - Race conditions
5. **SCC with many nodes** - Algorithm correctness
6. **Long-running with many safe_points** - Deferred processing

## Memory Safety Tests (with sanitizers)

1. **AddressSanitizer** - Buffer overflows, use-after-free
2. **ThreadSanitizer** - Data races
3. **MemorySanitizer** - Uninitialized reads
4. **Valgrind** - Memory leaks

## Test Infrastructure

```
runtime/tests/
├── test_main.c           # Test runner
├── test_constructors.c   # Object constructor tests
├── test_memory.c         # Memory management tests
├── test_primitives.c     # Primitive operation tests
├── test_lists.c          # List operation tests
├── test_closures.c       # Closure tests
├── test_arena.c          # Arena allocator tests
├── test_scc.c            # SCC detection tests
├── test_concurrency.c    # Channel, atom, thread tests
├── test_edge_cases.c     # Edge cases and error handling
├── test_stress.c         # Stress tests
└── Makefile              # Build tests
```

## Coverage Target

- **Line coverage**: 100%
- **Branch coverage**: 100%
- **Function coverage**: 100%

Total functions to test: ~100
Total test cases needed: ~500+
