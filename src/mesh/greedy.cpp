#include "mesh/greedy.hpp"

#include <array>
#include <cstddef>

#include "voxel/morton.hpp"

namespace mesh {

namespace {

constexpr int kEdge = static_cast<int>(kChunkEdge);
constexpr std::uint32_t kNoFace = ~0u; // mask sentinel: no visible face here

// Internal cell grid: slot per cell in plain x + 16*(y + 16*z) order
// (re-indexed once from the Morton input; the sweep loops then index
// arithmetically), plus per-slot opacity resolved once from the palette.
struct Grid {
	std::array<std::uint32_t, kChunkEdge * kChunkEdge * kChunkEdge> slot{};
	std::vector<bool> slotOpaque;

	std::uint32_t at(int x, int y, int z) const
	{
		return slot[static_cast<std::size_t>(x + kEdge * (y + kEdge * z))];
	}
	bool opaque(int x, int y, int z) const
	{
		return slotOpaque[at(x, y, z)];
	}
};

bool buildGrid(const world::UnpackedChunk &chunk, OpaquePredicate isOpaque, Grid &out)
{
	if (chunk.log2Extent != 4 ||
			chunk.cells.size() != static_cast<std::size_t>(kEdge) * kEdge * kEdge)
		return false;
	out.slotOpaque.resize(chunk.palette.size());
	for (std::size_t s = 0; s < chunk.palette.size(); ++s)
		out.slotOpaque[s] = isOpaque(chunk.palette[s]);
	// Cell -> slot: palettes are small (<= 4096, typically tiny); resolve by
	// value equality against the palette. A per-cell linear probe would be
	// O(cells * palette); instead walk Morton order remembering the last
	// match (mesher inputs are spatially coherent), falling back to a scan.
	std::uint32_t last = 0;
	for (std::uint32_t m = 0; m < static_cast<std::uint32_t>(chunk.cells.size()); ++m) {
		const world::CellValue &cell = chunk.cells[m];
		std::uint32_t s = last;
		if (chunk.palette[s] != cell) {
			s = 0;
			const auto n = static_cast<std::uint32_t>(chunk.palette.size());
			while (s < n && chunk.palette[s] != cell)
				++s;
			if (s == n)
				return false; // cell value absent from palette: malformed input
		}
		last = s;
		const voxel::LocalPos p = voxel::mortonDecode(m);
		out.slot[static_cast<std::size_t>(static_cast<int>(p.x) +
				kEdge * (static_cast<int>(p.y) + kEdge * static_cast<int>(p.z)))] = s;
	}
	return true;
}

// Opacity of the cell at boundary-adjacent coordinates inside a neighbor
// view (Morton order; empty view = nothing there).
bool neighborOpaque(std::span<const world::CellValue> view, int x, int y, int z,
		OpaquePredicate isOpaque)
{
	if (view.size() != static_cast<std::size_t>(kEdge) * kEdge * kEdge)
		return false;
	const std::uint32_t m = voxel::mortonEncode({static_cast<std::uint32_t>(x),
			static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(z)});
	return isOpaque(view[m]);
}

struct Emitter {
	ChunkMesh mesh;

	// One merged rectangle: axis d with in-plane axes u=(d+1)%3, v=(d+2)%3,
	// plane coordinate p, rect origin (u0, v0) spanning du x dv. Positive
	// faces wind U-edge-first, negative V-edge-first (U x V = +axis by the
	// cyclic axis choice), giving CCW from the normal side in both cases.
	void quad(int d, bool positive, int p, int u0, int v0, int du, int dv,
			std::uint32_t slot)
	{
		const int u = (d + 1) % 3;
		const int v = (d + 2) % 3;
		int base[3] = {0, 0, 0};
		base[d] = p;
		base[u] = u0;
		base[v] = v0;
		const Face face = static_cast<Face>(2 * d + (positive ? 0 : 1));
		const auto corner = [&](int au, int av) {
			int c[3] = {base[0], base[1], base[2]};
			c[u] += au;
			c[v] += av;
			mesh.vertices.push_back(packVertex(static_cast<std::uint32_t>(c[0]),
					static_cast<std::uint32_t>(c[1]), static_cast<std::uint32_t>(c[2]),
					face, slot));
		};
		const auto firstIndex = static_cast<std::uint32_t>(mesh.vertices.size());
		if (positive) {
			corner(0, 0);
			corner(du, 0);
			corner(du, dv);
			corner(0, dv);
		} else {
			corner(0, 0);
			corner(0, dv);
			corner(du, dv);
			corner(du, 0);
		}
		for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
			mesh.indices.push_back(firstIndex + i);
		++mesh.quadCount;
	}
};

// Fill the 16x16 visibility mask for one slice of one face direction:
// mask[u*16+v] = slot when the face is visible, kNoFace otherwise.
void buildMask(const Grid &grid, const NeighborViews &nb, OpaquePredicate isOpaque,
		int d, bool positive, int slice, std::uint32_t (&mask)[kChunkEdge * kChunkEdge])
{
	const int u = (d + 1) % 3;
	const int v = (d + 2) % 3;
	for (int uu = 0; uu < kEdge; ++uu)
		for (int vv = 0; vv < kEdge; ++vv) {
			int c[3] = {0, 0, 0};
			c[d] = slice;
			c[u] = uu;
			c[v] = vv;
			std::uint32_t &cellMask = mask[uu * kEdge + vv];
			cellMask = kNoFace;
			if (!grid.opaque(c[0], c[1], c[2]))
				continue;
			int n[3] = {c[0], c[1], c[2]};
			n[d] += positive ? 1 : -1;
			bool covered = false;
			if (n[d] >= 0 && n[d] < kEdge) {
				covered = grid.opaque(n[0], n[1], n[2]);
			} else {
				// Boundary: consult the neighbor view at the wrapped layer.
				const int w = n[d] < 0 ? kEdge - 1 : 0;
				int nn[3] = {n[0], n[1], n[2]};
				nn[d] = w;
				std::span<const world::CellValue> view;
				switch (2 * d + (positive ? 0 : 1)) {
				case 0: view = nb.posX; break;
				case 1: view = nb.negX; break;
				case 2: view = nb.posY; break;
				case 3: view = nb.negY; break;
				case 4: view = nb.posZ; break;
				default: view = nb.negZ; break;
				}
				covered = neighborOpaque(view, nn[0], nn[1], nn[2], isOpaque);
			}
			if (!covered)
				cellMask = grid.at(c[0], c[1], c[2]);
		}
}

ChunkMesh meshImpl(const world::UnpackedChunk &chunk, const NeighborViews &neighbors,
		OpaquePredicate isOpaque, bool merge)
{
	Emitter em;
	Grid grid;
	if (!buildGrid(chunk, isOpaque, grid))
		return em.mesh; // documented precondition: empty mesh on wrong shape

	std::uint32_t mask[kChunkEdge * kChunkEdge];
	bool claimed[kChunkEdge * kChunkEdge];
	for (int d = 0; d < 3; ++d)
		for (const bool positive : {true, false})
			for (int slice = 0; slice < kEdge; ++slice) {
				buildMask(grid, neighbors, isOpaque, d, positive, slice, mask);
				const int p = positive ? slice + 1 : slice;
				if (!merge) {
					for (int uu = 0; uu < kEdge; ++uu)
						for (int vv = 0; vv < kEdge; ++vv)
							if (mask[uu * kEdge + vv] != kNoFace)
								em.quad(d, positive, p, uu, vv, 1, 1,
										mask[uu * kEdge + vv]);
					continue;
				}
				for (auto &c : claimed)
					c = false;
				for (int uu = 0; uu < kEdge; ++uu)
					for (int vv = 0; vv < kEdge; ++vv) {
						const std::uint32_t slot = mask[uu * kEdge + vv];
						if (slot == kNoFace || claimed[uu * kEdge + vv])
							continue;
						// Extend width along v, then height along u, the
						// classic maximal-rectangle greedy step.
						int dv = 1;
						while (vv + dv < kEdge &&
								mask[uu * kEdge + vv + dv] == slot &&
								!claimed[uu * kEdge + vv + dv])
							++dv;
						int du = 1;
						for (; uu + du < kEdge; ++du) {
							bool rowOk = true;
							for (int k = 0; k < dv; ++k)
								if (mask[(uu + du) * kEdge + vv + k] != slot ||
										claimed[(uu + du) * kEdge + vv + k]) {
									rowOk = false;
									break;
								}
							if (!rowOk)
								break;
						}
						for (int a = 0; a < du; ++a)
							for (int k = 0; k < dv; ++k)
								claimed[(uu + a) * kEdge + vv + k] = true;
						em.quad(d, positive, p, uu, vv, du, dv, slot);
					}
			}
	return em.mesh;
}

} // namespace

ChunkMesh greedyMesh(const world::UnpackedChunk &chunk, const NeighborViews &neighbors,
		OpaquePredicate isOpaque)
{
	return meshImpl(chunk, neighbors, isOpaque, /*merge=*/true);
}

ChunkMesh naiveMesh(const world::UnpackedChunk &chunk, const NeighborViews &neighbors,
		OpaquePredicate isOpaque)
{
	return meshImpl(chunk, neighbors, isOpaque, /*merge=*/false);
}

} // namespace mesh
