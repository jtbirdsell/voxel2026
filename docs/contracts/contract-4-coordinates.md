# Contract 4 — Coordinates & origin rebasing

**Owner:** runtime · **Consumers:** everything that holds a world-space position ·
**Status:** FROZEN 2026-06-06 ([ADR-0006](../adr/0006-contract-4-freeze.md)) — changes by RFC
only, gated on the replay harness

## Normative invariants

1. **World addressing is s32 node coordinates** (±2.1B per axis); chunk keys pack to 64 bits.
   Sort/order keys over signed coordinates MUST use sign-bit-flipped encoding — an
   architectural rule generalizing a measured spike-1 finding (raw u32 reinterpretation
   inverted mapgen anchor order at negative coordinates; the same trap exists in any signed
   key, so the rule is engine-wide by policy, measured so far in mapgen).
2. **Simulation/render float space is anchored to a streaming origin** (s32, snapped to 16-node
   chunk granularity) and rebased on threshold crossings. Server and client rebase identically —
   same thresholds, same schedule inputs — because rebase timing and deltas are part of the
   deterministic state machine (spike-2: identical schedules ⇒ bit-identical machines,
   unconditionally; differing schedules genuinely diverge).
3. **World translation by the origin is integer-only.** Translating world queries (chunk/node
   lookups) by an s32 origin MUST NOT pass through float arithmetic; spike-2 validated that the
   integer `WorldOffset` path cannot perturb FP trajectories (spike-3 goldens byte-identical
   through the change).
4. **World-index arithmetic is int64** wherever origin + cell indices combine; until a call site
   adopts int64 it MUST carry the interim `|origin| <= 2^30` precondition assert (int32
   origin+cell addition overflows otherwise — added in spike-2's review and validated by its
   harness; the assert ships in `src/sim/collision_step.hpp`).
5. **Mod-facing semantics:** the Lua API exposes *stable virtual absolutes* in s32 node space;
   rebasing is engine-internal and invisible to mods. Cross-boundary positions marshal as f64,
   exact at v1 ranges (|coordinate| ≤ 2^31 with ≥ 2^-9-node local resolution spans ≤ 40 of f64's
   53 mantissa bits). Engine paths that are *identity-sensitive* (replay comparison, anti-cheat
   re-simulation) MUST compare {origin, local} pairs, never re-derived single doubles — the
   single-double reconstruction is exact only while the origin↔local bit span fits 53 bits
   (spike-2 review finding).
6. **Atomicity:** every intra-process absolute-position cache (anti-cheat last-good-position,
   attachments, particles, audio anchors, queued network position commands) is offset inside the
   rebase critical section. The engine keeps an enumerated registry of these caches; adding one
   without registering it is a review-blocking defect.
7. **Honest precision language:** rebase subtraction is *guaranteed* bit-exact only inside the
   Sterbenz window (delta/2 ≤ local ≤ 2·delta, same sign); outside it, loss is deterministic and
   bounded by 0.5 ulp of the result. The contract guarantees **determinism always; losslessness
   only near the new origin** (spike-2 measured both).

## Decisions (previously open, resolved at freeze)

- **Rebase threshold & hysteresis (v1 values):** rebase when the local-space anchor (camera on
  the client, per-player on the server) exceeds **2^14 nodes** on any axis; the new origin
  re-centers on the anchor (chunk-snapped). Re-centering is the hysteresis: the next trigger
  requires another 2^14 nodes of travel, so flapping is structurally impossible. Rationale:
  |local| ≤ ~2^14 keeps float ulp ≤ 2^-9 nodes (~2 mm at 1 m nodes), three orders below the
  v1 step displacement cap (0.225 nodes); rebase cadence at sprint speed is ~one per 9 minutes
  of straight-line travel. These are v1 defaults, not physics: changing them is an RFC plus a
  replay-harness rerun.
- **Entity position storage:** frame-wide streaming origin + **fp32 local** per entity.
  Fixed-point storage is *rejected for v1* — no measured need; the replay harness is the gate
  that would surface one.
- **Frame-wide vs per-region rebasing:** **frame-wide** for v1, accepting the deterministic
  ≤ 0.5-ulp micro-rounding for out-of-window entities (measured, bounded, deterministic).
  Falsifiable revisit trigger: a replay-harness workload showing positional drift > 10^-3 nodes
  per simulated minute attributable to rebase rounding, or a visible-jitter report reproducible
  at v1 thresholds — either re-opens this decision by RFC (per-region restores losslessness
  everywhere at bookkeeping cost).

## Spike evidence binding this freeze

Spike 2 (`docs/spikes/spike-2-origin-rebasing.md`, issue #2, commit 90bf276): determinism
unconditional under identical schedules; Sterbenz-window exactness; deterministic bounded loss
outside; ulp-starvation freeze measured at |x| ≈ 2^22 (the disease rebasing exists to prevent);
integer-only translation validated against spike-3 goldens.

## CI gates

- The spike-2 rebase goldens (pinned + control-divergence variants) run in every build.
- Any change to rebase thresholds, the translation path, or position marshaling MUST rerun the
  cross-build prediction-replay harness (Contract 5's gate) — the two contracts share the
  determinism budget.
