// Pinned-FP translation unit — see src/sim/CMakeLists.txt. The only float
// operations are the rebase subtraction/add-back, the collision kernel
// (already pinned), and exact int/float/double conversions.

#include "sim/rebase.hpp"

#include <cassert>

#include "sim/det_math.hpp"

#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__)
#include <immintrin.h>
#define SIM_REBASE_HAVE_MXCSR 1
#else
#define SIM_REBASE_HAVE_MXCSR 0
#endif

namespace sim {

namespace {

struct ScopedDenormalsOn {
#if SIM_REBASE_HAVE_MXCSR
	unsigned int saved = _mm_getcsr();

	ScopedDenormalsOn() { _mm_setcsr(saved & ~0x8040u); }
	~ScopedDenormalsOn() { _mm_setcsr(saved); }
#else
	ScopedDenormalsOn() = default;
#endif
	ScopedDenormalsOn(const ScopedDenormalsOn &) = delete;
	ScopedDenormalsOn &operator=(const ScopedDenormalsOn &) = delete;
};

struct SplitMix64 {
	std::uint64_t state;

	std::uint64_t next()
	{
		state += 0x9E3779B97F4A7C15ull;
		std::uint64_t z = state;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}

	float nextUnit()
	{
		const std::uint32_t r = static_cast<std::uint32_t>(next() >> 40);
		return static_cast<float>(r) * (1.0f / 16777216.0f);
	}

	float nextSigned(float range) { return (nextUnit() * 2.0f - 1.0f) * range; }
};

struct Fnv1a64 {
	std::uint64_t hash = 0xCBF29CE484222325ull;

	void mixBytes(std::uint64_t v, int bytes)
	{
		for (int i = 0; i < bytes; ++i) {
			hash ^= (v >> (i * 8)) & 0xFFu;
			hash *= 0x100000001B3ull;
		}
	}
};

std::uint64_t doubleBits(double d)
{
	return std::bit_cast<std::uint64_t>(d);
}

std::uint64_t hashVirtualState(const Frame &frame, const Entity *entities, int count)
{
	Fnv1a64 h;
	for (int i = 0; i < count; ++i) {
		const Entity &e = entities[i];
		h.mixBytes(doubleBits(virtualCoord(frame.ox, e.pos.x)), 8);
		h.mixBytes(doubleBits(virtualCoord(frame.oy, e.pos.y)), 8);
		h.mixBytes(doubleBits(virtualCoord(frame.oz, e.pos.z)), 8);
		h.mixBytes(floatBits(e.vel.x), 4);
		h.mixBytes(floatBits(e.vel.y), 4);
		h.mixBytes(floatBits(e.vel.z), 4);
		h.mixBytes(e.onGround ? 1u : 0u, 1);
	}
	return h.hash;
}

} // namespace

bool rebaseFrame(Frame &frame, std::span<Entity> entities,
		std::int32_t dx, std::int32_t dy, std::int32_t dz)
{
	const float fdx = static_cast<float>(dx);
	const float fdy = static_cast<float>(dy);
	const float fdz = static_cast<float>(dz);

	bool allExact = true;
	for (Entity &e : entities) {
		const float px = e.pos.x, py = e.pos.y, pz = e.pos.z;
		e.pos.x = px - fdx;
		e.pos.y = py - fdy;
		e.pos.z = pz - fdz;
		// Add-back reconstruction. ONE-DIRECTIONAL property (review-corrected:
		// not an iff): successful reconstruction implies the subtraction was
		// exact — the safe direction; a failed reconstruction can rarely be a
		// false negative (e.g. signed zero under a zero delta, where
		// -0.0 - 0.0 is exact but -0.0 + 0.0 yields +0.0). Zero-delta axes
		// are identity rebases and are skipped outright, which removes the
		// structural signed-zero exposure.
		if (dx != 0)
			allExact = allExact && floatBits(e.pos.x + fdx) == floatBits(px);
		if (dy != 0)
			allExact = allExact && floatBits(e.pos.y + fdy) == floatBits(py);
		if (dz != 0)
			allExact = allExact && floatBits(e.pos.z + fdz) == floatBits(pz);
	}
	frame.ox += dx;
	frame.oy += dy;
	frame.oz += dz;
	return allExact;
}

RebasedReplayResult runRebasedReplay(std::span<const RebaseEvent> schedule)
{
	const ScopedDenormalsOn denormalsPinned;

	SplitMix64 rng{kRebaseSeed};
	Entity entities[kRebaseEntities];
	Frame frame;

	// Drift population: spawned above the obstacle band around local x ~ 0,
	// with sustained +x impulses below. The net migration is modest (tens of
	// cells over the run — damping eats most of each kick), so the +192-cell
	// schedule DELIBERATELY overshoots it: every rebase lands the whole
	// population far on the negative, opposite-sign side of the Sterbenz
	// window (review-corrected narrative — this is the worst case for
	// rebase losslessness, exercised on purpose; in-window exactness is
	// pinned separately by the targeted single-entity test).
	for (Entity &e : entities) {
		e.pos.x = rng.nextSigned(64.0f);
		e.pos.y = 24.0f + rng.nextUnit() * 16.0f;
		e.pos.z = rng.nextSigned(64.0f);
		e.vel.x = 4.0f + rng.nextUnit() * 4.0f;
		e.vel.y = rng.nextSigned(2.0f);
		e.vel.z = rng.nextSigned(4.0f);
		e.halfExtents = {0.3f, 0.45f, 0.3f};
	}

	const StepParams params{};
	RebasedReplayResult result;
	std::size_t nextEvent = 0;
	int checkpoint = 0;

	for (int step = 1; step <= kRebaseSteps; ++step) {
		// Forward-biased impulses keep the population drifting +x.
		if (step % 16 == 0) {
			Entity &kicked = entities[(step / 16) % kRebaseEntities];
			kicked.vel.x = kicked.vel.x + 6.0f + rng.nextUnit() * 6.0f;
			kicked.vel.y = kicked.vel.y + rng.nextUnit() * 9.0f;
			kicked.vel.z = kicked.vel.z + rng.nextSigned(6.0f);
		}

		const WorldOffset origin{frame.ox, frame.oy, frame.oz};
		for (Entity &e : entities)
			collisionStep(e, params, origin);

		// Precondition (documented in rebase.hpp): schedule sorted ascending
		// by step — an out-of-order event would otherwise be silently
		// skipped. Asserted so a malformed schedule fails loudly.
		assert(nextEvent >= schedule.size() || schedule[nextEvent].step >= step);
		while (nextEvent < schedule.size() && schedule[nextEvent].step == step) {
			result.allRebasesExact =
					rebaseFrame(frame, entities, schedule[nextEvent].dx, 0, 0) &&
					result.allRebasesExact;
			++nextEvent;
		}

		if (step % kRebaseCheckpointInterval == 0)
			result.checkpoints[static_cast<std::size_t>(checkpoint++)] =
					hashVirtualState(frame, entities, kRebaseEntities);
	}

	return result;
}

} // namespace sim
