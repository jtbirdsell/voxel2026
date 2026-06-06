# Contract 5 — Shared collision step

**Owner:** runtime · **Consumers:** networking (prediction + server re-simulation), scripting
(movement-affecting APIs) · **Status:** Draft

## What this contract freezes

- One collision/movement step function compiled into both client and server binaries — the
  authority path and the prediction path are the same code.
- **FP determinism mechanism (review-mandated — identical source is necessary, not sufficient):**
  the step builds in an isolated translation unit with `-ffp-contract=off`, no fast-math, no
  autovectorized reductions, and vendored deterministic implementations of the libm calls it
  makes. **Plus two preconditions the compile flags cannot deliver (spike-3 review findings):**
  (a) **FTZ/DAZ off is runtime state** — the engine pins MXCSR (x86) / FPCR.FZ (ARM) on every
  simulation thread; (b) header-inline helpers shared with unpinned TUs must contain no
  contractable/reassociable expressions (COMDAT folding can pick an unpinned instantiation).
  The determinism CI gate includes a **contraction-enabled negative control** that must diverge,
  so the mechanism stays testably load-bearing.
- Parameter sequencing: `physics_override` changes, attach/detach, and teleports carry the input
  sequence number at which they take effect; attached entities suspend prediction.
- The cross-build prediction-replay harness (recorded inputs → bit-identical positions on client
  and server builds, across the supported OS/compiler matrix) is a **blocking CI gate** on any
  change touching this step or FP flags.

## Open questions

- Exact step semantics vs Luanti 1.x `collisionMoveSimple` (document divergences in the porting
  guide rather than bug-for-bug compatibility).
- CCD policy for the predicted path (the Jolt backend handles non-predicted CCD).
