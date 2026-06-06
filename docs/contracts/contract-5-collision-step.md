# Contract 5 — Shared collision step

**Owner:** runtime · **Consumers:** networking (prediction + server re-simulation), scripting
(movement-affecting APIs) · **Status:** FROZEN 2026-06-06
([ADR-0007](../adr/0007-contract-5-freeze.md)) — changes by RFC only, gated on the replay harness

## Normative invariants

1. **One step function, two binaries.** A single collision/movement step compiles into both
   client and server; the authority path and the prediction path are the same code. No
   per-binary specializations of any kind inside the step's translation unit.
2. **FP determinism mechanism** (identical source is necessary, NOT sufficient — spike-3):
   - The step builds in an **isolated translation unit** with contraction off
     (MSVC `/fp:strict`, GCC/Clang `-ffp-contract=off -fno-fast-math`), no autovectorized
     reductions, and IEEE-determined operations only (no transcendentals; vendored deterministic
     `truncf`-class helpers where needed).
   - **FTZ/DAZ off is runtime state, not a compile flag**: the engine pins MXCSR (x86) /
     FPCR.FZ (ARM) on every simulation thread before stepping; subnormal inputs are part of the
     test corpus.
   - Header-inline helpers shared with unpinned TUs MUST contain no contractable or
     reassociable expressions (COMDAT/ODR folding can otherwise select an unpinned
     instantiation — spike-3 review finding).
   - The determinism CI gate includes a **contraction-enabled negative control build that must
     diverge** — if the control stops diverging, the mechanism is no longer load-bearing and
     the gate fails the build.
3. **Tunneling impossibility invariant (CCD policy for the predicted path):** the step is
   fixed-dt swept-AABB, and the contract pins `maxSpeed * dt < 0.5 node` for everything on the
   predicted path (v1 values: dt = 0.0075 s, maxSpeed = 30 → 0.225 nodes/step). The server
   clamps `physics_override` applications that would violate the bound. Faster things
   (projectiles, vehicles) are *non-predicted* and take the Jolt backend's CCD — Jolt never
   touches the predicted path or authority.
4. **Parameter sequencing:** `physics_override` changes, attach/detach, and teleports carry the
   input sequence number at which they take effect and replay deterministically during
   reconciliation; attached entities suspend prediction (server-driven transform).
5. **Origin interaction:** the step accepts an integer `WorldOffset` (Contract 4); world
   translation cannot perturb trajectories (validated: spike-3 goldens byte-identical through
   the spike-2 change).

## Decisions (previously open, resolved at freeze)

- **Semantics vs Luanti 1.x `collisionMoveSimple`:** the contract freezes the *interface and
  the determinism mechanism*, not 1.x behavior. Divergences (step order, sliding response,
  touching-face rules) are documented in the porting guide as they are discovered — no
  bug-for-bug compatibility target exists.
- **CCD on the predicted path:** resolved by invariant 3 — construction, not detection. The
  falsifiable revisit trigger: a legitimate predicted-path feature that needs
  `maxSpeed * dt >= 0.5` (e.g. high-speed movement mods on the *predicted* player) re-opens
  this by RFC, costed against substepping inside the isolated TU.

## Spike evidence binding this freeze

Spike 3 (`docs/spikes/spike-3-fp-determinism.md`, issue #3): six pinned CI legs reproduce
MSVC-minted golden replay hashes bit-for-bit; the unpinned control diverges; FTZ/DAZ and
FLT_EVAL_METHOD findings folded into invariant 2. Spike 2 (issue #2): WorldOffset integration.
The v1 step interface exists in `src/sim/collision_step.hpp` (StepParams, WorldOffset overload,
kWorldBound) and is exercised by the replay harness in every CI build.

## CI gates

- **Cross-build prediction-replay harness is a blocking gate** on any change touching this
  step, its TU's flags, or shared inline helpers: recorded inputs MUST re-simulate to
  bit-identical positions across the supported OS/compiler matrix (currently 6 pinned legs +
  sanitizer legs + the mandatory diverging control).
- Subnormal-input probes stay in the replay corpus (entities 0–1) so the FTZ/DAZ precondition
  is continuously exercised.
