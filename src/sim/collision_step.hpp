// Representative entity-vs-voxel collision step for the Contract 5
// FP-determinism spike (issue #3).
//
// This is NOT the final engine collision algorithm. It is a stand-in with the
// same arithmetic profile as a Luanti-style collisionMoveSimple: per-axis
// substepped sweep of an AABB against an integer voxel field, using only
// IEEE-determined operations (det_math.hpp). Contract 5 freezes the *final*
// step's semantics later; this kernel exists to answer one question — does
// the pinned-flags mechanism yield bit-identical trajectories across
// compilers, platforms, and optimization levels?
//
// The implementation translation units are compiled with pinned FP flags
// (MSVC /fp:strict; GCC/Clang -ffp-contract=off -fno-fast-math) regardless of
// the parent build type — see src/sim/CMakeLists.txt.

#pragma once

#include <cstdint>

namespace sim {

struct Vec3 {
	float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Entity {
	Vec3 pos;          // box center, world units (1 unit = 1 voxel)
	Vec3 vel;          // units / second
	Vec3 halfExtents;  // box half-size per axis; each must be < 0.5
	bool onGround = false;
};

struct StepParams {
	// DELIBERATELY inexact constants (review finding): with power-of-two dt
	// and exact gravity, every a*b in the kernel had an exactly-representable
	// product, making fma(a,b,c) == a*b + c — the anti-contraction flags were
	// not load-bearing and the experiment could not falsify them. These values
	// make gravity*dt (and friends) inexact, so contraction WOULD change bits
	// — which is exactly what the unpinned control build must demonstrate.
	float dt = 0.0075f;
	float gravity = -9.81f;     // applied to vel.y each step
	float damping = 0.98f;      // per-step velocity decay
	float maxSpeed = 30.0f;     // per-axis velocity clamp
};

// Deterministic procedural solidity test for the spike world: a hash-derived
// rolling floor (y <= floorHeight(x,z), heights in [-4, 4]) plus scattered
// solid blocks for y in [0, 16). Pure integer math — no FP hazard.
bool worldSolid(std::int32_t x, std::int32_t y, std::int32_t z);

// Advance one entity by params.dt: apply gravity + damping + speed clamp,
// then per-axis (Y, X, Z) substepped move-and-clamp against worldSolid().
// Sets onGround when downward motion is blocked. Positions are clamped to
// stay well inside the float-exact integer range.
void collisionStep(Entity &e, const StepParams &params);

} // namespace sim
