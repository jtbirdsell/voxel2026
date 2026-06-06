#include "mesh/parallel.hpp"

namespace mesh {

std::vector<ChunkMesh> meshChunksSerial(std::span<const world::UnpackedChunk> chunks,
		OpaquePredicate isOpaque)
{
	std::vector<ChunkMesh> out(chunks.size());
	const NeighborViews none{};
	for (std::size_t i = 0; i < chunks.size(); ++i)
		out[i] = greedyMesh(chunks[i], none, isOpaque);
	return out;
}

std::vector<ChunkMesh> meshChunksParallel(std::span<const world::UnpackedChunk> chunks,
		OpaquePredicate isOpaque, jobs::JobSystem &js)
{
	std::vector<ChunkMesh> out(chunks.size());
	const NeighborViews none{};
	// Grain 1: one chunk is already a coarse task (~hundreds of microseconds);
	// spreading maximally lets the work-stealer balance surface-vs-air chunks.
	js.parallelFor(chunks.size(), 1, [&](std::size_t i) {
		out[i] = greedyMesh(chunks[i], none, isOpaque);
	});
	return out;
}

} // namespace mesh
