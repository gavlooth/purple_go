# Test Fixes - 2025-01-03

## Problem
The C runtime test suite was failing with a double-free error when running SCC (Strongly Connected Component) tests.

## Root Cause Analysis

### Issue 1: Uninitialized SCC/Scan Fields
**Location**: Object constructors (`mk_float`, `mk_char`, `mk_sym`, `mk_box`, and any heap-backed constructors)

**Problem**: `scc_id` and/or `scan_tag` were left uninitialized on some heap objects. This caused:
- `scc_add_member` to skip objects that appeared to already be in an SCC (`scc_id >= 0`)
- Tarjan traversal to treat objects as already visited (`scan_tag != 0`)

Both issues produced incorrect SCC membership and occasionally double-free conditions when stale metadata interacted with cycle release.

**Evidence**: Debug output showed object `0x5a48f2435ad0` appearing in two different SCCs:
```
DEBUG: Releasing SCC with 1 members
DEBUG: Member 0: addr=0x5a48f2435ad0, mark=1
DEBUG: Releasing SCC with 10 members
DEBUG: Member 0: addr=0x5a48f2435ad0, mark=1  # Same address!
```

### Issue 2: Improper Child Pointer Cleanup
**Location**: `runtime/src/runtime.c:release_scc` (line 949)

**Problem**: When freeing objects in an SCC, child pointers weren't cleared before calling `invalidate_weak_refs_for`. This could cause the invalidation functions to access freed memory through child pointers.

### Issue 3: Fallback to `dec_ref` After SCC Free
**Location**: `runtime/src/runtime.c:release_with_scc` (line 1007)

**Problem**: When `find_scc` returned NULL (because the SCC was already freed), the code fell through to calling `dec_ref` on objects that were already freed, causing double-free.

### Issue 4: Uninitialized Fields
**Location**: Object constructors (`mk_int`, `mk_float`, `mk_pair`)

**Problem**: Using `malloc` instead of `calloc` left `scc_id` and `scan_tag` uninitialized with garbage values, causing:
- SCC membership checks to fail
- Tarjan algorithm to incorrectly identify visited nodes

## Fixes Applied

### Fix 1: Prevent Duplicate SCC Membership (Safety Net)
```c
// runtime/src/runtime.c:918-934
void scc_add_member(SCC* scc, Obj* obj) {
    if (!scc || !obj) return;
    /* Check if object is already in an SCC */
    if (obj->scc_id >= 0) {
        /* Object is already in an SCC, don't add again */
        return;
    }
    // ... rest of function
}
```

### Fix 2: Two-Phase SCC Release
```c
// runtime/src/runtime.c:949-977
void release_scc(SCC* scc) {
    if (!scc) return;
    scc->ref_count--;
    if (scc->ref_count <= 0 && scc->frozen) {
        /* Phase 1: Mark all objects as freed */
        for (int i = 0; i < scc->member_count; i++) {
            Obj* obj = scc->members[i];
            if (obj) {
                obj->mark = -1;  /* Mark as freed */
            }
        }

        /* Phase 2: Clear pointers, invalidate refs, then free */
        for (int i = 0; i < scc->member_count; i++) {
            Obj* obj = scc->members[i];
            if (obj) {
                /* Clear child pointers - important for cyclic references! */
                if (obj->is_pair) {
                    obj->a = NULL;
                    obj->b = NULL;
                }
                // ... handle other types
                invalidate_weak_refs_for(obj);
                borrow_invalidate_obj(obj);
                free(obj);
                scc->members[i] = NULL;  /* Prevent double-free */
            }
        }
        // ... cleanup SCC struct
    }
}
```

### Fix 3: Early Return for Already-Freed SCCs
```c
// runtime/src/runtime.c:1007-1027
void release_with_scc(Obj* obj) {
    if (!obj) return;
    /* Check if object was already freed by SCC release (mark == -1) */
    if (obj->mark < 0) {
        /* Object was already freed, don't do anything */
        return;
    }
    if (obj->scc_id >= 0) {
        SCC* scc = find_scc(obj->scc_id);
        if (scc) {
            release_scc(scc);
            return;
        }
        /* SCC was already freed - object was already freed as part of SCC
         * Don't call dec_ref because it would cause a double-free.
         * Just return and leave the object alone. */
        return;
    }
    dec_ref(obj);
}
```

### Fix 4: Initialize Object Fields
```c
// runtime/src/runtime.c:359-418 (and other constructors)
Obj* mk_int(long i) {
    Obj* x = malloc(sizeof(Obj));
    if (!x) return NULL;
    x->generation = _next_generation();
    x->mark = 1;
    x->tag = TAG_INT;
    x->is_pair = 0;
    x->scc_id = -1;      /* Initialize to not in SCC */
    x->scan_tag = 0;     /* Initialize to not visited by Tarjan */
    x->i = i;
    return x;
}

// Similar initialization for mk_float, mk_char (boxed), mk_sym, mk_box, mk_pair
```

## Test Results

### Before Fixes
```
=== Summary ===
  Total:  446
  Passed: 442
  Failed: 4
```

Failures:
- `test_scc_add_member`: Expected 1 member, got 0
- `test_scc_add_multiple_members`: Expected 10 members, got 0
- `test_scc_member_capacity_growth`: Capacity didn't grow
- `test_scc_stress_many_members`: Expected 1000 members, got 23
- Plus: Double-free crash during `test_is_char_int`

### After Fixes
```
=== Summary ===
  Total:  446
  Passed: 446
  Failed: 0
```

All tests now pass.

## Lessons Learned

1. **Always Initialize SCC/Scan Fields**: `scc_id` and `scan_tag` must be set for every heap object to keep Tarjan traversal and SCC membership consistent.

2. **Cycle Collection Requires Careful Ordering**: When freeing objects in a cycle, always:
   - Mark all objects as freed first (prevents recursive freeing)
   - Clear all outgoing pointers (prevents access to freed memory)
   - Then free the objects

3. **`malloc` Leaves Memory Uninitialized**: Never rely on `malloc` to zero-initialize. Always explicitly initialize fields that have sentinel values (like `-1` for `scc_id` or `0` for `scan_tag`).

4. **Use `calloc` or Explicit Initialization**: For critical fields that determine program logic, either use `calloc` or explicitly initialize in constructors.
