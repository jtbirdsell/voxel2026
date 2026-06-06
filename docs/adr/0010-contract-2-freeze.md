# ADR-0010 — Freeze Contract 2: LOD pyramid structure (payload stays measured)

**Status:** Accepted
**Date:** 2026-06-06

## Context

Contract 2 ([docs/contracts/contract-2-lod-pyramid.md](../contracts/contract-2-lod-pyramid.md))
is the shared schema between the far-terrain renderer and far-world streaming. Its stub carried
a review mandate unique among the contracts: the per-node payload must satisfy a GPU sampler
*and* network deltas, "resolve with measurements, not assertion" — and those measurements
require the Phase-2 renderer, which does not exist yet. A full freeze would violate the
contract's own bar; no freeze would block both consumers on each other.

## Options

1. **Split freeze: structure now, payload versioned-open** — topology, keys, and the Merkle
   hash discipline freeze with a reference codec and real-content tests; the payload carries
   its own version byte and an invariant that hashes derive from content only, so payload
   revision is structurally non-breaking. Consumers integrate against keys/hashes/v1-payload
   immediately; the measured payload decision lands in Phase 2 without an RFC against the
   structure.
2. **Freeze everything now** — asserts the payload layout the stub explicitly said must be
   measured; the likely outcome is an early RFC churning the one section consumers bind
   tightest to.
3. **Defer entirely** — far streaming and far rendering both queue behind Phase-2; the
   contract system exists to prevent exactly that serialization.

Structural choices inside option 1: Merkle hashing (parent = H(children)) vs per-node
independent hashes (no O(log) staleness discovery; clients would diff full node lists) —
Merkle wins, and the spike-1 minigen content makes it testable today; floor-division parents
with an 8-root octant forest vs offset-coordinate single root (offsetting signed space into
unsigned trades a clean fixed point for a global constant every consumer must agree on
forever) — the forest wins; 13-byte big-endian sign-flipped storage keys (lexicographic ==
numeric, zero custom-comparator code in any store) vs u64 packed Morton keys (cannot cover
±2^27 chunk coords with a level field in 64 bits) — the byte key wins.

## Decision

Option 1. The load-bearing seam is invariant 4 (identity tracks content, never payload): it is
what makes the split freeze honest rather than a half-freeze — every hash, key, and client
comparison survives any payload revision, so "payload stays measured" costs the structure
nothing.

## Exit strategy

Structure: RFC + the reference-codec/test update in one change (the Merkle test over minigen
content is the regression tripwire — 3-of-73 dirty nodes is a falsifiable fact). Payload:
bump `kLodPayloadVersion` behind the Phase-2 measurements; old readers reject newer versions
by construction (pinned by test), so a payload migration is a refetch, never a misread.
