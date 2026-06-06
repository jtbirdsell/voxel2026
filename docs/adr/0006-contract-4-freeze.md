# ADR-0006 — Freeze Contract 4: coordinates & origin rebasing

**Status:** Accepted
**Date:** 2026-06-06

## Context

Contract 4 ([docs/contracts/contract-4-coordinates.md](../contracts/contract-4-coordinates.md))
is consumed by every subsystem that holds a world-space position; parallel pillar work
(architecture.md Part V Phase 1) queues behind its freeze. Spike 2 (issue #2) delivered the
evidence the stub was waiting on: unconditional schedule determinism, Sterbenz-window
exactness bounds, the ulp-starvation failure mode, integer-only world translation, and two
review-surfaced engine obligations (int64 world indexing, {origin, local} pair comparisons).
The remaining open questions were v1 *value* choices, not mechanism unknowns.

## Options

1. **Freeze now on spike evidence, with v1 values and falsifiable revisit triggers** — pins the
   mechanism (measured) and names default numbers (threshold 2^14, fp32 locals, frame-wide
   origin) as RFC-changeable; unblocks consumers immediately. Risk: a v1 value proves wrong in
   engine-scale code — mitigated by the replay-harness gate that any change must rerun.
2. **Defer freezing until engine-scale code exists** — maximally informed values, but every
   consumer pillar blocks on the runtime pillar for months, defeating the contract system's
   purpose (Part IV froze contracts precisely so pillars can parallelize).
3. **Freeze the mechanism, leave values open** — a half-freeze; consumers cannot size precision
   budgets or buffer layouts without the numbers, so it unblocks little while looking done.

## Decision

Option 1. The frozen text distinguishes three layers explicitly: *invariants* (mechanism —
measured by spike 2, change requires re-running the determinism evidence), *v1 values*
(threshold/hysteresis/storage — RFC plus harness rerun), and *deferred decisions* (per-region
rebasing — re-opened only by the named falsifiable triggers).

## Exit strategy

The contract carries its own exit: every decision lists the evidence that would re-open it
(replay-harness drift > 10^-3 nodes/min from rebase rounding, a reproducible jitter report, a
workload needing fixed-point). Re-opening is an RFC; the spike-2 goldens and the prediction
replay harness gate any successor text, so a bad freeze cannot silently regress determinism.
