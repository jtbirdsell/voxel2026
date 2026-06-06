// Greedy chunk mesher (issue #17) — the first real renderer component,
// consuming the frozen Contract-1 cell representation CPU-side: the
// pre-unpacked UnpackedChunk (Morton-ordered CellValue spans, exactly what
// unpackVoxelBlob produces). The architecture's in-shader blob readers are
// the Phase-1 render *backend*, a separate component — this mesher never
// touches blob bytes.
//
// Faces merge ONLY when their cells' full (content, state) values are equal
// — two cells that could ever render differently never share a quad. What
// counts as opaque is an injected predicate: opacity is content-registry
// data (Contract 1's scoping), not mesher knowledge. The predicate MUST be
// pure/stable — opacity is queried multiple times per cell across the six
// sweeps, and a predicate that answers inconsistently produces incoherent
// geometry (precondition, not a checked error).
//
// Output is the v1 slice of the packed vertex layout the architecture
// sketches (8 bytes; the sketch also names packed light/AO and a tile
// index, which are Phase-2 work when Tier-A lighting lands — the spare
// bits below are their reserved home, the divergence is deliberate and
// scoped, not silent):
//   word 0: x | y<<5 | z<<10 | face<<15   (corner coords 0..16 in 5 bits
//                                          each; face 0..5 in 3 bits;
//                                          bits 18..31 reserved: light/AO)
//   word 1: palette slot / material id    (16 bits used; high half
//                                          reserved: tile-index mapping)
// Quads emit 4 vertices + 6 indices (0,1,2, 0,2,3), wound CCW as seen from
// the face-normal side (pinned by a cross-product test).
//
// The brute-force per-face mesher is exported as the ORACLE: greedy output
// must cover exactly the same unit faces with the same values (set
// equality, verified by decomposing the packed quads in the tests).

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "world/voxel_blob.hpp"

namespace mesh {

inline constexpr std::uint32_t kChunkEdge = 16; // Contract 1 v1 extent

// Face order: +X, -X, +Y, -Y, +Z, -Z (axis pairs, positive first).
enum class Face : std::uint8_t {
	kPosX = 0,
	kNegX = 1,
	kPosY = 2,
	kNegY = 3,
	kPosZ = 4,
	kNegZ = 5,
};

struct PackedVertex {
	std::uint32_t posFace = 0; // x | y<<5 | z<<10 | face<<15
	std::uint32_t slot = 0;    // material identity (palette slot / content key)

	friend constexpr bool operator==(PackedVertex, PackedVertex) = default;
};

constexpr PackedVertex packVertex(std::uint32_t x, std::uint32_t y, std::uint32_t z,
		Face face, std::uint32_t slot)
{
	return PackedVertex{
			x | (y << 5) | (z << 10) |
					(static_cast<std::uint32_t>(face) << 15),
			slot};
}

constexpr std::uint32_t vertexX(PackedVertex v)
{
	return v.posFace & 0x1F;
}
constexpr std::uint32_t vertexY(PackedVertex v)
{
	return (v.posFace >> 5) & 0x1F;
}
constexpr std::uint32_t vertexZ(PackedVertex v)
{
	return (v.posFace >> 10) & 0x1F;
}
constexpr Face vertexFace(PackedVertex v)
{
	return static_cast<Face>((v.posFace >> 15) & 0x7);
}

struct ChunkMesh {
	std::vector<PackedVertex> vertices; // 4 per quad
	std::vector<std::uint32_t> indices; // 6 per quad: 0,1,2, 0,2,3
	std::uint32_t quadCount = 0;

	// Bit-exact equality — the parallel-pool determinism gate compares
	// whole meshes (meshing is pure per chunk, so schedule cannot matter).
	friend bool operator==(const ChunkMesh &, const ChunkMesh &) = default;
};

// Optional neighbor cell views (Morton order, kChunkEdge^3 each, or empty =
// nothing there: boundary faces toward an absent neighbor are emitted).
struct NeighborViews {
	std::span<const world::CellValue> negX, posX, negY, posY, negZ, posZ;
};

using OpaquePredicate = bool (*)(const world::CellValue &);

// Greedy mesher over the Contract-1 unpack product. The emitted vertex
// `slot` is the chunk's palette index — for canonical blobs (no duplicate
// entries) slot equality IS (content, state) equality, so merging on slot
// is exactly the render-identity merge rule. Chunks must be the Contract-1
// v1 extent (16^3); anything else yields an empty mesh (documented
// precondition, pinned by test).
ChunkMesh greedyMesh(const world::UnpackedChunk &chunk, const NeighborViews &neighbors,
		OpaquePredicate isOpaque);

// The oracle: one quad per visible unit face, no merging. Same visibility
// predicate, same emission contract — what the property test isolates is
// the MERGE correctness.
ChunkMesh naiveMesh(const world::UnpackedChunk &chunk, const NeighborViews &neighbors,
		OpaquePredicate isOpaque);

} // namespace mesh
