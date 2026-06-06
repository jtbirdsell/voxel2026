// Contract 2 reference codec — the compiled anchor for the LOD-aggregate-
// pyramid freeze (docs/contracts/contract-2-lod-pyramid.md). Header-only.
//
// What is STRUCTURAL here (frozen): octree topology over signed chunk space
// with floor-division parent semantics, the node key and its 13-byte
// lexicographic storage encoding, and the Merkle hash discipline that gives
// clients O(log) staleness discovery. What is NOT structural: the per-node
// aggregate payload, which carries its own version byte and is explicitly
// revisable by Phase-2 measurements (the renderer-vs-network tension named
// in the contract).
//
// Topology facts the tests pin:
//   - A level-L node covers 2^L x 2^L x 2^L chunks; level 0 = one chunk.
//   - parent(level L, c) = (L+1, floor(c / 2)) per axis (arithmetic shift —
//     floor semantics for negative coordinates, NOT truncation).
//   - Floor division makes {-1, 0} a fixed point, so signed space never
//     collapses to a single root: the pyramid tops out as a FOREST OF 8
//     sign-octant roots at level kLodTopLevel (coords in {-1,0}^3). Client
//     staleness checks start from 8 root hashes.
//
// Hash discipline (frozen — H itself is part of the schema):
//   - level-0 node hash = FNV-1a-64 over the chunk's CANONICAL Contract-1
//     blob bytes (canonical form is what hashes, per Contract 1).
//   - level-L hash = FNV-1a-64 over [level u8][child presence mask u8]
//     [present children's hashes, octant order, u64 LE each]. Absent
//     children contribute only through the mask.
//   - Therefore: identity tracks CONTENT, never the payload — payload
//     format can evolve without invalidating any hash.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace world {

// Levels 0..28: chunk coords span +/-2^27 (s32 node coords / 16-chunk), and
// floor-halving reaches the {-1,0} fixed point at level 28.
inline constexpr std::uint8_t kLodTopLevel = 28;

struct LodNodeKey {
	std::uint8_t level = 0;
	std::int32_t x = 0, y = 0, z = 0; // node coords AT this level

	friend constexpr bool operator==(const LodNodeKey &, const LodNodeKey &) = default;
};

constexpr LodNodeKey lodKeyForChunk(std::int32_t cx, std::int32_t cy, std::int32_t cz)
{
	return LodNodeKey{0, cx, cy, cz};
}

// Floor division by 2 (arithmetic shift) — the frozen parent semantics.
constexpr std::int32_t lodFloorHalf(std::int32_t v)
{
	return v >> 1;
}

constexpr std::optional<LodNodeKey> lodParent(LodNodeKey k)
{
	if (k.level >= kLodTopLevel)
		return std::nullopt;
	return LodNodeKey{static_cast<std::uint8_t>(k.level + 1), lodFloorHalf(k.x),
			lodFloorHalf(k.y), lodFloorHalf(k.z)};
}

// Octant of k within its parent: bit 0 = x, bit 1 = y, bit 2 = z (low coord
// bit per axis — consistent with the Morton convention's axis order).
constexpr std::uint8_t lodOctantOf(LodNodeKey k)
{
	return static_cast<std::uint8_t>((k.x & 1) | ((k.y & 1) << 1) | ((k.z & 1) << 2));
}

constexpr std::optional<LodNodeKey> lodChild(LodNodeKey k, std::uint8_t octant)
{
	if (k.level == 0 || octant > 7)
		return std::nullopt;
	return LodNodeKey{static_cast<std::uint8_t>(k.level - 1),
			(k.x * 2) + (octant & 1), (k.y * 2) + ((octant >> 1) & 1),
			(k.z * 2) + ((octant >> 2) & 1)};
}

// 13-byte storage key: [level u8][x][y][z], each coordinate sign-bit-flipped
// and BIG-endian so plain lexicographic byte order equals (level, x, y, z)
// numeric order — the column-family grouping property. Deliberate contrast
// with the blob's little-endian payload bytes: this is an ORDER key for the
// store, not data, and the contrast is documented in the contract.
inline constexpr std::size_t kLodStorageKeyBytes = 13;

constexpr std::array<std::uint8_t, kLodStorageKeyBytes> lodStorageKey(LodNodeKey k)
{
	std::array<std::uint8_t, kLodStorageKeyBytes> out{};
	out[0] = k.level;
	const auto put = [&out](std::size_t at, std::int32_t v) {
		const std::uint32_t flipped = static_cast<std::uint32_t>(v) ^ 0x80000000u;
		out[at + 0] = static_cast<std::uint8_t>(flipped >> 24);
		out[at + 1] = static_cast<std::uint8_t>(flipped >> 16);
		out[at + 2] = static_cast<std::uint8_t>(flipped >> 8);
		out[at + 3] = static_cast<std::uint8_t>(flipped);
	};
	put(1, k.x);
	put(5, k.y);
	put(9, k.z);
	return out;
}

// ---- Hash discipline ---------------------------------------------------------

inline constexpr std::uint64_t kLodFnvOffset = 0xCBF29CE484222325ull;
inline constexpr std::uint64_t kLodFnvPrime = 0x00000100000001B3ull;

constexpr std::uint64_t lodFnv1a64(std::span<const std::byte> bytes,
		std::uint64_t seed = kLodFnvOffset)
{
	std::uint64_t h = seed;
	for (const std::byte b : bytes) {
		h ^= std::to_integer<std::uint8_t>(b);
		h *= kLodFnvPrime;
	}
	return h;
}

// Level-0 identity: hash of the chunk's canonical Contract-1 blob bytes.
constexpr std::uint64_t lodChunkHash(std::span<const std::byte> canonicalBlob)
{
	return lodFnv1a64(canonicalBlob);
}

// Interior identity: [level][presence mask][present child hashes, octant
// order, u64 LE]. childHashes[i] is consulted only when mask bit i is set.
//
// INVARIANT-4 TRIPWIRE (Contract 2): no payload field may EVER enter a hash
// input — identity tracks content (canonical blob bytes + child hashes)
// only, which is what makes payload revision non-breaking. Referencing
// LodPayloadV1 from any lod*Hash function is a review-blocking defect; the
// 3-of-73 Merkle test is the runtime regression gate.
constexpr std::uint64_t lodParentHash(std::uint8_t level, std::uint8_t presenceMask,
		const std::array<std::uint64_t, 8> &childHashes)
{
	std::uint64_t h = kLodFnvOffset;
	const auto eat = [&h](std::uint8_t byte) {
		h ^= byte;
		h *= kLodFnvPrime;
	};
	eat(level);
	eat(presenceMask);
	for (std::uint8_t i = 0; i < 8; ++i) {
		if (!(presenceMask & (1u << i)))
			continue;
		std::uint64_t v = childHashes[i];
		for (int b = 0; b < 8; ++b) {
			eat(static_cast<std::uint8_t>(v & 0xFF));
			v >>= 8;
		}
	}
	return h;
}

// The exact node set a single chunk edit re-hashes: the level-0 node and
// every ancestor up to (and including) the sign-octant root.
inline std::vector<LodNodeKey> lodDirtyChain(std::int32_t cx, std::int32_t cy,
		std::int32_t cz)
{
	std::vector<LodNodeKey> chain;
	chain.reserve(kLodTopLevel + 1);
	LodNodeKey k = lodKeyForChunk(cx, cy, cz);
	chain.push_back(k);
	while (const auto p = lodParent(k)) {
		chain.push_back(*p);
		k = *p;
	}
	return chain;
}

// ---- v1 aggregate payload (versioned; revisable by Phase-2 measurement) -----
//
// Held deliberately minimal: identity (the Merkle hash), a coarse 4^3
// occupancy mask (one u64, the same width discipline as Contract 1 state),
// the dominant content id, and the solid-cell count. The renderer/network
// payload tension named in the contract is resolved by MEASUREMENT in
// Phase 2; this section carries its own version byte so that revision
// never touches keys or hashes.

inline constexpr std::uint8_t kLodPayloadVersion = 1;
inline constexpr std::size_t kLodPayloadBytes = 1 + 8 + 8 + 4 + 4; // 25

struct LodPayloadV1 {
	std::uint64_t contentHash = 0;
	std::uint64_t occupancy4 = 0; // 4^3 coarse mask, bit = x + 4*(y + 4*z)
	std::uint32_t dominantContent = 0;
	std::uint32_t solidCells = 0;

	friend constexpr bool operator==(const LodPayloadV1 &, const LodPayloadV1 &) = default;
};

inline std::array<std::byte, kLodPayloadBytes> lodPayloadSerialize(const LodPayloadV1 &p)
{
	std::array<std::byte, kLodPayloadBytes> out{};
	out[0] = static_cast<std::byte>(kLodPayloadVersion);
	const auto putU64 = [&out](std::size_t at, std::uint64_t v) {
		for (int i = 0; i < 8; ++i)
			out[at + i] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
	};
	const auto putU32 = [&out](std::size_t at, std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
			out[at + i] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
	};
	putU64(1, p.contentHash);
	putU64(9, p.occupancy4);
	putU32(17, p.dominantContent);
	putU32(21, p.solidCells);
	return out;
}

inline std::optional<LodPayloadV1> lodPayloadDeserialize(std::span<const std::byte> in)
{
	if (in.size() != kLodPayloadBytes ||
			std::to_integer<std::uint8_t>(in[0]) != kLodPayloadVersion)
		return std::nullopt;
	const auto getU64 = [&in](std::size_t at) {
		std::uint64_t v = 0;
		for (int i = 0; i < 8; ++i)
			v |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[at + i]))
					<< (8 * i);
		return v;
	};
	const auto getU32 = [&in](std::size_t at) {
		std::uint32_t v = 0;
		for (int i = 0; i < 4; ++i)
			v |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(in[at + i]))
					<< (8 * i);
		return v;
	};
	return LodPayloadV1{getU64(1), getU64(9), getU32(17), getU32(21)};
}

} // namespace world
