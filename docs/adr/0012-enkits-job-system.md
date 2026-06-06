# ADR-0012 — enkiTS as the job-system substrate

**Status:** Accepted
**Date:** 2026-06-06

## Context

Architecture §2: no subsystem owns a thread; everything is tasks on a work-stealing scheduler,
migrated one pool at a time (meshing first — it never touches the env lock, per the round-2
review record). Issue #20 lands the substrate plus the first pool. Per Part VII, a new
dependency enters only through the supply-chain gate with an ADR.

## Options

1. **enkiTS, pinned v1.11** — small (~2 kLOC core), zlib-licensed, C++11, battle-tested in
   shipped games, pinned-task support for blocking I/O pools later. Tag cadence is slow (the
   GNS lesson: cadence is not activity — main is maintained); the library is small enough that
   a frozen upstream is absorbable.
2. **Taskflow** — richer graphs (dependencies, conditionals), heavier (~20 kLOC headers),
   MIT. The architecture needs parallel-for over pools far more than dataflow graphs; the
   richer surface is unused weight and a larger audit target at the gate.
3. **Hand-rolled `std::jthread` pool** — zero dependencies, but work-stealing schedulers are
   exactly the code one should not write casually under a TSan-gated quality bar; the saved
   dependency is paid for in owned concurrency bugs.

## Decision

Option 1: enkiTS via FetchContent, `GIT_TAG v1.11`, examples/C-interface off. The engine never
includes enkiTS headers outside `src/jobs/` — the `jobs::JobSystem` facade (pimpl) is the only
surface other code sees, and that boundary IS the exit strategy. Tracy zones for the task graph
land with the Tracy pin (its own supply-chain ADR), deliberately not smuggled in here.

## Validation note (2026-06-06, first TSan run)

The TSan CI leg's first pass over the scheduler flagged races between caller writes after
`parallelFor` returned and worker reads inside `fn` — enkiTS v1.11's wait path carries no
sanitizer-visible happens-before edge from worker execution to the waiter. Resolution, and a
facade-design principle worth keeping: **the facade owns its memory-ordering contract.**
`parallelFor` establishes its own release/acquire edge (per-partition release counts, acquire
check after the wait), so "returns happens-after every fn invocation" is enforced by our code
regardless of the dependency's internals — and a future scheduler swap inherits the guarantee.

## Exit strategy

The facade exposes exactly what the engine uses (currently: thread count + parallel-for).
Replacing enkiTS (Taskflow, a custom scheduler) is reimplementing one .cpp behind an unchanged
header; the parallel-meshing determinism test (parallel result bit-equal to serial) transfers
to any successor unchanged, so a swap cannot silently change results.
