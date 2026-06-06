// The conveyor belt (issue #21): generate -> canonical Contract-1 blob ->
// store (load-or-generate) -> unpack, plus the slot adapter the renderer
// needs. Header-only; consumers link voxel2026_gen.

#pragma once

#include <cstdint>
#include <vector>

#include "gen/minigen.hpp"
#include "mesh/greedy.hpp"
#include "store/store.hpp"
#include "voxel/morton.hpp"
#include "world/chunk_key.hpp"
#include "world/voxel_blob.hpp"

namespace world {

struct BeltChunk {
	std::int32_t x = 0, y = 0, z = 0;
	UnpackedChunk chunk;
	bool fromStore = false; // true = loaded; false = generated this call
	// Persistence is BEST-EFFORT per chunk (review finding): a failed put
	// still yields a usable, deterministic chunk — the failure surfaces as
	// persisted=false here and as fromStore=false on the NEXT pass.
	bool persisted = false;
};

// Load every chunk in the inclusive box from the store, generating (and
// persisting) the misses — the second run over the same region must come
// back entirely fromStore, bit-identical (pinned by test). A blob that
// fails validation is treated as a miss and regenerated over (corruption
// heals to deterministic content rather than failing the region).
inline std::vector<BeltChunk> loadOrGenerateRegion(KvStore &store, std::uint64_t seed,
		std::int32_t x0, std::int32_t y0, std::int32_t z0, std::int32_t x1,
		std::int32_t y1, std::int32_t z1)
{
	std::vector<BeltChunk> out;
	for (std::int32_t cz = z0; cz <= z1; ++cz)
		for (std::int32_t cy = y0; cy <= y1; ++cy)
			for (std::int32_t cx = x0; cx <= x1; ++cx) {
				const auto key = chunkStoreKey(cx, cy, cz);
				const std::span<const std::byte> keySpan{
						reinterpret_cast<const std::byte *>(key.data()), key.size()};
				BeltChunk bc;
				bc.x = cx;
				bc.y = cy;
				bc.z = cz;
				if (const auto blob = store.get(StoreSpace::kChunks, keySpan)) {
					if (auto chunk = unpackVoxelBlob(*blob)) {
						bc.chunk = std::move(*chunk);
						bc.fromStore = true;
						bc.persisted = true;
						out.push_back(std::move(bc));
						continue;
					}
				}
				gen::GenChunk g;
				gen::generateChunk(seed, {cx, cy, cz}, g);
				std::vector<CellValue> cells(4096);
				for (int z = 0; z < gen::kGenChunk; ++z)
					for (int y = 0; y < gen::kGenChunk; ++y)
						for (int x = 0; x < gen::kGenChunk; ++x)
							cells[voxel::mortonEncode({static_cast<std::uint32_t>(x),
									static_cast<std::uint32_t>(y),
									static_cast<std::uint32_t>(z)})] =
									CellValue{g.at(x, y, z), 0};
				const auto blob = packVoxelBlob(cells, 4);
				bc.persisted = store.put(StoreSpace::kChunks, keySpan, blob);
				bc.chunk = *unpackVoxelBlob(blob);
				bc.fromStore = false;
				out.push_back(std::move(bc));
			}
	return out;
}

// Renderer adapter: the mesher emits CHUNK-LOCAL palette slots (its
// contract); a world view needs WORLD-CONSISTENT material identity, or two
// uniform chunks of different content would share slot 0 and a color.
// Rewrites each vertex's slot to the palette entry's global content id.
// SCOPED v1 SIMPLIFICATIONS (deliberate, not silent): the state field
// drops out of the RENDERED identity (two cells, same content, different
// state share a color until the Phase-2 tile-index mapping carries state),
// and content ids truncate to the vertex's 16 slot bits (minigen ids are
// single digits; the general mapping is the same Phase-2 work).
inline void remapSlotsToContent(mesh::ChunkMesh &m, const UnpackedChunk &chunk)
{
	for (mesh::PackedVertex &v : m.vertices) {
		// The mesher guarantees slots index its source palette; guard the
		// chain anyway — a violated contract should stop here, not render.
		if (v.slot >= chunk.palette.size())
			continue; // leaves the local slot: visibly wrong, never UB
		v.slot = chunk.palette[v.slot].content & 0xFFFFu;
	}
}

} // namespace world
