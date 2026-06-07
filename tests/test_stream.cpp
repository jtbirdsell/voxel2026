// Dynamic world streaming tests (issue #24). The claims under test, in
// rising order of strength:
//
//   1. Window mechanics — the resident set converges to exactly the desired
//      window, admissions respect the pump budget, eviction leaves nothing
//      outside the window.
//   2. Neighbor-aware meshing — interior boundary faces between resident
//      neighbors are culled (the overdraw the fixed-region demo carried).
//   3. PURITY — at quiescence every resident mesh equals an ORACLE that
//      re-derives the same mesh from nothing but (seed, window): generate
//      the chunk and its in-window neighbors fresh, mesh, remap. The
//      streamer's incremental bookkeeping must be unobservable.
//   4. PATH INDEPENDENCE — driving the window to the same center along
//      different routes (with evictions and partial pumps en route) yields
//      bit-identical resident mesh maps.
//   5. RE-ENTRY DETERMINISM — evicting a region and coming back reproduces
//      the exact meshes (store round-trip is already pinned by the belt
//      tests; this pins that streaming on top of it adds no history).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <vector>

#include "gen/minigen.hpp"
#include "store/memory_store.hpp"
#include "voxel/morton.hpp"
#include "world/stream.hpp"

namespace {

bool opaqueNonZero(const world::CellValue &c)
{
	return c.content != 0;
}

// Fresh minigen chunk -> canonical UnpackedChunk, independent of any
// streamer state: the belt's own generate-then-canonicalize path (pack ->
// unpack yields the palette the mesher resolves slots against), re-derived
// from nothing but (seed, position).
world::UnpackedChunk freshChunk(std::uint64_t seed, world::ChunkKey k)
{
	gen::GenChunk g;
	gen::generateChunk(seed, {k.cx, k.cy, k.cz}, g);
	std::vector<world::CellValue> cells(4096);
	for (int z = 0; z < gen::kGenChunk; ++z)
		for (int y = 0; y < gen::kGenChunk; ++y)
			for (int x = 0; x < gen::kGenChunk; ++x)
				cells[voxel::mortonEncode({static_cast<std::uint32_t>(x),
						static_cast<std::uint32_t>(y),
						static_cast<std::uint32_t>(z)})] =
						world::CellValue{g.at(x, y, z), 0};
	const auto unpacked = world::unpackVoxelBlob(world::packVoxelBlob(cells, 4));
	REQUIRE(unpacked.has_value());
	return *unpacked;
}

int pumpUntilQuiescent(world::Streamer &s, int maxPumps)
{
	int pumps = 0;
	while (!s.quiescent() && pumps < maxPumps) {
		s.pump();
		++pumps;
	}
	REQUIRE(s.quiescent());
	return pumps;
}

} // namespace

TEST_CASE("Streaming window fills to exactly the desired set under budget")
{
	world::MemoryStore store;
	world::Streamer s(store, gen::kStandardSeed,
			{.radius = 2, .cyMin = 0, .cyMax = 1, .budget = 7}, &opaqueNonZero);
	s.setCenter(4, 4);

	// 5 x 5 x 2 = 50 chunks at 7 admissions per pump = 8 loading pumps.
	std::uint32_t totalLoaded = 0;
	int pumps = 0;
	while (!s.quiescent()) {
		REQUIRE(pumps < 12); // bounded convergence, not a spin
		const world::PumpStats st = s.pump();
		REQUIRE(st.loaded <= 7);
		totalLoaded += st.loaded;
		++pumps;
	}
	REQUIRE(totalLoaded == 50);
	REQUIRE(s.residents().size() == 50);
	REQUIRE(pumps == 8);

	// Exactly the desired box, nothing else.
	for (const auto &[key, res] : s.residents()) {
		REQUIRE(s.inWindow(key));
		REQUIRE(res.meshed);
	}
	for (std::int32_t cz = 2; cz <= 6; ++cz)
		for (std::int32_t cy = 0; cy <= 1; ++cy)
			for (std::int32_t cx = 2; cx <= 6; ++cx)
				REQUIRE(s.residents().contains(world::ChunkKey{cx, cy, cz}));

	// A quiescent pump is a no-op.
	const world::PumpStats idle = s.pump();
	REQUIRE_FALSE(idle.changed());
}

TEST_CASE("Neighbor-aware meshing culls interior boundary faces")
{
	world::MemoryStore store;
	world::Streamer s(store, gen::kStandardSeed,
			{.radius = 2, .cyMin = 0, .cyMax = 1, .budget = 64}, &opaqueNonZero);
	s.setCenter(4, 4);
	pumpUntilQuiescent(s, 8);

	// The same chunks meshed with EMPTY neighbor views (the fixed-region
	// demo's pool shape) must carry strictly more quads — the difference is
	// exactly the interior boundary overdraw streaming removes.
	std::uint64_t streamed = 0, blind = 0;
	for (const auto &[key, res] : s.residents()) {
		streamed += res.mesh.quadCount;
		blind += mesh::greedyMesh(res.chunk, {}, &opaqueNonZero).quadCount;
	}
	REQUIRE(streamed < blind);
	REQUIRE(streamed > 0);
}

TEST_CASE("Quiescent meshes equal the pure (seed, window) oracle")
{
	world::MemoryStore store;
	world::Streamer s(store, gen::kStandardSeed,
			{.radius = 2, .cyMin = 0, .cyMax = 1, .budget = 5}, &opaqueNonZero);
	s.setCenter(-3, 7); // negative-coordinate window on purpose
	pumpUntilQuiescent(s, 16);

	for (const auto &[key, res] : s.residents()) {
		// Re-derive from scratch: fresh chunk, fresh in-window neighbors,
		// fresh mesh — no streamer state involved.
		const world::UnpackedChunk chunk = freshChunk(gen::kStandardSeed, key);
		const std::uint8_t mask = s.windowMask(key);
		const world::ChunkKey nk[6] = {
				{key.cx + 1, key.cy, key.cz}, {key.cx - 1, key.cy, key.cz},
				{key.cx, key.cy + 1, key.cz}, {key.cx, key.cy - 1, key.cz},
				{key.cx, key.cy, key.cz + 1}, {key.cx, key.cy, key.cz - 1}};
		world::UnpackedChunk nbr[6];
		for (int f = 0; f < 6; ++f)
			if (mask & (1u << f))
				nbr[f] = freshChunk(gen::kStandardSeed, nk[f]);
		mesh::NeighborViews views;
		const auto span = [&](int f) -> std::span<const world::CellValue> {
			if (!(mask & (1u << f)))
				return {};
			return nbr[f].cells;
		};
		views.posX = span(0);
		views.negX = span(1);
		views.posY = span(2);
		views.negY = span(3);
		views.posZ = span(4);
		views.negZ = span(5);
		mesh::ChunkMesh oracle = mesh::greedyMesh(chunk, views, &opaqueNonZero);
		world::remapSlotsToContent(oracle, chunk);

		REQUIRE(res.mesh == oracle);
	}
}

TEST_CASE("Window paths to the same center yield bit-identical resident meshes")
{
	const world::StreamConfig cfg{.radius = 2, .cyMin = 0, .cyMax = 1, .budget = 3};

	// Two routes from (0,0) to (3,2), moving every pump (the window outruns
	// the budget, so admissions, evictions, and remeshes interleave
	// differently along each route), then quiescing at the destination.
	const std::vector<std::pair<std::int32_t, std::int32_t>> pathA = {
			{0, 0}, {1, 0}, {2, 0}, {3, 0}, {3, 1}, {3, 2}};
	const std::vector<std::pair<std::int32_t, std::int32_t>> pathB = {
			{0, 0}, {0, 1}, {0, 2}, {1, 2}, {2, 2}, {3, 2}};

	const auto drive = [&](const std::vector<std::pair<std::int32_t, std::int32_t>>
							&path) {
		world::MemoryStore store;
		world::Streamer s(store, gen::kStandardSeed, cfg, &opaqueNonZero);
		for (const auto &[cx, cz] : path) {
			s.setCenter(cx, cz);
			s.pump();
			// Eviction invariant holds mid-flight, not just at rest.
			for (const auto &[key, res] : s.residents())
				REQUIRE(s.inWindow(key));
		}
		pumpUntilQuiescent(s, 64);
		std::map<world::ChunkKey, mesh::ChunkMesh> out;
		for (const auto &[key, res] : s.residents())
			out.emplace(key, res.mesh);
		return out;
	};

	const auto a = drive(pathA);
	const auto b = drive(pathB);
	REQUIRE(a.size() == b.size());
	REQUIRE(a == b);
}

TEST_CASE("Eviction and re-entry reproduce the exact meshes from the store")
{
	world::MemoryStore store;
	world::Streamer s(store, gen::kStandardSeed,
			{.radius = 1, .cyMin = 0, .cyMax = 1, .budget = 6}, &opaqueNonZero);

	s.setCenter(0, 0);
	pumpUntilQuiescent(s, 8);
	std::map<world::ChunkKey, mesh::ChunkMesh> first;
	for (const auto &[key, res] : s.residents()) {
		REQUIRE_FALSE(res.fromStore); // first visit generated everything
		first.emplace(key, res.mesh);
	}

	// Far away: disjoint window — everything from the first visit evicts.
	s.setCenter(10, 0);
	pumpUntilQuiescent(s, 8);
	for (const auto &[key, res] : first)
		REQUIRE_FALSE(s.residents().contains(key));

	// And back: every chunk reloads FROM THE STORE, meshes bit-identical.
	s.setCenter(0, 0);
	pumpUntilQuiescent(s, 8);
	REQUIRE(s.residents().size() == first.size());
	for (const auto &[key, res] : s.residents()) {
		REQUIRE(res.fromStore);
		REQUIRE(first.at(key) == res.mesh);
	}
}

TEST_CASE("Window-edge chunks remesh when the moving window reveals neighbors")
{
	world::MemoryStore store;
	world::Streamer s(store, gen::kStandardSeed,
			{.radius = 1, .cyMin = 0, .cyMax = 1, .budget = 64}, &opaqueNonZero);
	s.setCenter(0, 0);
	pumpUntilQuiescent(s, 4);

	// (1, 0, 0) sits on the window's +X edge: its +X neighbor (2, 0, 0) is
	// out-of-window, so the shared plane's faces are emitted. The cy = 0
	// chunk is solid stone well above y = 8 on both sides of that plane
	// (minigen heights are >= 8), so once the neighbor enters the window
	// the culled mesh must be strictly smaller.
	const world::ChunkKey edge{1, 0, 0};
	REQUIRE(s.residents().contains(edge));
	const std::uint8_t maskBefore = s.residents().at(edge).meshedMask;
	REQUIRE_FALSE(maskBefore & 1u); // +X neighbor not in window yet
	const std::uint32_t quadsBefore = s.residents().at(edge).mesh.quadCount;

	s.setCenter(1, 0);
	pumpUntilQuiescent(s, 4);
	REQUIRE(s.residents().contains(edge));
	const std::uint8_t maskAfter = s.residents().at(edge).meshedMask;
	REQUIRE((maskAfter & 1u) != 0); // +X neighbor now in window
	REQUIRE(s.residents().at(edge).mesh.quadCount < quadsBefore);
}
