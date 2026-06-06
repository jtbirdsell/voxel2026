# Contract 2 — LOD aggregate pyramid schema

**Owner:** world data · **Consumers:** renderer (far-terrain clipmap), networking (far streaming),
persistence (own column family) · **Status:** Draft

## What this contract freezes

- Pyramid structure: sparse octree where level L aggregates 2^L³ chunks; per-node payload =
  dominant-material/coverage palette + occupancy/heightfield mask + content bounds + content hash.
- Generation: byproduct of mapgen (from the density field); re-derived on edit via dirty
  propagation up the pyramid (coalesced/throttled).
- Storage: keyed {level, morton} in the lod-aggregate column family; coarse levels persisted,
  fine levels regenerable (store-vs-regenerate policy per level).
- Consumption: the renderer uploads the same blob the network streams — one schema, two readers.
- Invalidation: per-node content hash; clients detect staleness and refetch.

## Open questions

- The genuine constraint-tension flagged in review: one blob must satisfy a GPU ray-march sampler
  *and* bandwidth-conscious network deltas — resolve with measurements, not assertion.
- Aggregation fidelity for thin/ornate geometry (downsampling must not erase silhouettes that
  matter).
