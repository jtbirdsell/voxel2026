# ADR-0009 — Freeze Contract 1: packed voxel blob layout

**Status:** Accepted
**Date:** 2026-06-06

## Context

Contract 1 ([docs/contracts/contract-1-voxel-blob.md](../contracts/contract-1-voxel-blob.md))
is the most-consumed contract in the program: the renderer reads it in-shader, scripting
snapshots copy it, persistence stores it compressed at rest. Phase-1 pillar work queues behind
it. Like Contract 3 it is a schema freeze with no spike dependency, anchored by a compiled
reference codec and a byte-level golden rather than prose alone. The two stub questions
(occupancy resolution, emissive channel) and the review-mandated repack-policy hook all
resolve structurally.

## Options

1. **Freeze the format now with a reference codec; pin extent as a v1 value** — the layout
   (palette entries, width set, Morton order, validation rules) is frozen byte-for-byte; chunk
   extent stays a parameterized value (16³ v1) with a measured revisit trigger. The
   valid-vs-canonical split makes deferred repack legal in the schema while keeping hysteresis
   policy in the engine.
2. **Freeze prose only, codec later** — the first GPU reader or converter would become the
   de-facto format definition; byte-level divergence (endianness, bit packing direction,
   palette order) would surface at integration, the most expensive place.
3. **Defer until the mesher exists** — serializes the world-data, renderer, and scripting
   pillars behind each other; Contract 1 exists precisely so they can proceed in parallel
   against one byte layout.

Layout choices considered inside option 1: 16-byte palette entries {content u32, flags u32,
state u64} (GPU-aligned, 4³ occupancy fits the state field exactly) vs 12-byte packed entries
(smaller but misaligned for in-shader reads) — 16 wins; index widths {0,1,2,4,8,16} (every
width divides 64: no word-straddling reads on GPU or CPU) vs arbitrary widths 1..16 (denser
for 3/5/6-bit palettes but straddling reads everywhere) — the power-of-two set wins; per-cell
inline state vs state-in-palette (Minecraft-model dedup; per-cell uniqueness pressure is
bounded by cell count, and 16³ caps the index width at 16 bits by construction) — palette wins.

## Decision

Option 1. The freeze distinguishes layout invariants (byte golden + property tests) from the
v1 extent value (RFC + perf-budget evidence to change) and from engine policy (repack
hysteresis), and resolves both stub questions at the layer they belong to: the state *width*
is blob schema (frozen, u64); the state and emissive *semantics* are content-registry data.

Consciously-recorded points from the freeze review:

- **"SoA" wording divergence**: the architecture draft called the layout "SoA"; the frozen
  layout is precisely *palette-indexed* (dense Morton per-cell index stream over AoS palette
  records). The architecture text is corrected to match; the per-cell stream is the dense
  array GPU paths scan, which is what the draft wording was reaching for.
- **Repack stepping policy is a conscious deferral, not a gap**: width stepping (through
  exactly the frozen width set, with hysteresis, bulk writes batched under one repack) is
  specified by the architecture and budgeted by Part VII's perf gates; the schema contributes
  the over-wide-validity hook that makes deferred repack legal. The named exit: when the
  engine's chunk-edit path lands (Phase 1), its repack policy is reviewed against this ADR
  and the Part VII read-unpack budget — divergence is an RFC, not a quiet reinterpretation.
- **Emissive LUT-upload scale validation is Phase-1 renderer work** — the spike-4 harness
  shows the shape (16-entry LUT), not registry scale; the freeze does not claim otherwise.

## Exit strategy

The version byte is the format's exit: any incompatible change bumps it behind an RFC, and the
converter owns migration (same posture as the 1.x world converter). Drift between contract
text, codec, golden, and tests fails CI by construction — the reference codec, not any later
GPU-side reimplementation, defines the format, so a divergent reader is a bug with a defined
arbiter rather than a fork.
