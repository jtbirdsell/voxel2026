# Vulkan bring-up (Phase 0 deliverable; spike-4 prerequisite)

**Status:** complete (2026-06-06) — runtime loading, device bring-up, capability probe,
headless compute with GPU/CPU parity and timestamps, **and the Slang toolchain slice**: the same
kernel compiled by slangc v2026.10.2 (pin: docs/tooling/slang.md) runs bit-exact on the 4090
beside the glslang compilation (both MATCH; Slang's SPIR-V ~19% smaller, 832 vs 1032 bytes),
reflection JSON validated, and mesh/task/vertex/fragment stage emission **checked compile-only**
(committed probe `.spv` artifacts; not yet executed on hardware — that transfers to the first
real mesh kernel). ADR-0002 is **Accepted** on this evidence.

## What exists

- `src/vk/device.*` — dynamic runtime loading (`vulkan-1.dll`/`libvulkan.so` at first use; no
  SDK or link-time dependency, so every host builds this and driverless hosts degrade to a
  reported "unavailable"), instance + discrete-GPU selection, capability report keyed to the
  architecture's tier flags (extension presence probed by NAME, so header vintage is not
  load-bearing), logical device + compute queue.
- `src/vk/compute.*` + `src/vk/shaders/parity.comp(.spv)` — headless compute path: storage
  buffer, pipeline from embedded SPIR-V (configure-time hex embed; the GLSL is the artifact of
  record, regeneration command in its header), dispatch bracketed by timestamp queries, host
  readback, bit-exact comparison against the shared `parityMix` CPU mirror.
- `tools/vk_probe` — prints the capability report + dispatch result.
- `tests/test_vk.cpp` — SKIP-graceful: hosts without a runtime/device SKIP visibly (the
  documented Part VII pattern: flagship-tier verdicts come from developer hardware until GPU
  runners exist); on GPU hosts the parity and sanity tests are hard requirements.

## Recorded capability report — the reference flagship (2026-06-06)

```text
device:                 NVIDIA GeForce RTX 4090 (discrete)
apiVersion:             1.4.341
driverVersion (raw):    0x988BC000        (NVIDIA 610.47)
compute queue family:   0
timestampPeriod (ns):   1.000
descriptor_indexing:    yes
descriptor_buffer:      yes
descriptor_heap:        yes               <- the pre-KHR Jan-2026 extension, live
mesh_shader:            yes
shader_object:          yes
ray_query:              yes
acceleration_structure: yes
timeline_semaphore:     yes
first dispatch:         2^20 u32 elements, GPU/CPU parity MATCH, 0.5588 ms
```

Every tier flag the architecture's renderer design (Part III §1) keys on is present on the
reference machine — including the descriptor-strategy fast path (`descriptor_buffer` AND
`descriptor_heap`) and the full flagship set (mesh shaders + ray query). The "floor =
Turing/RDNA2, fast paths gated by capability" tiering is therefore exercisable end-to-end on
this hardware the moment renderer work starts.

## Notes & boundaries

- First-dispatch timing (0.56 ms for a trivial 4 MiB pass over host-visible memory) is a
  smoke number, not a benchmark — it includes host-visible memory traffic and cold pipeline
  state. Spike-4's RC/VCT measurements will use device-local memory and warmed pipelines.
- The capability probe reports extension PRESENCE; feature-struct negotiation (what subset of
  each extension is actually usable) belongs to the renderer phase.
- CI behavior (review-corrected — the SKIP must be a contract, not an image accident): builds
  everywhere (headers via pinned FetchContent, `${CMAKE_DL_LIBS}` for older glibc). The
  sanitizer and determinism-control legs set `VOXEL2026_VK_DISABLE=1`, which the Device honors
  before touching any driver — so a future runner image that ships lavapipe can never silently
  run the Mesa ICD under ASan/TSan. On the plain pinned legs the tests SKIP where no loader
  exists and are *permitted* to run if a software device appears (integer parity is
  ICD-agnostic). The SKIP path is design-verified locally via the env var; per-leg ctest logs
  are the evidence of which path each runner took.
