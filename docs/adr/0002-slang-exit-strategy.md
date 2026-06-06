# ADR-0002 — Slang as shader toolchain, with exit strategy

**Status:** Proposed
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

## Decision (proposed)

Option 1: Slang, **version-pinned** through the supply-chain gate. The Phase 0 Vulkan bring-up
spike must validate vertex/fragment/compute *and* mesh/task-stage emission plus reflection before
the decision is marked Accepted.

## Exit strategy

Shaders are kept structurally portable: specialization-constant feature flags and reflection-driven
binding (no Slang-only language features without a recorded exception). If Slang stalls, the exit
is mechanical translation to GLSL/HLSL + glslang/DXC of ~the shader corpus, costed at the time of
exit; the offline-compile + cache architecture is toolchain-agnostic by design.
