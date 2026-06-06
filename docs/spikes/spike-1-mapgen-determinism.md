# Spike 1 — Parallel mapgen determinism + the seam-ownership rule

**Issue:** [#1](https://github.com/jtbirdsell/voxel2026/issues/1) · **Architecture:**
Part III §3 "Mapgen" · **Status:** **PASS** (2026-06-06, commit `9cd5436`,
[run on the full matrix](https://github.com/jtbirdsell/voxel2026/actions) — all 7 jobs green:
identity at 1/4/32 threads + shuffled orders on MSVC/GCC/Clang × Debug/Release, golden
reproduced cross-platform, scatter controls diverging, oracle matching on every chunk)

## Question

Can a staged, parallel chunk generator produce **bit-identical world output regardless of worker
count and scheduling order** when structures span chunk boundaries — the property the
architecture's 1/4/32-worker CI gate demands — and is the **gather ownership rule** (each chunk
*pulls* from every structure that can reach it) the design that delivers it?

## Method

1. **Minigen** (`src/gen/minigen.{hpp,cpp}`): a miniature staged generator — integer-only, so
   determinism is purely structural (no FP environment involved, unlike spike 3). Stage A
   terrain and the structure-anchor predicate are pure functions of (seed, world column).
   Stage B applies **gather**: generating chunk C enumerates every anchor column in C's xz
   extent widened by the structure reach (`kReachXZ < kGenChunk`, enforced by `static_assert` —
   a 1-ring window provably suffices) and applies each reaching structure's cells in **canonical
   anchor order** (z-major, x-minor ascending; trunk emitted after canopy so a trunk wins its own
   cells; later anchors win contested cells). Chunk content is therefore a **pure function of
   (seed, chunkPos)** — there is no cross-chunk communication to order. Stage C is a
   snapshot-isolated decoration hook that receives *only its own chunk's buffer* (the async
   `on_generated` analog: structurally incapable of cross-chunk access).
2. **Scatter negative control** (`generateRegionScatter`): Luanti-style first-writer-wins —
   chunks generate in a caller-given order; each chunk's own anchors *push* structure cells
   region-wide; a chunk's terrain pass clobbers any spill pushed into it earlier, so spills into
   later-generated chunks are lost while spills into earlier-generated chunks persist. Output is
   schedule-dependent **by design** — this models the disease (Luanti's
   `blitBackAll(overwrite_generated=false)` seam behavior) that gather cures.
3. **Shared instrumentation**: one `forEachStructureCell` feeds gather, scatter, *and* the
   coverage statistics; one canonical FNV-1a-64 hash (cells x-fastest/y/z; chunks
   cx-fastest/cy/cz) hashes both modes — any gather-vs-scatter difference is attributable to
   chunk *content* only.
4. **Experiment** (`tests/test_minigen.cpp`, 11 test cases):
   - Gather region hash identical at **1, 4, and 32 worker threads** (atomic work-index pool;
     each chunk buffer written by exactly one worker) and under **3 shuffled work orders**.
   - **Purity**: every one of the 256 chunks generated standalone equals its in-region value.
   - **Non-vacuity** (spike-3 lesson — prove the hard case exists *before* trusting green):
     the pinned standard region must contain **≥ 10 cross-chunk structures and ≥ 1 same-cell
     conflict**, computed from the pure coverage functions.
   - **Controls**: scatter forward-vs-reverse hashes differ; scatter differs from gather on the
     same seed.
   - **Golden region hash** committed (`0xE1694C3CD43FD13A`); boundary seeds (0, UINT64_MAX)
     must not degenerate; decoration hook idempotence.

## Why gather works (the purity argument)

A chunk's cells are computed exclusively from pure functions of (seed, coordinates): terrain
height, anchor predicate, structure shape, and a *coordinate-canonical* application order. No
step reads "has chunk X been generated?", any mutable neighbor state, or any scheduling fact.
Schedule-independence is therefore not an emergent property to hope for — it is a **type-level
consequence of the data flow**, and the tests pin it empirically (and pin it against regression
by golden hash). The cost is bounded re-evaluation: each chunk re-hashes a
(16+2·2)² column window — pure arithmetic, no synchronization, embarrassingly parallel.

## Success / failure criteria

- **Pass:** all gather identity tests green at every thread count/order on the full CI matrix
  (the existing 6 pinned legs run them in both Debug and Release), AND both scatter-control
  divergence tests green (falsifying power), AND non-vacuity thresholds met on the pinned seed.
- **Fail (gather diverges):** the ownership rule as specified does not deliver
  determinism-by-construction — Architecture §3's mapgen design needs rework.
- **Fail (controls match):** the experiment is vacuous (no real cross-chunk pressure) — fix the
  region/seed, do not trust green.

## Adversarial review (pre-land)

Three-lens review (determinism/design, C++ correctness/portability, test rigor): **6 findings
(2 major, 4 minor), all applied**:

- **Major — `anchorOrderKey` inverted at the negative gather margin** (raw u32 reinterpretation
  sorted column −2 *after* column 0; an engineer transferring the exported rule into the engine
  would have reintroduced schedule-dependence at every chunk window crossing x=0/z=0). Fixed
  with a sign-bit-flipped order-preserving encoding valid over the full signed domain + negative
  test cases.
- **Major — the golden was minted from the code it checks**: a too-narrow gather window or wrong
  application order was unfalsifiable. Fixed with an **independent oracle test**: a brute-force
  reference path that re-states terrain locally, scans ALL anchors with *no window reasoning*,
  and applies them **sorted by `anchorOrderKey`** — production gather must match it on every
  chunk, making window sufficiency, conflict order, and the key itself load-bearing.
- Minor: tautological purity test (now compares standalone vs a *threaded, shuffled* region
  run); a load-bearing comment claiming a then-false key equivalence (now true and documented);
  decoration test only checked idempotence (now asserts the exact diff is GRASS→FLOWER with ≥1
  change, plus idempotence).

## Results (local — CI verdict pending)

- Plain Release (MSVC): **29/29 tests green** (17 pre-existing + 12 new, including the
  independent oracle over all 256 chunks).
- **ASan, whole-program** (MSVC `/fsanitize=address`, dedicated `VOXEL2026_SANITIZE=address`
  tree): **29/29 green**, including the 32-thread gather runs — no memory errors.
- Golden minted and reproducing (unchanged by the review fixes — the production content path
  was already correct; the fixes hardened the *exported rule* and the *evidence*); scatter
  controls diverge as required; non-vacuity stats well above thresholds.

## Honest limitations

- Minigen is a stand-in: real mapgen adds caves/ores/biomes/liquids, Lua hooks, and far larger
  structure inventories. The *rule* validated here (pure-function gather + canonical conflict
  order + bounded reach windows) is the transferable artifact; per-stage costs are not.
- Thread counts are exercised in-process with `std::jthread`; the engine's job system will
  re-run this gate on its own scheduler when it lands.
- **TSan is pending**: unavailable on Windows/MSVC. The gather write pattern is disjoint by
  construction (one worker per chunk buffer) and ASan-clean; a follow-up issue adds Linux
  ASan/UBSan/TSan CI legs (deviation resolved per `.ultrabuild/stakeholder.md` — local
  sanitizer option ships now, CI legs follow as separate maintenance).
- Snapshot isolation for Stage C is enforced structurally (signature receives one chunk), which
  is the architecture's actual proposal for async `on_generated`; a Lua-sandbox enforcement test
  belongs to the engine phase, not this spike.
