# Contract 1 — Packed voxel blob layout

**Owner:** world data · **Consumers:** renderer (mesher/ray-marcher read it in-shader), scripting
(packed-snapshot buffers), persistence (compressed at rest) · **Status:** FROZEN 2026-06-06
([ADR-0009](../adr/0009-contract-1-freeze.md)) — changes by RFC only

## Normative invariants

1. **Blob = header + palette + bit-packed index stream, little-endian throughout**, exactly as
   laid out in `src/world/voxel_blob.hpp` and pinned byte-for-byte by the golden in
   `tests/test_voxel_blob.cpp`. Header: magic `VXB1`, version, log2Extent, indexBits,
   paletteCount, reserved bytes (writers zero, readers ignore — the forward-compatibility rule,
   also pinned by test). Any future format extension that gives reserved bytes meaning MUST
   bump the version byte — old readers then reject rather than misread — and consumers that
   cache decoded blobs key their caches on the version byte.
2. **Palette entry = 16 bytes `{content u32, flags u32 (reserved), state u64}`** — one entry per
   distinct *(content, state)* value pair; cells are palette references, never inline values.
   `content` is the Contract 3 u32 id. The **state field width (u64) is frozen**; it holds a
   4³ microblock occupancy mask exactly (or 2³ in 8 bits, or orientation/level packings). The
   state *schema per content type* is registry data — a consumer concern, not blob layout.
3. **Index widths ∈ {0, 1, 2, 4, 8, 16} bits**; width 0 = uniform chunk (no stream). Every legal
   width divides 64, so entries never straddle the u64 stream words. Cell *i* occupies bits
   `[i·w, (i+1)·w)` of the stream, little-endian within each word.
4. **Cell order is Morton** (x → bit 0, per `src/voxel/morton.hpp`), which makes every aligned
   8³ brick a contiguous 512-cell run *by construction* (pinned by test: the brick base code is
   512-aligned) — the brick granularity GPU paths consume without any second ordering.
   Terminology note (review finding): the architecture draft said "SoA"; the frozen layout is
   precisely *palette-indexed* — the dense per-cell array is the index stream, palette entries
   are AoS records. The per-cell stream is what GPU paths scan densely; the divergence from the
   draft wording is recorded in ADR-0009, and the architecture text now matches.
5. **Valid ≠ canonical (the repack hysteresis hook):** a blob is VALID with any width whose
   range covers paletteCount — over-wide blobs are legal, so in-place edits need not repack
   instantly. The CANONICAL form — minimal width, first-appearance palette order, no duplicate
   or unused entries — is what `packVoxelBlob()` emits and what persistence/network hash;
   canonicalization happens at serialization points. *When* the engine re-canonicalizes is
   engine policy outside the schema — but the policy is not unspecified: the architecture's
   mandate stands as its spec (width grows in steps through exactly this contract's width set;
   bulk writes batch under one repack; the read-unpack budget sits in Part VII's perf gates),
   and ADR-0009 records the deferral consciously with that named pin. The schema's contribution
   is making deferred repack legal. Canonical re-pack of an unpacked blob is byte-deterministic
   (pinned by test).
6. **Byte-identity guarantee** (scoped per the architecture's own review correction): the blob
   bytes are the same object CPU-side and GPU-side — the in-shader reader consumes
   header/palette/stream directly, no per-frame transcode. Disk holds the *same layout*; the
   at-rest transform is persistence-layer POLICY, not blob schema: the codec election (zstd is
   the architecture's standing answer) is deferred per ADR-0013 with a named trigger, and the
   v1 default store writes the layout uncompressed — palette compression already does the
   heavy lifting (a uniform 16³ chunk is 32 bytes). What this invariant freezes is layout
   identity through any at-rest transform: one decode on load, canonical thereafter.
7. **Full validation on every untrusted read**: magic/version/extent/width/count rules, exact
   blob size, and index-range checks (`unpackVoxelBlob` over the whole stream; `blobCellAt`
   for the cell touched) — nullopt on any violation, never partial output. Random access is
   O(1) reads without unpacking (the read-unpack perf budget's structural floor).

## Decisions (previously open, resolved at freeze)

- **Chunk extent (v1 value): 16³** (`log2Extent = 4`); the format itself is extent-parameterized
  (1..5, bounded by the Morton codec's 5 bits/axis), so this is a value, not a layout. Rationale:
  1.x interop (MapBlock-sized units through the converter), small palettes stay narrow (4096
  cells cap the index width at 16 bits with headroom), and the spike-1 generator already
  produces 16³. Falsifiable revisit trigger: engine-scale evidence that per-chunk overhead or
  GPU dispatch under-occupancy dominates (the spike-4 benches already showed small dispatches
  under-occupying the 4090 — watch item, not yet chunk-size evidence), measured against the
  Part VII perf budgets — re-opens the value (32³ fits the same format) by RFC.
- **Microblock occupancy resolution (2³ vs 4³):** resolved at the layer it belongs to — the
  frozen u64 state width holds either (4³ = 64 bits exactly); which resolution a drawtype uses
  is content-registry schema the mesher consumes. The blob never needs to know.
- **Emissive channel:** derived at upload from palette + content registry — emissive is a
  function of the *(content, state)* pair, i.e. of the palette entry, so a per-cell in-blob
  array would duplicate palette-determined data. Evidence honesty (review-corrected): the
  spike-4 harness is *shaped* this way (a content→emissive LUT feeding the inject pass) but
  with a 16-entry hand-coded palette — it shows the shape, not the scale; validating the
  registry-sized LUT-upload path is named Phase-1 renderer work, not something this freeze
  claims as demonstrated. Falsifiable revisit trigger: a feature needing per-cell emissive
  that is NOT a function of (content, state) — e.g. baked per-cell light values — re-opens
  this by RFC as a new blob section behind a version bump.

## Reference codec (the compiled anchor)

`src/world/voxel_blob.hpp`: canonical packer, fully-validating unpacker, O(1) random access.
`tests/test_voxel_blob.cpp`: byte-level golden (every offset and the endianness), seeded-PRNG
round-trips across every width boundary (1→0 bits through 4096→16 bits), random-access
agreement, brick-contiguity pin, malformed-input rejection (truncations at every structural
boundary, field corruption, out-of-range stream indices), reserved-byte tolerance, the
valid-but-not-canonical over-wide case, and canonical re-pack determinism.

## CI gates

- `tests/test_voxel_blob.cpp` runs in every build (all legs, both control variants).
- Changing any header field, entry layout, width set, cell order, or validation rule is an RFC
  that must update the contract text, the reference codec, the byte golden, and the version
  byte in the same change. The GPU in-shader reader (Phase 1) must be validated against blobs
  produced by `packVoxelBlob()` — the reference codec, not a parallel implementation, defines
  the format.
