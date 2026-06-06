// THE Contract 5 spike experiment (issue #3): every pinned build on every
// platform in the CI matrix must reproduce these exact checkpoint hashes. The
// goldens were minted on Windows x64 MSVC /fp:strict Release; Linux GCC and
// Clang, Debug and Release, reproducing them bit-for-bit is the spike's pass
// criterion.
//
// Falsifiability: when built with -DVOXEL2026_SIM_UNPINNED_CONTROL=ON (FMA
// contraction enabled), the same replay must DIVERGE from the goldens —
// proving the pinned flags are load-bearing. The kernel contains a fusible
// multiply-add with an inexact product (vel.y + gravity*dt) specifically so
// this control has something to detect.
//
// To regenerate after an INTENTIONAL kernel/harness change: build and run the
// `replay_hash` tool (pinned build!) and paste its output here. Any
// UNINTENTIONAL change to these values is a determinism regression and must
// be treated as a bug.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include "sim/replay.hpp"

namespace {

// Minted by tools/replay_hash on Windows x64, MSVC 19.50 /fp:strict, Release,
// 2026-06-05 — see header comment.
constexpr std::array<std::uint64_t, sim::kReplayCheckpoints> kGolden = {
	0xFDC0E39C32AEA3E9ull, // checkpoint 0 (step  512)
	0xFBBAC90B5BA9CF0Bull, // checkpoint 1 (step 1024)
	0xE300F80824C8C0B5ull, // checkpoint 2 (step 1536)
	0xDFC3F8FCCD2218B7ull, // checkpoint 3 (step 2048)
	0xCAA1319398AA6A8Cull, // checkpoint 4 (step 2560)
	0xDE2EC820F360F669ull, // checkpoint 5 (step 3072)
	0x6D4FCCF2D0FD21F0ull, // checkpoint 6 (step 3584)
	0x171FD77F31463217ull, // checkpoint 7 (step 4096)
};

} // namespace

#if defined(VOXEL2026_SIM_UNPINNED_CONTROL)

TEST_CASE("NEGATIVE CONTROL: unpinned (contraction-enabled) build diverges from the goldens")
{
	const sim::ReplayResult r = sim::runReplay();
	bool anyDifference = false;
	for (std::size_t i = 0; i < kGolden.size(); ++i)
		anyDifference = anyDifference || (r.checkpoints[i] != kGolden[i]);
	// If this fails, the experiment has no power: either the kernel lost its
	// inexact-product mul-add, or the control flags did not actually enable
	// contraction on this toolchain. Investigate before trusting any green
	// pinned-matrix result.
	REQUIRE(anyDifference);
}

#else

TEST_CASE("Replay reproduces the golden checkpoint hashes (cross-build bit-identity)")
{
	const sim::ReplayResult r = sim::runReplay();
	for (std::size_t i = 0; i < kGolden.size(); ++i) {
		CAPTURE(i, r.checkpoints[i]);
		REQUIRE(r.checkpoints[i] == kGolden[i]);
	}
}

#endif

TEST_CASE("Replay is repeatable within one process")
{
	const sim::ReplayResult a = sim::runReplay();
	const sim::ReplayResult b = sim::runReplay();
	REQUIRE(a.checkpoints == b.checkpoints);
}
