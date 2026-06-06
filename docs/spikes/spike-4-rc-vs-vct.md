# Spike 4 — Tier-B voxel GI: Radiance Cascades vs Voxel Cone Tracing

**Issue:** [#4](https://github.com/jtbirdsell/voxel2026/issues/4) · **Architecture:** Part III
§1 Tier B · **Status:** COMPLETE — stages 4a (VCT) and 4b (radiance cascades) both measured;
verdict below: **VCT remains the Tier-B default** per the pre-registered tie-break

## Question

Is world-space 3D Radiance Cascades viable at horizon-scale view distances — the cascade-0
voxelization cost and ringing-fix overhead being the unproven parts — or is voxel cone tracing
the right de-risked Tier-B default? Decided by **measured cost drivers on real hardware**.

## Method (stage 4a — VCT)

Substrate (`src/vk/bench.*`): the spike-1 minigen world voxelized into a packed scene buffer,
uploaded device-local; an RGBA8 3D radiance volume with full mip chain (per-level storage
views plus a trilinear sampled view); three Slang kernels (`vct_inject`, `vct_mip`,
`vct_trace` — pinned
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

## Stage 4b — pre-registered comparison, symmetric by policy

Radiance-cascade passes on the same substrate, scene, and measurement discipline: cascade
ray-march cost per level, merge pass, total build cost. **Symmetry rule (review-corrected):**
both techniques are compared in their *static, un-multiplied* measured form, with known
unmeasured costs listed side-by-side rather than charged to one side. Tie-break favors VCT
**as a declared de-risk policy** (it is the architecture's incumbent default), not as a hidden
thumb on the scale: RC displaces it only by a clear measured advantage.

### Method (stage 4b — RC)

World-space 3D radiance cascades (`rc_march`, `rc_merge`, `rc_resolve`, `rc_sample` — pinned
per docs/tooling/slang.md) marching the SAME pre-filtered radiance mip chain the VCT path
samples, on the same scenes, with the same discipline (REPEAT-tiled full marches, executed-step
instrumentation per cascade, 8-warm-iteration medians, `timestampValidBits` masking, serialized
passes as a conservative upper bound).

Canonical scaling, per level: probe spacing x2 (count x1/8), octahedral direction-map side x2
(directions x4), radial interval scale 4 with march step 2^i voxels sampling mip i — rays
halve, steps double, so march work per cascade is ~constant by construction. Levels are added
while the probe grid stays >= 2 per axis, stopping once radial reach covers the volume
diagonal; reach is reported with every row (a shortfall is data, never silent).

The merge compositing is associative, which the invariant tests exploit: on a uniform solid
volume the fully merged cascade 0 must equal one continuous march over the whole radial range
(`rgb = v(1-0.65^N)/0.35` for N total steps) — an analytic closed form pinning march
arithmetic, the cascade layout, and the merge chain end-to-end. Further pins: exact zeros in
an empty world; a slab placed beyond cascade 0's interval that is invisible pre-merge and
visible post-merge (radiance transport down the hierarchy); per-cascade executed-step means
exactly equal to nominal without early-out and exactly 11 (= the analytic 0.65^k < 0.01
crossing) on the cut-off cascade with it.

### Results — stage 4b (RTX 4090, driver 610.47, 2026-06-06, median of 8 warm iterations)

256³ scene, interval scale 4, r0 = 4, cascade-0 spacing 4 voxels / 16 dirs (5 cascades, reach
1364 of 443 needed; 124 MB cascade entries + 24 MB ambient cubes **on top of the ~73 MB
radiance volume both techniques share**):

| Row | volume (inject+mips) | cascade build (march+merge) | query (resolve+sample) | rebuilt-every-frame total |
|---|---|---|---|---|
| RC minigen 256³, early-out ON | 0.17 | 1.55 | 0.26 | **1.98 ms** |
| RC EMPTY 256³ (worst), OFF | 0.17 | 1.54 | 0.26 | **1.98 ms** |
| VCT minigen 256³ (same run) | 0.17 | — | 1.64 (gather) | **1.82 ms** |
| VCT EMPTY 256³ (worst), OFF | 0.17 | — | 1.88 (gather) | **2.06 ms** |

The VCT rows above are re-measured in the SAME tool run as the RC rows (that is the valid
comparison); they sit ~13% above the stage-4a table's figures — session-to-session GPU clock
state, which is exactly why cross-session deltas are never quoted here and within-run ones are.

Per-cascade march medians at 256³: 0.290 / 0.254 / 0.199 / 0.045 / 0.046 ms — ~constant for
the three GPU-saturating levels (the constant-work construction holds), then dominated by
under-occupancy, not work. The merge chain **inverts**: merge0 (0.43 ms, 4.19M entries x 32
fetches) is the single most expensive RC pass. Executed steps/ray confirm full marches
(4/8/16/32/64 nominal; 63.3 mean at the deepest level on minigen). EMPTY == minigen within
noise on both techniques: both are content-insensitive at full march. Volume scaling: RC build
cost tracks probe count (~volume): 0.11 / 0.25 / 1.98 ms totals at 64/128/256³ — RC is
**volume-bound** where VCT's gather is **pixel-bound**.

Interval-scale variant (scale 2 **necessarily coupled with r0 = 8** for diagonal coverage —
a two-variable change, so this is a regime-vs-regime comparison, not a single-knob isolation):
build 1.88 ms and reach 504 voxels vs scale-4's 1.55 ms and 1364 — the scale-4 regime
dominates on both axes here.

### What the comparison does and does not say (review-hardened)

- **The headline asymmetry is cost *shape*, not raw speed.** RC's expensive part (build) is
  per-volume-per-update and camera-independent; VCT's (gather) is per-pixel-per-frame. The
  0.26 ms RC query is the per-frame cost **only under a no-rebuild cadence** (static lighting,
  moving camera). If cascades rebuild every frame, RC totals 1.98 ms vs VCT's 2.06 ms worst
  case — competitive, not 7x. Any quote of the query number without its cadence assumption is
  a misquote.
- **The queries are different products.** VCT's gather produces per-pixel cone-traced
  irradiance + occlusion; RC's sample produces trilinearly interpolated, ambient-cube-projected
  irradiance from probes spaced 4 voxels apart. Neither row is quality-normalized; this spike
  measures cost drivers under declared parameterizations, not equal-quality configurations.
- **Unmeasured costs, side-by-side (expanded in review):** RC — ringing mitigation, dynamic
  re-march cadence policy, probe light leaking, ambient-cube directional loss, bilinear-fix
  variants; VCT — dynamic re-injection policy, anisotropic voxels, clipmap-ring transitions.
  Neither side's list is charged to the other.

### Verdict (per the pre-registered tie-break)

**VCT remains the Tier-B default.** The rebuilt-every-frame totals are a statistical tie
(1.98 vs 2.06 ms worst case); RC's structural advantage (7x cheaper steady-state query) is
*conditional* on an update-cadence profile and is bought with ~148 MB of additional memory at
256³ and two extra passes of pipeline complexity; the quality comparison is unmeasured. Under
the declared policy — "RC displaces VCT only by a clear measured advantage" — a conditional
advantage with a memory tax and unmeasured quality is not displacement. What stage 4b *does*
establish: 3D RC at this scale is **measured-viable** (the cascade-0 voxelization cost feared
in the architecture doc is real but bounded: march0 = 0.29 ms), its constant-work-per-cascade
construction holds on real hardware until occupancy runs out, and the merge — not the march —
is the pass to optimize. If Phase-2 lighting lands on incremental light updates (block edits,
day/night ramps) rather than per-frame rebuilds, RC's cost shape fits that cadence and a
quality-normalized bake-off (with ringing mitigation costed) is the pre-registered follow-up.
