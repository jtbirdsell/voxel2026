// Behavioral properties of the spike collision kernel. The kernel is a
// stand-in (see collision_step.hpp), but it must still be *correct* — the
// determinism experiment is meaningless if the trajectories it pins are
// garbage.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

#include "sim/collision_step.hpp"
#include "sim/det_math.hpp"

namespace {

// INDEPENDENT overlap oracle (review finding: the previous version reproduced
// the kernel's overlapRange/half-open logic char-for-char, so a shared
// boundary bug would have been invisible). This one uses a different
// formulation: std::floor for the candidate window and a raw strict-inequality
// interval predicate — box [bmin,bmax) intersects cell [c,c+1) iff
// bmin < c+1 && c < bmax — with a +-1-cell safety margin on the window.
bool boxOverlapsAnySolid(const sim::Entity &e)
{
	const float bmin[3] = {e.pos.x - e.halfExtents.x, e.pos.y - e.halfExtents.y,
			e.pos.z - e.halfExtents.z};
	const float bmax[3] = {e.pos.x + e.halfExtents.x, e.pos.y + e.halfExtents.y,
			e.pos.z + e.halfExtents.z};

	std::int32_t lo[3], hi[3];
	for (int a = 0; a < 3; ++a) {
		lo[a] = static_cast<std::int32_t>(std::floor(bmin[a])) - 1;
		hi[a] = static_cast<std::int32_t>(std::floor(bmax[a])) + 1;
	}

	const auto intersects = [&](int a, std::int32_t c) {
		return bmin[a] < static_cast<float>(c + 1) && static_cast<float>(c) < bmax[a];
	};

	for (std::int32_t z = lo[2]; z <= hi[2]; ++z)
		for (std::int32_t y = lo[1]; y <= hi[1]; ++y)
			for (std::int32_t x = lo[0]; x <= hi[0]; ++x)
				if (intersects(0, x) && intersects(1, y) && intersects(2, z) &&
						sim::worldSolid(x, y, z))
					return true;
	return false;
}

sim::Entity spawnAt(float x, float y, float z)
{
	sim::Entity e;
	e.pos = {x, y, z};
	e.halfExtents = {0.3f, 0.45f, 0.3f};
	return e;
}

} // namespace

TEST_CASE("Entity falls under gravity in open air")
{
	sim::Entity e = spawnAt(0.5f, 30.0f, 0.5f);
	const sim::StepParams p{};
	sim::collisionStep(e, p);
	REQUIRE(e.vel.y < 0.0f);
	REQUIRE(e.pos.y < 30.0f);
	REQUIRE_FALSE(e.onGround);
}

TEST_CASE("Entity lands on terrain and comes to rest without penetrating")
{
	sim::Entity e = spawnAt(0.5f, 30.0f, 0.5f);
	const sim::StepParams p{};

	bool landed = false;
	for (int i = 0; i < 2000; ++i) {
		sim::collisionStep(e, p);
		REQUIRE_FALSE(boxOverlapsAnySolid(e));
		if (e.onGround) {
			landed = true;
			break;
		}
	}
	REQUIRE(landed);
	REQUIRE(e.vel.y == 0.0f);

	// First contact can be followed by a brief deterministic settle phase
	// (flush-clamp rounding may land the box a hair above the face, after
	// which it sinks ~6e-4/step until contact re-establishes). Let it settle,
	// then require a true fixpoint.
	for (int i = 0; i < 300; ++i)
		sim::collisionStep(e, p);

	const float restY = e.pos.y;
	for (int i = 0; i < 256; ++i) {
		sim::collisionStep(e, p);
		REQUIRE(e.onGround);
		REQUIRE(e.pos.y == restY);
		REQUIRE_FALSE(boxOverlapsAnySolid(e));
	}
}

TEST_CASE("Maximum-speed motion cannot tunnel through terrain")
{
	const sim::StepParams p{};
	// Slam entities straight down at the speed clamp from many columns;
	// none may ever end a step overlapping a solid voxel.
	for (int col = -8; col <= 8; ++col) {
		sim::Entity e = spawnAt(static_cast<float>(col) + 0.5f, 28.0f,
				static_cast<float>(-col) + 0.5f);
		e.vel.y = -p.maxSpeed;
		bool everGrounded = false;
		for (int i = 0; i < 1200; ++i) {
			sim::collisionStep(e, p);
			REQUIRE_FALSE(boxOverlapsAnySolid(e));
			everGrounded = everGrounded || e.onGround;
		}
		REQUIRE(everGrounded);
	}
}

TEST_CASE("Per-axis speed clamp lands exactly on the boundary, both signs")
{
	const sim::StepParams p{};
	// (review finding: <=/>= assertions passed for many wrong results and
	// half the clamp limbs were never exercised — assert exact boundaries
	// and cover both sign branches per axis.)
	{
		sim::Entity e = spawnAt(0.5f, 40.0f, 0.5f);
		e.vel = {1000.0f, -1000.0f, 1000.0f};
		sim::collisionStep(e, p);
		REQUIRE(e.vel.x == p.maxSpeed);
		REQUIRE(e.vel.y == -p.maxSpeed);
		REQUIRE(e.vel.z == p.maxSpeed);
	}
	{
		sim::Entity e = spawnAt(0.5f, 40.0f, 0.5f);
		e.vel = {-1000.0f, 1000.0f, -1000.0f};
		sim::collisionStep(e, p);
		REQUIRE(e.vel.x == -p.maxSpeed);
		REQUIRE(e.vel.y == p.maxSpeed);
		REQUIRE(e.vel.z == -p.maxSpeed);
	}
}

TEST_CASE("Substepping is load-bearing: a large per-step delta cannot skip a thin wall")
{
	// (review finding: at default params, max displacement 0.225/step can
	// never skip a cell, so deleting the substep loop passed every test.)
	// With dt = 1 the displacement is several cells per step; a single
	// move-then-check would jump clean over a 1-thick wall, so this test
	// fails unless the substep decomposition actually stops at the face.
	sim::StepParams p{};
	p.dt = 1.0f;
	p.gravity = 0.0f; // isolate the X axis

	// Find a solid cell in the obstacle band with a clear approach lane of
	// >= 6 air cells on its -X side (same row, same column z).
	for (std::int32_t x = 8; x < 72; ++x)
		for (std::int32_t z = 0; z < 72; ++z)
			for (std::int32_t y = 8; y < 14; ++y) {
				if (!sim::worldSolid(x, y, z))
					continue;
				bool laneClear = true;
				for (std::int32_t k = 1; k <= 6 && laneClear; ++k)
					laneClear = !sim::worldSolid(x - k, y, z) &&
							!sim::worldSolid(x - k, y - 1, z) && !sim::worldSolid(x - k, y + 1, z);
				if (!laneClear)
					continue;

				sim::Entity e = spawnAt(static_cast<float>(x) - 5.5f,
						static_cast<float>(y) + 0.5f, static_cast<float>(z) + 0.5f);
				e.vel.x = 8.0f; // ~7.84 units in ONE step after damping
				sim::collisionStep(e, p);

				// Must be stopped at (or just before) the wall face — never
				// beyond it — with the axis velocity killed.
				REQUIRE(e.vel.x == 0.0f);
				REQUIRE(e.pos.x + e.halfExtents.x <= static_cast<float>(x));
				REQUIRE(e.pos.x > static_cast<float>(x) - 2.0f); // actually reached it
				REQUIRE_FALSE(boxOverlapsAnySolid(e));
				return; // one witness suffices
			}
	FAIL("no suitable wall-with-lane configuration found in search region");
}

TEST_CASE("Horizontal motion is blocked by walls, not absorbed into them")
{
	const sim::StepParams p{};
	// Find a solid voxel in the obstacle band with air beside it, then drive
	// an entity into it along +X and verify flush clamping.
	for (std::int32_t x = 0; x < 64; ++x)
		for (std::int32_t z = 0; z < 64; ++z)
			for (std::int32_t y = 8; y < 14; ++y) {
				if (!sim::worldSolid(x, y, z))
					continue;
				if (sim::worldSolid(x - 1, y, z) || sim::worldSolid(x - 1, y - 1, z) ||
						sim::worldSolid(x - 1, y + 1, z))
					continue;

				sim::Entity e = spawnAt(static_cast<float>(x) - 0.5f,
						static_cast<float>(y) + 0.5f, static_cast<float>(z) + 0.5f);
				e.vel.x = 10.0f;
				// The box starts 0.2 from the wall face and covers ~0.077 per
				// step; a few steps reach contact. Gravity sag over 10 steps
				// (~0.03) keeps the box within voxel row y.
				for (int i = 0; i < 10 && e.vel.x != 0.0f; ++i)
					sim::collisionStep(e, p);
				REQUIRE(e.vel.x == 0.0f);
				REQUIRE(e.pos.x + e.halfExtents.x <= static_cast<float>(x));
				return; // one witness suffices
			}
	FAIL("no suitable wall configuration found in search region");
}
