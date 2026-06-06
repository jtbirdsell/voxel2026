// Miniature staged chunk generator for the spike-1 mapgen-determinism
// experiment (issue #1, docs/spikes/spike-1-mapgen-determinism.md).
//
// The property under test: with the GATHER ownership rule, chunk content is a
// PURE FUNCTION of (seed, chunk position) — no dependence on generation order
// or worker count. Cross-chunk structures are handled by each chunk PULLING
// from every anchor that can reach it, applied in a canonical order, instead
// of anchors PUSHING writes into neighbor chunks (the Luanti-style scatter
// model, provided here as `generateRegionScatter` — the schedule-DEPENDENT
// negative control).
//
// This module is INTEGER-ONLY: it contains no floating-point arithmetic at
// all, which is why — unlike src/sim — it needs no pinned FP compile flags.
// Determinism here is a property of the design, not of the FP environment.
//
// This is a spike stand-in, not the engine mapgen: no caves, ores, biomes,
// liquids, or Lua hooks (see the spike doc's limitations section).

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gen {

using Content = std::uint16_t;

// Cell contents. Values are part of the golden-hash contract: reordering or
// renumbering them changes every committed hash.
inline constexpr Content kAir = 0;
inline constexpr Content kStone = 1;
inline constexpr Content kDirt = 2;
inline constexpr Content kGrass = 3;
inline constexpr Content kWood = 4;
inline constexpr Content kLeaves = 5;
inline constexpr Content kFlower = 6;

inline constexpr int kGenChunk = 16; // cells per chunk edge

// Stage A surface band: kHeightMin <= surfaceHeight < kHeightMax.
inline constexpr int kHeightMin = 8;
inline constexpr int kHeightMax = 24;

// Structure (tree) reach from its anchor column: at most kReachXZ cells
// horizontally (canopy radius) and kReachY cells above the surface
// (max trunk 6 + canopy 2). Both strictly less than the chunk edge, so a
// 1-ring column window (chunk's xz extent widened by kReachXZ) is sufficient
// to enumerate every anchor able to reach a chunk — the gather window can
// never miss a contributor.
inline constexpr int kReachXZ = 2;
inline constexpr int kReachY = 8;
static_assert(kReachXZ < kGenChunk && kReachY < kGenChunk,
		"1-ring gather sufficiency requires structure reach < chunk edge");

// Standard experiment region (in chunks) and the pinned seed. The seed was
// chosen so the standard region demonstrably contains >= 10 cross-chunk
// structures and >= 1 same-cell structure conflict (asserted by the
// non-vacuity test BEFORE the golden hash means anything).
inline constexpr int kRegionCX = 8;
inline constexpr int kRegionCY = 4;
inline constexpr int kRegionCZ = 8;
inline constexpr std::uint64_t kStandardSeed = 0x5350494B45312031ull; // "SPIKE1 1"

struct GenChunkPos {
	int cx = 0, cy = 0, cz = 0;

	friend constexpr bool operator==(const GenChunkPos &, const GenChunkPos &) = default;
};

// Cell storage order is part of the hash contract: x-fastest, then y, then z.
struct GenChunk {
	std::array<Content, static_cast<std::size_t>(kGenChunk * kGenChunk * kGenChunk)> cells{};

	static constexpr std::size_t index(int x, int y, int z)
	{
		return static_cast<std::size_t>(x + kGenChunk * (y + kGenChunk * z));
	}
	Content at(int x, int y, int z) const { return cells[index(x, y, z)]; }
	void set(int x, int y, int z, Content c) { cells[index(x, y, z)] = c; }
};

// Fixed-size region of kRegionCX x kRegionCY x kRegionCZ chunks with chunk
// (0,0,0) at world origin. Chunk storage order (and hash order): cx-fastest,
// then cy, then cz.
struct GenRegion {
	std::vector<GenChunk> chunks;

	GenRegion() : chunks(static_cast<std::size_t>(kRegionCX * kRegionCY * kRegionCZ)) {}

	static constexpr std::size_t index(int cx, int cy, int cz)
	{
		return static_cast<std::size_t>(cx + kRegionCX * (cy + kRegionCY * cz));
	}
	GenChunk &chunkAt(int cx, int cy, int cz) { return chunks[index(cx, cy, cz)]; }
	const GenChunk &chunkAt(int cx, int cy, int cz) const { return chunks[index(cx, cy, cz)]; }
};

// ---- Stage A: terrain (pure functions of seed + world column) -------------

// Surface height for a world column; always in [kHeightMin, kHeightMax).
int surfaceHeight(std::uint64_t seed, int wx, int wz);

// ---- Stage B: structures ---------------------------------------------------

// Pure anchor predicate: does a tree anchor at this column? Reads only
// (seed, column, surfaceHeight) — never any chunk state.
bool isAnchor(std::uint64_t seed, int wx, int wz);

// Canonical total order over anchor columns used for conflict resolution:
// structures are applied in ascending anchorOrderKey — z-major, x-minor,
// signed-ascending — and the last writer under this order wins a contested
// cell (within one structure the trunk is emitted after the canopy, so a
// trunk always wins its own cells). Valid over the FULL signed coordinate
// domain, including the negative gather margin at region-edge chunks
// (review-hardened: sign-bit-flipped encoding keeps u64 key order identical
// to signed (wz, wx) lexicographic order everywhere).
std::uint64_t anchorOrderKey(int wx, int wz);

// Enumerate the world-space cells of the structure anchored at (wx, wz), in
// the structure's canonical emission order (canopy first, trunk last).
// The SAME function feeds gather, scatter, and the coverage statistics, so a
// gather-vs-scatter difference can never be an artifact of divergent
// structure definitions.
void forEachStructureCell(std::uint64_t seed, int wx, int wz,
		void (*cb)(void *ctx, int wx, int wy, int wz, Content c), void *ctx);

// ---- Generation entry points ----------------------------------------------

// GATHER: generate one chunk completely (terrain -> gathered structures in
// canonical anchor order -> decoration). Pure function of (seed, cp): calling
// this for the same arguments always yields byte-identical cells, regardless
// of any other generation activity.
void generateChunk(std::uint64_t seed, GenChunkPos cp, GenChunk &out);

// GATHER over the whole standard region, single-threaded convenience.
// (Parallel callers drive generateChunk themselves over disjoint chunks.)
void generateRegionGather(std::uint64_t seed, GenRegion &out);

// SCATTER (negative control): Luanti-style first-writer-wins. Chunks are
// generated in the caller-given order. Structure spill writes apply to the
// target chunk's array immediately; if that chunk's own generation runs
// LATER, its terrain pass overwrites the whole array and the spill is lost,
// while spills into chunks generated EARLIER persist. Output therefore
// depends on `order` BY DESIGN — this models the scheduling disease the
// gather rule cures.
void generateRegionScatter(std::uint64_t seed, std::span<const GenChunkPos> order,
		GenRegion &out);

// Stage C decoration hook (exposed for direct testing): own-chunk-only —
// receives nothing but the owning chunk's buffer, so it is structurally
// incapable of cross-chunk reads or writes. The snapshot-isolated
// `on_generated` analog.
void decorateChunk(std::uint64_t seed, GenChunkPos cp, GenChunk &inout);

// ---- Hashing (shared verbatim by both modes) -------------------------------

// FNV-1a-64 over cells in storage order (x-fastest, y, z).
std::uint64_t hashChunk(const GenChunk &chunk);

// FNV-1a-64 over chunk hashes in storage order (cx-fastest, cy, cz).
// Depends only on final region CONTENT — never on generation schedule.
std::uint64_t hashRegion(const GenRegion &region);

// ---- Non-vacuity statistics -------------------------------------------------

struct SpanStats {
	int crossChunkStructures = 0; // structures whose cells span >= 2 chunks
	int sameCellConflicts = 0;    // cells targeted by >= 2 distinct structures
};

// Computed from the pure coverage functions over the standard region extent
// (anchors within the region's columns). Used by tests to prove the golden
// region actually exercises cross-chunk gathering and conflict resolution.
SpanStats structureSpanStats(std::uint64_t seed);

} // namespace gen
