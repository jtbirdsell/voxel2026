# Spike 2 — Streaming origin rebasing correctness

**Issue:** [#2](https://github.com/jtbirdsell/voxel2026/issues/2) · **Contract:**
[Contract 4](../contracts/contract-4-coordinates.md) · **Status:** implementation verified
locally; CI verdict pending

## Question

Is cell-aligned origin rebasing a sound mechanism for Contract 4 — specifically: (a) do two
machines applying the **same** rebase schedule stay bit-identical in virtual-absolute space,
(b) is the "server and client rebase identically" clause genuinely load-bearing, (c) when is the
rebase subtraction bit-exact, and (d) is the far-origin precision disease it cures real and
demonstrable?

## Method

1. **Frame model** (`src/sim/rebase.{hpp,cpp}`, pinned-FP TU): entity positions are floats LOCAL
   to an integer cell origin; the collision kernel queries the world at origin+local via a new
   **integer-only `WorldOffset`** translation (`collisionStep` overload — the offset adds int to
   int cell coordinates, so the FP trajectory cannot be perturbed; the spike-3 goldens were
   re-verified byte-identical after the change, mechanically).
2. **Rebase** subtracts an integer delta from locals and adds it to the origin; **exactness is
   measured, not assumed**, by add-back bit reconstruction. Virtual absolutes are reconstructed
   in double (exact for int32 origin + binary32 local — both embed losslessly in binary64).
3. **Harness**: a 32-entity population with sustained +x drift stepped 4,096 times through the
   collision kernel, rebased per schedule, with FNV-1a-64 checkpoints over the **virtual** state
   (so differently-framed machines are comparable). MXCSR pinned as in spike 3.
4. **Tests** (8 cases): two-machine identical-schedule identity; committed golden checkpoints
   (cross-build leg rides the CI matrix); **negative control** — a never-rebasing machine must
   diverge from a rebasing one in virtual space; per-entity Sterbenz exactness; deterministic
   bounded loss outside the window; frame-invariance of velocities/extents; the ulp-starvation
   disease/cure; zero-offset overload bit-identity with the original entry point.

## Findings

1. **Identical schedules ⇒ bit-identical machines** — including when rebases round (float
   subtraction is deterministic whether or not it is exact). Contract 4(a) holds.
2. **The identical-schedule clause is load-bearing**: a machine that rebases and one that does
   not — same seed, same physics — genuinely diverge in *virtual* space, because post-rebase
   arithmetic runs at different magnitudes and rounds differently. Rebase timing and deltas are
   therefore part of the deterministic state machine, exactly as Contract 4 demands of
   server/client.
3. **Exactness is per-entity and positional, NOT frame-wide** (the spike's sharpest result —
   the harness's original `allRebasesExact` assertion failed *correctly* and the test now pins
   the opposite). Stated precisely (review-corrected wording): subtraction is **guaranteed**
   bit-exact inside the Sterbenz window (delta/2 ≤ local ≤ 2·delta, same sign); outside the
   window exactness is *not guaranteed* — many out-of-window subtractions still happen to be
   exact — and **any loss is deterministic and bounded by 0.5 ulp of the result** (one
   correctly-rounded operation). Harness honesty note: the +192/quarter schedule deliberately
   overshoots the population's actual drift (tens of cells), landing every entity on the
   far-negative, opposite-sign side of the window — the worst case, exercised on purpose;
   in-window exactness is pinned by the targeted single-entity test. Consequence for
   Contract 4: rebasing preserves cross-machine determinism unconditionally, but "lossless"
   must be claimed only near the new origin — distant entities micro-round identically on
   every machine. The engine either accepts that (recommended: bounded, deterministic) or
   rebases per-region.
4. **The disease is real and crisp**: at |x| ≈ 2²² a kicked entity's per-step displacement
   (~0.037) falls below the position's ulp (0.5) — **motion freezes entirely**. The same
   virtual position expressed as origin 2²² + local 0 moves normally. (Demonstrating this
   required raising the kernel's position guard from 30,000 to 2²³ — identity for all
   previously valid positions; goldens unchanged.)

## Success / failure criteria

- **Pass:** identity + golden tests green on all 6 pinned CI legs; the negative control and the
  disease tests green (falsifying power present); spike-3 goldens still green (kernel changes
  trajectory-neutral).
- **Fail:** any pinned leg diverges on the rebased goldens → the rebase path has a
  platform-dependent operation; investigate before Contract 4 freezes.

## Results (local — CI verdict pending)

- Plain Release (MSVC): **37/37 tests green** (29 pre-existing + 8 new).
- Whole-program ASan: **37/37 green.**
- Spike-3 goldens byte-identical after the kernel changes (verified via `replay_hash` before and
  after); spike-2 goldens minted; `allRebasesExact == false` pinned as the documented reality.

## Honest limitations

- x-only rebases in the harness (the drift axis); y/z rebasing is the same code path with the
  same math, but is not separately golden-pinned.
- The harness rebases all entities in one frame; per-region rebasing (the alternative Contract 4
  could choose for losslessness) is designed but not prototyped here.
- "Mods see stable virtual absolutes" (Contract 4's API clause) is exercised only via the
  double-reconstruction used for hashing; the Lua-facing representation is engine-phase work.
