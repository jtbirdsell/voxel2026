# Contract 1 — Packed voxel blob layout

**Owner:** world data · **Consumers:** renderer (mesher/ray-marcher read it in-shader), scripting
(packed-snapshot buffers), persistence (compressed at rest) · **Status:** Draft

## What this contract freezes

- Chunk dimensions (16³) and brick granularity (8³); Morton bit-order within the chunk
  (x → bit 0, y → bit 1, z → bit 2 — already enforced by `src/voxel/morton.hpp` and its tests).
- Per-chunk palette format: entry encoding (u32 global content ID), adaptive index widths
  (1/2/4/8/16-bit), header layout the GPU reads in-shader.
- **Index-width growth & repack policy** (review-mandated): width grows in steps with hysteresis;
  bulk writes batch under one repack; read-unpack cost has a perf budget.
- In-cell typed state field width, including the **fixed-width microblock occupancy mask** —
  GPU-visible state lives in the blob; the CPU-only side-table never holds anything the renderer
  reads per frame (ownership + precedence rule).
- Byte-identity guarantee: CPU ↔ GPU identical; disk = same layout compressed at rest (one
  decompress on load).

## Open questions

- Exact occupancy resolution (2³ vs 4³) and its interaction with the mesher's special drawtypes.
- Emissive channel: in-blob array vs derived from palette + content registry at upload.
