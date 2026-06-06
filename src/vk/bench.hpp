// GPU benchmark substrate for spike #4 (RC vs VCT, issue #4): device-local
// 3D images with mip chains, scene-buffer upload, multi-pass timestamped
// dispatch, and image readback for invariant tests.
//
// Deliberately spike-shaped, not engine-shaped: pipelines and descriptor
// layouts are hardcoded per kernel family below rather than abstracted — the
// engine's frame graph is Phase-2 work; this exists to produce trustworthy
// per-pass numbers on real hardware.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "vk/device.hpp"

namespace vk {

// One radiance volume level-0 + full mip chain (RGBA8, power-of-two cube),
// plus a same-extent R32_UINT occupancy/emissive scene volume.
struct BenchVolumes;

struct PassTiming {
	std::string name;
	double millis = -1.0;
};

struct VctBenchResult {
	bool ran = false;
	std::string failureReason;

	std::uint32_t extent = 0;     // level-0 cube edge, voxels
	std::uint32_t mipLevels = 0;
	std::uint32_t traceCount = 0; // cone-gather invocations (synthetic pixels)
	std::uint32_t conesPerTrace = 0;
	std::uint32_t iterations = 0; // timed iterations contributing to medians

	// MEDIAN per-pass time across iterations, first (warmup) iteration
	// excluded (review-corrected: single cold submissions carried first-touch
	// bias and unjustified significant figures).
	std::vector<PassTiming> passes; // inject, mip[1..N], coneGather

	// Average EXECUTED steps per cone, from the instrumented (untimed) stats
	// pass — costs are quoted per real traversal work, never per nominal cap
	// (review blocker: the original kernel executed ~8 of 64 nominal steps).
	double meanStepsPerCone = -1.0;

	// Invariant-test hooks (filled when `readback` requested):
	float topMipAverage[4] = {0, 0, 0, 0}; // top 1x1x1 mip texel, 0..255
	std::vector<float> gatherSamples;      // RGBA quads, first kGatherSampleCount
};

inline constexpr std::uint32_t kGatherSampleCount = 8;

struct VctBenchParams {
	std::uint32_t extent = 128;      // level-0 edge (power of two)
	std::uint32_t traceWidth = 1920; // synthetic gather grid
	std::uint32_t traceHeight = 1080;
	std::uint32_t coneSteps = 64;    // nominal step cap per cone
	// Fixed ABSOLUTE surface height in voxels (review blocker: a normalized
	// height made each extent row measure different geometry vs the 64-tall
	// minigen world). Default sits just above the terrain+tree band.
	std::uint32_t originVoxelY = 40;
	bool occlusionEarlyOut = true;   // false = fixed-work worst-case envelope
	std::uint32_t iterations = 8;    // timed repeats (median; +1 warmup)
	bool readback = false;           // fill invariant-test hooks (slower)
	std::span<const std::uint32_t> scene; // extent^3 packed voxels
};

// Voxelize the spike-1 minigen world into the packed scene format above
// (deterministic; chunk content -> albedo/emissive mapping documented in the
// implementation). Extent must be a multiple of gen::kGenChunk.
std::vector<std::uint32_t> fillSceneFromMinigen(std::uint32_t extent);

// ---- Stage 4b: world-space 3D radiance cascades on the same substrate. ----
//
// Cascade scaling (canonical, documented in docs/spikes/spike-4-rc-vs-vct.md):
// per level, probe spacing x2 (count x1/8), octahedral direction-map side x2
// (directions x4), radial interval scale `intervalScale` with march step 2^i
// voxels sampling mip i. Levels are added while probesAxis >= 2, stopping
// once the accumulated reach covers the volume diagonal; actual reach is
// reported so a coverage shortfall is data, never silent.

// Per-merged-entry readback window (cascade 0, first entries in layout order).
inline constexpr std::uint32_t kRcReadbackEntries = 512;

struct RcBenchResult {
	bool ran = false;
	std::string failureReason;

	std::uint32_t extent = 0;
	std::uint32_t cascadeCount = 0;
	std::uint32_t iterations = 0;

	// Per-cascade geometry, in level order (0 = finest).
	std::vector<std::uint32_t> probesAxis;
	std::vector<std::uint32_t> dirCount;
	std::vector<std::uint64_t> rays;
	std::vector<std::uint32_t> stepsNominal;
	std::vector<double> meanStepsPerRay; // instrumented (untimed) pass

	std::uint64_t cascadeMemoryBytes = 0; // packed float4 entries, all levels
	double reachVoxels = 0.0;             // radial coverage of the last level
	double diagonalVoxels = 0.0;          // sqrt(3) * extent (coverage target)

	// MEDIAN per-pass times: inject, mip1..N, march0..L-1, merge(L-2)..merge(0),
	// resolve, sample — same warm-median discipline as the VCT bench.
	std::vector<PassTiming> passes;

	// Invariant-test hooks (filled when `readback` requested): cascade-0
	// entries before and after the merge chain, and the first sample quads.
	std::vector<float> preMergeC0;  // kRcReadbackEntries x RGBA (A = transmit)
	std::vector<float> postMergeC0; // kRcReadbackEntries x RGBA
	std::vector<float> sampleQuads; // kGatherSampleCount x RGBA
};

struct RcBenchParams {
	std::uint32_t extent = 128;       // level-0 edge (power of two)
	std::uint32_t probeSpacing0 = 4;  // cascade-0 probe spacing, voxels
	std::uint32_t octaSide0 = 4;      // cascade-0 direction-map side (16 dirs)
	float intervalR0 = 4.0f;          // cascade-0 interval length, voxels
	std::uint32_t intervalScale = 4;  // interval growth per level (4 or 2)
	std::uint32_t sampleWidth = 1920; // per-pixel query grid (VCT-comparable)
	std::uint32_t sampleHeight = 1080;
	std::uint32_t originVoxelY = 40;  // fixed ABSOLUTE surface height
	bool occlusionEarlyOut = true;    // false = fixed-work worst-case envelope
	std::uint32_t iterations = 8;     // timed repeats (median; +1 warmup)
	bool readback = false;
	std::span<const std::uint32_t> scene; // extent^3 packed voxels
};

// Run the full RC cost-driver sequence on the device:
//   upload scene -> inject + mip chain (SHARED with the VCT path: the
//   cascades march the same pre-filtered radiance volume)
//                -> per-cascade interval march (REPEAT-tiled, instrumented)
//                -> top-down merge chain (level L-2 .. 0, in place)
//                -> cascade-0 probe resolve (6-face ambient cubes)
//                -> per-pixel sample pass (the VCT cone gather's analog)
RcBenchResult runRcBench(Device &dev, const RcBenchParams &params);

// Run the full VCT cost-driver sequence on the device:
//   upload scene -> inject (scene buffer -> radiance L0)
//                -> mip chain (L0 -> ... -> 1x1x1)
//                -> cone gather (6 cones x coneSteps through the sampled
//                   mip chain from a synthetic surface grid)
// Every pass is bracketed by timestamp queries.
VctBenchResult runVctBench(Device &dev, const VctBenchParams &params);

} // namespace vk
