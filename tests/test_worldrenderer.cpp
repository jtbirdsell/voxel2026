// Issue #22: the persistent renderer's gates. THE load-bearing one is
// equivalence: the fast path (created-once pipeline, device-local geometry,
// reused command slots) must produce pixels BIT-IDENTICAL to issue #21's
// validated one-shot path for the same scene and camera — "faster" may
// never quietly become "different". Plus: stability across many frames
// (slot rotation reuses state correctly), scene re-upload, and GPU
// timestamps reporting plausible spans.

#include <catch2/catch_test_macros.hpp>

// Same documented GCC 13 -O2 ledger entry as the other render tests
// (vector equality in REQUIRE), same file-scoped suppression.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wstringop-overread"
#endif

#include <cstdint>
#include <vector>

#include "mesh/greedy.hpp"
#include "store/memory_store.hpp"
#include "vk/device.hpp"
#include "vk/worldrenderer.hpp"
#include "vk/worldview.hpp"
#include "world/belt.hpp"

namespace {

bool opaqueNonZero(const world::CellValue &c)
{
	return c.content != 0;
}

struct Scene {
	std::vector<mesh::ChunkMesh> meshes;
	std::vector<vk::ChunkDraw> draws;
};

Scene buildScene()
{
	Scene s;
	world::MemoryStore store;
	const auto belt =
			world::loadOrGenerateRegion(store, gen::kStandardSeed, 0, 0, 0, 3, 1, 3);
	s.meshes.reserve(belt.size());
	for (const auto &bc : belt) {
		mesh::ChunkMesh m = mesh::greedyMesh(bc.chunk, {}, opaqueNonZero);
		world::remapSlotsToContent(m, bc.chunk);
		s.meshes.push_back(std::move(m));
	}
	for (std::size_t i = 0; i < belt.size(); ++i)
		s.draws.push_back({{static_cast<float>(belt[i].x * 16),
								   static_cast<float>(belt[i].y * 16),
								   static_cast<float>(belt[i].z * 16)},
				&s.meshes[i]});
	return s;
}

vk::WorldCamera testCamera()
{
	vk::WorldCamera cam;
	cam.eye = {32, 56, 110};
	cam.target = {32, 8, 32};
	return cam;
}

} // namespace

TEST_CASE("world renderer fast path is bit-identical to the validated one-shot path")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);
	if (!dev.report().meshPipelineReady && !dev.report().presentReady)
		SKIP("graphics tier unavailable: " << dev.report().meshExecBlocked);

	const Scene scene = buildScene();
	const vk::WorldCamera cam = testCamera();

	const vk::WorldViewResult reference =
			vk::renderWorldView(dev, scene.draws, cam, 160, 120);
	CAPTURE(reference.failureReason);
	REQUIRE(reference.ran);

	vk::WorldRenderer renderer(dev, VK_FORMAT_R8G8B8A8_UNORM, 160, 120);
	CAPTURE(renderer.error());
	REQUIRE(renderer.ok());
	REQUIRE(renderer.uploadScene(scene.draws));

	const vk::WorldViewResult fast = renderer.renderOffscreen(cam);
	CAPTURE(fast.failureReason);
	REQUIRE(fast.ran);

	// The equivalence gate.
	REQUIRE(fast.pixels == reference.pixels);
	REQUIRE(fast.drawnQuads == reference.drawnQuads);
}

TEST_CASE("world renderer stays stable and timed across many reused frames")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);
	if (!dev.report().meshPipelineReady && !dev.report().presentReady)
		SKIP("graphics tier unavailable: " << dev.report().meshExecBlocked);

	const Scene scene = buildScene();
	vk::WorldRenderer renderer(dev, VK_FORMAT_R8G8B8A8_UNORM, 128, 96);
	REQUIRE(renderer.ok());
	REQUIRE(renderer.uploadScene(scene.draws));

	// Many frames through the rotating slots — every frame must equal the
	// first (same scene, same camera; slot reuse cannot leak state), and a
	// moving camera must change the image (the frames are not stale).
	const vk::WorldCamera cam = testCamera();
	const vk::WorldViewResult first = renderer.renderOffscreen(cam);
	REQUIRE(first.ran);
	for (int i = 0; i < 7; ++i) {
		const vk::WorldViewResult again = renderer.renderOffscreen(cam);
		CAPTURE(i, again.failureReason);
		REQUIRE(again.ran);
		REQUIRE(again.pixels == first.pixels);
	}
	if (dev.report().timestampPeriodNs > 0.0f) {
		REQUIRE(renderer.lastGpuMillis() >= 0.0);
		REQUIRE(renderer.lastGpuMillis() < 1000.0);
	}
	vk::WorldCamera moved = cam;
	moved.eye.x += 24.0f;
	const vk::WorldViewResult different = renderer.renderOffscreen(moved);
	REQUIRE(different.ran);
	REQUIRE(different.pixels != first.pixels);
}

TEST_CASE("world renderer re-uploads scenes and renders the empty scene clear")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);
	if (!dev.report().meshPipelineReady && !dev.report().presentReady)
		SKIP("graphics tier unavailable: " << dev.report().meshExecBlocked);

	const Scene scene = buildScene();
	vk::WorldRenderer renderer(dev, VK_FORMAT_R8G8B8A8_UNORM, 96, 96);
	REQUIRE(renderer.ok());

	// Empty scene first: exact clear everywhere.
	REQUIRE(renderer.uploadScene({}));
	const vk::WorldViewResult empty = renderer.renderOffscreen(testCamera());
	REQUIRE(empty.ran);
	REQUIRE(empty.drawnQuads == 0);
	for (std::size_t i = 0; i + 3 < empty.pixels.size(); i += 4) {
		CAPTURE(i);
		REQUIRE(empty.pixels[i + 0] == vk::kWorldViewClear[0]);
		REQUIRE(empty.pixels[i + 1] == vk::kWorldViewClear[1]);
		REQUIRE(empty.pixels[i + 2] == vk::kWorldViewClear[2]);
	}

	// Then the real scene through the SAME renderer: terrain appears.
	REQUIRE(renderer.uploadScene(scene.draws));
	const vk::WorldViewResult full = renderer.renderOffscreen(testCamera());
	REQUIRE(full.ran);
	REQUIRE(full.drawnQuads > 0);
	REQUIRE(full.pixels != empty.pixels);
}
