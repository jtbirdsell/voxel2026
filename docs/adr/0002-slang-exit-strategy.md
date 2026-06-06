# ADR-0002 — Slang as shader toolchain, with exit strategy

**Status:** Accepted (2026-06-06) — with one standing condition, see Validation
**Date:** 2026-06-05

## Context

The renderer (architecture.md §1) authors all shaders in Slang, compiled offline to SPIR-V with a
shader cache; feature flags become specialization constants. Round-2 adversarial review confirmed
Slang is a **single point of failure** for the entire render pipeline and that the original
justification (glslang HLSL-frontend deprecation) was a non-sequitur — the choice must stand on
its own merits, with a real exit plan.

## Options

1. **Slang, pinned, with DXC/GLSL fallback path** — Khronos-governed (Nov 2024), in the Vulkan
   SDK, shipping in Valve Source 2; write-once targeting SPIR-V today and Metal IR/WGSL if
   backends arrive. Risk: fast-moving compiler; mesh/task-stage tooling and reflection maturity
   unproven for this codebase.
2. **GLSL + glslang** — boring, proven, but locks shaders to Vulkan-GLSL and reintroduces the
   string-#define specialization the architecture retires.
3. **HLSL + DXC** — mature SPIR-V backend, but glslang's HLSL path is deprecated and HLSL is a
   worse fit for a Khronos-aligned open project.

## Decision

Option 1: Slang, **version-pinned** through the supply-chain gate (validated against
**slangc v2026.10.2**, official shader-slang/slang release binaries).

## Validation (2026-06-06 — the acceptance gate, met)

Toolchain pin (release tag, asset, SHA-256, regeneration commands):
[docs/tooling/slang.md](../tooling/slang.md).

- **Compute, executing on hardware**: `src/vk/shaders/parity.slang` compiled by slangc to
  SPIR-V (committed `parity.slang.spv`, 832 bytes vs glslang's 1032 — ~19% smaller, for what a
  23-line kernel's size is worth) runs on the RTX 4090 with **bit-exact GPU/CPU parity**, side
  by side with the glslang kernel, in the test suite (`tests/test_vk.cpp`).
- **Reflection**: `-reflection-json` emits correct binding metadata
  (`parity.slang.reflection.json`, committed: set/slot 0, readWrite structuredBuffer<uint32>,
  entry/stage) — the extraction the bindless/MDI paths will consume.
- **Mesh-stage emission (compile-only)**: `meshprobe.slang` ([shader("mesh")]) → committed
  `meshprobe.spv` (SPV_EXT_mesh_shader) — a reader can `spirv-val` the artifact rather than
  trust a byte count.
- **Vertex / fragment / amplification (task) emission (compile-only)**: `stageprobe.slang`
  (three entries, one module) → committed `stageprobe.spv`.
- **Standing condition**: pipeline-level mesh/task validation (an actual mesh pipeline executing
  on the 4090) transfers to the first real mesh-shader kernel — emission alone is accepted as
  the toolchain gate, execution as the renderer gate. Only the compute stage has *executed* on
  hardware so far.

## Exit strategy

Shaders are kept structurally portable: specialization-constant feature flags and reflection-driven
binding (no Slang-only language features without a recorded exception). If Slang stalls, the exit
is mechanical translation to GLSL/HLSL + glslang/DXC of ~the shader corpus, costed at the time of
exit; the offline-compile + cache architecture is toolchain-agnostic by design.
