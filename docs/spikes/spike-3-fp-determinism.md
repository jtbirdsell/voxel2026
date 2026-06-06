# Spike 3 — Shared collision step FP determinism across builds

**Issue:** [#3](https://github.com/jtbirdsell/voxel2026/issues/3) · **Contract:**
[Contract 5](../contracts/contract-5-collision-step.md) · **Status:** **PASS** (current matrix,
2026-06-05)

## Question

Does the pinned mechanism — an isolated translation unit built with `/fp:strict` (MSVC) /
`-ffp-contract=off -fno-fast-math` (GCC/Clang), restricted to IEEE-determined operations
(+, −, ×, ÷, comparisons, exact int↔float conversions, bit-exact floor/trunc; **no
transcendentals**) — produce **bit-identical** simulation trajectories across compilers,
platforms, and optimization levels?

## Method

1. **Kernel** (`src/sim/collision_step.cpp`): a representative entity-vs-voxel AABB collision
   step — per-axis substepped sweep against a procedural integer voxel field, gravity, damping,
   velocity clamps. Deliberately the same arithmetic profile as a Luanti-style
   `collisionMoveSimple`, but *not* the final engine algorithm (Contract 5 freezes semantics
   separately). **Falsifiability (added in adversarial review):** the step constants
   (`dt = 0.0075`, `g = −9.81`, `damping = 0.98`) are *deliberately inexact* so that
   `vel + g*dt` is a fusible multiply-add with an inexact product — with the original
   power-of-two constants every product was exact, FMA contraction could never have changed a
   bit, and the flags under test would not have been load-bearing.
2. **Math vocabulary** (`src/sim/det_math.hpp`): bit-exact `detFloor`/`detTrunc`/`detFloorToInt`
   built from exact int conversions, with debug asserts at every float→int site (out-of-range
   conversion is UB with compiler-divergent results — the asserts turn a silent false verdict
   into a loud failure); compile-time guards (`is_iec559`, `FLT_EVAL_METHOD` defined and `== 0`)
   reject excess-precision targets.
3. **Replay harness** (`src/sim/replay.cpp`): 64 entities, 4,096 steps, splitmix64-driven spawns
   and periodic impulses (all float inputs derived from integers by exact/correctly-rounded
   ops). FNV-1a-64 over the **bit patterns** of all positions/velocities/ground flags at 8
   checkpoints. **Runtime FP state is pinned, not assumed:** the harness saves MXCSR, clears
   FTZ/DAZ for the run, and restores it — flush-to-zero is register state no compile flag
   controls. Two **subnormal probe entities** (excluded from impulses) decay their velocities
   through the denormal band, so every checkpoint hash exercises the regime where an unpinned
   FTZ/DAZ leg would diverge.
4. **Goldens** minted on Windows x64 MSVC `/fp:strict` (Release) and committed
   (`tests/test_replay_determinism.cpp`).
5. **Experiment**: the CI matrix — windows-msvc, linux-gcc, linux-clang **× Debug, Release**
   (6 jobs) — must reproduce all 8 checkpoint hashes bit-for-bit, **and a 7th
   negative-control job** (linux-gcc, `-DVOXEL2026_SIM_UNPINNED_CONTROL=ON`:
   `-ffp-contract=fast -mfma`) must **diverge** from them. Without the control, a green matrix
   could not distinguish "the mechanism works" from "nothing here was sensitive to the
   mechanism." The `replay_hash` tool prints per-checkpoint hashes to localize any divergence.

## Why the flags suffice in theory

- IEEE 754 fully determines +, −, ×, ÷, comparisons, and in-range int↔float conversions.
- FMA contraction (`a*b+c` fused) is the classic cross-compiler divergence → disabled by
  `/fp:strict` and `-ffp-contract=off` — and the kernel now contains an inexact-product
  mul-add specifically so this flag is *testably* load-bearing (negative control).
- FP reassociation (which would make autovectorized reductions order-dependent) is only legal
  under fast-math → disabled (and pinned, so no parent flag can leak in).
- x86-64 uses SSE scalar math (no x87 excess precision); `FLT_EVAL_METHOD == 0` is asserted at
  compile time.
- libm variance is excluded by construction: the kernel calls no libm function. (IEEE requires
  correctly-rounded sqrt, so sqrt would also be safe; transcendentals would not be.)
- **What the flags do NOT cover (review finding):** FTZ/DAZ flush-to-zero is process-global
  MXCSR *runtime* state — any linked library or CRT can flip it, and `/fp:strict`/
  `-ffp-contract=off` do not pin it. It is therefore a **Contract 5 precondition** (the engine
  must establish FTZ/DAZ-off on simulation threads), enforced here by the harness itself.
  Header-inline helpers are likewise not "isolated to the pinned TU" — they inline into
  unpinned includers — so `det_math.hpp` carries a structural rule: no contractable or
  reassociable expressions in the header, ever.

The spike exists because "suffices in theory" has historically not been the same as "suffices in
shipping toolchains" — this measures it.

## Success / failure criteria

- **Pass:** all 6 pinned CI jobs green on the golden-hash test **and** the unpinned control job
  green on its *divergence* assertion → the mechanism is validated for the current matrix, and
  the harness graduates to the blocking CI gate mandated by Contract 5.
- **Fail (pinned leg diverges):** the first differing checkpoint + `replay_hash` output localize
  it; fallback per the issue: fixed-point arithmetic for the shared step.
- **Fail (control leg matches goldens):** the experiment has no power — the kernel lost its
  inexact-product mul-add or the control flags didn't enable contraction; fix before trusting
  any green pinned result.

## Results

**PASS — 2026-06-05, commit `607a296`,
[run 27049326083](https://github.com/jtbirdsell/voxel2026/actions/runs/27049326083).**

| Leg | Expectation | Result |
|---|---|---|
| windows-msvc Debug + Release | reproduce goldens | ✅ bit-identical (mint platform) |
| linux-gcc Debug + Release | reproduce goldens | ✅ **bit-identical across OS + compiler + optimizer** |
| linux-clang Debug + Release | reproduce goldens | ✅ **bit-identical across OS + compiler + optimizer** |
| linux-gcc unpinned control (`-ffp-contract=fast -mfma`) | **diverge** | ✅ diverged at every checkpoint |

All 8 golden checkpoint hashes — minted on Windows x64 MSVC `/fp:strict` Release — were
reproduced exactly by GCC and Clang on Linux at both optimization levels, including the
subnormal-probe trajectories. The negative control diverged from checkpoint 0 onward, proving
the experiment had the power to detect contraction (locally verified the same on MSVC
`/fp:fast /arch:AVX2`, which also surfaced that `/fp:fast` sets `FLT_EVAL_METHOD` to −1 — the
conformance guard correctly rejects such builds, and the control bypasses it explicitly).

**Verdict for Contract 5:** the pinned mechanism (isolated TU + `/fp:strict` /
`-ffp-contract=off -fno-fast-math` + IEEE-determined-ops-only + pinned FTZ/DAZ as a runtime
precondition) is **validated on x86-64 Windows/Linux across MSVC/GCC/Clang and Debug/Release**.
The replay harness runs in every CI build from now on — the blocking determinism gate Contract 5
mandates exists as of this commit. Fixed-point fallback not needed on this matrix.

## Known limitations (honest scope)

- macOS / ARM64 (NEON) is not yet in the CI matrix — untested; AArch64 FP is IEEE-conforming and
  contraction is controlled by the same flags, but the claim is only as good as the matrix
  (and the MXCSR pin is x86-only; ARM needs the FPCR.FZ equivalent when it joins).
- The kernel is single-threaded; the job-system question (deterministic results under parallel
  entity batches) is a separate property — per-entity math is independent, so batch order cannot
  affect per-entity results, but that claim gets its own harness when the job system lands.
- The `FLT_EVAL_METHOD` guard catches x87 *excess precision* (GCC/Clang targeting the x87 ABI),
  not 32-bit x86 per se — SSE2 32-bit builds legitimately pass (review-corrected wording).
