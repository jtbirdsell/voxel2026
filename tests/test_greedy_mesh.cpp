// Issue #17: greedy mesher property tests. The load-bearing one is coverage
// set-equivalence: decomposing every greedy quad into unit faces (via an
// independent unpack of the packed vertices, NOT the mesher's own helpers)
// must reproduce exactly the naive per-face oracle's face set — same faces,
// same materials, no overlap, no gap. Merging is the only thing greedy adds,
// so that equality isolates it.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <vector>

#include "gen/minigen.hpp"
#include "mesh/greedy.hpp"
#include "voxel/morton.hpp"
#include "world/voxel_blob.hpp"

namespace {

bool opaqueNonZero(const world::CellValue &c)
{
	return c.content != 0;
}

// Build the canonical Contract-1 chunk for a cell array by running it
// through the real codec — the mesher's input is the unpack product, so the
// tests exercise the actual blob -> mesh pipeline.
world::UnpackedChunk chunkFromCells(const std::vector<world::CellValue> &cells)
{
	const auto blob = world::packVoxelBlob(cells, 4);
	REQUIRE_FALSE(blob.empty());
	auto chunk = world::unpackVoxelBlob(blob);
	REQUIRE(chunk.has_value());
	return *chunk;
}

std::vector<world::CellValue> emptyCells()
{
	return std::vector<world::CellValue>(4096);
}

void setCell(std::vector<world::CellValue> &cells, std::uint32_t x, std::uint32_t y,
		std::uint32_t z, world::CellValue v)
{
	cells[voxel::mortonEncode({x, y, z})] = v;
}

// One unit face: (face, plane, u, v, slot) — derived from a quad's packed
// vertices by an INDEPENDENT unpack (min/max corner analysis), so this also
// round-trips the vertex packing.
using UnitFace = std::array<std::uint32_t, 5>;

std::set<UnitFace> decompose(const mesh::ChunkMesh &m, bool &overlap)
{
	std::set<UnitFace> faces;
	overlap = false;
	REQUIRE(m.vertices.size() == std::size_t{4} * m.quadCount);
	REQUIRE(m.indices.size() == std::size_t{6} * m.quadCount);
	for (std::uint32_t q = 0; q < m.quadCount; ++q) {
		const mesh::PackedVertex *v = &m.vertices[std::size_t{4} * q];
		const auto face = mesh::vertexFace(v[0]);
		const std::uint32_t slot = v[0].slot;
		for (int k = 1; k < 4; ++k) {
			REQUIRE(mesh::vertexFace(v[k]) == face);
			REQUIRE(v[k].slot == slot);
		}
		std::uint32_t mn[3] = {~0u, ~0u, ~0u}, mx[3] = {0, 0, 0};
		for (int k = 0; k < 4; ++k) {
			const std::uint32_t c[3] = {mesh::vertexX(v[k]), mesh::vertexY(v[k]),
					mesh::vertexZ(v[k])};
			for (int a = 0; a < 3; ++a) {
				mn[a] = std::min(mn[a], c[a]);
				mx[a] = std::max(mx[a], c[a]);
			}
		}
		const int d = static_cast<int>(face) / 2;
		REQUIRE(mn[d] == mx[d]); // flat in the face axis
		const int u = (d + 1) % 3, w = (d + 2) % 3;
		REQUIRE(mx[u] > mn[u]);
		REQUIRE(mx[w] > mn[w]);
		for (std::uint32_t a = mn[u]; a < mx[u]; ++a)
			for (std::uint32_t b = mn[w]; b < mx[w]; ++b)
				if (!faces.insert({static_cast<std::uint32_t>(face), mn[d], a, b, slot})
								.second)
					overlap = true;
	}
	return faces;
}

void requireEquivalent(const world::UnpackedChunk &chunk,
		const mesh::NeighborViews &nb)
{
	const mesh::ChunkMesh greedy = mesh::greedyMesh(chunk, nb, opaqueNonZero);
	const mesh::ChunkMesh naive = mesh::naiveMesh(chunk, nb, opaqueNonZero);
	bool overlapG = false, overlapN = false;
	const auto facesG = decompose(greedy, overlapG);
	const auto facesN = decompose(naive, overlapN);
	REQUIRE_FALSE(overlapG);
	REQUIRE_FALSE(overlapN);
	REQUIRE(facesG == facesN);
	REQUIRE(greedy.quadCount <= naive.quadCount);
}

// Deterministic PRNG (same xorshift discipline as the blob tests).
struct Prng {
	std::uint64_t s = 0xA0761D5C43E2B2ull;
	std::uint64_t next()
	{
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return s;
	}
};

} // namespace

TEST_CASE("packed vertex round-trips its fields")
{
	constexpr mesh::PackedVertex v = mesh::packVertex(16, 7, 0, mesh::Face::kNegZ, 4095);
	STATIC_REQUIRE(mesh::vertexX(v) == 16);
	STATIC_REQUIRE(mesh::vertexY(v) == 7);
	STATIC_REQUIRE(mesh::vertexZ(v) == 0);
	STATIC_REQUIRE(mesh::vertexFace(v) == mesh::Face::kNegZ);
	STATIC_REQUIRE(v.slot == 4095);
}

TEST_CASE("greedy mesher: empty and degenerate inputs")
{
	const auto chunk = chunkFromCells(emptyCells());
	const mesh::NeighborViews none{};
	REQUIRE(mesh::greedyMesh(chunk, none, opaqueNonZero).quadCount == 0);
	REQUIRE(mesh::naiveMesh(chunk, none, opaqueNonZero).quadCount == 0);

	// Wrong extent: documented precondition, empty mesh (never UB).
	// (assign, not a one-element initializer list: GCC 13's -Warray-bounds
	// false-fires on the init-list vector copy under -O2 + -Werror.)
	world::UnpackedChunk tiny;
	tiny.log2Extent = 1;
	tiny.palette.assign(1, world::CellValue{});
	tiny.cells.assign(8, world::CellValue{});
	REQUIRE(mesh::greedyMesh(tiny, none, opaqueNonZero).quadCount == 0);
}

TEST_CASE("greedy mesher: a full solid chunk collapses to exactly 6 quads")
{
	auto cells = emptyCells();
	for (auto &c : cells)
		c = world::CellValue{7, 0};
	const auto chunk = chunkFromCells(cells);
	const mesh::NeighborViews none{};

	const auto greedy = mesh::greedyMesh(chunk, none, opaqueNonZero);
	const auto naive = mesh::naiveMesh(chunk, none, opaqueNonZero);
	REQUIRE(greedy.quadCount == 6);          // one 16x16 quad per side
	REQUIRE(naive.quadCount == 6 * 256);     // the oracle's unmerged count
	requireEquivalent(chunk, none);
}

TEST_CASE("greedy mesher: single cell and solid/air checkerboard cannot merge")
{
	{
		auto cells = emptyCells();
		setCell(cells, 5, 6, 7, {3, 0});
		const auto chunk = chunkFromCells(cells);
		const mesh::NeighborViews none{};
		REQUIRE(mesh::greedyMesh(chunk, none, opaqueNonZero).quadCount == 6);
		requireEquivalent(chunk, none);
	}
	{
		auto cells = emptyCells();
		for (std::uint32_t z = 0; z < 16; ++z)
			for (std::uint32_t y = 0; y < 16; ++y)
				for (std::uint32_t x = 0; x < 16; ++x)
					if (((x + y + z) & 1) == 0)
						setCell(cells, x, y, z, {2, 0});
		const auto chunk = chunkFromCells(cells);
		const mesh::NeighborViews none{};
		const auto greedy = mesh::greedyMesh(chunk, none, opaqueNonZero);
		const auto naive = mesh::naiveMesh(chunk, none, opaqueNonZero);
		REQUIRE(greedy.quadCount == naive.quadCount); // nothing adjacent to merge
		requireEquivalent(chunk, none);
	}
}

TEST_CASE("greedy mesher merges within one material but never across")
{
	// One-cell-thick floor: x < 8 is material A, x >= 8 material B. The top
	// of the floor must merge to exactly TWO 8x16 quads, not one 16x16.
	auto cells = emptyCells();
	for (std::uint32_t z = 0; z < 16; ++z)
		for (std::uint32_t x = 0; x < 16; ++x)
			setCell(cells, x, 0, z, {x < 8 ? 10u : 11u, 0});
	const auto chunk = chunkFromCells(cells);
	const mesh::NeighborViews none{};
	const auto greedy = mesh::greedyMesh(chunk, none, opaqueNonZero);
	std::uint32_t topQuads = 0;
	for (std::uint32_t q = 0; q < greedy.quadCount; ++q)
		if (mesh::vertexFace(greedy.vertices[std::size_t{4} * q]) == mesh::Face::kPosY)
			++topQuads;
	REQUIRE(topQuads == 2);
	requireEquivalent(chunk, none);
}

TEST_CASE("greedy mesher culls boundary faces against solid neighbors")
{
	auto cells = emptyCells();
	for (auto &c : cells)
		c = world::CellValue{7, 0};
	const auto chunk = chunkFromCells(cells);

	// Fully solid +X neighbor: the +X side disappears, 5 quads remain.
	std::vector<world::CellValue> solidNeighbor(4096, world::CellValue{9, 0});
	mesh::NeighborViews nb{};
	nb.posX = solidNeighbor;
	const auto greedy = mesh::greedyMesh(chunk, nb, opaqueNonZero);
	REQUIRE(greedy.quadCount == 5);
	for (std::uint32_t q = 0; q < greedy.quadCount; ++q)
		REQUIRE(mesh::vertexFace(greedy.vertices[std::size_t{4} * q]) !=
				mesh::Face::kPosX);
	requireEquivalent(chunk, nb);
}

TEST_CASE("greedy quads wind counter-clockwise seen from the face normal")
{
	auto cells = emptyCells();
	setCell(cells, 4, 4, 4, {5, 0});
	setCell(cells, 11, 2, 9, {6, 0});
	const auto chunk = chunkFromCells(cells);
	const mesh::NeighborViews none{};
	const auto m = mesh::greedyMesh(chunk, none, opaqueNonZero);
	REQUIRE(m.quadCount == 12);
	for (std::uint32_t q = 0; q < m.quadCount; ++q) {
		const mesh::PackedVertex *v = &m.vertices[std::size_t{4} * q];
		const auto face = mesh::vertexFace(v[0]);
		int p[3][3];
		for (int k = 0; k < 3; ++k) {
			p[k][0] = static_cast<int>(mesh::vertexX(v[k]));
			p[k][1] = static_cast<int>(mesh::vertexY(v[k]));
			p[k][2] = static_cast<int>(mesh::vertexZ(v[k]));
		}
		const int e1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
		const int e2[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
		const int cross[3] = {e1[1] * e2[2] - e1[2] * e2[1],
				e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
		const int d = static_cast<int>(face) / 2;
		const int sign = (static_cast<int>(face) % 2 == 0) ? 1 : -1;
		CAPTURE(q, static_cast<int>(face));
		REQUIRE(cross[d] * sign > 0);
		REQUIRE(cross[(d + 1) % 3] == 0);
		REQUIRE(cross[(d + 2) % 3] == 0);
	}
}

TEST_CASE("greedy mesher coverage equals the oracle on random and minigen chunks")
{
	Prng rng;
	for (int round = 0; round < 4; ++round) {
		auto cells = emptyCells();
		// ~40% solid cells over a few materials (content 0 stays air).
		for (auto &c : cells)
			if (rng.next() % 5 < 2)
				c = world::CellValue{1 + static_cast<std::uint32_t>(rng.next() % 4),
						rng.next() % 2};
		const auto chunk = chunkFromCells(cells);
		CAPTURE(round);
		requireEquivalent(chunk, mesh::NeighborViews{});
	}

	// Real terrain through the real codec.
	gen::GenChunk g;
	gen::generateChunk(gen::kStandardSeed, {0, 0, 0}, g);
	auto cells = emptyCells();
	for (int z = 0; z < gen::kGenChunk; ++z)
		for (int y = 0; y < gen::kGenChunk; ++y)
			for (int x = 0; x < gen::kGenChunk; ++x)
				setCell(cells, static_cast<std::uint32_t>(x),
						static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(z),
						{g.at(x, y, z), 0});
	const auto chunk = chunkFromCells(cells);
	const auto greedy = mesh::greedyMesh(chunk, {}, opaqueNonZero);
	const auto naive = mesh::naiveMesh(chunk, {}, opaqueNonZero);
	CAPTURE(greedy.quadCount, naive.quadCount);
	REQUIRE(greedy.quadCount > 0);
	REQUIRE(greedy.quadCount < naive.quadCount); // terrain must actually compress
	requireEquivalent(chunk, mesh::NeighborViews{});
}
