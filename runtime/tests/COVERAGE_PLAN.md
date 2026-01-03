# Runtime Test Coverage Plan

Goal: increase runtime test coverage and reliability without changing language semantics.

## Plan (execute top-to-bottom)

- [x] 1) Add fast/slow test gating and Makefile targets
  - [x] 1.1) Gate concurrency + stress tests behind `RUNTIME_TEST_LEVEL=slow`
  - [x] 1.2) Add `fast`/`slow`/`tsan-slow`/`asan-slow` targets in `runtime/tests/Makefile`

- [x] 2) Align public API surface with the library symbols
  - [x] 2.1) Fix header/runtime mismatches (list_length return type, channel_send return type)
  - [x] 2.2) Export wrapper symbols: `channel_create`, `atom_create`, `thread_create`, `atom_compare_and_set`
  - [x] 2.3) Implement `borrow_get` in the runtime

- [x] 3) Add a public-API test binary that only includes the header + links the library
  - [x] 3.1) New `runtime/tests/test_api.c` with minimal harness
  - [x] 3.2) Cover constructors, primitives, lists, closures, arena, borrow, channels, atoms, threads
  - [x] 3.3) Add `api` Makefile target to build/run it

- [x] 4) Implement unbuffered channel semantics (capacity 0)
  - [x] 4.1) Allow capacity 0 in channel creation
  - [x] 4.2) Add synchronous send/recv handshake for capacity 0
  - [x] 4.3) Update channel cleanup for the unbuffered slot

- [x] 5) Expand ownership/refcount tests for runtime destructors
  - [x] 5.1) Add tests for `release_children` on pair/box/closure/sym/error/channel/atom/thread
  - [x] 5.2) Add tests for immediate-safe `free_tree`
  - [x] 5.3) Add list non-list input tests where missing

- [x] 6) Stabilize unbuffered channel test (reduce timing flakiness)
  - [x] 6.1) Add condition-variable synchronization for sender/receiver
  - [x] 6.2) Keep a minimal timeout-based fallback to avoid hangs
