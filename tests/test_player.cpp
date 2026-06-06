// Player story tests (issue #23), three layers:
//
//   1. SolidityWorld adapter correctness — the integer-only chunk bridge
//      must agree with direct cell reads everywhere, including negative
//      coordinates, chunk borders, and missing chunks.
//   2. Walking semantics on hand-built worlds — fall/land/rest, wall stop,
//      jump arc, ledge fall. The player half extent is the dyadic rational
//      7/16 precisely so flush clamping is EXACT (see player.hpp), which is
//      why these tests can use exact float equality on rest positions.
//   3. The walk-replay determinism golden — a scripted input stream driven
//      over the minigen world (same seed and region as the world_view demo)
//      must reproduce pinned checkpoint hashes on every pinned build, and
//      DIVERGE on the contraction-enabled control build (same falsifiability
//      pattern as tests/test_replay_determinism.cpp). The hash is a RUNNING
//      FNV over the full entity state every tick, so a single-bit divergence
//      at any step poisons every later checkpoint.
//
// To regenerate the goldens after an INTENTIONAL kernel/controller/world
// change: run this test on a pinned build — the failure message prints a
// paste-ready block. Any UNINTENTIONAL change is a determinism regression.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "gen/minigen.hpp"
#include "sim/det_math.hpp"
#include "sim/player.hpp"
#include "voxel/morton.hpp"
#include "world/solidity.hpp"
#include "world/voxel_blob.hpp"

namespace {

constexpr float kHe = 0.4375f; // == sim::kPlayerHalfExtents per axis

template <typename F>
world::UnpackedChunk makeChunk(F &&content)
{
	world::UnpackedChunk c;
	c.log2Extent = 4;
	c.cells.resize(4096);
	for (int z = 0; z < 16; ++z)
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x)
				c.cells[voxel::mortonEncode({static_cast<std::uint32_t>(x),
						static_cast<std::uint32_t>(y),
						static_cast<std::uint32_t>(z)})] =
						world::CellValue{content(x, y, z), 0};
	return c;
}

sim::Entity makePlayer(float x, float y, float z)
{
	sim::Entity e;
	e.pos = {x, y, z};
	e.halfExtents = sim::kPlayerHalfExtents;
	return e;
}

void tick(sim::Entity &e, const world::SolidityWorld &w, const sim::PlayerInput &in)
{
	sim::playerStep(e, in, sim::WalkParams{}, sim::StepParams{}, sim::WorldOffset{},
			&world::SolidityWorld::solidAt, &w);
}

void settle(sim::Entity &e, const world::SolidityWorld &w, int ticks)
{
	for (int i = 0; i < ticks; ++i)
		tick(e, w, sim::PlayerInput{});
}

// ---- Walk-replay harness over the minigen world ----------------------------

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

constexpr int kWalkSteps = 4096;
constexpr int kWalkCheckpointInterval = 512;
constexpr int kWalkCheckpoints = kWalkSteps / kWalkCheckpointInterval; // 8

// The demo's world: minigen over chunks (0..7, 0..1, 0..7) at the standard
// seed (terrain surface in [8, 24), trees reaching y <= 31 — all content
// lives inside this box; everything outside is air via the missing-chunk
// rule). The conversion mirrors world/belt.hpp's generate path.
struct WalkWorld {
	std::vector<world::UnpackedChunk> chunks;
	world::SolidityWorld solidity;

	WalkWorld()
	{
		chunks.reserve(8 * 2 * 8);
		for (int cz = 0; cz < 8; ++cz)
			for (int cy = 0; cy < 2; ++cy)
				for (int cx = 0; cx < 8; ++cx) {
					gen::GenChunk g;
					gen::generateChunk(gen::kStandardSeed, {cx, cy, cz}, g);
					chunks.push_back(makeChunk([&g](int x, int y, int z) {
						return static_cast<std::uint32_t>(g.at(x, y, z));
					}));
				}
		// Register AFTER the vector is fully built: addChunk stores raw
		// pointers, and reallocation would invalidate them.
		std::size_t i = 0;
		for (int cz = 0; cz < 8; ++cz)
			for (int cy = 0; cy < 2; ++cy)
				for (int cx = 0; cx < 8; ++cx)
					solidity.addChunk(cx, cy, cz, &chunks[i++]);
	}
};

// Scripted intent, a pure function of the step index built from EXACT float
// values (+-1.0f / 0.0f), so the input bits are identical on every build:
// 512-step segments cycling +X, +Z, -X, -Z (a bounded wander around the
// region center), with a 4-tick jump window every 64 steps.
sim::PlayerInput scriptedInput(int step)
{
	sim::PlayerInput in;
	switch (((step - 1) / kWalkCheckpointInterval) & 3) {
	case 0: in.moveX = 1.0f; break;
	case 1: in.moveZ = 1.0f; break;
	case 2: in.moveX = -1.0f; break;
	default: in.moveZ = -1.0f; break;
	}
	in.jump = (step % 64) < 4;
	return in;
}

struct WalkReplay {
	std::array<std::uint64_t, kWalkCheckpoints> checkpoints{};
	int groundedSteps = 0;
	int airborneSteps = 0;
	sim::Vec3 finalPos;
};

// MXCSR note: unlike the spike-3 replay (which pins FTZ/DAZ off because its
// probe entities decay INTO the subnormal band by design), the walk dynamics
// stay far from subnormals — velocities are either exactly zero or set fresh
// each tick from O(1) constants — so default MXCSR state is immaterial here;
// the subnormal-regime coverage remains spike-3's job.
WalkReplay runWalkReplay(const WalkWorld &w)
{
	sim::Entity p = makePlayer(64.0f, 34.0f, 64.0f); // just above max tree height
	WalkReplay r;
	Fnv1a64 h;
	int checkpoint = 0;
	for (int step = 1; step <= kWalkSteps; ++step) {
		tick(p, w.solidity, scriptedInput(step));
		h.mix32(sim::floatBits(p.pos.x));
		h.mix32(sim::floatBits(p.pos.y));
		h.mix32(sim::floatBits(p.pos.z));
		h.mix32(sim::floatBits(p.vel.x));
		h.mix32(sim::floatBits(p.vel.y));
		h.mix32(sim::floatBits(p.vel.z));
		h.mix32(p.onGround ? 1u : 0u);
		if (p.onGround)
			++r.groundedSteps;
		else
			++r.airborneSteps;
		if (step % kWalkCheckpointInterval == 0)
			r.checkpoints[static_cast<std::size_t>(checkpoint++)] = h.hash;
	}
	r.finalPos = p.pos;
	return r;
}

// Minted on Windows x64, MSVC 19.50 /fp:strict, Release, 2026-06-06 — see
// header comment for the regeneration procedure. Like the spike-3 goldens,
// these are FROZEN once the CI matrix (Linux GCC/Clang, Debug and Release)
// reproduces them: a later divergence on any pinned build is a determinism
// REGRESSION, never a platform quirk to re-mint around.
constexpr std::array<std::uint64_t, kWalkCheckpoints> kWalkGolden = {
	0x36FF700970113448ull, // checkpoint 0 (step  512)
	0x3A60B4C2867EAAC0ull, // checkpoint 1 (step 1024)
	0xB4B1893BCE3961EBull, // checkpoint 2 (step 1536)
	0x34C852A9F80C8B89ull, // checkpoint 3 (step 2048)
	0xC26A33CAC8795605ull, // checkpoint 4 (step 2560)
	0x737A25D8EC26BF6Full, // checkpoint 5 (step 3072)
	0x11284F18152ACD76ull, // checkpoint 6 (step 3584)
	0x1628D6F0564E8E11ull, // checkpoint 7 (step 4096)
};

std::string pasteBlock(const std::array<std::uint64_t, kWalkCheckpoints> &cps)
{
	std::string out;
	for (std::size_t i = 0; i < cps.size(); ++i) {
		char line[96];
		std::snprintf(line, sizeof(line),
				"\t0x%016llXull, // checkpoint %zu (step %4zu)\n",
				static_cast<unsigned long long>(cps[i]), i,
				(i + 1) * kWalkCheckpointInterval);
		out += line;
	}
	return out;
}

} // namespace

// ---- 1. Solidity adapter ----------------------------------------------------

TEST_CASE("SolidityWorld agrees with direct cell reads, positive and negative chunks")
{
	const auto checker = makeChunk(
			[](int x, int y, int z) { return ((x + y + z) & 1) ? 7u : 0u; });
	world::SolidityWorld w;
	w.addChunk(0, 0, 0, &checker);
	w.addChunk(-1, 2, -3, &checker);

	for (int z = 0; z < 16; ++z)
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 16; ++x) {
				const bool expect = ((x + y + z) & 1) != 0;
				REQUIRE(world::SolidityWorld::solidAt(x, y, z, &w) == expect);
				// Same local cell inside chunk (-1, 2, -3).
				REQUIRE(world::SolidityWorld::solidAt(
								x - 16, y + 32, z - 48, &w) == expect);
			}
}

TEST_CASE("SolidityWorld treats missing chunks and air content as not solid")
{
	const auto air = makeChunk([](int, int, int) { return 0u; });
	world::SolidityWorld w;
	w.addChunk(0, 0, 0, &air);

	REQUIRE_FALSE(world::SolidityWorld::solidAt(8, 8, 8, &w));     // air cell
	REQUIRE_FALSE(world::SolidityWorld::solidAt(100, 8, 8, &w));   // missing chunk
	REQUIRE_FALSE(world::SolidityWorld::solidAt(-1, -1, -1, &w));  // missing, negative
	REQUIRE(w.chunkCount() == 1);
}

// ---- 2. Walking semantics ---------------------------------------------------

namespace {

// One 16^3 chunk: solid floor for y in [0, 4), plus an optional wall plane.
world::UnpackedChunk floorChunk(bool withWall)
{
	return makeChunk([withWall](int x, int y, int z) -> std::uint32_t {
		(void)z;
		if (y < 4)
			return 1u;
		if (withWall && x == 12 && y < 9)
			return 1u;
		return 0u;
	});
}

} // namespace

TEST_CASE("Player falls, lands, and rests BIT-EXACTLY flush on the floor top")
{
	const auto chunk = floorChunk(false);
	world::SolidityWorld w;
	w.addChunk(0, 0, 0, &chunk);

	sim::Entity p = makePlayer(8.0f, 8.0f, 8.0f);
	REQUIRE_FALSE(p.onGround);
	settle(p, w, 600);

	REQUIRE(p.onGround);
	// Floor top face is y = 4.0; the dyadic half extent makes the flush
	// clamp exact (the point of 7/16 — see player.hpp).
	REQUIRE(p.pos.y == 4.0f + kHe);
	REQUIRE(p.vel.y == 0.0f);
	REQUIRE(p.pos.x == 8.0f);
	REQUIRE(p.pos.z == 8.0f);

	// Rest is stable: more ticks change nothing.
	settle(p, w, 100);
	REQUIRE(p.onGround);
	REQUIRE(p.pos.y == 4.0f + kHe);
}

TEST_CASE("Wall blocks horizontal walking at the exact face offset")
{
	const auto chunk = floorChunk(true);
	world::SolidityWorld w;
	w.addChunk(0, 0, 0, &chunk);

	sim::Entity p = makePlayer(8.0f, 6.0f, 8.0f);
	settle(p, w, 600); // land first
	REQUIRE(p.onGround);

	sim::PlayerInput in;
	in.moveX = 1.0f;
	for (int i = 0; i < 600; ++i)
		tick(p, w, in);

	// Wall plane occupies x in [12, 13); the +X face of the player box stops
	// flush against it.
	REQUIRE(p.pos.x == 12.0f - kHe);
	REQUIRE(p.pos.y == 4.0f + kHe); // still grounded on the floor
	REQUIRE(p.onGround);
}

TEST_CASE("Jump leaves the ground, arcs, and lands back flush")
{
	const auto chunk = floorChunk(false);
	world::SolidityWorld w;
	w.addChunk(0, 0, 0, &chunk);

	sim::Entity p = makePlayer(8.0f, 8.0f, 8.0f);
	settle(p, w, 600);
	REQUIRE(p.onGround);

	sim::PlayerInput jump;
	jump.jump = true;
	tick(p, w, jump);
	REQUIRE_FALSE(p.onGround);
	REQUIRE(p.pos.y > 4.0f + kHe);

	float peak = p.pos.y;
	int landedAt = -1;
	for (int i = 0; i < 2000; ++i) {
		tick(p, w, sim::PlayerInput{});
		peak = p.pos.y > peak ? p.pos.y : peak;
		if (p.onGround) {
			landedAt = i;
			break;
		}
	}
	REQUIRE(landedAt >= 0);
	REQUIRE(peak > 5.0f); // a real arc, not a hop of a few millicells
	REQUIRE(peak < 7.0f); // and bounded — jump strength is not runaway
	REQUIRE(p.pos.y == 4.0f + kHe);
}

TEST_CASE("Walking off a ledge transitions to falling")
{
	// Floor only for x in [0, 8): a ledge with void beyond.
	const auto chunk = makeChunk([](int x, int y, int z) -> std::uint32_t {
		(void)z;
		return (y < 4 && x < 8) ? 1u : 0u;
	});
	world::SolidityWorld w;
	w.addChunk(0, 0, 0, &chunk);

	sim::Entity p = makePlayer(4.0f, 6.0f, 8.0f);
	settle(p, w, 600);
	REQUIRE(p.onGround);

	sim::PlayerInput in;
	in.moveX = 1.0f;
	bool fell = false;
	for (int i = 0; i < 2000 && !fell; ++i) {
		tick(p, w, in);
		fell = !p.onGround && p.pos.y < 3.0f;
	}
	REQUIRE(fell);
	// Support ends once the box's -X face clears the last solid column.
	REQUIRE(p.pos.x > 8.0f + kHe);
}

TEST_CASE("Ceiling bonk: jump is blocked overhead without granting ground contact")
{
	// Floor y in [0, 4) plus a ceiling slab at y = 5: a single open cell of
	// headroom (the 0.875 box fits with 1/8 to spare), so even the first
	// jump ticks hit the ceiling — robust against any feel-constant tuning,
	// unlike a ceiling placed near the apex of the unobstructed arc.
	const auto chunk = makeChunk([](int x, int y, int z) -> std::uint32_t {
		(void)x;
		(void)z;
		return (y < 4 || y == 5) ? 1u : 0u;
	});
	world::SolidityWorld w;
	w.addChunk(0, 0, 0, &chunk);

	sim::Entity p = makePlayer(8.0f, 4.5f, 8.0f); // inside the one-cell gap
	settle(p, w, 600);
	REQUIRE(p.onGround);

	sim::PlayerInput jump;
	jump.jump = true;
	tick(p, w, jump);
	REQUIRE_FALSE(p.onGround);

	// Rise to the ceiling: the +Y face stops flush at its underside, and
	// blocked-while-moving-UP must never read as ground contact.
	float peak = p.pos.y;
	bool touchedCeiling = false;
	int landedAt = -1;
	for (int i = 0; i < 2000; ++i) {
		tick(p, w, sim::PlayerInput{});
		peak = p.pos.y > peak ? p.pos.y : peak;
		if (p.pos.y == 5.0f - kHe) {
			touchedCeiling = true;
			REQUIRE_FALSE(p.onGround);
		}
		if (p.onGround) {
			landedAt = i;
			break;
		}
	}
	REQUIRE(touchedCeiling);
	REQUIRE(peak == 5.0f - kHe); // flush clamp under the ceiling, never inside
	REQUIRE(landedAt >= 0);
	REQUIRE(p.pos.y == 4.0f + kHe); // back to flush rest on the floor
}

TEST_CASE("Diagonal walk into a two-wall corner stops flush on BOTH axes")
{
	// Floor plus two perpendicular wall planes: x = 12 and z = 12 (above the
	// floor). Diagonal input drives the player into the inside corner; the
	// fixed Y->X->Z axis order must clamp each axis independently.
	const auto chunk = makeChunk([](int x, int y, int z) -> std::uint32_t {
		if (y < 4)
			return 1u;
		if ((x == 12 || z == 12) && y < 9)
			return 1u;
		return 0u;
	});
	world::SolidityWorld w;
	w.addChunk(0, 0, 0, &chunk);

	sim::Entity p = makePlayer(8.0f, 6.0f, 8.0f);
	settle(p, w, 600);
	REQUIRE(p.onGround);

	sim::PlayerInput in;
	in.moveX = 1.0f;
	in.moveZ = 1.0f; // |(1,1)| > 1 is fine: per-axis clamp, not a speed claim
	for (int i = 0; i < 600; ++i)
		tick(p, w, in);

	REQUIRE(p.pos.x == 12.0f - kHe);
	REQUIRE(p.pos.z == 12.0f - kHe);
	REQUIRE(p.pos.y == 4.0f + kHe);
	REQUIRE(p.onGround);
}

TEST_CASE("Spawn inside solid is stable: stuck in place, no tunnel, no corruption")
{
	// The kernel's documented fallback for a box that is ALREADY overlapping
	// (sweepAxis cannot separate it by clamping): restore the pre-substep
	// coordinate and zero the axis velocity. A buried player therefore stays
	// put deterministically — escaping is the engine spawn logic's job, the
	// kernel just guarantees it never tunnels or corrupts.
	const auto solid = makeChunk([](int, int, int) { return 1u; });
	world::SolidityWorld w;
	w.addChunk(0, 0, 0, &solid);

	sim::Entity p = makePlayer(8.0f, 8.0f, 8.0f);
	const sim::Vec3 start = p.pos;
	sim::PlayerInput in;
	in.moveX = 1.0f;
	in.jump = true;
	for (int i = 0; i < 200; ++i)
		tick(p, w, in);

	REQUIRE(p.pos.x == start.x);
	REQUIRE(p.pos.y == start.y);
	REQUIRE(p.pos.z == start.z);
	REQUIRE(p.vel.x == 0.0f);
	REQUIRE(p.vel.y == 0.0f);
	REQUIRE(p.vel.z == 0.0f);
}

TEST_CASE("Walking across negative-coordinate chunk borders keeps exact rest height")
{
	// Four chunks at cy = -1 whose TOP layer (world y = -1) is solid: floor
	// top face at y = 0.0 spanning x, z in [-16, 16).
	const auto slab = makeChunk(
			[](int, int y, int) -> std::uint32_t { return y == 15 ? 1u : 0u; });
	world::SolidityWorld w;
	w.addChunk(-1, -1, -1, &slab);
	w.addChunk(0, -1, -1, &slab);
	w.addChunk(-1, -1, 0, &slab);
	w.addChunk(0, -1, 0, &slab);

	sim::Entity p = makePlayer(0.0f, 2.0f, 0.0f); // box straddles the x = 0 border
	settle(p, w, 600);
	REQUIRE(p.onGround);
	REQUIRE(p.pos.y == 0.0f + kHe);

	// Walk -X across the chunk border; the rest height must never wobble.
	sim::PlayerInput in;
	in.moveX = -1.0f;
	for (int i = 0; i < 200; ++i) {
		tick(p, w, in);
		REQUIRE(p.pos.y == 0.0f + kHe);
		REQUIRE(p.onGround);
	}
	REQUIRE(p.pos.x < -4.0f); // actually traveled into the negative chunk
}

// ---- 3. Walk-replay determinism over the minigen world ----------------------

#if defined(VOXEL2026_SIM_UNPINNED_CONTROL)

TEST_CASE("NEGATIVE CONTROL: unpinned build diverges from the walk-replay goldens")
{
	const WalkWorld w;
	const WalkReplay r = runWalkReplay(w);
	bool anyDifference = false;
	for (std::size_t i = 0; i < kWalkGolden.size(); ++i)
		anyDifference = anyDifference || (r.checkpoints[i] != kWalkGolden[i]);
	// If this fails the experiment has no power — same caveat as the spike-3
	// control: investigate the toolchain before trusting pinned results.
	REQUIRE(anyDifference);
}

#else

TEST_CASE("Walk replay reproduces the golden checkpoint hashes")
{
	const WalkWorld w;
	const WalkReplay r = runWalkReplay(w);
	INFO("computed checkpoints (paste into kWalkGolden if change was intentional):\n"
			<< pasteBlock(r.checkpoints));
	for (std::size_t i = 0; i < kWalkGolden.size(); ++i) {
		CAPTURE(i);
		REQUIRE(r.checkpoints[i] == kWalkGolden[i]);
	}
}

#endif

TEST_CASE("Walk replay is repeatable within one process and non-vacuous")
{
	const WalkWorld w;
	const WalkReplay a = runWalkReplay(w);
	const WalkReplay b = runWalkReplay(w);
	REQUIRE(a.checkpoints == b.checkpoints);

	// Non-vacuity: the trajectory must exercise BOTH regimes — grounded
	// walking (integer-callback collision against real minigen terrain) and
	// airborne integration (the fusible gravity mul-add the control build
	// detects) — and must end inside the loaded region, not in the void
	// (a run that walked off the world would silently test nothing).
	REQUIRE(a.groundedSteps > 500);
	REQUIRE(a.airborneSteps > 500);
	REQUIRE(a.finalPos.x > 1.0f);
	REQUIRE(a.finalPos.x < 127.0f);
	REQUIRE(a.finalPos.z > 1.0f);
	REQUIRE(a.finalPos.z < 127.0f);
	REQUIRE(a.finalPos.y > 1.0f);
	REQUIRE(a.finalPos.y < 40.0f);
}
