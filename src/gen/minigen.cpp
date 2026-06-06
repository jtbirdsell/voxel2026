// Spike-1 minigen implementation. Integer-only throughout (see header): every
// quantity is derived from splitmix64 hashing of (seed, coordinates, salt),
// so determinism is structural and no FP flags are involved.

#include "gen/minigen.hpp"

#include <unordered_map>
#include <unordered_set>

namespace gen {

namespace {

constexpr std::uint64_t splitmix64(std::uint64_t x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

// Domain-separated coordinate hash. Coordinates are folded as zig-zag-free
// u32 reinterpretations (the documented domain is bounded, far from wrap).
constexpr std::uint64_t hashCoords(std::uint64_t seed, int a, int b, int c, std::uint64_t salt)
{
	std::uint64_t x = seed ^ salt;
	x = splitmix64(x ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32 |
			static_cast<std::uint32_t>(b)));
	x = splitmix64(x ^ static_cast<std::uint32_t>(c));
	return x;
}

constexpr std::uint64_t kSaltHeight = 0x484549474854ull;  // "HEIGHT"
constexpr std::uint64_t kSaltAnchor = 0x414E43484F52ull;  // "ANCHOR"
constexpr std::uint64_t kSaltTrunk = 0x5452554E4Bull;     // "TRUNK"
constexpr std::uint64_t kSaltDeco = 0x4445434Full;        // "DECO"

// World extent of the standard region, in cells.
constexpr int kWorldX = kRegionCX * kGenChunk; // 128
constexpr int kWorldY = kRegionCY * kGenChunk; // 64
constexpr int kWorldZ = kRegionCZ * kGenChunk; // 128

// Tree trunk height in [4, 6]; with the canopy radius 2 this caps the
// structure's vertical reach at trunkHeight + 2 <= 8 = kReachY.
int trunkHeight(std::uint64_t seed, int wx, int wz)
{
	return 4 + static_cast<int>(hashCoords(seed, wx, wz, 0, kSaltTrunk) % 3u);
}

void terrainFillChunk(std::uint64_t seed, GenChunkPos cp, GenChunk &out)
{
	const int bx = cp.cx * kGenChunk, by = cp.cy * kGenChunk, bz = cp.cz * kGenChunk;
	for (int z = 0; z < kGenChunk; ++z)
		for (int x = 0; x < kGenChunk; ++x) {
			const int h = surfaceHeight(seed, bx + x, bz + z);
			for (int y = 0; y < kGenChunk; ++y) {
				const int wy = by + y;
				Content c = kAir;
				if (wy < h - 3)
					c = kStone;
				else if (wy < h)
					c = kDirt;
				else if (wy == h)
					c = kGrass;
				out.set(x, y, z, c);
			}
		}
}

struct ChunkWriteCtx {
	GenChunk *chunk;
	int bx, by, bz;
};

void writeCellIntoChunk(void *ctxRaw, int wx, int wy, int wz, Content c)
{
	auto *ctx = static_cast<ChunkWriteCtx *>(ctxRaw);
	const int x = wx - ctx->bx, y = wy - ctx->by, z = wz - ctx->bz;
	if (x >= 0 && x < kGenChunk && y >= 0 && y < kGenChunk && z >= 0 && z < kGenChunk)
		ctx->chunk->set(x, y, z, c);
}

struct RegionWriteCtx {
	GenRegion *region;
};

void writeCellIntoRegion(void *ctxRaw, int wx, int wy, int wz, Content c)
{
	auto *ctx = static_cast<RegionWriteCtx *>(ctxRaw);
	if (wx < 0 || wx >= kWorldX || wy < 0 || wy >= kWorldY || wz < 0 || wz >= kWorldZ)
		return;
	GenChunk &chunk = ctx->region->chunkAt(wx / kGenChunk, wy / kGenChunk, wz / kGenChunk);
	chunk.set(wx % kGenChunk, wy % kGenChunk, wz % kGenChunk, c);
}

} // namespace

int surfaceHeight(std::uint64_t seed, int wx, int wz)
{
	return kHeightMin + static_cast<int>(hashCoords(seed, wx, wz, 0, kSaltHeight) %
			static_cast<std::uint64_t>(kHeightMax - kHeightMin));
}

bool isAnchor(std::uint64_t seed, int wx, int wz)
{
	// ~1 anchor per 48 columns; the predicate reads only pure inputs.
	return hashCoords(seed, wx, wz, surfaceHeight(seed, wx, wz), kSaltAnchor) % 48u == 0;
}

std::uint64_t anchorOrderKey(int wx, int wz)
{
	// Order-preserving over the FULL signed domain: flipping the sign bit
	// maps int32 ordering onto u32 ordering (review-corrected — the previous
	// raw u32 reinterpretation inverted the order for the negative gather
	// margin at region-edge chunks, which would have reintroduced
	// schedule-dependence in any consumer that sorted by the key).
	const std::uint32_t ux = static_cast<std::uint32_t>(wx) ^ 0x80000000u;
	const std::uint32_t uz = static_cast<std::uint32_t>(wz) ^ 0x80000000u;
	return (static_cast<std::uint64_t>(uz) << 32) | ux;
}

void forEachStructureCell(std::uint64_t seed, int wx, int wz,
		void (*cb)(void *ctx, int wx, int wy, int wz, Content c), void *ctx)
{
	const int h = surfaceHeight(seed, wx, wz);
	const int top = h + trunkHeight(seed, wx, wz);

	// Canopy first (diamond of radius kReachXZ around the trunk top), trunk
	// last — so the trunk always wins its own column's cells.
	for (int dz = -kReachXZ; dz <= kReachXZ; ++dz)
		for (int dy = -kReachXZ; dy <= kReachXZ; ++dy)
			for (int dx = -kReachXZ; dx <= kReachXZ; ++dx) {
				const int manhattan = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) +
						(dz < 0 ? -dz : dz);
				if (manhattan <= kReachXZ)
					cb(ctx, wx + dx, top + dy, wz + dz, kLeaves);
			}
	for (int y = h + 1; y <= top; ++y)
		cb(ctx, wx, y, wz, kWood);
}

void decorateChunk(std::uint64_t seed, GenChunkPos cp, GenChunk &inout)
{
	const int bx = cp.cx * kGenChunk, by = cp.cy * kGenChunk, bz = cp.cz * kGenChunk;
	for (int z = 0; z < kGenChunk; ++z)
		for (int y = 0; y < kGenChunk; ++y)
			for (int x = 0; x < kGenChunk; ++x)
				if (inout.at(x, y, z) == kGrass &&
						hashCoords(seed, bx + x, by + y, bz + z, kSaltDeco) % 7u == 0)
					inout.set(x, y, z, kFlower);
}

void generateChunk(std::uint64_t seed, GenChunkPos cp, GenChunk &out)
{
	terrainFillChunk(seed, cp, out);

	// GATHER: enumerate every column whose structure could reach this chunk
	// (the chunk's xz extent widened by kReachXZ — sufficient because
	// kReachXZ < kGenChunk, see the header static_assert) in canonical order:
	// signed-ascending (wz, then wx), which equals ascending anchorOrderKey
	// over the full signed domain (the key uses a sign-bit-flipped encoding
	// precisely so this equivalence holds at the negative margin too — the
	// reference oracle in test_minigen.cpp applies anchors by SORTING on the
	// key, so any divergence between loop order and key order fails tests).
	// Later structures overwrite earlier ones at contested cells; the order
	// is a pure function of coordinates, so the outcome is
	// schedule-independent.
	const int bx = cp.cx * kGenChunk, by = cp.cy * kGenChunk, bz = cp.cz * kGenChunk;
	ChunkWriteCtx ctx{&out, bx, by, bz};
	for (int wz = bz - kReachXZ; wz < bz + kGenChunk + kReachXZ; ++wz)
		for (int wx = bx - kReachXZ; wx < bx + kGenChunk + kReachXZ; ++wx)
			if (isAnchor(seed, wx, wz))
				forEachStructureCell(seed, wx, wz, &writeCellIntoChunk, &ctx);

	decorateChunk(seed, cp, out);
}

void generateRegionGather(std::uint64_t seed, GenRegion &out)
{
	for (int cz = 0; cz < kRegionCZ; ++cz)
		for (int cy = 0; cy < kRegionCY; ++cy)
			for (int cx = 0; cx < kRegionCX; ++cx)
				generateChunk(seed, {cx, cy, cz}, out.chunkAt(cx, cy, cz));
}

void generateRegionScatter(std::uint64_t seed, std::span<const GenChunkPos> order,
		GenRegion &out)
{
	for (const GenChunkPos cp : order) {
		GenChunk &chunk = out.chunkAt(cp.cx, cp.cy, cp.cz);

		// Terrain pass clobbers the whole array — including any structure
		// spill an earlier-generated chunk pushed into it (first-writer-wins
		// scheduling disease, by design).
		terrainFillChunk(seed, cp, chunk);

		// This chunk's OWN anchors (owner = the chunk containing the anchor's
		// surface cell, so each anchor is placed exactly once) push their
		// structures region-wide.
		const int bx = cp.cx * kGenChunk, bz = cp.cz * kGenChunk;
		RegionWriteCtx ctx{&out};
		for (int wz = bz; wz < bz + kGenChunk; ++wz)
			for (int wx = bx; wx < bx + kGenChunk; ++wx)
				if (isAnchor(seed, wx, wz) &&
						surfaceHeight(seed, wx, wz) / kGenChunk == cp.cy)
					forEachStructureCell(seed, wx, wz, &writeCellIntoRegion, &ctx);

		decorateChunk(seed, cp, chunk);
	}
}

std::uint64_t hashChunk(const GenChunk &chunk)
{
	std::uint64_t h = 0xCBF29CE484222325ull;
	for (const Content c : chunk.cells) {
		h = (h ^ (c & 0xFFu)) * 0x100000001B3ull;
		h = (h ^ (c >> 8)) * 0x100000001B3ull;
	}
	return h;
}

std::uint64_t hashRegion(const GenRegion &region)
{
	std::uint64_t h = 0xCBF29CE484222325ull;
	for (const GenChunk &chunk : region.chunks) {
		std::uint64_t ch = hashChunk(chunk);
		for (int i = 0; i < 8; ++i) {
			h = (h ^ (ch & 0xFFu)) * 0x100000001B3ull;
			ch >>= 8;
		}
	}
	return h;
}

SpanStats structureSpanStats(std::uint64_t seed)
{
	struct StatsCtx {
		std::unordered_set<std::uint64_t> structureCells; // dedup within one structure
		std::unordered_map<std::uint64_t, int> cellWriters;
		std::unordered_set<std::uint64_t> chunksTouched;
	};
	StatsCtx ctx;

	SpanStats stats;
	for (int wz = 0; wz < kWorldZ; ++wz)
		for (int wx = 0; wx < kWorldX; ++wx) {
			if (!isAnchor(seed, wx, wz))
				continue;
			ctx.structureCells.clear();
			ctx.chunksTouched.clear();
			forEachStructureCell(seed, wx, wz,
					[](void *raw, int cx_, int cy_, int cz_, Content) {
						auto *c = static_cast<StatsCtx *>(raw);
						if (cx_ < 0 || cx_ >= kWorldX || cy_ < 0 || cy_ >= kWorldY ||
								cz_ < 0 || cz_ >= kWorldZ)
							return;
						const std::uint64_t key =
								(static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx_))
										<< 40) ^
								(static_cast<std::uint64_t>(static_cast<std::uint32_t>(cy_))
										<< 20) ^
								static_cast<std::uint32_t>(cz_);
						c->structureCells.insert(key);
						c->chunksTouched.insert(
								(static_cast<std::uint64_t>(static_cast<std::uint32_t>(
										 cx_ / kGenChunk))
										<< 40) ^
								(static_cast<std::uint64_t>(static_cast<std::uint32_t>(
										 cy_ / kGenChunk))
										<< 20) ^
								static_cast<std::uint32_t>(cz_ / kGenChunk));
					},
					&ctx);
			if (ctx.chunksTouched.size() >= 2)
				++stats.crossChunkStructures;
			for (const std::uint64_t key : ctx.structureCells)
				++ctx.cellWriters[key];
		}

	for (const auto &[key, writers] : ctx.cellWriters) {
		(void)key;
		if (writers >= 2)
			++stats.sameCellConflicts;
	}
	return stats;
}

} // namespace gen
