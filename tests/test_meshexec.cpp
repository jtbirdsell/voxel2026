// Issue #16: mesh-pipeline EXECUTION on hardware — the ADR-0002 renderer
// gate. SKIP-graceful at two distinct levels: no Vulkan at all (driverless
// CI), or Vulkan without the mesh tier (the device records WHICH
// precondition blocked it). On tier-capable hardware the output is asserted
// byte-exactly: the mesh-emitted quad covers exactly the left half.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

#include "vk/device.hpp"
#include "vk/meshexec.hpp"

TEST_CASE("mesh tier report is internally coherent")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	const vk::CapabilityReport &rep = dev.report();
	if (rep.meshPipelineReady) {
		// Ready implies every precondition; the blocked reason must be empty.
		REQUIRE(rep.meshShader);
		REQUIRE(rep.graphicsQueueFamily != ~0u);
		REQUIRE(rep.meshExecBlocked.empty());
		REQUIRE(dev.graphicsQueue() != nullptr);
	} else {
		// Not ready implies a recorded reason — silence is the one failure
		// mode this report may never have.
		REQUIRE_FALSE(rep.meshExecBlocked.empty());
	}
}

TEST_CASE("mesh pipeline executes with exact rasterized output")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);
	if (!dev.report().meshPipelineReady)
		SKIP("mesh tier unavailable: " << dev.report().meshExecBlocked);

	const vk::MeshExecResult r = vk::runMeshExec(dev, 64, 64);
	CAPTURE(r.failureReason);
	REQUIRE(r.ran);
	REQUIRE(r.pixels.size() == std::size_t{64} * 64 * 4);

	// Exact two-region check: every pixel left of x=32 is the fill color,
	// everything at x>=32 is the clear color. Pixel centers sit at x+0.5,
	// the quad edge at framebuffer x=32.0 — no center lands on the edge.
	std::size_t wrong = 0;
	for (std::uint32_t y = 0; y < 64; ++y)
		for (std::uint32_t x = 0; x < 64; ++x) {
			const std::size_t at = (std::size_t{y} * 64 + x) * 4;
			const std::uint8_t *want = x < 32 ? vk::kMeshExecFill : vk::kMeshExecClear;
			for (int c = 0; c < 4; ++c)
				if (r.pixels[at + c] != want[c])
					++wrong;
		}
	REQUIRE(wrong == 0);

	// The boundary columns, asserted explicitly (the test above already
	// covers them; this names the contract).
	const std::size_t inEdge = (std::size_t{31} * 64 + 31) * 4;
	const std::size_t outEdge = (std::size_t{31} * 64 + 32) * 4;
	REQUIRE(r.pixels[inEdge + 0] == vk::kMeshExecFill[0]);
	REQUIRE(r.pixels[outEdge + 0] == vk::kMeshExecClear[0]);
}

TEST_CASE("mesh exec rejects an odd width instead of asserting an ambiguous edge")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);
	if (!dev.report().meshPipelineReady)
		SKIP("mesh tier unavailable: " << dev.report().meshExecBlocked);

	const vk::MeshExecResult r = vk::runMeshExec(dev, 63, 64);
	REQUIRE_FALSE(r.ran);
	REQUIRE_FALSE(r.failureReason.empty());
}
