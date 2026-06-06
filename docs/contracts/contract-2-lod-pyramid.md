# Contract 2 — LOD aggregate pyramid schema

**Owner:** world data · **Consumers:** renderer (far-terrain clipmap), networking (far streaming),
persistence (own column family) · **Status:** FROZEN 2026-06-06 (structural schema;
[ADR-0010](../adr/0010-contract-2-freeze.md)) — the per-node payload is a versioned section
explicitly held open for Phase-2 measurement, per this contract's own review mandate

## Normative invariants (structural — frozen)

1. **Topology:** sparse octree over signed chunk space; a level-L node covers 2^L³ chunks,
   level 0 = one chunk. Parent semantics are **floor division** per axis (arithmetic shift —
   truncation would merge the sign octants at the boundary). Floor-halving makes {−1, 0} a
   fixed point, so the pyramid tops out as a **forest of 8 sign-octant roots at level 28**
   (chunk coords span ±2^27 from Contract 4's s32 node space); client staleness checks start
   from 8 root hashes. Pinned by `lodParent`/`lodChild`/`lodOctantOf` round-trip tests across
   the sign boundary.
2. **Node key:** `{level u8, x s32, y s32, z s32}` (coords at that node's level). **Storage
   key = 13 bytes** `[level][x][y][z]`, each coordinate sign-bit-flipped and **big-endian**, so
   plain lexicographic byte order equals (level, x, y, z) numeric order — per-level spatial
   grouping in the column family with zero comparator code. Deliberate contrast with the
   Contract-1 blob's little-endian data bytes: this is an *order key*, not data (pinned by an
   ordering test across the sign boundary).
3. **Hash discipline (Merkle):** H = FNV-1a-64 (standard offset/prime, pinned against published
   test vectors). Level-0 node hash = H(the chunk's **canonical** Contract-1 blob bytes) —
   canonical form is what hashes, per Contract 1. Level-L hash =
   H(`[level u8][child presence mask u8][present children's hashes, octant order, u64 LE]`);
   absent children contribute only
   through the mask (pinned by test: varying an absent child's hash cannot change the parent).
   Consequence, the property the whole streaming design leans on: **a chunk edit re-hashes
   exactly its ancestor chain** — 29 nodes, O(log) staleness discovery root-down (pinned by the
   minigen Merkle test: one cell edit in a 73-node pyramid changes exactly 3 hashes).
4. **Identity tracks content, never payload.** Hashes are computed from canonical chunk bytes
   and child hashes only — the aggregate payload can be regenerated, revised, or re-versioned
   without invalidating any hash, key, or client cache-comparison. This is the seam that lets
   the payload stay measurement-driven while the schema freezes.
5. **Generation/invalidation frame:** aggregates are a mapgen byproduct, re-derived on edit via
   the dirty chain (coalesced/throttled by engine policy); coarse levels persisted, fine levels
   regenerable (store-vs-regenerate boundary per level is engine policy within this frame);
   the renderer uploads the same bytes the network streams — one schema, two readers.

## v1 payload (versioned — deliberately NOT load-bearing)

`LodPayloadV1` (25 bytes LE: version, contentHash u64, occupancy4 u64 — the same 4³-in-u64
width discipline as Contract 1 state — dominantContent u32, solidCells u32). It exists so
consumers have a concrete v1 to integrate against; its own version byte means revision never
touches keys or hashes (old readers reject newer versions, pinned by test). **The stub's
review mandate stands as the revision condition:** one blob must serve a GPU ray-march sampler
*and* bandwidth-conscious network deltas, and that tension is resolved **with Phase-2
measurements, not assertion** — the payload section is where those measurements land.

## Decisions (previously open, resolved or scoped at freeze)

- **GPU-vs-network payload tension:** not decided here, by design — the structural freeze makes
  payload revision cheap (invariant 4), which is the honest resolution available before
  measurements exist. Falsifiable revisit trigger: the Phase-2 far-terrain renderer's first
  measured bandwidth/sample-rate numbers against the Part VII budgets.
- **Aggregation fidelity for thin/ornate geometry:** a renderer-quality criterion on the
  *generator* of payloads, not on this schema; tracked to the Phase-2 LOD work. The schema's
  contribution: payload versioning means a fidelity-driven payload change is non-breaking.

## Reference codec (the compiled anchor)

`src/world/lod_pyramid.hpp`: key navigation (floor semantics), 13-byte storage key, FNV-1a-64
(published vectors), parent-hash composition (with the invariant-4 tripwire documented at the
hash functions), dirty chain, v1 payload codec. `tests/test_lod_pyramid.cpp`, by invariant:

1. *Topology (invariant 1)* — floor-semantics parents at the sign boundary,
   `child(parent(k), octantOf(k)) == k` sweeps across signs, the 29-node dirty chain, and the
   octant-forest fixed point (root `{28, −1, 0, −1}` computed, parent-of-root rejected).
2. *Storage key (invariant 2)* — lexicographic byte order equals (level, x, y, z) numeric
   order across the sign boundary.
3. *Hash function (invariant 3)* — FNV-1a-64 pinned against three published vectors.
4. *Hash composition (invariant 3)* — absent children influence the parent only through the
   mask; present-child and level sensitivity.
5. *Merkle end-to-end (invariants 3+4)* — real spike-1 minigen content through the canonical
   Contract-1 packer: rebuild determinism, then a one-cell edit changing exactly the 3-node
   ancestor chain in a 73-node pyramid with all 70 others bit-identical.
6. *Payload (versioned section)* — round-trip, future-version rejection, size rejection.

## CI gates

- `tests/test_lod_pyramid.cpp` runs in every build, all legs.
- Changing the topology, key encoding, hash function, or hash composition is an RFC updating
  contract text, codec, and tests in one change; payload revisions bump `kLodPayloadVersion`
  and may not alter any hash input (invariant 4 is the review tripwire).
