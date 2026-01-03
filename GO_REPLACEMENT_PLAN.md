# Go Replacement Plan (C-Only Toolchain)

Goal: replace the current Go-based compiler toolchain with a C99 + POSIX implementation, using lightweight C libraries where helpful, while preserving behavior and tests.

## Phase 0 — Scope, Inventory, and Parity Targets ✅
- [x] Inventory all Go entrypoints and flags (CLI behavior, inputs, outputs, environment expectations).
- [x] Map Go packages to C modules (parser, AST, analysis, codegen, runtime glue, CLI).
- [x] Define feature parity targets (compiler output, diagnostics, test suite, runtime expectations).
- [x] Decide if any Go-only features are explicitly dropped (and document them).
- [x] Rename language and tooling from **Purple** to **OmniLisp** (CLI, docs, runtime headers, outputs).

## Phase 1 — C Build and Project Layout ✅
- [x] Create a top-level C build system (Makefile or CMake) that builds:
  - compiler front-end (parsing + analysis)
  - codegen to C99
  - CLI driver
- [x] Define a consistent C module layout (e.g. `csrc/parser`, `csrc/analysis`, `csrc/codegen`, `csrc/cli`).
- [x] Add a test runner target that can execute all existing test suites.
- [x] Add compile profiles (debug, release, asan, tsan, ubsan) for memory safety validation.

## Phase 2 — Dependencies (Lightweight C Libraries)
- [ ] Adopt concrete lightweight libs (C99-compatible, permissive license), and vendor under `third_party/`:
  - Hash maps: `uthash` (BSD-1)
  - Dynamic arrays + hash tables: `stb_ds` (MIT-like)
  - String builder: `sds` (BSD-2/3 style) or local minimal string builder
  - Optional REPL line editing: `linenoise` (BSD-2)
- [ ] Add license notices for each dependency.
- [ ] Wrap each dependency behind thin adapters so you can swap later.
- [ ] Verify the build is warning-clean with each dependency enabled.

Reference URLs (for license and vendoring docs):
```
uthash:    https://troydhanson.github.io/uthash/
stb_ds:    https://github.com/nothings/stb
sds:       https://github.com/antirez/sds
linenoise: https://github.com/antirez/linenoise
```

Vendoring checklist:
- [ ] Pin exact commit hashes for each dependency in `third_party/README.md`.
- [ ] Copy LICENSE files into `third_party/<lib>/LICENSE`.
- [ ] Add a top-level `NOTICE` (or `THIRD_PARTY_NOTICES`) with attribution.
- [ ] Record the source URL + commit hash in the build output (optional).

## Phase 3 — AST + IR Foundations (C) ✅
- [x] Define C AST structs mirroring `pkg/ast` semantics.
- [x] Define a minimal arena allocator for AST/IR nodes (or use a tiny arena lib).
- [x] Implement AST printing and debugging helpers.
- [x] Port/verify AST tests.

## Phase 4 — Parser Replacement (C) ✅
- [x] Integrate the standalone C Pika parser (`omnilisp/src/runtime/pika_c`).
- [x] Create a C `parser` module that maps Pika matches into AST nodes.
- [x] Port parser tests from Go (`pkg/parser/*_test.go`).
- [x] Verify grammar parity against Java/Paper (existing tests + new golden tests).
- [x] Parsing must follow the Pika algorithm **and** the Omnilisp parser module semantics (treat both as authoritative).
- [x] Use Omnilisp design docs as the syntax guide: `omnilisp/DESIGN.md`, `omnilisp/DESIGN_DECISIONS.md`, `omnilisp/DECISIONS.md`, `omnilisp/SYNTAX.md`, `omnilisp/SUMMARY.md`.

## Phase 5 — Analysis Passes (C) ✅
- [x] Port liveness analysis.
- [x] Port escape analysis.
- [x] Port ownership/region analysis.
- [x] Port reuse analysis and memoization.
- [x] Port concurrency analysis and thread-safe constructs.
- [x] Port SCC/graph algorithms used by analysis.
- [x] Port tests under `pkg/analysis/*_test.go`.

## Phase 6 — Codegen (C) ✅
- [x] Port codegen modules (`pkg/codegen/*`), including:
  - type lowering
  - region/arena strategy
  - reuse optimization
  - DPS transforms
  - exception handling
- [x] Ensure output is C99 + POSIX pthreads.
- [x] Port codegen tests.

## Phase 7 — Compiler Driver and CLI (C) ✅
- [x] Implement CLI flags matching `main.go` (`-c`, `-o`, `-e`, `-v`, `--interp` removal or stub).
- [x] Implement file/STDIN input handling and REPL mode.
- [x] Implement external runtime path discovery (mirror Go logic).
- [x] Port `pkg/compiler` behavior for compile-to-C and compile-to-binary.

## Phase 8 — Runtime Integration ✅
- [x] Ensure generated C code links against `runtime/libpurple.a`.
- [x] Validate that runtime ABI remains consistent with codegen output.
- [x] Add runtime API consistency tests in C.

## Phase 9 — Test Suite Migration ✅
- [x] Port or rewrite Go tests into C or shell-based equivalents.
- [x] Preserve existing validation suites (`asan`, `tsan`, `valgrind`, stress tests).
- [ ] Add coverage reporting for C (gcov/llvm-cov).

## Phase 10 — Remove Go Toolchain (In Progress)
- [ ] Remove Go sources and `go.mod` once parity is complete.
- [x] Update docs to describe the C-only build and toolchain.
- [x] Add a final parity checklist and signed-off verification results.
- [x] Complete repo-wide rename: `purple` → `omnilisp` (binary name, docs, examples, file headers, tests).

## Optional Enhancements
- [ ] Add a stable C API for embedding the compiler as a library.
- [ ] Add a C FFI test harness to validate behavior across platforms.
- [ ] Add a language server or tooling hooks in C if required.

## Enhancements (Developer Experience Priorities)
- [ ] Best-in-class diagnostics:
  - [ ] Source-span annotations with caret highlights and inline context lines.
  - [ ] Error codes + short explanations + actionable fix-it hints.
  - [ ] Optional rule-trace output for parser failures (toggle via flag/env).
  - [ ] JSON diagnostics output for editor/CI integrations.
- [ ] Build cache (fast incremental builds):
  - [ ] Content-hash compilation units and cache outputs (AST, analysis summaries, C output).
  - [ ] Dependency graph invalidation (only rebuild affected units).
  - [ ] Cache versioning and debug flag to dump cache hits/misses.
- [ ] FFI ergonomics (user-friendly by default):
  - [ ] Declarative FFI blocks in the language (imports, types, ownership, lifetimes).
  - [ ] Auto-generated C headers for exported functions.
  - [ ] Safe wrappers for ownership transfer and error handling.
- [ ] Debugging should be a breeze:
  - [ ] High-quality stack traces with source spans, inlined frames, and macro expansion info.
  - [ ] CL-style restarts integration (actionable recovery options at error sites).
  - [ ] Optional “explain mode” to show evaluation steps and variable bindings.
  - [ ] Pluggable debug hooks (breakpoints, tracepoints, step/continue).

## Package-by-Package Migration Checklist
- [ ] `main.go` → `csrc/cli/main.c` (flags, REPL, compile/run, runtime discovery)
- [ ] `pkg/parser` → `csrc/parser/*` (Pika integration + AST construction)
- [ ] `pkg/ast` → `csrc/ast/*` (AST structs + printer + helpers)
- [ ] `pkg/analysis` → `csrc/analysis/*` (liveness/escape/ownership/regions/reuse/concurrency)
- [ ] `pkg/codegen` → `csrc/codegen/*` (C99 emission + runtime ABI glue)
- [ ] `pkg/compiler` → `csrc/compiler/*` (orchestration, file IO, build pipeline)
- [ ] `pkg/runtimecheck` → `csrc/runtimecheck/*` (ABI consistency tests)
- [ ] `pkg/memory` → `csrc/memory/*` (arena/region/deferred/free list helpers)
- [ ] `pkg/jit` → `csrc/jit/*` (if still required; otherwise deprecate)
- [ ] `pkg/eval` → decide: keep (port) or remove (compiler-only)

Test migration mapping:
- [ ] `pkg/parser/*_test.go` → `csrc/parser/tests/*`
- [ ] `pkg/analysis/*_test.go` → `csrc/analysis/tests/*`
- [ ] `pkg/codegen/*_test.go` → `csrc/codegen/tests/*`
- [ ] `pkg/compiler/*_test.go` → `csrc/compiler/tests/*`
- [ ] `test/validation/*` → `c_tests/validation/*` (asan/tsan/valgrind parity)
- [ ] `test/stress/*` → `c_tests/stress/*`
- [ ] `test/benchmark/*` → `c_tests/benchmark/*`

## Example Guides (How-To)

### Build the C toolchain (proposed)
Makefile path:
```
make -C csrc
```

CMake path:
```
cmake -S csrc -B build
cmake --build build
```

### Run the compiler (proposed CLI name: `purplec`)
Emit C:
```
./build/purplec -c examples/demo.purple -o demo.c
```

Compile and run:
```
./build/purplec examples/demo.purple -o demo
./demo
```

### Add a new C module (port a Go package)
1) Create module folder: `csrc/<module>/` with `<module>.c` and `<module>.h`.
2) Add module to build system (Makefile/CMake).
3) Add a small unit test under `csrc/<module>/tests/`.
4) Wire the module into the compiler pipeline (parser/analysis/codegen as needed).

### Port a Go analysis pass (example workflow)
1) Identify Go entrypoints and data structures in `pkg/analysis/<pass>.go`.
2) Define C structs (inputs/outputs) and ownership model.
3) Port algorithm into `csrc/analysis/<pass>.c`.
4) Create minimal tests from `pkg/analysis/<pass>_test.go`.
5) Wire into the C codegen pipeline and add regression tests.

### Vendor a lightweight dependency (example)
```
mkdir -p third_party/uthash
# Copy headers/sources
cp /path/to/uthash.h third_party/uthash/
cp /path/to/uthash/LICENSE third_party/uthash/LICENSE
```
Then record commit + URL in `third_party/README.md` and update `NOTICE`.

### Add a new runtime API and keep ABI stable
1) Add function declaration to `runtime/include/purple.h`.
2) Implement in `runtime/src/runtime.c`.
3) Add a test in `runtime/tests/`.
4) Add a compiler/runtime consistency check in `csrc/runtimecheck/`.

### Diagnostics example (proposed)
```
./build/purplec --diag=json examples/demo.purple > diag.json
./build/purplec --trace-parser examples/demo.purple
```

### Build cache example (proposed)
```
PURPLE_CACHE_DIR=.cache/purple ./build/purplec examples/demo.purple -o demo
```
