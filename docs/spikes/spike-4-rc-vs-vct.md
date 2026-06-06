# Spike 4 — Tier-B voxel GI: Radiance Cascades vs Voxel Cone Tracing

**Issue:** [#4](https://github.com/jtbirdsell/voxel2026/issues/4) · **Architecture:** Part III
§1 Tier B · **Status:** stage 4a complete (VCT cost drivers measured); stage 4b (radiance
cascades) pending

## Question

Is world-space 3D Radiance Cascades viable at horizon-scale view distances — the cascade-0
voxelization cost and ringing-fix overhead being the unproven parts — or is voxel cone tracing
the right de-risked Tier-B default? Decided by **measured cost drivers on real hardware**.

## Method (stage 4a — VCT)

Substrate (`src/vk/bench.*`): the spike-1 minigen world voxelized into a packed scene buffer,
uploaded device-local; an RGBA8 3D radiance volume with full mip chain (per-level storage views
+ trilinear sampled view); three Slang kernels (`vct_inject`, `vct_mip`, `vct_trace` — pinned
per docs/tooling/slang.md) with per-pass timestamp queries.

**Measurement validity (hardened by adversarial review — the first version had 3 blockers):**

- **Executed work is measured, not assumed**: the original kernel exited the unit volume after
  ~8 of 64 nominal steps (caught by a reviewer *simulating the march*), under-measuring the
  gather ~4.6×. The volume sampler now uses REPEAT addressing (cones march the infinitely tiled
  scene), and an instrumented untimed pass counts **actual executed steps/cone**, reported with
  every row.
- **Warm medians, not cold single-shots**: each row is the median of 8 warm iterations after a
  discarded warmup (timestamps masked to the family's `timestampValidBits`).
- **Comparable rows**: the synthetic surface sits at a fixed *absolute* voxel height (40, just
  above the minigen terrain+tree band) instead of a normalized height that changed geometry per
  extent.
- **Cost envelope, not one scene**: the table reports realistic content (minigen, occlusion
  early-out ON) and the fixed-work worst case (empty volume — early-out impossible, full cap
  marched by every cone).
- Passes are measured fully serialized (barrier-isolated), so totals are a conservative upper
  bound a real frame graph can partially overlap away. The closing timestamp at the compute
  stage measures dispatch completion (sync model verified in review — recorded so nobody
  "fixes" the non-bug).

Invariant tests (`tests/test_vct.cpp`, SKIP-graceful): mip averaging pinned on a **non-uniform
checkerboard** (every level analytically exact at 128 — the uniform case alone would pass under
averaging bugs); empty world gathers exactly zero; bright solid world bounded (the telescoping
occlusion series caps single-cone accumulation at 1/0.35 ≈ 2.86, under the asserted 4.0);
timing sanity.

## Results — stage 4a (RTX 4090, driver 610.47, 2026-06-06, median of 8 warm iterations)

2,073,600 gathers (1920×1080) × 6 cones, step cap 64:

| Row | inject | mips (all) | cone gather | **total** | steps/cone |
|---|---|---|---|---|---|
| minigen 64³, early-out ON | 0.004 | 0.013 | 1.18 | **1.19 ms** | 53.3 |
| minigen 128³, early-out ON | 0.018 | 0.017 | 1.42 | **1.46 ms** | 64.0 |
| minigen 256³, early-out ON | 0.136 | 0.038 | 1.45 | **1.62 ms** | 64.0 |
| **EMPTY 256³ (worst case), early-out OFF** | 0.136 | 0.037 | 1.45 | **1.62 ms** | 64.0 |

Numbers quoted to the precision the warm-median spread supports (~2 significant figures).

## Interim verdict (scoped to what 4a measured)

**VCT's cost drivers fit the Tier-B budget on Ada under static, isotropic, single-ring
assumptions**: the full-march worst case totals **~1.6 ms ≈ 23% of a 144 Hz frame** (or ~12% of
72 Hz) at 1080p-equivalent gather resolution. The gather dominates and is **step-count-bound and
content-insensitive at full march** (empty ≡ minigen at cap); it is **sub-linear in volume**
(+23% gather across a 64× volume increase at fixed step count — *not* volume-independent; the
original draft's "resolution-bound" claim was wrong and is retracted). Cheap knobs exist with
near-linear effect: step cap, cone count, and gather resolution (half-res ≈ 4× cheaper) — the
governor's levers.

**What this does NOT establish** (review-corrected scope): image quality, dynamic re-injection
cost, anisotropic voxels, multi-ring clipmaps, or bounce lighting — "viable as the default" is
decided at 4b, not here. Floor-tier (Turing/RDNA2) projection: the engine multiplier (est. 3–6×,
*author estimate, unsourced*) × hardware gap (est. 5–8×, *author estimate, unsourced*) compound
two unmeasured factors — treat the floor-tier picture as "likely tens of ms before knobs,
single-digit ms after half-res + reduced cones", an order-of-magnitude bracket, **not a budget
target**.

## Stage 4b (pending) — pre-registered comparison, symmetric by policy

Radiance-cascade passes on the same substrate, scene, and measurement discipline: cascade
ray-march cost per level, merge pass, total build cost. **Symmetry rule (review-corrected):**
both techniques are compared in their *static, un-multiplied* measured form, with known
unmeasured costs listed side-by-side (RC: ringing mitigation; VCT: dynamic re-injection,
anisotropy) rather than charged to one side. Tie-break favors VCT **as a declared de-risk
policy** (it is the architecture's incumbent default), not as a hidden thumb on the scale: RC
displaces it only by a clear measured advantage.
