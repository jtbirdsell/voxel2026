// Spike-4 GI cost-driver benchmark: stage 4a (voxel cone tracing) and stage
// 4b (radiance cascades) on the same scene, substrate, and measurement
// discipline. Reports MEDIAN per-pass GPU times over warm iterations (first
// iteration discarded) plus actual executed steps, across the cost envelope:
//   - minigen scene with occlusion early-out (realistic content), and
//   - empty volume with early-out disabled (fixed-work worst case).
// Developer-hardware tool; prints "unavailable" cleanly on driverless hosts.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "vk/bench.hpp"
#include "vk/device.hpp"

namespace {

int runVctRow(vk::Device &dev, const char *label, vk::VctBenchParams &params)
{
	const vk::VctBenchResult r = vk::runVctBench(dev, params);
	std::printf("=== VCT %s: extent %u^3, %u mips, %u traces x %u cones, step cap %u ===\n",
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

int runRcRow(vk::Device &dev, const char *label, vk::RcBenchParams &params)
{
	const vk::RcBenchResult r = vk::runRcBench(dev, params);
	// Surface BOTH interval parameters in the header (review finding: the
	// scale-2 row necessarily couples r0=8 for coverage — hiding r0 invites
	// single-variable readings of a two-variable change).
	std::printf("=== RC %s: extent %u^3, scale %u, r0 %.0f, %u cascades ===\n", label,
			r.extent, params.intervalScale, static_cast<double>(params.intervalR0),
			r.cascadeCount);
	if (!r.ran) {
		std::printf("FAILED: %s\n", r.failureReason.c_str());
		return 1;
	}
	// Memory, itemized (review finding): cascade entries + ambient cubes are
	// RC-inherent ON TOP of the radiance volume both techniques share; the
	// per-pixel output buffer is harness overhead symmetric with VCT's.
	const double mb = 1024.0 * 1024.0;
	const double cubesMb = static_cast<double>(r.probesAxis.empty() ? 0
											: static_cast<std::uint64_t>(r.probesAxis[0]) *
							r.probesAxis[0] * r.probesAxis[0] * 6 * 16) / mb;
	std::printf("  RC-inherent memory: %.1f MB cascades + %.1f MB cubes (on top of the shared radiance volume)\n",
			static_cast<double>(r.cascadeMemoryBytes) / mb, cubesMb);
	std::printf("  reach %.0f of %.0f voxels (diagonal)%s\n", r.reachVoxels,
			r.diagonalVoxels, r.reachVoxels >= r.diagonalVoxels ? "" : "  ** SHORTFALL **");
	for (std::uint32_t i = 0; i < r.cascadeCount; ++i)
		std::printf("  cascade %u: %u^3 probes x %u dirs = %llu rays, steps %.1f of %u nominal\n",
				i, r.probesAxis[i], r.dirCount[i],
				static_cast<unsigned long long>(r.rays[i]), r.meanStepsPerRay[i],
				r.stepsNominal[i]);
	double volume = 0.0, build = 0.0, query = 0.0;
	for (const vk::PassTiming &p : r.passes) {
		std::printf("  %-12s %8.4f ms\n", p.name.c_str(), p.millis);
		const double v = p.millis > 0 ? p.millis : 0;
		if (p.name.rfind("march", 0) == 0 || p.name.rfind("merge", 0) == 0)
			build += v;
		else if (p.name == "resolve" || p.name == "sample")
			query += v;
		else
			volume += v;
	}
	std::printf("  volume(inject+mips) %.4f ms | cascade build %.4f ms | query(resolve+sample) %.4f ms\n",
			volume, build, query);
	// Review finding: the query-only number is the per-frame cost ONLY under
	// a no-rebuild cadence; print both cadence extremes so neither can be
	// quoted without its assumption.
	std::printf("  per-frame if cascades static: %.4f ms | if rebuilt every frame: %.4f ms\n",
			query, volume + build + query);
	std::printf("  TOTAL %.4f ms   (median of %u warm iterations)\n\n",
			volume + build + query, r.iterations);
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

	// ---- Stage 4a rows: VCT. Realistic content, then the worst case.
	for (const std::uint32_t extent : {64u, 128u, 256u}) {
		vk::VctBenchParams p;
		p.extent = extent;
		p.occlusionEarlyOut = true;
		const std::vector<std::uint32_t> scene = vk::fillSceneFromMinigen(extent);
		p.scene = scene;
		if (runVctRow(dev, "minigen, early-out ON", p) != 0)
			return 1;
	}
	{
		vk::VctBenchParams p;
		p.extent = 256;
		p.occlusionEarlyOut = false;
		const std::vector<std::uint32_t> scene(
				static_cast<std::size_t>(256) * 256 * 256, 0u);
		p.scene = scene;
		if (runVctRow(dev, "EMPTY (worst case), early-out OFF", p) != 0)
			return 1;
	}

	// ---- Stage 4b rows: radiance cascades, same scenes and discipline.
	for (const std::uint32_t extent : {64u, 128u, 256u}) {
		vk::RcBenchParams p;
		p.extent = extent;
		p.occlusionEarlyOut = true;
		const std::vector<std::uint32_t> scene = vk::fillSceneFromMinigen(extent);
		p.scene = scene;
		if (runRcRow(dev, "minigen, early-out ON", p) != 0)
			return 1;
	}
	// Interval-scale-2 variant: constant steps/ray per cascade instead of
	// doubling; needs r0=8 so the reach still covers the 256^3 diagonal
	// before the probe-grid floor (documented in the spike doc).
	{
		vk::RcBenchParams p;
		p.extent = 256;
		p.intervalScale = 2;
		p.intervalR0 = 8.0f;
		p.occlusionEarlyOut = true;
		const std::vector<std::uint32_t> scene = vk::fillSceneFromMinigen(256);
		p.scene = scene;
		if (runRcRow(dev, "minigen, scale 2, early-out ON", p) != 0)
			return 1;
	}
	{
		vk::RcBenchParams p;
		p.extent = 256;
		p.occlusionEarlyOut = false;
		const std::vector<std::uint32_t> scene(
				static_cast<std::size_t>(256) * 256 * 256, 0u);
		p.scene = scene;
		if (runRcRow(dev, "EMPTY (worst case), early-out OFF", p) != 0)
			return 1;
	}
	return 0;
}
