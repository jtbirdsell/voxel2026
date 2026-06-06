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

## Open questions

- Rebase threshold and hysteresis values (precision budget vs rebase frequency).
- Entity position storage: {s32 cell + float offset} vs fixed-point — decide with the prediction
  replay harness in the loop.
