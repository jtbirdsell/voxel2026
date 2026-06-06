# ADR-0007 — Freeze Contract 5: shared collision step

**Status:** Accepted
**Date:** 2026-06-06

## Context

Contract 5 ([docs/contracts/contract-5-collision-step.md](../contracts/contract-5-collision-step.md))
underwrites client prediction and server re-simulation — the architecture's anti-cheat and
latency story both stand on it. Spike 3 (issue #3) proved the FP-determinism mechanism
cross-compiler with golden hashes and a diverging negative control; spike 2 (issue #2) bound
the integer WorldOffset interaction. The stub's two open questions (1.x semantic compatibility,
CCD policy) were policy choices, not unknowns.

## Options

1. **Freeze the interface + determinism mechanism now; pin CCD as a construction invariant**
   (`maxSpeed * dt < 0.5 node` on the predicted path) — tunneling becomes impossible rather
   than detected; 1.x divergences go to the porting guide. Risk: a future predicted-path
   feature needs higher speeds — handled by a named RFC trigger costed against substepping.
2. **Adopt 1.x `collisionMoveSimple` semantics bug-for-bug** — eases porting at the cost of
   freezing two decades of accreted edge cases into the deterministic TU, where every quirk
   becomes load-bearing forever. Rejected: the clean break exists to shed exactly this.
3. **Detect-and-correct CCD (swept tests with substepping) on the predicted path now** — more
   general, but adds branches and iteration counts to the code whose cross-build bit-identity
   is the product; no v1 feature needs it.

## Decision

Option 1. The speed-cap invariant moves CCD from runtime machinery to a precondition the server
enforces at `physics_override` application time — zero cost inside the deterministic TU.

## Exit strategy

Re-opening is an RFC with the cross-build prediction-replay harness as the non-negotiable gate
(it blocks any change to the step, its flags, or shared inline helpers). The negative-control
build keeps the mechanism testably load-bearing: if the control leg stops diverging, CI fails —
the freeze cannot rot invisibly.
