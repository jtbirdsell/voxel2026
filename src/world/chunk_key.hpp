// Chunk store-key convention (issue #21): the kChunks-space key for a chunk
// at signed chunk coordinates — 12 bytes, per-axis sign-bit-flipped and
// big-endian, so plain lexicographic byte order equals (x, y, z) numeric
// order. Same construction as Contract 2's LOD storage key (and the same
// engine-wide signed-key policy from Contract 4), minus the level byte:
// chunks live at exactly one level.

#pragma once

#include <array>
#include <cstdint>

namespace world {

inline constexpr std::size_t kChunkStoreKeyBytes = 12;

constexpr std::array<std::uint8_t, kChunkStoreKeyBytes> chunkStoreKey(std::int32_t x,
		std::int32_t y, std::int32_t z)
{
	std::array<std::uint8_t, kChunkStoreKeyBytes> out{};
	const auto put = [&out](std::size_t at, std::int32_t v) {
		const std::uint32_t flipped = static_cast<std::uint32_t>(v) ^ 0x80000000u;
		out[at + 0] = static_cast<std::uint8_t>(flipped >> 24);
		out[at + 1] = static_cast<std::uint8_t>(flipped >> 16);
		out[at + 2] = static_cast<std::uint8_t>(flipped >> 8);
		out[at + 3] = static_cast<std::uint8_t>(flipped);
	};
	put(0, x);
	put(4, y);
	put(8, z);
	return out;
}

} // namespace world
