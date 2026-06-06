# Contract 4 — Coordinates & origin rebasing

**Owner:** runtime · **Consumers:** everything that holds a world-space position · **Status:** Draft

## What this contract freezes

- s32 node coordinates for world addressing (±2.1B per axis); 64-bit packed chunk keys.
- Simulation/render float space anchored to a streaming origin, rebased on threshold crossings;
  **server and client rebase identically** (prediction determinism).
- **Mod-facing semantics (review-mandated):** the Lua API exposes *stable virtual absolutes* in
  s32 node space; rebasing is engine-internal and invisible to mods.
- Atomicity: all intra-process absolute-position caches (anti-cheat last-good-position,
  attachments, particles, audio anchors, queued network position commands) are offset inside the
  rebase critical section — enumerated and tested by the Phase 0 spike.

## Spike-2 findings (2026-06-06 — bind these into the freeze)

- **Identical schedules ⇒ bit-identical machines, unconditionally** — float subtraction is
  deterministic whether or not it is exact; rebase timing/deltas are part of the deterministic
  state machine (the never-rebasing negative control genuinely diverges in virtual space).
- **Losslessness is per-entity and positional**: rebase subtraction is *guaranteed* bit-exact
  inside the Sterbenz window (delta/2 ≤ local ≤ 2·delta, same sign); outside the window
  exactness is not guaranteed (though often still occurs), and any loss is **deterministic and
  bounded by 0.5 ulp of the result**. Spread populations always include out-of-window entities.
  The contract must say "deterministic always; losslessness guaranteed only near the new
  origin" — not "lossless".
- **Two engine-phase items surfaced by review**: (a) world-index arithmetic needs an int64 path
  (or a |origin| ≤ 2^30 precondition) before contract-scale s32 origins are legal — int32
  origin+cell addition overflows otherwise; (b) virtual-absolute reconstruction as a single
  double sum is exact only while the origin↔local bit span fits 53 bits — at s32-scale origins,
  compare {origin, local} pairs or origin-relative diffs, never one double.
- World-query translation by the origin must be **integer-only** (validated: cannot perturb FP
  trajectories; spike-3 goldens byte-identical through the change).
- The far-origin disease is ulp starvation (motion freezes when per-step displacement < ulp of
  position) — measured at |x| ≈ 2²² with the kernel's step sizes.

## Open questions

- Rebase threshold and hysteresis values (precision budget vs rebase frequency).
- Entity position storage: {s32 cell + float offset} vs fixed-point — decide with the prediction
  replay harness in the loop.
- Frame-wide vs per-region rebasing (per-region restores losslessness everywhere at bookkeeping
  cost; frame-wide accepts deterministic micro-rounding for distant entities).
