// Pinned-FP translation unit — see src/sim/CMakeLists.txt. All float values
// fed into the simulation are derived from integers by exact or
// correctly-rounded operations, so the whole replay is IEEE-determined.

#include "sim/replay.hpp"

#include "sim/collision_step.hpp"
#include "sim/det_math.hpp"

// MXCSR control (review finding): FTZ/DAZ are process-global RUNTIME state —
// no compile flag pins them, and a leg that flushes subnormals to zero would
// diverge bit-wise from one that does not. The harness therefore pins them
// OFF for the duration of the replay: FTZ/DAZ-off is a documented Contract 5
// PRECONDITION the engine must establish on simulation threads, not a
// consequence of the compile flags.
#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__)
#include <immintrin.h>
#define SIM_HAVE_MXCSR 1
#else
#define SIM_HAVE_MXCSR 0 // ARM FPCR.FZ etc. — out of scope until in the matrix
#endif

namespace sim {

namespace {

struct ScopedDenormalsOn {
#if SIM_HAVE_MXCSR
	unsigned int saved = _mm_getcsr();

	ScopedDenormalsOn()
	{
		// Clear FTZ (bit 15, 0x8000) and DAZ (bit 6, 0x0040).
		_mm_setcsr(saved & ~0x8040u);
	}
	~ScopedDenormalsOn() { _mm_setcsr(saved); }
#endif
	ScopedDenormalsOn(const ScopedDenormalsOn &) = delete;
	ScopedDenormalsOn &operator=(const ScopedDenormalsOn &) = delete;
#if !SIM_HAVE_MXCSR
	ScopedDenormalsOn() = default;
#endif
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

	// Uniform float in [0, 1): a 24-bit integer scaled by 2^-24. The int ->
	// float conversion and the multiply are both exact.
	float nextUnit()
	{
		const std::uint32_t r = static_cast<std::uint32_t>(next() >> 40); // 24 bits
		return static_cast<float>(r) * (1.0f / 16777216.0f);
	}

	// Uniform float in [-range, range). Correctly-rounded ops only.
	float nextSigned(float range)
	{
		return (nextUnit() * 2.0f - 1.0f) * range;
	}
};

struct Fnv1a64 {
	std::uint64_t hash = 0xCBF29CE484222325ull;

	void mix32(std::uint32_t v)
	{
		for (int i = 0; i < 4; ++i) {
			hash ^= (v >> (i * 8)) & 0xFFu;
			hash *= 0x100000001B3ull;
		}
	}
};

std::uint64_t hashEntities(const Entity *entities, int count)
{
	Fnv1a64 h;
	for (int i = 0; i < count; ++i) {
		const Entity &e = entities[i];
		h.mix32(floatBits(e.pos.x));
		h.mix32(floatBits(e.pos.y));
		h.mix32(floatBits(e.pos.z));
		h.mix32(floatBits(e.vel.x));
		h.mix32(floatBits(e.vel.y));
		h.mix32(floatBits(e.vel.z));
		h.mix32(e.onGround ? 1u : 0u);
	}
	return h.hash;
}

} // namespace

ReplayResult runReplay()
{
	const ScopedDenormalsOn denormalsPinned;

	SplitMix64 rng{kReplaySeed};
	Entity entities[kReplayEntities];

	// Spawn above the obstacle band (worldSolid is air for y >= 16 above the
	// floor), spread across a wide region so trajectories decorrelate.
	for (Entity &e : entities) {
		e.pos.x = rng.nextSigned(512.0f);
		e.pos.y = 24.0f + rng.nextUnit() * 16.0f;
		e.pos.z = rng.nextSigned(512.0f);
		e.vel.x = rng.nextSigned(8.0f);
		e.vel.y = rng.nextSigned(2.0f);
		e.vel.z = rng.nextSigned(8.0f);
		e.halfExtents = {0.3f, 0.45f, 0.3f};
	}

	// Subnormal probes (review finding): entities 0 and 1 carry tiny
	// horizontal velocities that geometric damping (*0.98/step) drives down
	// into the subnormal band well within the run (1e-35 reaches ~1.2e-38 in
	// ~330 steps; the decay then sticks at denormal-min under round-to-
	// nearest). Their bits enter every checkpoint hash, so the run exercises
	// the regime where FTZ/DAZ — pinned off above — would otherwise diverge.
	// They are excluded from impulses below so the decay is never reset.
	entities[0].pos = {600.5f, 35.0f, 600.5f};
	entities[0].vel = {1e-30f, 0.0f, 1e-35f};
	entities[1].pos = {-600.5f, 35.0f, -600.5f};
	entities[1].vel = {-1e-35f, 0.0f, 1e-30f};

	const StepParams params{};
	ReplayResult result;
	int checkpoint = 0;

	for (int step = 1; step <= kReplaySteps; ++step) {
		// Periodic impulses keep the population from settling into static
		// rest, so the hash keeps exercising live arithmetic all the way.
		// Probes (entities 0, 1) are never kicked.
		if (step % 16 == 0) {
			Entity &kicked = entities[2 + (step / 16) % (kReplayEntities - 2)];
			kicked.vel.x = kicked.vel.x + rng.nextSigned(12.0f);
			kicked.vel.y = kicked.vel.y + rng.nextUnit() * 9.0f;
			kicked.vel.z = kicked.vel.z + rng.nextSigned(12.0f);
		}

		for (Entity &e : entities)
			collisionStep(e, params);

		if (step % kReplayCheckpointInterval == 0)
			result.checkpoints[static_cast<std::size_t>(checkpoint++)] =
					hashEntities(entities, kReplayEntities);
	}

	return result;
}

} // namespace sim
