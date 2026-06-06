// The first migrated pool (issue #20): chunk meshing on the job system.
// Chosen first deliberately — meshing is a pure function of its chunk (no
// env lock, per the round-2 review record), so the migration has a
// falsifiable gate: the parallel result must be BIT-EQUAL to the serial
// one, whatever the schedule. Chunks mesh with empty neighbor views in this
// pool shape (neighbor-aware batching is a later refinement of the pool,
// not of the mesher).

#pragma once

#include <span>
#include <vector>

#include "jobs/jobs.hpp"
#include "mesh/greedy.hpp"

namespace mesh {

// Serial reference — the determinism gate's ground truth.
std::vector<ChunkMesh> meshChunksSerial(std::span<const world::UnpackedChunk> chunks,
		OpaquePredicate isOpaque);

// Same chunks on the job system: one task per chunk (each writes a disjoint
// result slot; no shared mutable state). Returns results in input order.
std::vector<ChunkMesh> meshChunksParallel(std::span<const world::UnpackedChunk> chunks,
		OpaquePredicate isOpaque, jobs::JobSystem &js);

} // namespace mesh
