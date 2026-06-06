// Spike-1 experiment (issue #1): the GATHER ownership rule makes chunk
// content a pure function of (seed, chunkPos), so parallel generation is
// schedule-independent BY CONSTRUCTION — proven here at 1/4/32 threads and
// under shuffled work orders, pinned to a committed golden region hash, with
// a Luanti-style SCATTER mode as the negative control that must diverge
// under reordering. See docs/spikes/spike-1-mapgen-determinism.md.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#include "gen/minigen.hpp"

namespace {

// All 256 chunk positions of the standard region in storage order.
std::vector<gen::GenChunkPos> allChunks()
{
	std::vector<gen::GenChunkPos> order;
	order.reserve(static_cast<std::size_t>(gen::kRegionCX * gen::kRegionCY * gen::kRegionCZ));
	for (int cz = 0; cz < gen::kRegionCZ; ++cz)
		for (int cy = 0; cy < gen::kRegionCY; ++cy)
			for (int cx = 0; cx < gen::kRegionCX; ++cx)
				order.push_back({cx, cy, cz});
	return order;
}

// Deterministic Fisher-Yates (own splitmix64 PRNG — std::shuffle's swap
// sequence is implementation-defined, and the OUTPUT must not depend on the
// order anyway; a fixed shuffle just makes the test reproducible).
std::vector<gen::GenChunkPos> shuffledChunks(std::uint64_t mixSeed)
{
	std::vector<gen::GenChunkPos> order = allChunks();
	std::uint64_t s = mixSeed;
	const auto next = [&s]() {
		s += 0x9E3779B97F4A7C15ull;
		std::uint64_t z = s;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	};
	for (std::size_t i = order.size() - 1; i > 0; --i)
		std::swap(order[i], order[next() % (i + 1)]);
	return order;
}

// Parallel gather: N workers pull chunk indices from an atomic counter over
// the given order; each chunk buffer is claimed and written by exactly one
// worker (disjoint writes — race-free by construction, ASan/TSan-checkable).
gen::GenRegion generateThreaded(std::uint64_t seed, int threadCount,
		const std::vector<gen::GenChunkPos> &order)
{
	gen::GenRegion region;
	std::atomic<std::size_t> next{0};
	{
		std::vector<std::jthread> workers;
		workers.reserve(static_cast<std::size_t>(threadCount));
		for (int t = 0; t < threadCount; ++t)
			workers.emplace_back([&] {
				for (std::size_t i = next.fetch_add(1); i < order.size();
						i = next.fetch_add(1)) {
					const gen::GenChunkPos cp = order[i];
					gen::generateChunk(seed, cp,
							region.chunkAt(cp.cx, cp.cy, cp.cz));
				}
			});
	}
	return region;
}

// Golden region hash for (kStandardSeed, gather mode), minted on the
// reference build. To regenerate after an INTENTIONAL generator change:
// flip the placeholder to 0, run this test, copy the reported actual value
// here, and re-run green. Any UNINTENDED change to this value is a
// determinism regression.
constexpr std::uint64_t kGoldenRegionHash = 0xE1694C3CD43FD13Aull; // minted 2026-06-06, MSVC Release

// INDEPENDENT ORACLE (review finding: every other gather test — and the
// golden itself, minted from the code it checks — would stay green if the
// gather window were too narrow or the application order wrong). This
// reference path is deliberately different on every axis that could hide a
// bug: terrain rules are re-stated locally; NO window reasoning exists (it
// scans every column of the region plus margin and lets per-cell bounds
// checks decide); and structures are applied by explicitly SORTING anchors
// on gen::anchorOrderKey instead of relying on loop iteration order — so
// window sufficiency, conflict order, and the exported key are all
// falsifiable against it. `withDeco=false` yields the pre-decoration chunk
// used by the decoration exact-diff test.
gen::GenChunk referenceChunk(std::uint64_t seed, gen::GenChunkPos cp, bool withDeco)
{
	gen::GenChunk out;
	const int bx = cp.cx * gen::kGenChunk, by = cp.cy * gen::kGenChunk,
			bz = cp.cz * gen::kGenChunk;

	// Stage A, re-stated independently of the module's internal fill loop.
	for (int z = 0; z < gen::kGenChunk; ++z)
		for (int y = 0; y < gen::kGenChunk; ++y)
			for (int x = 0; x < gen::kGenChunk; ++x) {
				const int h = gen::surfaceHeight(seed, bx + x, bz + z);
				const int wy = by + y;
				gen::Content c = gen::kAir;
				if (wy < h - 3)
					c = gen::kStone;
				else if (wy < h)
					c = gen::kDirt;
				else if (wy == h)
					c = gen::kGrass;
				out.set(x, y, z, c);
			}

	// Stage B: ALL anchors of the region+margin, sorted by the canonical key.
	std::vector<std::pair<std::uint64_t, std::pair<int, int>>> anchors;
	constexpr int kMargin = gen::kReachXZ;
	for (int wz = -kMargin; wz < gen::kRegionCZ * gen::kGenChunk + kMargin; ++wz)
		for (int wx = -kMargin; wx < gen::kRegionCX * gen::kGenChunk + kMargin; ++wx)
			if (gen::isAnchor(seed, wx, wz))
				anchors.push_back({gen::anchorOrderKey(wx, wz), {wx, wz}});
	std::sort(anchors.begin(), anchors.end());

	struct Ctx {
		gen::GenChunk *chunk;
		int bx, by, bz;
	} ctx{&out, bx, by, bz};
	for (const auto &[key, col] : anchors) {
		(void)key;
		gen::forEachStructureCell(seed, col.first, col.second,
				[](void *raw, int wx, int wy, int wz, gen::Content c) {
					auto *cc = static_cast<Ctx *>(raw);
					const int x = wx - cc->bx, y = wy - cc->by, z = wz - cc->bz;
					if (x >= 0 && x < gen::kGenChunk && y >= 0 && y < gen::kGenChunk &&
							z >= 0 && z < gen::kGenChunk)
						cc->chunk->set(x, y, z, c);
				},
				&ctx);
	}

	if (withDeco)
		gen::decorateChunk(seed, cp, out);
	return out;
}

} // namespace

TEST_CASE("Gather region hash is identical across 1, 4, and 32 worker threads")
{
	const auto order = allChunks();
	const std::uint64_t h1 = gen::hashRegion(generateThreaded(gen::kStandardSeed, 1, order));
	const std::uint64_t h4 = gen::hashRegion(generateThreaded(gen::kStandardSeed, 4, order));
	const std::uint64_t h32 = gen::hashRegion(generateThreaded(gen::kStandardSeed, 32, order));
	REQUIRE(h1 == h4);
	REQUIRE(h1 == h32);
}

TEST_CASE("Gather region hash is identical under shuffled work orders")
{
	const std::uint64_t base =
			gen::hashRegion(generateThreaded(gen::kStandardSeed, 4, allChunks()));
	for (std::uint64_t mix = 1; mix <= 3; ++mix) {
		const std::uint64_t shuffled = gen::hashRegion(
				generateThreaded(gen::kStandardSeed, 4, shuffledChunks(mix)));
		CAPTURE(mix);
		REQUIRE(shuffled == base);
	}
}

TEST_CASE("Gather chunk is pure: standalone equals threaded shuffled in-region generation")
{
	// Reference side crosses a real scheduling boundary (review-corrected:
	// comparing against the serial region run was f(x)==f(x) by construction).
	const gen::GenRegion region =
			generateThreaded(gen::kStandardSeed, 8, shuffledChunks(7));
	for (const gen::GenChunkPos cp : allChunks()) {
		gen::GenChunk standalone;
		gen::generateChunk(gen::kStandardSeed, cp, standalone);
		CAPTURE(cp.cx, cp.cy, cp.cz);
		REQUIRE(standalone.cells == region.chunkAt(cp.cx, cp.cy, cp.cz).cells);
	}
}

TEST_CASE("Gather equals the independent oracle on every chunk")
{
	// The load-bearing correctness anchor (review finding): the oracle scans
	// ALL anchors with no window and applies them sorted by anchorOrderKey,
	// so a too-narrow gather window, a wrong application order, or a key
	// inconsistent with the loop order each fail here.
	for (const gen::GenChunkPos cp : allChunks()) {
		gen::GenChunk production;
		gen::generateChunk(gen::kStandardSeed, cp, production);
		const gen::GenChunk reference = referenceChunk(gen::kStandardSeed, cp, true);
		CAPTURE(cp.cx, cp.cy, cp.cz);
		REQUIRE(production.cells == reference.cells);
	}
}

TEST_CASE("Standard region is non-vacuous: cross-chunk structures and conflicts exist")
{
	const gen::SpanStats stats = gen::structureSpanStats(gen::kStandardSeed);
	CAPTURE(stats.crossChunkStructures, stats.sameCellConflicts);
	REQUIRE(stats.crossChunkStructures >= 10);
	REQUIRE(stats.sameCellConflicts >= 1);
}

TEST_CASE("Scatter control: forward vs reverse generation orders diverge")
{
	const auto forward = allChunks();
	auto reverse = forward;
	for (std::size_t i = 0; i < reverse.size() / 2; ++i)
		std::swap(reverse[i], reverse[reverse.size() - 1 - i]);

	gen::GenRegion a, b;
	gen::generateRegionScatter(gen::kStandardSeed, forward, a);
	gen::generateRegionScatter(gen::kStandardSeed, reverse, b);
	REQUIRE(gen::hashRegion(a) != gen::hashRegion(b));
}

TEST_CASE("Scatter control diverges from the gather result on the same seed")
{
	gen::GenRegion gather, scatter;
	gen::generateRegionGather(gen::kStandardSeed, gather);
	gen::generateRegionScatter(gen::kStandardSeed, allChunks(), scatter);
	// Same structure/terrain/deco definitions, same hash functions — any
	// difference is purely the ownership rule dropping spills.
	REQUIRE(gen::hashRegion(gather) != gen::hashRegion(scatter));
}

TEST_CASE("Gather region hash matches the committed golden")
{
	gen::GenRegion region;
	gen::generateRegionGather(gen::kStandardSeed, region);
	const std::uint64_t actual = gen::hashRegion(region);
	CAPTURE(actual);
	REQUIRE(actual == kGoldenRegionHash);
}

TEST_CASE("Terrain and anchor functions are pure and the height band holds")
{
	for (int wx = -8; wx < 136; wx += 5)
		for (int wz = -8; wz < 136; wz += 5) {
			const int h1 = gen::surfaceHeight(gen::kStandardSeed, wx, wz);
			const int h2 = gen::surfaceHeight(gen::kStandardSeed, wx, wz);
			REQUIRE(h1 == h2);
			REQUIRE(h1 >= gen::kHeightMin);
			REQUIRE(h1 < gen::kHeightMax);
			REQUIRE(gen::isAnchor(gen::kStandardSeed, wx, wz) ==
					gen::isAnchor(gen::kStandardSeed, wx, wz));
		}
}

TEST_CASE("Canonical anchor order key is z-major, x-minor, signed ascending")
{
	REQUIRE(gen::anchorOrderKey(0, 0) < gen::anchorOrderKey(1, 0));
	REQUIRE(gen::anchorOrderKey(127, 0) < gen::anchorOrderKey(0, 1));
	REQUIRE(gen::anchorOrderKey(5, 7) < gen::anchorOrderKey(4, 8));
	// Negative gather margin (review-added — the raw-u32 encoding this
	// replaced inverted exactly these cases):
	REQUIRE(gen::anchorOrderKey(-2, 0) < gen::anchorOrderKey(0, 0));
	REQUIRE(gen::anchorOrderKey(-1, 0) < gen::anchorOrderKey(0, 0));
	REQUIRE(gen::anchorOrderKey(127, -1) < gen::anchorOrderKey(-2, 0));
	REQUIRE(gen::anchorOrderKey(-2, -2) < gen::anchorOrderKey(-1, -2));
}

TEST_CASE("Boundary seeds do not degenerate")
{
	for (const std::uint64_t seed : {std::uint64_t{0}, ~std::uint64_t{0}}) {
		CAPTURE(seed);
		// Heights must not collapse to a single value across the region...
		const int first = gen::surfaceHeight(seed, 0, 0);
		bool varied = false;
		for (int wx = 0; wx < 128 && !varied; ++wx)
			for (int wz = 0; wz < 128 && !varied; ++wz)
				varied = gen::surfaceHeight(seed, wx, wz) != first;
		REQUIRE(varied);
		// ...and at least one anchor must exist in the region.
		bool anyAnchor = false;
		for (int wx = 0; wx < 128 && !anyAnchor; ++wx)
			for (int wz = 0; wz < 128 && !anyAnchor; ++wz)
				anyAnchor = gen::isAnchor(seed, wx, wz);
		REQUIRE(anyAnchor);
	}
}

TEST_CASE("Decoration hook diff is exactly grass-to-flower, and it is idempotent")
{
	const gen::GenChunkPos cp{3, 0, 5};

	// Exact-diff check (review-corrected: idempotence alone would not catch a
	// hook that also corrupted non-grass cells): every changed cell must be
	// kGrass before and kFlower after; at least one such change must exist.
	const gen::GenChunk preDeco = referenceChunk(gen::kStandardSeed, cp, false);
	gen::GenChunk decorated = preDeco;
	gen::decorateChunk(gen::kStandardSeed, cp, decorated);

	int changed = 0;
	for (std::size_t i = 0; i < preDeco.cells.size(); ++i) {
		if (preDeco.cells[i] == decorated.cells[i])
			continue;
		++changed;
		CAPTURE(i);
		REQUIRE(preDeco.cells[i] == gen::kGrass);
		REQUIRE(decorated.cells[i] == gen::kFlower);
	}
	REQUIRE(changed >= 1);

	gen::GenChunk again = decorated;
	gen::decorateChunk(gen::kStandardSeed, cp, again);
	REQUIRE(again.cells == decorated.cells);
}
