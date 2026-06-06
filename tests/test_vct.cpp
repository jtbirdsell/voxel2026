// Spike-4a invariant tests for the VCT benchmark substrate. SKIP-graceful
// like all GPU tests; on GPU hosts these pin that the measured passes
// actually compute the right thing (a benchmark of wrong kernels measures
// nothing).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "vk/bench.hpp"
#include "vk/device.hpp"

namespace {

// Uniform scene: every voxel solid with mid-gray emissive (128,128,128).
std::vector<std::uint32_t> uniformScene(std::uint32_t extent, std::uint32_t packed)
{
	return std::vector<std::uint32_t>(
			static_cast<std::size_t>(extent) * extent * extent, packed);
}

} // namespace

TEST_CASE("VCT mip chain averages correctly on a non-uniform checkerboard")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	// (Review finding: the uniform test is trivially exact under any
	// averaging bug — this pins the actual arithmetic and neighbor indexing.)
	// Checkerboard by cell parity: emissive 100 or 156 on every channel; every
	// 2x2x2 block holds four of each, so EVERY mip level averages to exactly
	// 128 — representable at each level, so RGBA8 rounding never enters.
	vk::VctBenchParams p;
	p.extent = 32;
	p.readback = true;
	std::vector<std::uint32_t> scene(static_cast<std::size_t>(32) * 32 * 32);
	for (std::uint32_t z = 0; z < 32; ++z)
		for (std::uint32_t y = 0; y < 32; ++y)
			for (std::uint32_t x = 0; x < 32; ++x) {
				const std::uint32_t v = ((x + y + z) & 1u) ? 100u : 156u;
				scene[x + 32 * (y + 32 * static_cast<std::size_t>(z))] =
						0x80000000u | (v << 16) | (v << 8) | v;
			}
	p.scene = scene;
	const vk::VctBenchResult r = vk::runVctBench(dev, p);
	CAPTURE(r.failureReason);
	REQUIRE(r.ran);
	REQUIRE(r.topMipAverage[0] == 128.0f);
	REQUIRE(r.topMipAverage[1] == 128.0f);
	REQUIRE(r.topMipAverage[2] == 128.0f);
	REQUIRE(r.topMipAverage[3] == 255.0f);
}

TEST_CASE("VCT mip chain conserves the mean: uniform scene tops out at its own value")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	// Solid everywhere, emissive (128, 64, 32): every mip level, including
	// the final 1x1x1, must average to exactly the same value (averaging a
	// constant), within RGBA8 rounding.
	vk::VctBenchParams p;
	p.extent = 32;
	p.readback = true;
	const auto scene = uniformScene(p.extent, 0x80000000u | (128u << 16) | (64u << 8) | 32u);
	p.scene = scene;
	const vk::VctBenchResult r = vk::runVctBench(dev, p);
	CAPTURE(r.failureReason);
	REQUIRE(r.ran);
	REQUIRE(r.topMipAverage[0] == 128.0f); // R
	REQUIRE(r.topMipAverage[1] == 64.0f);  // G
	REQUIRE(r.topMipAverage[2] == 32.0f);  // B
	REQUIRE(r.topMipAverage[3] == 255.0f); // A (occupancy: solid everywhere)
}

TEST_CASE("VCT cone gather: emissive-filled volume yields radiance, empty volume yields none")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	vk::VctBenchParams p;
	p.extent = 32;
	p.traceWidth = 64;
	p.traceHeight = 64;
	p.readback = true;

	// Empty world: gather must be exactly zero everywhere.
	{
		const auto scene = uniformScene(p.extent, 0u);
		p.scene = scene;
		const vk::VctBenchResult r = vk::runVctBench(dev, p);
		CAPTURE(r.failureReason);
		REQUIRE(r.ran);
		for (std::size_t i = 0; i < r.gatherSamples.size(); ++i) {
			CAPTURE(i, r.gatherSamples[i]);
			REQUIRE(r.gatherSamples[i] == 0.0f);
		}
	}
	// Bright solid world: every gather must accumulate positive radiance and
	// positive occlusion, bounded by the accumulation maxima.
	{
		const auto scene =
				uniformScene(p.extent, 0x80000000u | (255u << 16) | (255u << 8) | 255u);
		p.scene = scene;
		const vk::VctBenchResult r = vk::runVctBench(dev, p);
		CAPTURE(r.failureReason);
		REQUIRE(r.ran);
		REQUIRE(r.gatherSamples.size() == vk::kGatherSampleCount * 4);
		for (std::size_t q = 0; q < vk::kGatherSampleCount; ++q) {
			const float red = r.gatherSamples[q * 4 + 0];
			const float occ = r.gatherSamples[q * 4 + 3];
			CAPTURE(q, red, occ);
			REQUIRE(red > 0.0f);
			REQUIRE(red <= 4.0f); // generous accumulation bound
			REQUIRE(occ > 0.0f);
			REQUIRE(occ <= 1.0f);
		}
	}
}

TEST_CASE("VCT passes report sane timings on the minigen scene")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	vk::VctBenchParams p;
	p.extent = 64;
	p.traceWidth = 256;
	p.traceHeight = 256;
	const auto scene = vk::fillSceneFromMinigen(p.extent);
	p.scene = scene;
	const vk::VctBenchResult r = vk::runVctBench(dev, p);
	CAPTURE(r.failureReason);
	REQUIRE(r.ran);
	REQUIRE(r.passes.size() == 1 + (r.mipLevels - 1) + 1);
	if (dev.report().timestampPeriodNs > 0.0f)
		for (const vk::PassTiming &t : r.passes) {
			CAPTURE(t.name, t.millis);
			REQUIRE(t.millis >= 0.0);
			REQUIRE(t.millis < 1000.0);
		}
}
