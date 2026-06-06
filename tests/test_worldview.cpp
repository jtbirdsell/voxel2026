// Issue #21: the connected belt. CPU-side: camera math projections and the
// chunk store-key ordering. Belt: load-or-generate round-trips through both
// store backends (second pass entirely fromStore, bit-identical). GPU
// (SKIP-graceful): render determinism (bit-identical frames), sky/terrain
// structure over real minigen content, two-chunk depth occlusion via
// color-set difference (no shader-math replication), and the empty scene
// clearing exactly.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

#include "mesh/greedy.hpp"
#include "store/memory_store.hpp"
#include "store/sqlite_store.hpp"
#include "vk/device.hpp"
#include "vk/worldview.hpp"
#include "voxel/morton.hpp"
#include "world/belt.hpp"
#include "world/chunk_key.hpp"

namespace {

bool opaqueNonZero(const world::CellValue &c)
{
	return c.content != 0;
}

// Project a world point through the same matrices the renderer uses;
// returns NDC (clip / w).
vk::Float3 project(const vk::WorldCamera &cam, float aspect, vk::Float3 p)
{
	const vk::Mat4 vp = vk::mat4Mul(
			vk::mat4Perspective(cam.fovYRadians, aspect, cam.nearZ, cam.farZ),
			vk::mat4LookAt(cam.eye, cam.target, cam.up));
	const float x = vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12];
	const float y = vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13];
	const float z = vp.m[2] * p.x + vp.m[6] * p.y + vp.m[10] * p.z + vp.m[14];
	const float w = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
	return {x / w, y / w, z / w};
}

bool isClear(const std::uint8_t *px)
{
	return px[0] == vk::kWorldViewClear[0] && px[1] == vk::kWorldViewClear[1] &&
			px[2] == vk::kWorldViewClear[2] && px[3] == vk::kWorldViewClear[3];
}

world::UnpackedChunk uniformChunk(std::uint32_t content)
{
	std::vector<world::CellValue> cells(4096, world::CellValue{content, 0});
	return *world::unpackVoxelBlob(world::packVoxelBlob(cells, 4));
}

// Distinct colors present in a frame, excluding the sky clear.
std::set<std::uint32_t> nonClearColors(const vk::WorldViewResult &r)
{
	std::set<std::uint32_t> colors;
	for (std::size_t i = 0; i + 3 < r.pixels.size(); i += 4)
		if (!isClear(&r.pixels[i]))
			colors.insert(static_cast<std::uint32_t>(r.pixels[i]) |
					(static_cast<std::uint32_t>(r.pixels[i + 1]) << 8) |
					(static_cast<std::uint32_t>(r.pixels[i + 2]) << 16));
	return colors;
}

} // namespace

TEST_CASE("camera math: known projections land where the conventions say")
{
	// Camera at +Z looking at the origin, 90-degree vertical FOV, square
	// aspect: f = 1, so a point at world (5, 0, 0) sits at NDC x = 0.5, and
	// the Vulkan Y flip sends world +Y to NDC -Y... after the flip in the
	// projection, world up = NDC negative y (framebuffer up).
	vk::WorldCamera cam;
	cam.eye = {0, 0, 10};
	cam.target = {0, 0, 0};
	cam.fovYRadians = 3.14159265f / 2.0f;
	cam.nearZ = 1.0f;
	cam.farZ = 100.0f;

	const vk::Float3 center = project(cam, 1.0f, {0, 0, 0});
	REQUIRE(std::abs(center.x) < 1e-5f);
	REQUIRE(std::abs(center.y) < 1e-5f);
	REQUIRE(center.z > 0.0f); // depth in (0, 1)
	REQUIRE(center.z < 1.0f);

	const vk::Float3 right = project(cam, 1.0f, {5, 0, 0});
	REQUIRE(std::abs(right.x - 0.5f) < 1e-5f);

	const vk::Float3 up = project(cam, 1.0f, {0, 5, 0});
	REQUIRE(std::abs(up.y + 0.5f) < 1e-5f); // Y flip: world up -> -y NDC

	// Nearer points get SMALLER depth (compare-op LESS is correct).
	const float zNear = project(cam, 1.0f, {0, 0, 5}).z;
	const float zFar = project(cam, 1.0f, {0, 0, -50}).z;
	REQUIRE(zNear < center.z);
	REQUIRE(zFar > center.z);
}

TEST_CASE("chunk store keys order numerically across the sign boundary")
{
	const auto a = world::chunkStoreKey(-2, 0, 0);
	const auto b = world::chunkStoreKey(-1, 7, -3);
	const auto c = world::chunkStoreKey(0, 0, 0);
	const auto d = world::chunkStoreKey(0, 0, 1);
	REQUIRE(a < b);
	REQUIRE(b < c);
	REQUIRE(c < d);
}

TEST_CASE("belt: second pass over a region comes entirely from the store, bit-equal")
{
	const auto run = [](world::KvStore &store) {
		const auto first =
				world::loadOrGenerateRegion(store, gen::kStandardSeed, 0, 0, 0, 2, 1, 2);
		REQUIRE(first.size() == 3 * 2 * 3);
		for (const auto &bc : first)
			REQUIRE_FALSE(bc.fromStore);

		const auto second =
				world::loadOrGenerateRegion(store, gen::kStandardSeed, 0, 0, 0, 2, 1, 2);
		REQUIRE(second.size() == first.size());
		for (std::size_t i = 0; i < first.size(); ++i) {
			CAPTURE(i);
			REQUIRE(second[i].fromStore);
			REQUIRE(second[i].chunk.cells == first[i].chunk.cells);
			REQUIRE(second[i].chunk.palette == first[i].chunk.palette);
		}
	};
	world::MemoryStore mem;
	run(mem);
	world::SqliteStore sql(":memory:");
	REQUIRE(sql.ok());
	run(sql);
}

TEST_CASE("world view renders deterministically with sky and terrain structure")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);
	if (!dev.report().meshPipelineReady && !dev.report().presentReady)
		SKIP("graphics tier unavailable: " << dev.report().meshExecBlocked);

	// The full belt: generate+store -> mesh -> remap slots -> render.
	world::MemoryStore store;
	const auto belt =
			world::loadOrGenerateRegion(store, gen::kStandardSeed, 0, 0, 0, 3, 1, 3);
	std::vector<mesh::ChunkMesh> meshes;
	std::vector<vk::ChunkDraw> draws;
	meshes.reserve(belt.size());
	for (const auto &bc : belt) {
		mesh::ChunkMesh m = mesh::greedyMesh(bc.chunk, {}, opaqueNonZero);
		world::remapSlotsToContent(m, bc.chunk);
		meshes.push_back(std::move(m));
	}
	for (std::size_t i = 0; i < belt.size(); ++i)
		draws.push_back({{static_cast<float>(belt[i].x * 16),
								 static_cast<float>(belt[i].y * 16),
								 static_cast<float>(belt[i].z * 16)},
				&meshes[i]});

	vk::WorldCamera cam;
	cam.eye = {32, 56, 110};
	cam.target = {32, 8, 32};

	const vk::WorldViewResult a = vk::renderWorldView(dev, draws, cam, 160, 120);
	CAPTURE(a.failureReason);
	REQUIRE(a.ran);
	REQUIRE(a.drawnQuads > 0);
	REQUIRE(a.pixels.size() == std::size_t{160} * 120 * 4);

	// Determinism: same scene, same camera -> bit-identical frame. SCOPE
	// (review-stated): same process, same physical device, same driver —
	// cross-vendor/cross-driver goldens are the Part VII scheduled-bench
	// story, not this test's claim.
	const vk::WorldViewResult b = vk::renderWorldView(dev, draws, cam, 160, 120);
	REQUIRE(b.ran);
	REQUIRE(a.pixels == b.pixels);

	// Structure: the sky is exactly the clear color at the top corners, and
	// the terrain fills a meaningful share of the frame.
	REQUIRE(isClear(&a.pixels[0]));
	REQUIRE(isClear(&a.pixels[(std::size_t{159}) * 4]));
	std::size_t terrain = 0, sky = 0;
	for (std::size_t i = 0; i + 3 < a.pixels.size(); i += 4)
		(isClear(&a.pixels[i]) ? sky : terrain) += 1;
	CAPTURE(terrain, sky);
	REQUIRE(terrain > std::size_t{160} * 120 / 10); // >10% terrain
	REQUIRE(sky > std::size_t{160} * 120 / 10);     // >10% sky
}

TEST_CASE("world view depth-occludes: a near chunk hides a far one")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);
	if (!dev.report().meshPipelineReady && !dev.report().presentReady)
		SKIP("graphics tier unavailable: " << dev.report().meshExecBlocked);

	// Two uniform chunks of DIFFERENT content (slots remapped to content ids
	// so their colors differ), far one directly behind the near one.
	const auto nearChunk = uniformChunk(7);
	const auto farChunk = uniformChunk(9);
	mesh::ChunkMesh nearMesh = mesh::greedyMesh(nearChunk, {}, opaqueNonZero);
	mesh::ChunkMesh farMesh = mesh::greedyMesh(farChunk, {}, opaqueNonZero);
	world::remapSlotsToContent(nearMesh, nearChunk);
	world::remapSlotsToContent(farMesh, farChunk);

	vk::WorldCamera cam;
	cam.eye = {8, 8, 60};
	cam.target = {8, 8, 0};

	// Far chunk alone: its colors are visible (recorded, not predicted —
	// no shader-math replication in the test).
	const vk::ChunkDraw farDraw{{0, 0, -40}, &farMesh};
	const vk::WorldViewResult alone =
			vk::renderWorldView(dev, {&farDraw, 1}, cam, 96, 96);
	CAPTURE(alone.failureReason);
	REQUIRE(alone.ran);
	const auto farColors = nonClearColors(alone);
	REQUIRE_FALSE(farColors.empty());

	// Near chunk in front: every far color disappears from the frame.
	const vk::ChunkDraw both[2] = {{{0, 0, 0}, &nearMesh}, {{0, 0, -40}, &farMesh}};
	const vk::WorldViewResult occluded = vk::renderWorldView(dev, both, cam, 96, 96);
	REQUIRE(occluded.ran);
	const auto visibleColors = nonClearColors(occluded);
	REQUIRE_FALSE(visibleColors.empty()); // the near chunk is on screen
	for (const std::uint32_t c : farColors) {
		CAPTURE(c);
		REQUIRE_FALSE(visibleColors.contains(c));
	}
}

TEST_CASE("world view projection orientation: an off-center object lands on its side")
{
	// The matrix-layout tripwire (review finding): if chunk.spv were ever
	// regenerated WITHOUT -matrix-layout-column-major, the view matrix
	// transposes silently. This directed test breaks under transposition by
	// construction: a single cell placed to the camera's LEFT must produce
	// pixels ONLY in the left half of the frame, and BELOW center must
	// land only in the bottom half (framebuffer Y points down).
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);
	if (!dev.report().meshPipelineReady && !dev.report().presentReady)
		SKIP("graphics tier unavailable: " << dev.report().meshExecBlocked);

	std::vector<world::CellValue> cells(4096);
	cells[voxel::mortonEncode({0, 0, 0})] = world::CellValue{5, 0};
	const auto chunk = *world::unpackVoxelBlob(world::packVoxelBlob(cells, 4));
	mesh::ChunkMesh m = mesh::greedyMesh(chunk, {}, opaqueNonZero);
	world::remapSlotsToContent(m, chunk);

	vk::WorldCamera cam;
	cam.eye = {0, 0, 30};
	cam.target = {0, 0, 0};
	// Cell at world x in [-12,-11], y in [-9,-8]: left of and below center.
	const vk::ChunkDraw draw{{-12.5f, -9.5f, 0.0f}, &m};
	const vk::WorldViewResult r = vk::renderWorldView(dev, {&draw, 1}, cam, 128, 128);
	CAPTURE(r.failureReason);
	REQUIRE(r.ran);

	std::size_t leftHits = 0, rightHits = 0, topHits = 0, bottomHits = 0;
	for (std::uint32_t y = 0; y < 128; ++y)
		for (std::uint32_t x = 0; x < 128; ++x) {
			if (isClear(&r.pixels[(std::size_t{y} * 128 + x) * 4]))
				continue;
			(x < 64 ? leftHits : rightHits) += 1;
			(y < 64 ? topHits : bottomHits) += 1;
		}
	CAPTURE(leftHits, rightHits, topHits, bottomHits);
	REQUIRE(leftHits > 0);        // the object is visible...
	REQUIRE(rightHits == 0);      // ...only on its own side,
	REQUIRE(bottomHits > 0);      // and below center (world -Y -> screen
	REQUIRE(topHits == 0);        // bottom via the projection's Y flip).
}

TEST_CASE("world view renders an empty scene as exactly the clear color")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);
	if (!dev.report().meshPipelineReady && !dev.report().presentReady)
		SKIP("graphics tier unavailable: " << dev.report().meshExecBlocked);

	vk::WorldCamera cam;
	cam.eye = {0, 0, 10};
	cam.target = {0, 0, 0};
	const vk::WorldViewResult r = vk::renderWorldView(dev, {}, cam, 64, 64);
	CAPTURE(r.failureReason);
	REQUIRE(r.ran);
	REQUIRE(r.drawnQuads == 0);
	for (std::size_t i = 0; i + 3 < r.pixels.size(); i += 4) {
		CAPTURE(i);
		REQUIRE(isClear(&r.pixels[i]));
	}
}
