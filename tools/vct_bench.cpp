// Spike-4 VCT cost-driver benchmark. Reports MEDIAN per-pass GPU times over
// warm iterations (first iteration discarded) plus the actual executed
// steps/cone, across the cost envelope:
//   - minigen scene with occlusion early-out (realistic content), and
//   - empty volume with early-out disabled (fixed-work worst case: every
//     cone runs the full nominal step budget).
// Developer-hardware tool; prints "unavailable" cleanly on driverless hosts.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "vk/bench.hpp"
#include "vk/device.hpp"

namespace {

int runRow(vk::Device &dev, const char *label, vk::VctBenchParams &params)
{
	const vk::VctBenchResult r = vk::runVctBench(dev, params);
	std::printf("=== %s: extent %u^3, %u mips, %u traces x %u cones, step cap %u ===\n",
			label, r.extent, r.mipLevels, r.traceCount, r.conesPerTrace,
			params.coneSteps);
	if (!r.ran) {
		std::printf("FAILED: %s\n", r.failureReason.c_str());
		return 1;
	}
	double total = 0.0;
	for (const vk::PassTiming &p : r.passes) {
		std::printf("  %-12s %8.4f ms\n", p.name.c_str(), p.millis);
		total += p.millis > 0 ? p.millis : 0;
	}
	std::printf("  %-12s %8.4f ms   (median of %u warm iterations)\n", "TOTAL", total,
			r.iterations);
	std::printf("  executed steps/cone: %.1f of %u nominal\n\n", r.meanStepsPerCone,
			params.coneSteps);
	return 0;
}

} // namespace

int main()
{
	vk::Device dev;
	if (!dev.available()) {
		std::printf("Vulkan unavailable: %s\n", dev.report().failureReason.c_str());
		return 0;
	}
	std::printf("device: %s (timestampValidBits %u)\n\n",
			dev.report().deviceName.c_str(), dev.report().timestampValidBits);

	// Realistic-content rows: minigen scene, occlusion early-out enabled.
	for (const std::uint32_t extent : {64u, 128u, 256u}) {
		vk::VctBenchParams p;
		p.extent = extent;
		p.occlusionEarlyOut = true;
		const std::vector<std::uint32_t> scene = vk::fillSceneFromMinigen(extent);
		p.scene = scene;
		if (runRow(dev, "minigen, early-out ON", p) != 0)
			return 1;
	}

	// Worst-case envelope: empty volume (no occlusion -> no early-out is
	// even possible) at the largest extent — every cone marches the full cap.
	{
		vk::VctBenchParams p;
		p.extent = 256;
		p.occlusionEarlyOut = false;
		const std::vector<std::uint32_t> scene(
				static_cast<std::size_t>(256) * 256 * 256, 0u);
		p.scene = scene;
		if (runRow(dev, "EMPTY (worst case), early-out OFF", p) != 0)
			return 1;
	}
	return 0;
}
