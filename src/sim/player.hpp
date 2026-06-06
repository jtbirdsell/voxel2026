// Player walking controller (issue #23): the deterministic layer that turns
// per-tick intent (move direction, jump) into velocity and delegates to the
// shared collision step. This is the piece that makes the rendered world
// WALKABLE — and because it lives in the pinned-FP simulation unit and uses
// only IEEE-determined operations, walking inherits the Contract 5
// bit-determinism story: same input bits + same world -> same trajectory
// bits, on every pinned build (gated by the walk-replay goldens in
// tests/test_player.cpp, with the unpinned control build as the negative).
//
// Like collision_step.hpp this is a v1 controller, not the final movement
// system: no acceleration ramps, no swim/climb/sneak states, no step-up
// assist. Those are gameplay-tuning layers over the same deterministic core.

#pragma once

#include "sim/collision_step.hpp"

namespace sim {

// One simulation tick of player intent. moveX/moveZ are world-space
// horizontal direction components, CALLER-normalized to |(moveX, moveZ)| <= 1
// (the controller multiplies by moveSpeed and trusts them — it never
// normalizes, because normalization belongs with the camera-frame trig on the
// presentation side, outside the deterministic kernel). The inputs are part
// of the replay-determinism contract: they are consumed as raw bits, so a
// recorded input stream re-simulates exactly.
struct PlayerInput {
	float moveX = 0.0f;
	float moveZ = 0.0f;
	bool jump = false;
};

struct WalkParams {
	// v1 feel constants, tunable like StepParams — not contract values.
	// NOTE the interplay with StepParams: collisionStep applies one damping
	// factor after these are set, so the effective ground speed is
	// moveSpeed * damping (~5.88 u/s with the defaults).
	float moveSpeed = 6.0f; // horizontal speed at |move| = 1, units/second
	float jumpSpeed = 7.0f; // instantaneous vertical takeoff speed
};

// v1 player box. Each half extent must be < 0.5 (collision-kernel
// constraint: the substep overlap scan is tunnel-proof only while the box
// never spans more than two voxels per axis), so the v1 player is a
// 0.875-cube; taller player boxes are a named kernel generalization for a
// later issue, not silently violated here.
//
// The value is the dyadic rational 7/16 ON PURPOSE: it is exactly
// representable in binary32 AND survives the kernel's flush-clamp round trip
// exactly — sweepAxis clamps to float(cell + 1) + he, then the post-clamp
// overlap RESCAN recomputes the face as (pos - he), and with a dyadic he
// that subtraction reproduces float(cell + 1) bit-exactly — so a landed
// player rests BIT-EXACTLY flush on the voxel face, with onGround stable
// every step. A non-dyadic extent (e.g. 0.45f) makes the rescanned face
// land one ULP INSIDE the solid cell, demoting every landing to the
// "restore previous substep position" fallback and rest height to
// substep-dependent creep.
inline constexpr Vec3 kPlayerHalfExtents{0.4375f, 0.4375f, 0.4375f};

// Advance the player one fixed tick against an injected world (same SolidFn
// contract as collisionStep: pure-integer callback). Horizontal velocity is
// DIRECT-DRIVE — intent overwrites vel.x/z every tick, so releasing input
// stops the player on the next tick. Jump consumes the previous tick's
// ground contact (e.onGround is the collision step's output flag); holding
// jump re-fires on every grounded tick by design — edge-triggering is an
// input-layer policy, not a kernel one.
void playerStep(Entity &e, const PlayerInput &in, const WalkParams &walk,
		const StepParams &params, const WorldOffset &origin, SolidFn solid,
		const void *ctx);

} // namespace sim
