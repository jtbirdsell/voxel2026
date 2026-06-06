// Mesh-pipeline execution harness (issue #16): builds a real graphics
// pipeline with a mesh stage, draws one workgroup into an offscreen target,
// and reads the pixels back — the ADR-0002 renderer gate (emission was the
// toolchain gate; execution is this).
//
// Spike-shaped like the rest of src/vk: hardcoded single-purpose pipeline,
// exhaustive VkResult checking, RAII teardown, results over exceptions.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vk/device.hpp"

namespace vk {

struct MeshExecResult {
	bool ran = false;
	std::string failureReason; // includes the tier-blocked reason on SKIP paths

	std::uint32_t width = 0, height = 0;
	std::vector<std::uint8_t> pixels; // RGBA8, row-major, width*height*4
};

// The exact bytes the kernel's covered half must produce (UNORM8-exact by
// construction) and the clear color for the uncovered half.
inline constexpr std::uint8_t kMeshExecFill[4] = {32, 64, 96, 255};
inline constexpr std::uint8_t kMeshExecClear[4] = {0, 0, 0, 255};

// Requires dev.report().meshPipelineReady; otherwise returns ran == false
// with the device's meshExecBlocked reason. Width must be even (the
// exact-pixel contract places the quad edge on a pixel boundary).
MeshExecResult runMeshExec(Device &dev, std::uint32_t width = 64,
		std::uint32_t height = 64);

} // namespace vk
