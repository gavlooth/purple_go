• Findings

  - Critical: BorrowRef in the public header uses Generation before it is defined, so any external-runtime build that includes the header will fail to compile. runtime/include/purple.h:35 runtime/include/
    purple.h:165
  - Critical: Object layout and IPGE semantics diverge between external runtime and embedded runtime; the compiler emits tethering but runtime/src/runtime.c has no tethered field, so tethering flips scan_tag
    bits used by SCC/scanners and can corrupt traversal state; embedded runtime hardcodes 64‑bit generations while external runtime defaults to 16‑bit. runtime/include/purple.h:219 runtime/src/runtime.c:193
    pkg/codegen/runtime.go:299 pkg/codegen/codegen.go:786 runtime/src/runtime.c:979
  - High: Public concurrency API mismatches across header, runtime, and compiler: header exports channel_create/channel_send/thread_create/atom_compare_and_set, runtime implements make_channel (and forces
    capacity ≥1), channel_send returns bool, spawn_thread, atom_cas, while compiler emits make_channel and expects channel_send to return a value; this will fail to link or change semantics depending on runtime
    mode. runtime/include/purple.h:518 runtime/src/runtime.c:2708 runtime/src/runtime.c:2747 runtime/src/runtime.c:3051 runtime/src/runtime.c:2983 pkg/compiler/compiler.go:1651 pkg/compiler/compiler.go:1670
  - High: Concurrency safety is not enforced in the memory subsystem: atomic_dec_ref can call free_obj, which mutates global freelist state without locks; _WEAK_REF_HEAD and STACK_POOL are also global and
    unsynchronized, so concurrent frees or stack allocations can corrupt state or double‑free. runtime/src/runtime.c:272 runtime/src/runtime.c:632 runtime/src/runtime.c:2669 runtime/src/runtime.c:311 runtime/
    src/runtime.c:282
  - High: Weak‑ref registry leaks and scales poorly: _WEAK_REF_HEAD nodes are never freed, and invalidation scans the full list on every free, so long‑running programs accumulate metadata and O(n) invalidation
    cost. runtime/src/runtime.c:301 runtime/src/runtime.c:335
  - Medium: Architecture/test drift: docs state a single compiled runtime library is the default, but the CLI defaults to embedded runtime; tests include runtime.c directly and skip the public header, so
    external‑runtime ABI/API mismatches won’t be caught. ARCHITECTURE.md:57 main.go:161 runtime/tests/test_framework.h:7

  Open Questions

  - Do you intend to keep both embedded and external runtime modes? If yes, should the header/library be generated from the same source as pkg/codegen/runtime.go to prevent drift?
  - Which concurrency API names and return conventions are canonical (channel_create vs make_channel, thread_create vs spawn_thread, void vs bool channel_send)?
  - Should channels support true unbuffered semantics (capacity 0), or is “capacity 1” the intended model?
  - Do you want thread‑safe RC/free‑list/weak‑ref handling, or should concurrency be limited to strict ownership transfer with no shared objects?

  I didn’t run tests; the assessment above is from static inspection. Overall the architecture is ambitious and coherent on paper, but the runtime duplication and concurrency safety gaps are the biggest
  friction points right now.
