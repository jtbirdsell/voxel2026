// Spike-2 experiment (issue #2): streaming origin rebasing — Contract 4's
// mechanism. Proves: identical rebase schedules keep machines bit-identical
// in virtual-absolute space; different schedules genuinely diverge (the
// identical-schedule clause is load-bearing); rebase subtraction is bit-exact
// in the Sterbenz window and deterministically lossy (<= 1 ulp) outside it;
// far-origin ulp starvation freezes motion and rebasing cures it.
// See docs/spikes/spike-2-origin-rebasing.md.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "sim/collision_step.hpp"
#include "sim/det_math.hpp"
#include "sim/rebase.hpp"

namespace {

// A drift-following schedule: rebase +192 cells after each quarter of the
// run, tracking the population's +x migration.
std::vector<sim::RebaseEvent> standardSchedule()
{
	return {{1024, 192}, {2048, 192}, {3072, 192}};
}

// Golden checkpoints for runRebasedReplay(standardSchedule()), minted on the
// reference build (MSVC /fp:strict Release). Regenerate like the spike-3
// goldens: zero them, run, paste the CAPTUREd actuals, re-run green.
constexpr std::array<std::uint64_t, sim::kRebaseCheckpoints> kGolden = {
	0xDA2E8EA372E491D5ull, // checkpoint 0 (step  512)
	0x79EFE0DA42CE77D9ull, // checkpoint 1 (step 1024)
	0x5C03DA9B70B4FEEFull, // checkpoint 2 (step 1536)
	0x5F32FF5AF2E0638Aull, // checkpoint 3 (step 2048)
	0x0E57476EC7FB3012ull, // checkpoint 4 (step 2560)
	0x855885A3E164B9E2ull, // checkpoint 5 (step 3072)
	0xD4014EDB7AD6E1BEull, // checkpoint 6 (step 3584)
	0x74151698AA0CA084ull, // checkpoint 7 (step 4096)
}; // minted 2026-06-06, MSVC /fp:strict Release (tools/replay_hash)

} // namespace

TEST_CASE("Identical rebase schedules keep two machines bit-identical in virtual space")
{
	const auto schedule = standardSchedule();
	const sim::RebasedReplayResult a = sim::runRebasedReplay(schedule);
	const sim::RebasedReplayResult b = sim::runRebasedReplay(schedule);
	REQUIRE(a.checkpoints == b.checkpoints);
}

#if defined(VOXEL2026_SIM_UNPINNED_CONTROL)

TEST_CASE("NEGATIVE CONTROL: unpinned rebased replay diverges from the goldens")
{
	// (Review blocker: without this guard the contraction-enabled control CI
	// leg — which builds the whole suite — would fail the equality assertion
	// below. Same split as test_replay_determinism.cpp.)
	const sim::RebasedReplayResult r = sim::runRebasedReplay(standardSchedule());
	bool anyDifference = false;
	for (std::size_t i = 0; i < kGolden.size(); ++i)
		anyDifference = anyDifference || (r.checkpoints[i] != kGolden[i]);
	REQUIRE(anyDifference);
}

#else

TEST_CASE("Rebased replay reproduces the committed golden checkpoints")
{
	const sim::RebasedReplayResult r = sim::runRebasedReplay(standardSchedule());
	for (std::size_t i = 0; i < kGolden.size(); ++i) {
		CAPTURE(i, r.checkpoints[i]);
		REQUIRE(r.checkpoints[i] == kGolden[i]);
	}
}

#endif

TEST_CASE("NEGATIVE CONTROL: different rebase schedules diverge in virtual space")
{
	// Same simulation, same seed — only the rebase timing differs. If this
	// did NOT diverge, the Contract 4 clause 'server and client rebase
	// identically' would be decorative; post-rebase float math runs at
	// different magnitudes, so rounding genuinely differs.
	const auto withRebases = standardSchedule();
	const std::vector<sim::RebaseEvent> without; // never rebases
	const sim::RebasedReplayResult a = sim::runRebasedReplay(withRebases);
	const sim::RebasedReplayResult b = sim::runRebasedReplay(without);

	bool anyDifference = false;
	for (std::size_t i = 0; i < a.checkpoints.size(); ++i)
		anyDifference = anyDifference || (a.checkpoints[i] != b.checkpoints[i]);
	REQUIRE(anyDifference);
}

#if !defined(VOXEL2026_SIM_UNPINNED_CONTROL)
// The two exactness-instrument tests below are meaningful ONLY under pinned
// FP semantics: in the contraction-enabled control build, fast-math folds the
// add-back reconstruction (x - d) + d algebraically back to x inside
// rebaseFrame, making the instrument read "exact" unconditionally —
// discovered when the control build failed these tests, which is itself
// evidence that the pinned flags are load-bearing for the *measurement*, not
// just the trajectories.

TEST_CASE("Rebase exactness is per-entity: in-window exact, spread populations are not all-exact")
{
	// SPIKE FINDING (this test originally asserted allRebasesExact == true
	// and failed — correctly): a frame-wide rebase over a spatially SPREAD
	// population always has stragglers below the Sterbenz window
	// (local < delta/2), whose subtraction must round. Determinism is
	// unaffected (the identical-schedule test passes; rounding is the same
	// on every machine), but losslessness is positional — the engine
	// contract must say so. Pin the reality:
	const sim::RebasedReplayResult r = sim::runRebasedReplay(standardSchedule());
	REQUIRE_FALSE(r.allRebasesExact);

	// Per-entity, in-window: 256 <= 600.3 <= 1024 (Sterbenz for d=512) => exact.
	sim::Frame f;
	sim::Entity e;
	e.pos = {600.3f, 30.0f, 0.5f};
	sim::Entity arr[1] = {e};
	REQUIRE(sim::rebaseFrame(f, arr, 512, 0, 0));
	REQUIRE(sim::floatBits(arr[0].pos.x + 512.0f) == sim::floatBits(600.3f));
	REQUIRE(f.ox == 512);
}

TEST_CASE("Rebase outside the Sterbenz window is deterministically lossy, bounded by one ulp")
{
	// local = 0.3f, delta = 512: result ~ -511.7 < 512 = 2^9 has exponent 8,
	// so ulp(result) = 2^(8-23) = 2^-15 ~ 3.05e-5 — coarser than 0.3f's low
	// bits, so the subtraction must round. The loss is deterministic (same on
	// every machine) and bounded by 0.5 ulp; we assert <= 1 ulp for slack.
	// (Review-corrected: comment previously claimed ulp = 2^-14.)
	sim::Frame f;
	sim::Entity arr[1];
	arr[0].pos = {0.3f, 30.0f, 0.5f};
	const bool exact = sim::rebaseFrame(f, arr, 512, 0, 0);
	REQUIRE_FALSE(exact);

	const float reconstructed = arr[0].pos.x + 512.0f;
	// Bit-level statement of the load-bearing fact (review-corrected from a
	// value inequality): the reconstruction lost bits.
	REQUIRE(sim::floatBits(reconstructed) != sim::floatBits(0.3f));
	REQUIRE(std::fabs(static_cast<double>(reconstructed) - 0.3) <= 3.1e-5);

	// Determinism of the loss: repeat -> identical bits.
	sim::Frame f2;
	sim::Entity arr2[1];
	arr2[0].pos = {0.3f, 30.0f, 0.5f};
	(void)sim::rebaseFrame(f2, arr2, 512, 0, 0);
	REQUIRE(sim::floatBits(arr2[0].pos.x) == sim::floatBits(arr[0].pos.x));
}

#endif // !VOXEL2026_SIM_UNPINNED_CONTROL

TEST_CASE("Velocities and extents are frame-invariant under rebase")
{
	sim::Frame f;
	sim::Entity arr[1];
	arr[0].pos = {600.0f, 30.0f, 0.5f};
	arr[0].vel = {3.25f, -1.5f, 0.125f};
	arr[0].halfExtents = {0.3f, 0.45f, 0.3f};
	arr[0].onGround = true;
	(void)sim::rebaseFrame(f, arr, 512, 0, 0);
	REQUIRE(sim::floatBits(arr[0].vel.x) == sim::floatBits(3.25f));
	REQUIRE(sim::floatBits(arr[0].vel.y) == sim::floatBits(-1.5f));
	REQUIRE(sim::floatBits(arr[0].vel.z) == sim::floatBits(0.125f));
	REQUIRE(arr[0].halfExtents.x == 0.3f);
	REQUIRE(arr[0].onGround);
}

TEST_CASE("DISEASE: far-origin ulp starvation freezes motion; rebasing cures it")
{
	const sim::StepParams p{};

	// Near the origin, a horizontally-kicked entity moves.
	sim::Entity near;
	near.pos = {100.5f, 40.0f, 0.5f};
	near.vel.x = 5.0f;
	near.halfExtents = {0.3f, 0.45f, 0.3f};
	for (int i = 0; i < 50; ++i)
		sim::collisionStep(near, p);
	REQUIRE(near.pos.x > 100.5f);

	// At |x| ~ 2^22 the per-step displacement (~0.037) is below ulp (0.5):
	// position addition rounds to no-op and motion FREEZES — the disease.
	sim::Entity far;
	far.pos = {4194304.0f, 40.0f, 0.5f}; // 2^22
	far.vel.x = 5.0f;
	far.halfExtents = {0.3f, 0.45f, 0.3f};
	for (int i = 0; i < 50; ++i)
		sim::collisionStep(far, p);
	REQUIRE(far.pos.x == 4194304.0f);

	// CURE: express the same virtual position as origin 2^22 + local 0 and
	// step in the local frame (world queries translated by the origin in
	// integer math) — motion resumes.
	sim::Entity cured;
	cured.pos = {0.0f, 40.0f, 0.5f};
	cured.vel.x = 5.0f;
	cured.halfExtents = {0.3f, 0.45f, 0.3f};
	const sim::WorldOffset origin{4194304, 0, 0};
	for (int i = 0; i < 50; ++i)
		sim::collisionStep(cured, p, origin);
	REQUIRE(cured.pos.x > 0.0f);
	REQUIRE(sim::virtualCoord(origin.x, cured.pos.x) > 4194304.0);
}

TEST_CASE("Zero-offset overload is bit-identical to the offset entry point")
{
	const sim::StepParams p{};
	sim::Entity a, b;
	a.pos = b.pos = {7.25f, 30.0f, -3.5f};
	a.vel = b.vel = {2.0f, -1.0f, 0.5f};
	a.halfExtents = b.halfExtents = {0.3f, 0.45f, 0.3f};
	for (int i = 0; i < 200; ++i) {
		sim::collisionStep(a, p);
		sim::collisionStep(b, p, sim::WorldOffset{});
	}
	REQUIRE(sim::floatBits(a.pos.x) == sim::floatBits(b.pos.x));
	REQUIRE(sim::floatBits(a.pos.y) == sim::floatBits(b.pos.y));
	REQUIRE(sim::floatBits(a.pos.z) == sim::floatBits(b.pos.z));
}
