# Memory Optimizations and Safety (ASAP-First)

This is a short index for memory-related optimizations and safety mechanisms.
For the detailed plan, pipeline, and strategy matrix, see
`docs/UNIFIED_OPTIMIZATION_PLAN.md`.

If you only read one thing: **GenRef/IPGE does NOT replace reference counting.**
Generational references detect use-after-free; they do not reclaim memory or
resolve cycles.

## Non-Negotiables

- No stop-the-world GC and no global heap scans.
- All deallocation points are decided at compile time.
- Runtime work is local (per-object, per-scope, or per-subgraph) and bounded.

## Safety Mechanisms (UAF Detection)

### GenRef / IPGE / Tagged Handles (Critical)

Generational checks are **only sound** if the generation field stays readable
after logical free. That requires ONE of:

1) **Stable slot pool** (recommended)
2) **Quarantine allocator** (delay actual free)
3) **Indirection table** (stable metadata)

Using raw `malloc/free` and then reading `obj->generation` is undefined behavior.

## Where to Look Next

- `ARCHITECTURE.md` - system-level memory strategy selection
- `docs/GENERATIONAL_MEMORY.md` - soundness requirements for GenRef/IPGE
- `docs/UNIFIED_OPTIMIZATION_PLAN.md` - detailed analysis/optimization flow
- `docs/OPTIMIZATION_PLAN.md` - prioritized implementation plan
- `runtime/src/runtime.c` - runtime implementations
- `csrc/analysis`, `csrc/codegen`, `csrc/memory` - compiler passes and glue
