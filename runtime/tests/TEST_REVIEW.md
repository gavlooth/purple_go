# Runtime Test Suite Review (incremental)

Scope: Review existing C runtime tests for coverage, correctness, and gaps. Notes are appended per file inspection.

Legend:
- ✅ Good coverage
- ⚠️ Risk/bug
- ➕ Add tests
- ➖ Remove/adjust tests

## test_framework.h
- ✅ Simple harness, clear PASS/FAIL accounting.
- ⚠️ ASSERT_EQ formats with %ld and casts to long; unsafe for pointers/uint64_t and will misreport on LLP64. Add ASSERT_EQ_PTR/ASSERT_EQ_U64 and use in tests.
- ⚠️ No ASSERT_NE / ASSERT_LE / ASSERT_GE helpers; repeated patterns in tests use raw comparisons.
- ➕ Add ASSERT_STR_NE (or explicit strcmp) for error messages where needed.
- ➕ Add ASSERT_ALIGNED / ASSERT_TAG for tagged pointer invariants.
- ➖ RUN_TEST macro exists but most tests call TEST() directly; consider standardizing to reduce missing PASS/FAIL paths.
- Note: Tests include runtime.c directly (single TU). This hides header/API drift (header not used). Consider an additional test binary that includes `purple.h` + links libpurple.a to validate the public API.

## test_main.c
- ✅ Single compilation unit simplifies linking and symbol visibility.
- ⚠️ Includes `runtime.c` directly, so public header/API drift is invisible. Add a second test target that compiles against `purple.h` + `libpurple.a`.
- ⚠️ Concurrency tests and stress tests are commented out; for a thread‑safe runtime these should be enabled (or split into a separate `make tsan` target).
- ➕ Add a “fast” vs “slow/tsan” test grouping so CI can run concurrency tests deterministically.

## test_constructors.c
- ✅ Broad coverage for mk_int/mk_float/mk_pair/mk_sym/mk_error/mk_box/mk_int_stack.
- ⚠️ mk_char tests only cover in‑range (immediate). Missing out‑of‑range/negative values to ensure boxed path works.
- ⚠️ No tests for mk_float_stack/mk_char_stack (stack pool variants).
- ➕ Add tests that verify ownership semantics for mk_pair/mk_box (do they inc_ref children or not) by checking child refcounts before/after freeing the parent.
- ➕ Add tests for mk_float NaN/Inf values (if supported) and mk_sym long string memory correctness (e.g., independence from input buffer mutation).

## test_memory.c
- ✅ Good spread across RC, free_tree, free_unique, freelist, deferred RC, box, and pair access.
- ⚠️ No tests for immediate values in inc_ref/dec_ref/free_obj/free_tree; these are special‑cased and should be validated.
- ⚠️ No tests for double‑free protection (free_obj on already‑freed object) or `mark < 0` semantics.
- ⚠️ `free_tree` tests don’t verify child release/refcount behavior or that symbols/errors free their `ptr` payloads.
- ⚠️ Pair accessor tests used `obj_car/obj_cdr` (owned) without `dec_ref`, inflating refcounts and masking RC issues.
- ➕ Add tests for `release_children` for each tag (pair/box/closure/sym/error/channel/user) with refcount assertions.
- ➕ Add UAF detection tests using BorrowRef/IPGE (borrow, free_obj, then borrow_get should fail).
- ➕ Add stress for `flush_freelist` order/size (e.g., N=10k) to catch leaks/slow paths.
- ➕ Add test for `safe_point` actually draining when above threshold (not just “no crash”).

## test_primitives.c
- ✅ Solid coverage for arithmetic/comparison/conversion happy paths.
- ⚠️ No explicit type‑mismatch tests (e.g., prim_add on non‑numbers, prim_lt on mixed types). Runtime likely returns error/null; behavior should be asserted.
- ⚠️ Minimal boolean coverage: prim_* should accept immediates; tests mostly use boxed values. Add tests for immediate ints/bools/chars.
- ⚠️ Lacks tests for prim_gt/prim_le/prim_ge false cases and prim_eq across floats/symbols/pairs (identity vs value).
- ⚠️ `prim_add(NULL, x)`/`prim_sub(NULL, x)` semantics are not asserted (tests just “no crash”). Add explicit assertions to lock in expected behavior.
- ⚠️ Missing negative cases for type predicates (prim_char/prim_sym/prim_float false cases), and conversions with wrong types/out‑of‑range.
- ⚠️ Runtime arithmetic/eq on non‑numeric tags uses `obj_to_int` on uninitialized fields (e.g., TAG_SYM); type‑mismatch behavior is undefined and currently untested.
- ➕ Add conversion error tests (int_to_char out‑of‑range, float_to_int overflow, char_to_int on non‑char).
- ➕ Add tests for NaN/Inf behavior if NaN‑boxing is enabled.

## test_lists.c
- ✅ Very comprehensive list functionality coverage (car/cdr/append/reverse/map/filter/fold/foldr).
- ⚠️ `list_length` (runtime function) is not tested; the helper `count_list_length` masks runtime behavior.
- ⚠️ Some tests avoid freeing returned lists when they alias inputs (e.g., append first empty) but also skip freeing the input, effectively leaking in test runs. Prefer explicit ownership assertions (`result == b`) then `dec_ref(b)` once.
- ⚠️ map/filter/fold use closures but free them via `free_obj` instead of `dec_ref`; inconsistent with RC model.
- ⚠️ Helper `count_list_length` used `obj_cdr` (owned) without `dec_ref`, inflating refcounts and masking RC bugs; switch to raw traversal.
- ⚠️ Several tests used `obj_car`/`obj_cdr` in assertions without releasing the owned refs; refcount leaks skew runtime behavior.
- ➕ Add tests for non‑list inputs to list_* APIs (list_length/list_map/list_filter) to assert behavior.
- ➕ Add tests for lists containing immediates (mk_int_unboxed/mk_char) to ensure tags are handled.

## test_closures.c
- ✅ Extensive coverage for closure arity, captures, and stress loops.
- ⚠️ Many tests use `free_obj` on regular heap objects/closures and args; this bypasses RC semantics and can mask refcount bugs. Prefer `dec_ref` for owned values.
- ⚠️ No tests for arity mismatch handling (e.g., call_closure with too few/too many args when arity is fixed).
- ⚠️ No tests for BorrowRef/IPGE validation of captured variables (UAF detection on capture).
- ⚠️ Helper list-length in closure tests used `obj_cdr` without releasing owned refs, inflating refcounts.
- ⚠️ `test_closure_capture_pair` leaked the returned `obj_car` reference (missing `dec_ref`).
- ➕ Add tests for closure capture lifetime: create closure capturing obj, free parent scope obj, then call closure and assert correct behavior (or BorrowRef failure).
- ➕ Add tests for `call_closure` error path (NULL closure, wrong tag, wrong arity).

## test_tagged_pointers.c
- ✅ Strong coverage of immediate tagging, extraction, and helpers.
- ⚠️ Uses internal arithmetic helper names (`add`, `sub`, `div_op`, etc.) rather than public `prim_*` API; this can mask API drift.
- ⚠️ No tests for out‑of‑range immediates (e.g., value > 61 bits) or sign extension edge cases.
- ⚠️ Uses `free_obj` on boxed values; prefer `dec_ref` for RC correctness.
- ⚠️ No tests for behavior when attempting to tag values outside representable range (overflow/boxing fallback).
- ➕ Add tests to ensure boxed values are not mis‑detected as immediates when pointer low bits overlap (alignment assumptions).
- ➕ Add tests for NaN‑boxing interactions if enabled (float values vs immediate tags).

## test_arena.c
- ✅ Thorough coverage of arena create/reset/destroy and bulk allocation behavior.
- ⚠️ Tests reach into Arena internals (`current`, `blocks`, `block_size`). That’s fine for internal runtime tests but will break if Arena becomes opaque; note as a coupling risk.
- ⚠️ `arena_register_external` isn’t declared in `purple.h` (public API); tests use it anyway. This hides header/runtime mismatches.
- ➕ Add tests for mixed arena/heap references (arena_mk_pair with heap children, and vice‑versa) to verify free/ownership behavior.
- ➕ Add tests for arena usage under concurrency if arenas are expected to be thread‑local or thread‑safe.

## test_scc.c
- ✅ Large coverage of SCC registry, Tarjan state, and simple cycles.
- ⚠️ Tests rely heavily on internal globals (SCC_REGISTRY) and do not reset global state, so test order can affect results.
- ⚠️ Several tests add members to SCC but never release the SCC; leaks can hide lifecycle bugs.
- ⚠️ `scc_add_member` does not inc_ref; some tests dec_ref members after adding, risking dangling pointers inside SCC.
- ➕ Add tests for large cycles >256 nodes (Tarjan uses fixed `scc_members[256]` array) to catch truncation bugs.
- ➕ Add tests for cycles involving boxes/closures/symbols (non‑pair edges) to ensure scanning covers all tag cases.
- ➕ Add tests to validate scan_tag cleanup after SCC detection (no residual marks interfering with other analyses).

## test_concurrency.c
- ⚠️ Tests appear stale vs runtime semantics: `atom_reset` is asserted to return old value, but runtime returns new value; `atom_cas` is asserted to return old/NULL, but runtime returns `mk_int(1/0)`.
- ⚠️ `spawn_thread(NULL)` currently returns a thread handle (closure optional), but test expects NULL; mismatch hides runtime semantics.
- ⚠️ Uses internal names (`make_channel`, `spawn_thread`, `atom_cas`) rather than public API in header, hiding header/runtime drift.
- ⚠️ Widespread use of `free_obj` on live shared objects/closures in concurrent tests can race with threads (e.g., freeing closure immediately after spawn). Should use `dec_ref` and join/flush to avoid UAF.
- ⚠️ Test suite currently disables `run_concurrency_tests()` in `test_main.c`; no concurrency coverage is executed.
- ➕ Add tests for unbuffered channel blocking and ownership transfer across threads (send should block until recv in capacity 0).
- ➕ Add TSAN-targeted tests for free list / RC safety under concurrent dec_ref/free_obj.

## test_weak_refs.c
- ✅ Good coverage of weak ref creation, deref, invalidation, and integration with free/dec_ref.
- ⚠️ Tests use internal weak‑ref APIs and global registry; no cleanup of registry between tests, so leaks accumulate (expected but worth noting).
- ⚠️ No tests for weak refs in presence of concurrency; registry is global and unprotected.
- ⚠️ `_mk_weak_ref` inserts into a global list with no removal API; freeing `_InternalWeakRef` pointers leaves dangling registry nodes (UAF risk). Tests should avoid `free()` on weak refs unless a removal mechanism exists.
- ➕ Add tests to ensure invalidation complexity doesn’t regress (e.g., timing/size threshold), and optional checks for registry growth over time.
- ➖ Test for weak refs to immediates may be questionable (immediates aren’t heap‑owned); decide whether to keep or drop based on runtime semantics.

## test_borrowref.c
- ✅ Comprehensive IPGE/borrow validity coverage, including stress and closure capture validation.
- ⚠️ Uses internal `closure_validate` and BorrowRef internals; fine for internal tests but not for public API drift checks.
- ⚠️ Uses `free_obj` in some paths without flushing freelist; might leave objects in deferred free and hide issues.
- ➕ Add tests for `borrow_get` if that is part of public API (currently declared in header but not implemented in runtime.c).
- ➕ Add tests for robustness over generation wrap (compact mode collision window) to clarify guarantees.

## test_deferred.c
- ✅ Comprehensive coverage of deferred RC lifecycle and batching.
- ⚠️ Tests rely on internal DEFERRED_CTX state and assume single‑threaded access; no thread‑safety coverage.
- ➕ Add tests for immediate values (defer_decrement on immediates should be no‑op).
- ➕ Add tests for memory pressure (large pending list) to ensure no unbounded growth/leaks.

## test_stress.c
- ✅ Broad stress coverage across alloc, lists, closures, arena, RC, SCC, weak refs, borrow refs, and concurrency.
- ⚠️ Many tests leak by not freeing results (e.g., `test_stress_closure_10k_calls` never releases `result`). If enabled, these can balloon memory.
- ⚠️ Uses internal APIs (`add`, `make_channel`, `spawn_thread`) and `free_obj` in many places; inconsistent with RC semantics and header API.
- ⚠️ Concurrency stress uses thread/channel APIs but `run_stress_tests()` is disabled in `test_main.c`, so coverage is currently zero.
- ⚠️ List stress helper used `obj_cdr` (owned) without releasing; refcount inflation during large tests.
- ➕ Add a stress target that runs under ASAN/TSAN with timeouts, and ensure all allocations are released.

## test_channel_semantics.c
- ✅ Validates unbuffered channel blocking semantics (important for correctness).
- ⚠️ Timing‑based (sleep) test can be flaky; consider using a condition variable/barrier to avoid timing dependence.
- ⚠️ Uses internal `make_channel` and `channel_send` names (not header API) and `free_obj` instead of `dec_ref`.
- ⚠️ Runtime currently forces `make_channel(0)` to capacity 1, so unbuffered semantics test will fail until runtime supports true capacity‑0 channels.
