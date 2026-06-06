// Spike-4b invariant tests for the radiance-cascade benchmark substrate.
// SKIP-graceful like all GPU tests. The load-bearing one is the analytic
// merge test: cascade compositing is associative, so on a uniform volume the
// fully merged cascade 0 must equal one continuous march over the whole
// radial range — a closed form that pins march arithmetic, the cascade
// layout, AND the merge chain end-to-end (a benchmark of wrong kernels
// measures nothing).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "vk/bench.hpp"
#include "vk/device.hpp"

namespace {

std::vector<std::uint32_t> uniformScene(std::uint32_t extent, std::uint32_t packed)
{
	return std::vector<std::uint32_t>(
			static_cast<std::size_t>(extent) * extent * extent, packed);
}

vk::RcBenchParams smallParams()
{
	vk::RcBenchParams p;
	p.extent = 32;
	p.probeSpacing0 = 4; // cascade 0: 8^3 probes, 16 dirs
	p.octaSide0 = 4;
	p.intervalR0 = 4.0f;
	p.intervalScale = 4;
	p.sampleWidth = 64;
	p.sampleHeight = 64;
	p.iterations = 1;
	p.readback = true;
	return p;
}

} // namespace

TEST_CASE("RC merged cascade 0 equals a continuous march on a uniform volume")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	// Solid mid-gray everywhere: every sample returns exactly (128/255, .., 1)
	// (RGBA8 exact, mip averages of equal values exact), so the merged result
	// has a closed form: rgb = v * (1 - 0.65^N) / 0.35, T = 0.65^N, where N is
	// the TOTAL nominal step count across all cascade intervals.
	vk::RcBenchParams p = smallParams();
	p.occlusionEarlyOut = false;
	const auto scene =
			uniformScene(p.extent, 0x80000000u | (128u << 16) | (128u << 8) | 128u);
	p.scene = scene;
	const vk::RcBenchResult r = vk::runRcBench(dev, p);
	CAPTURE(r.failureReason);
	REQUIRE(r.ran);
	REQUIRE(r.cascadeCount == 3); // 8^3 -> 4^3 -> 2^3 probes at extent 32
	REQUIRE(r.stepsNominal.size() == 3);

	std::uint32_t totalSteps = 0;
	for (const std::uint32_t s : r.stepsNominal)
		totalSteps += s;
	REQUIRE(totalSteps == 28); // 4 + 8 + 16 (intervals [0,4),[4,20),[20,84))

	const double v = 128.0 / 255.0;
	const double tEnd = std::pow(0.65, totalSteps);
	const double rgbEnd = v * (1.0 - tEnd) / 0.35;
	const double tPre = std::pow(0.65, r.stepsNominal[0]);
	const double rgbPre = v * (1.0 - tPre) / 0.35;

	REQUIRE(r.preMergeC0.size() == vk::kRcReadbackEntries * 4);
	REQUIRE(r.postMergeC0.size() == vk::kRcReadbackEntries * 4);
	for (std::size_t e = 0; e < vk::kRcReadbackEntries; ++e) {
		for (int ch = 0; ch < 3; ++ch) {
			const double pre = r.preMergeC0[e * 4 + ch];
			const double post = r.postMergeC0[e * 4 + ch];
			CAPTURE(e, ch, pre, post, rgbPre, rgbEnd);
			REQUIRE(std::abs(pre - rgbPre) < 1e-3);
			REQUIRE(std::abs(post - rgbEnd) < 1e-3);
		}
		CAPTURE(e, r.preMergeC0[e * 4 + 3], r.postMergeC0[e * 4 + 3]);
		REQUIRE(std::abs(r.preMergeC0[e * 4 + 3] - tPre) < 1e-4);
		REQUIRE(std::abs(r.postMergeC0[e * 4 + 3] - tEnd) < 1e-4);
	}

	// The sample pass blends equal probe cubes, so every pixel sees ~rgbEnd.
	REQUIRE(r.sampleQuads.size() == vk::kGatherSampleCount * 4);
	for (std::size_t q = 0; q < vk::kGatherSampleCount; ++q) {
		const double red = r.sampleQuads[q * 4 + 0];
		CAPTURE(q, red, rgbEnd);
		REQUIRE(std::abs(red - rgbEnd) < 2e-3);
	}
}

TEST_CASE("RC cascades and samples are exactly zero in an empty world")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	// Occupancy 0 everywhere makes every accumulation step a multiply by
	// exactly zero and every transmittance update a multiply by exactly one,
	// so the asserts are exact, not approximate.
	vk::RcBenchParams p = smallParams();
	const auto scene = uniformScene(p.extent, 0u);
	p.scene = scene;
	const vk::RcBenchResult r = vk::runRcBench(dev, p);
	CAPTURE(r.failureReason);
	REQUIRE(r.ran);
	for (std::size_t e = 0; e < vk::kRcReadbackEntries; ++e) {
		CAPTURE(e);
		for (int ch = 0; ch < 3; ++ch) {
			REQUIRE(r.preMergeC0[e * 4 + ch] == 0.0f);
			REQUIRE(r.postMergeC0[e * 4 + ch] == 0.0f);
		}
		REQUIRE(r.preMergeC0[e * 4 + 3] == 1.0f);
		REQUIRE(r.postMergeC0[e * 4 + 3] == 1.0f);
	}
	for (std::size_t i = 0; i < r.sampleQuads.size(); i += 4) {
		CAPTURE(i, r.sampleQuads[i]);
		REQUIRE(r.sampleQuads[i + 0] == 0.0f);
		REQUIRE(r.sampleQuads[i + 1] == 0.0f);
		REQUIRE(r.sampleQuads[i + 2] == 0.0f);
	}
}

TEST_CASE("RC merge transports radiance from beyond cascade 0 reach")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	// Bright slab at y in [24,28) only. The cascade-0 probe at grid (0,2,0)
	// sits at world y=10: every direction's cascade-0 interval [0,4) sees
	// only air (nearest content is 14 voxels away, and the REPEAT tiling's
	// nearest image below is also >= 14 away), so its pre-merge entries are
	// exactly zero — but cascade 1's interval [4,20) reaches the slab, so
	// post-merge the probe MUST see it. This fails if the merge chain drops,
	// misindexes, or never runs.
	vk::RcBenchParams p = smallParams();
	std::vector<std::uint32_t> scene(static_cast<std::size_t>(32) * 32 * 32, 0u);
	for (std::uint32_t z = 0; z < 32; ++z)
		for (std::uint32_t y = 24; y < 28; ++y)
			for (std::uint32_t x = 0; x < 32; ++x)
				scene[x + 32 * (y + 32 * static_cast<std::size_t>(z))] =
						0x80000000u | (255u << 16) | (255u << 8) | 255u;
	p.scene = scene;
	const vk::RcBenchResult r = vk::runRcBench(dev, p);
	CAPTURE(r.failureReason);
	REQUIRE(r.ran);

	// Probe (0,2,0) in layout order: ((pz*8 + py)*8 + px) = 16; 16 dirs each.
	const std::size_t probeBase = 16 * 16;
	REQUIRE(probeBase + 16 <= vk::kRcReadbackEntries);
	float postMax = 0.0f;
	for (std::size_t dir = 0; dir < 16; ++dir) {
		const std::size_t e = probeBase + dir;
		CAPTURE(dir, r.preMergeC0[e * 4 + 0]);
		REQUIRE(r.preMergeC0[e * 4 + 0] == 0.0f);
		REQUIRE(r.preMergeC0[e * 4 + 3] == 1.0f);
		postMax = std::max(postMax, r.postMergeC0[e * 4 + 0]);
	}
	CAPTURE(postMax);
	REQUIRE(postMax > 0.0f);
}

TEST_CASE("RC step instrumentation matches nominal and reflects early-out")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	vk::RcBenchParams p = smallParams();
	const auto scene =
			uniformScene(p.extent, 0x80000000u | (255u << 16) | (255u << 8) | 255u);
	p.scene = scene;

	// Early-out OFF: every ray executes exactly its nominal step budget, so
	// the per-cascade means are exact integers.
	p.occlusionEarlyOut = false;
	{
		const vk::RcBenchResult r = vk::runRcBench(dev, p);
		CAPTURE(r.failureReason);
		REQUIRE(r.ran);
		for (std::size_t i = 0; i < r.meanStepsPerRay.size(); ++i) {
			CAPTURE(i, r.meanStepsPerRay[i], r.stepsNominal[i]);
			REQUIRE(r.meanStepsPerRay[i] == static_cast<double>(r.stepsNominal[i]));
		}
	}
	// Early-out ON in a fully solid world: transmittance crosses the 0.01
	// cutoff after 11 samples (0.65^11 ~ 0.0087), so cascades 0 and 1 (4 and
	// 8 steps) still run their full budget while cascade 2 (16 steps) breaks
	// at exactly 11 — identically for every ray.
	p.occlusionEarlyOut = true;
	{
		const vk::RcBenchResult r = vk::runRcBench(dev, p);
		CAPTURE(r.failureReason);
		REQUIRE(r.ran);
		REQUIRE(r.meanStepsPerRay[0] == 4.0);
		REQUIRE(r.meanStepsPerRay[1] == 8.0);
		CAPTURE(r.meanStepsPerRay[2]);
		REQUIRE(r.meanStepsPerRay[2] == 11.0);
	}
}

TEST_CASE("RC passes report sane timings on the minigen scene")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	vk::RcBenchParams p;
	p.extent = 64;
	p.sampleWidth = 256;
	p.sampleHeight = 256;
	const auto scene = vk::fillSceneFromMinigen(p.extent);
	p.scene = scene;
	const vk::RcBenchResult r = vk::runRcBench(dev, p);
	CAPTURE(r.failureReason);
	REQUIRE(r.ran);
	// inject + mips + marches + merges + resolve + sample.
	const std::size_t mips = 7; // extent 64
	REQUIRE(r.cascadeCount == 4);
	REQUIRE(r.passes.size() == 1 + (mips - 1) + 4 + 3 + 1 + 1);
	REQUIRE(r.reachVoxels >= r.diagonalVoxels); // coverage achieved, not silent
	if (dev.report().timestampPeriodNs > 0.0f)
		for (const vk::PassTiming &t : r.passes) {
			CAPTURE(t.name, t.millis);
			REQUIRE(t.millis >= 0.0);
			REQUIRE(t.millis < 1000.0);
		}
}
