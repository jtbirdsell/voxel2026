// World solidity adapter (issue #23): the integer-only bridge between
// unpacked Contract-1 chunks and the collision kernel's injected SolidFn.
// Header-only; consumers link voxel2026_sim for the callback typedef.
//
// The callback is PURE INTEGER work — hash-map lookup, shifts, masks, a
// content compare — which is what the SolidFn contract requires: it executes
// outside the pinned-FP translation units, and that is safe precisely
// because no floating point can touch it.

#pragma once

#include <cassert>
#include <cstdint>
#include <unordered_map>

#include "sim/collision_step.hpp"
#include "voxel/morton.hpp"
#include "world/voxel_blob.hpp"

namespace world {

// Sparse map of 16^3 chunks answering "is this world cell solid?".
//
// v1 scoping (deliberate, not silent): every non-air content (content != 0)
// is solid, and in-cell state does not affect collision — per-content
// solidity classes and microblock occupancy masks are registry-driven
// Phase-2 work over the same callback. A missing chunk is air, matching the
// belt's "ungenerated space is empty" semantics.
class SolidityWorld {
public:
	// Chunk-coordinate domain: |c| < 2^20 per axis. Kernel-side positions
	// are clamped to +-2^23 (chunk coords +-2^19); the spare bit is origin-
	// translation headroom. Asserted because the packed key would alias
	// outside it.
	static constexpr std::int32_t kMaxChunkCoord = 1 << 20;

	// The chunk must outlive this object. Only log2Extent and cells are
	// read (the local-coordinate math below hardcodes the v1 16^3 extent —
	// asserted, like the belt). Re-adding a position replaces the mapping.
	void addChunk(std::int32_t cx, std::int32_t cy, std::int32_t cz,
			const UnpackedChunk *chunk)
	{
		assert(chunk != nullptr);
		assert(chunk->log2Extent == kVoxelBlobChunkLog2ExtentV1);
		assert(chunk->cells.size() == 4096);
		m_chunks[packKey(cx, cy, cz)] = chunk;
	}

	// sim::SolidFn-compatible entry point; ctx is the SolidityWorld.
	static bool solidAt(std::int32_t x, std::int32_t y, std::int32_t z,
			const void *ctx)
	{
		const auto *self = static_cast<const SolidityWorld *>(ctx);
		// Arithmetic shift = floor division by 16 (two's complement is
		// guaranteed since C++20), and & 15 yields the matching non-negative
		// local coordinate for negatives too.
		const auto it = self->m_chunks.find(packKey(x >> 4, y >> 4, z >> 4));
		if (it == self->m_chunks.end())
			return false; // ungenerated space is air
		const std::uint32_t cell = voxel::mortonEncode(
				{static_cast<std::uint32_t>(x & 15),
						static_cast<std::uint32_t>(y & 15),
						static_cast<std::uint32_t>(z & 15)});
		return it->second->cells[cell].content != 0;
	}

	std::size_t chunkCount() const { return m_chunks.size(); }

private:
	// 21 bits per axis, bias-encoded; injective over the asserted domain.
	static std::uint64_t packKey(std::int32_t cx, std::int32_t cy, std::int32_t cz)
	{
		assert(cx > -kMaxChunkCoord && cx < kMaxChunkCoord);
		assert(cy > -kMaxChunkCoord && cy < kMaxChunkCoord);
		assert(cz > -kMaxChunkCoord && cz < kMaxChunkCoord);
		const auto enc = [](std::int32_t c) {
			return static_cast<std::uint64_t>(
					(static_cast<std::uint32_t>(c) + (1u << 20)) & 0x1FFFFFu);
		};
		return (enc(cx) << 42) | (enc(cy) << 21) | enc(cz);
	}

	std::unordered_map<std::uint64_t, const UnpackedChunk *> m_chunks;
};

} // namespace world
