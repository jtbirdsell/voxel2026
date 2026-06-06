// Pinned-FP translation unit — see src/sim/CMakeLists.txt. Only
// IEEE-determined operations are permitted here (det_math.hpp).

#include "sim/collision_step.hpp"

#include "sim/det_math.hpp"

namespace sim {

namespace {

// Position guard: keeps every coordinate (and the voxel indices derived from
// box corners) far inside the float-exact integer range.
constexpr float kWorldBound = 30000.0f;

// Substep length cap: with halfExtents < 0.5 the box can never skip a whole
// voxel in one substep, so the overlap scan cannot tunnel.
constexpr float kMaxSubstep = 0.0625f; // 1/16 — exactly representable

constexpr std::uint64_t splitmix64(std::uint64_t x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

constexpr std::uint64_t cellHash(std::int32_t x, std::int32_t y, std::int32_t z)
{
	const std::uint64_t ux = static_cast<std::uint32_t>(x);
	const std::uint64_t uy = static_cast<std::uint32_t>(y);
	const std::uint64_t uz = static_cast<std::uint32_t>(z);
	return splitmix64((ux << 42) ^ (uy << 21) ^ uz ^ 0x564F58454C32366Bull);
}

constexpr std::int32_t floorHeight(std::int32_t x, std::int32_t z)
{
	// Rolling terrain in [-4, 4].
	return static_cast<std::int32_t>(cellHash(x, 12345, z) % 9) - 4;
}

struct AxisRange {
	std::int32_t lo, hi; // inclusive voxel index range overlapped by the box
};

// Voxel indices overlapped by the half-open interval [min, max). A box face
// exactly on a voxel boundary does NOT overlap the next cell.
AxisRange overlapRange(float minCoord, float maxCoord)
{
	const std::int32_t lo = detFloorToInt(minCoord);
	std::int32_t hi = detFloorToInt(maxCoord);
	if (static_cast<float>(hi) == maxCoord)
		hi -= 1;
	return {lo, hi};
}

// Does the entity box, at its current position, overlap any solid voxel?
// Scan order is fixed (z, y, x ascending) — iteration order never affects the
// result here (pure existence test), but we keep it canonical on principle.
bool boxOverlapsSolid(const Entity &e)
{
	const AxisRange rx = overlapRange(e.pos.x - e.halfExtents.x, e.pos.x + e.halfExtents.x);
	const AxisRange ry = overlapRange(e.pos.y - e.halfExtents.y, e.pos.y + e.halfExtents.y);
	const AxisRange rz = overlapRange(e.pos.z - e.halfExtents.z, e.pos.z + e.halfExtents.z);
	for (std::int32_t z = rz.lo; z <= rz.hi; ++z)
		for (std::int32_t y = ry.lo; y <= ry.hi; ++y)
			for (std::int32_t x = rx.lo; x <= rx.hi; ++x)
				if (worldSolid(x, y, z))
					return true;
	return false;
}

float &axisOf(Vec3 &v, int axis)
{
	return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

// Move the entity along one axis by delta, in substeps no longer than
// kMaxSubstep. On the first substep that lands the box inside a solid voxel,
// clamp the box flush to the voxel face it crossed, zero the axis velocity,
// and stop. Returns true if the axis was blocked.
bool sweepAxis(Entity &e, int axis, float delta)
{
	if (delta == 0.0f)
		return false;

	const bool positive = delta > 0.0f;
	const float magnitude = positive ? delta : -delta;

	// Substep count: ceil(magnitude / kMaxSubstep) via exact integer math on
	// a truncated quotient. magnitude <= maxSpeed * dt < 1, so count is tiny.
	const std::int32_t count = static_cast<std::int32_t>(magnitude / kMaxSubstep) + 1;
	const float sub = delta / static_cast<float>(count);

	float &p = axisOf(e.pos, axis);
	const float he = axis == 0 ? e.halfExtents.x : (axis == 1 ? e.halfExtents.y : e.halfExtents.z);

	for (std::int32_t i = 0; i < count; ++i) {
		const float prev = p;
		p = prev + sub;
		if (!boxOverlapsSolid(e))
			continue;

		// Blocked: clamp flush to the face of the voxel the leading face
		// entered. Integer -> float is exact here (|index| << 2^24).
		const float lead = positive ? p + he : p - he;
		const std::int32_t cell = detFloorToInt(lead);
		p = positive ? static_cast<float>(cell) - he
		             : static_cast<float>(cell + 1) + he;

		// If clamping did not separate the box (e.g. it was already
		// overlapping before this axis moved — spawn intersections), restore
		// the pre-substep coordinate instead of inventing a position.
		if (boxOverlapsSolid(e))
			p = prev;

		float &v = axisOf(e.vel, axis);
		v = 0.0f;
		return true;
	}
	return false;
}

} // namespace

bool worldSolid(std::int32_t x, std::int32_t y, std::int32_t z)
{
	if (y <= floorHeight(x, z))
		return true;
	// Scattered obstacles only in the low band; the replay harness spawns
	// entities above y = 24, so spawn-inside-solid is impossible.
	if (y >= 0 && y < 16)
		return (cellHash(x, y, z) & 0xFFu) < 20u; // ~7.8% density
	return false;
}

void collisionStep(Entity &e, const StepParams &params)
{
	// Entry clamp (review finding): callers may hand in arbitrary positions;
	// the detFloorToInt preconditions must hold from the FIRST overlap scan,
	// not only after the end-of-step clamp. Identity for in-range values, so
	// trajectories are unaffected.
	e.pos.x = detClamp(e.pos.x, -kWorldBound, kWorldBound);
	e.pos.y = detClamp(e.pos.y, -kWorldBound, kWorldBound);
	e.pos.z = detClamp(e.pos.z, -kWorldBound, kWorldBound);

	// Integrate velocity. Every operation is IEEE-determined; evaluation
	// order is fixed by the expressions. NOTE: gravity * dt has an INEXACT
	// product by design — this line is the fusible mul-add that makes the
	// contraction flags load-bearing (see StepParams).
	e.vel.y = e.vel.y + params.gravity * params.dt;
	e.vel.x = e.vel.x * params.damping;
	e.vel.y = e.vel.y * params.damping;
	e.vel.z = e.vel.z * params.damping;
	e.vel.x = detClamp(e.vel.x, -params.maxSpeed, params.maxSpeed);
	e.vel.y = detClamp(e.vel.y, -params.maxSpeed, params.maxSpeed);
	e.vel.z = detClamp(e.vel.z, -params.maxSpeed, params.maxSpeed);

	const bool movingDown = e.vel.y < 0.0f;

	// Fixed axis order: Y first (gravity axis), then X, then Z.
	const bool blockedY = sweepAxis(e, 1, e.vel.y * params.dt);
	sweepAxis(e, 0, e.vel.x * params.dt);
	sweepAxis(e, 2, e.vel.z * params.dt);

	e.onGround = blockedY && movingDown;

	e.pos.x = detClamp(e.pos.x, -kWorldBound, kWorldBound);
	e.pos.y = detClamp(e.pos.y, -kWorldBound, kWorldBound);
	e.pos.z = detClamp(e.pos.z, -kWorldBound, kWorldBound);
}

} // namespace sim
