# Memory Optimizations and Safety (ASAP-First)

This is a short index for memory-related optimizations and safety mechanisms.
For the detailed plan, pipeline, and strategy matrix, see
`docs/UNIFIED_OPTIMIZATION_PLAN.md`.

If you only read one thing: **GenRef/IPGE does NOT replace reference counting.**
Generational references detect use-after-free; they do not reclaim memory or
resolve cycles.

## Optimization Implementation Status (2026-01-03)

**All 11 planned memory optimizations are COMPLETE** with full test coverage.

| # | Optimization | Tests | Key Features |
|---|--------------|-------|--------------|
| 1 | GenRef/IPGE Soundness Fix | - | Stable slot pool |
| 2 | Full Liveness-Driven Free Insertion | - | CFG-based analysis |
| 3 | Ownership-Driven Codegen | - | `free_unique`/`free_tree`/`dec_ref` |
| 4 | Escape-Aware Stack Allocation | - | `STACK_INT`/`STACK_CELL` |
| 5 | Shape Analysis + Weak Back-Edge | 7 | Auto cycle/weak detection |
| 6 | Perceus Reuse Analysis | 7 | `reuse_as_*`/`REUSE_OR_NEW_*` |
| 7 | Region-Aware RC Elision | 11 | `INC_REF_IF_NEEDED`/`REGION_LOCAL_REF` |
| 8 | Per-Region External Refcount | 7 | `REGION_CAN_BULK_FREE` |
| 9 | Borrow/Tether Loop Insertion | 8 | `TETHER`/`BORROW_FOR_LOOP` |
| 10 | Interprocedural Summaries | 11 | `PARAM_BORROWED`/`RETURN_FRESH` |
| 11 | Concurrency Ownership Inference | 14 | `ATOMIC_INC_REF`/`Channel`/`SPAWN_THREAD` |

**Total: 65+ tests in `csrc/tests/`**

## Non-Negotiables

- No stop-the-world GC and no global heap scans.
- All deallocation points are decided at compile time.
- Runtime work is local (per-object, per-scope, or per-subgraph) and bounded.

## Post‑11 Enhancements (Backlog, ASAP‑Compatible)

These are **optional** extensions beyond the original 11 optimizations.
They preserve ASAP‑first semantics and add **no language restrictions**.
See `docs/UNIFIED_OPTIMIZATION_PLAN.md` for sketches.

12) Linear/offset regions for serialization & FFI buffers  
13) Pluggable region backends (IRegion‑style vtable)  
14) Weak ref control blocks (merge‑friendly weak refs)  
15) Transmigration / isolation on region escape  
16) External handle indexing (FFI + deterministic replay)

**References (inspiration):**
- Vale `LinearRegion`: `Vale/docs/LinearRegion.md`
- Vale region interface: `Vale/docs/IRegion.md`
- Vale weak refs: `Vale/docs/WeakRef.md`
- Vale transmigration: `Vale/docs/regions/Transmigration.md`
- Vale replayability handle map: `Vale/docs/PerfectReplayability.md`

## Safety Mechanisms (UAF Detection)

### GenRef / IPGE / Tagged Handles (Critical)

Generational checks are **only sound** if the generation field stays readable
after logical free. That requires ONE of:

1) **Stable slot pool** (recommended) ✅ Implemented
2) **Quarantine allocator** (delay actual free)
3) **Indirection table** (stable metadata)

Using raw `malloc/free` and then reading `obj->generation` is undefined behavior.

## Where to Look Next

- `ARCHITECTURE.md` - system-level memory strategy selection
- `docs/GENERATIONAL_MEMORY.md` - soundness requirements for GenRef/IPGE
- `docs/UNIFIED_OPTIMIZATION_PLAN.md` - detailed analysis/optimization flow
- `docs/OPTIMIZATION_PLAN.md` - prioritized implementation plan
- `runtime/src/runtime.c` - runtime implementations
- `csrc/analysis`, `csrc/codegen` - compiler passes
- `csrc/tests/test_*.c` - optimization test suites
