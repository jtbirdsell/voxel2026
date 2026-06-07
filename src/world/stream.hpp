// Dynamic world streaming (issue #24): a camera-centered chunk window that
// loads-or-generates (belt), meshes neighbor-aware (the NeighborViews seam
// the fixed-region demo left unfilled), and evicts — replacing the
// pre-loaded region. Header-only; consumers link voxel2026_gen,
// voxel2026_mesh, and a store backend.
//
// THE determinism rule (what the tests pin): a resident chunk's mesh is a
// PURE FUNCTION of (store content, window) — it is built only once every
// IN-WINDOW face-neighbor's data is resident, against exactly those
// neighbors (out-of-window sides mesh as absent), and it is re-meshed when
// a window move changes which neighbors are in-window. Arrival order can
// therefore never leak into quiescent state: walking the window to a center
// along any path, with any pump budget, yields bit-identical resident
// meshes (pinned by the path-independence test and by an oracle that
// re-derives every mesh fresh from the window definition).
//
// v1 scoping (deliberate, not silent):
//   - The window is a horizontal Chebyshev square times a FIXED vertical
//     chunk band (minigen content lives in cy 0..1); vertical streaming
//     arrives with taller worlds and the LOD pyramid.
//   - Belt admissions run SYNCHRONOUSLY inside pump(), bounded by
//     cfg.budget per call — "never stalls on the belt" is delivered as a
//     bounded per-frame slice, not background generation. Running the belt
//     itself on the job system (store thread-safety permitting) is the
//     named refinement.
//   - Meshes are stored render-ready (slots remapped to global content ids
//     via remapSlotsToContent), so the renderer consumes residents as-is;
//     consumers re-upload the scene wholesale on change (issue #25 tracks
//     the incremental device-local pools that replace that).

#pragma once

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstdint>
#include <map>
#include <vector>

#include "jobs/jobs.hpp"
#include "mesh/greedy.hpp"
#include "store/store.hpp"
#include "world/belt.hpp"

namespace world {

struct ChunkKey {
	std::int32_t cx = 0, cy = 0, cz = 0;

	friend constexpr auto operator<=>(const ChunkKey &, const ChunkKey &) = default;
};

// Fixed at construction (v1): dynamic window resizing — radius or band
// changes mid-flight — is out of scope until something needs it.
struct StreamConfig {
	std::int32_t radius = 4;            // horizontal Chebyshev radius, chunks
	std::int32_t cyMin = 0, cyMax = 1;  // inclusive vertical band
	std::uint32_t budget = 8;           // max belt admissions per pump
};

struct ResidentChunk {
	UnpackedChunk chunk;
	mesh::ChunkMesh mesh; // render-ready (slots remapped to content ids)
	bool fromStore = false;
	bool meshed = false;
	std::uint8_t meshedMask = 0; // in-window neighbor mask the mesh was built with
};

struct PumpStats {
	std::uint32_t loaded = 0;          // belt admissions this pump
	std::uint32_t loadedFromStore = 0; // of those, store hits (rest generated)
	std::uint32_t meshed = 0;          // meshes built or rebuilt this pump
	std::uint32_t evicted = 0;

	bool changed() const { return loaded + meshed + evicted > 0; }
};

class Streamer {
public:
	// The store must outlive the streamer. budget >= 1 (asserted).
	Streamer(KvStore &store, std::uint64_t seed, StreamConfig cfg,
			mesh::OpaquePredicate isOpaque)
			: m_store(store), m_seed(seed), m_cfg(cfg), m_isOpaque(isOpaque)
	{
		assert(m_cfg.budget >= 1);
		assert(m_cfg.radius >= 0);
		assert(m_cfg.cyMin <= m_cfg.cyMax);
	}

	// Move the desired window; cheap — all work happens in pump().
	void setCenter(std::int32_t cx, std::int32_t cz)
	{
		m_centerX = cx;
		m_centerZ = cz;
	}

	// One streaming slice: evict residents that left the window, admit up to
	// cfg.budget missing chunks nearest-first (deterministic order), then
	// (re)mesh every resident whose full in-window neighborhood is resident
	// and whose mesh is missing or was built against a different
	// neighborhood mask. Meshing parallelizes over `js` when given (pure per
	// chunk — bit-equal to serial by the issue-#20 gate); pass nullptr for
	// serial.
	PumpStats pump(jobs::JobSystem *js = nullptr)
	{
		PumpStats stats;

		// 1. Evict. (Map nodes are stable: erasing never moves survivors,
		// so spans/pointers into remaining residents stay valid.)
		for (auto it = m_resident.begin(); it != m_resident.end();) {
			if (!inWindow(it->first)) {
				it = m_resident.erase(it);
				++stats.evicted;
			} else {
				++it;
			}
		}

		// 2. Admit nearest-first: missing desired chunks ordered by
		// (Chebyshev distance from center, key) — fully deterministic.
		std::vector<ChunkKey> missing;
		forEachDesired([&](ChunkKey k) {
			if (!m_resident.contains(k))
				missing.push_back(k);
		});
		std::sort(missing.begin(), missing.end(),
				[&](const ChunkKey &a, const ChunkKey &b) {
					const std::int32_t da = chebyshev(a), db = chebyshev(b);
					if (da != db)
						return da < db;
					return a < b;
				});
		// Parenthesized to stay immune to Windows' min() macro in consumers
		// that include windows.h without NOMINMAX.
		const std::size_t admit =
				(std::min)(missing.size(), static_cast<std::size_t>(m_cfg.budget));
		for (std::size_t i = 0; i < admit; ++i) {
			const ChunkKey k = missing[i];
			auto belt = loadOrGenerateRegion(
					m_store, m_seed, k.cx, k.cy, k.cz, k.cx, k.cy, k.cz);
			assert(belt.size() == 1);
			ResidentChunk r;
			r.chunk = std::move(belt[0].chunk);
			r.fromStore = belt[0].fromStore;
			if (r.fromStore)
				++stats.loadedFromStore;
			m_resident.emplace(k, std::move(r));
			++stats.loaded;
		}

		// 3. Mesh. Collect first (admissions are done; node addresses are
		// stable from here), then build — parallel tasks write disjoint
		// residents and read const neighbor cells.
		std::vector<std::pair<const ChunkKey, ResidentChunk> *> work;
		for (auto &entry : m_resident) {
			const std::uint8_t mask = windowMask(entry.first);
			const bool need =
					!entry.second.meshed || entry.second.meshedMask != mask;
			if (need && neighborsResident(entry.first, mask))
				work.push_back(&entry);
		}
		const auto buildOne = [&](std::size_t i) {
			auto &[key, res] = *work[i];
			const std::uint8_t mask = windowMask(key);
			mesh::ChunkMesh m = mesh::greedyMesh(
					res.chunk, neighborViews(key, mask), m_isOpaque);
			remapSlotsToContent(m, res.chunk);
			res.mesh = std::move(m);
			res.meshed = true;
			res.meshedMask = mask;
		};
		if (js) {
			js->parallelFor(work.size(), 1, buildOne);
		} else {
			for (std::size_t i = 0; i < work.size(); ++i)
				buildOne(i);
		}
		stats.meshed = static_cast<std::uint32_t>(work.size());

		return stats;
	}

	// True when the desired set is fully resident and every mesh was built
	// against the current window — pump() would be a no-op. setCenter()
	// invalidates quiescence until the next pump (old-window residents are
	// pruned there, never here — this is a pure query).
	bool quiescent() const
	{
		std::size_t desired = 0;
		bool allIn = true;
		forEachDesired([&](ChunkKey k) {
			++desired;
			const auto it = m_resident.find(k);
			if (it == m_resident.end() || !it->second.meshed ||
					it->second.meshedMask != windowMask(k))
				allIn = false;
		});
		return allIn && desired == m_resident.size();
	}

	// Residents in deterministic (key-sorted) order. Node addresses are
	// stable across pump() calls — pointers into chunks/meshes remain valid
	// until the chunk is evicted.
	const std::map<ChunkKey, ResidentChunk> &residents() const
	{
		return m_resident;
	}

	// The window predicate, exposed for tests and oracles.
	bool inWindow(ChunkKey k) const
	{
		if (k.cy < m_cfg.cyMin || k.cy > m_cfg.cyMax)
			return false;
		return chebyshev(k) <= m_cfg.radius;
	}

	// Which face-neighbors of k are inside the window (bit order matches
	// mesh::Face: +X, -X, +Y, -Y, +Z, -Z).
	std::uint8_t windowMask(ChunkKey k) const
	{
		const ChunkKey n[6] = {
				{k.cx + 1, k.cy, k.cz}, {k.cx - 1, k.cy, k.cz},
				{k.cx, k.cy + 1, k.cz}, {k.cx, k.cy - 1, k.cz},
				{k.cx, k.cy, k.cz + 1}, {k.cx, k.cy, k.cz - 1}};
		std::uint8_t mask = 0;
		for (int f = 0; f < 6; ++f)
			if (inWindow(n[f]))
				mask = static_cast<std::uint8_t>(mask | (1u << f));
		return mask;
	}

private:
	std::int32_t chebyshev(ChunkKey k) const
	{
		const std::int32_t dx = k.cx >= m_centerX ? k.cx - m_centerX : m_centerX - k.cx;
		const std::int32_t dz = k.cz >= m_centerZ ? k.cz - m_centerZ : m_centerZ - k.cz;
		return dx > dz ? dx : dz;
	}

	template <typename F>
	void forEachDesired(F &&fn) const
	{
		for (std::int32_t cz = m_centerZ - m_cfg.radius;
				cz <= m_centerZ + m_cfg.radius; ++cz)
			for (std::int32_t cy = m_cfg.cyMin; cy <= m_cfg.cyMax; ++cy)
				for (std::int32_t cx = m_centerX - m_cfg.radius;
						cx <= m_centerX + m_cfg.radius; ++cx)
					fn(ChunkKey{cx, cy, cz});
	}

	bool neighborsResident(ChunkKey k, std::uint8_t mask) const
	{
		const ChunkKey n[6] = {
				{k.cx + 1, k.cy, k.cz}, {k.cx - 1, k.cy, k.cz},
				{k.cx, k.cy + 1, k.cz}, {k.cx, k.cy - 1, k.cz},
				{k.cx, k.cy, k.cz + 1}, {k.cx, k.cy, k.cz - 1}};
		for (int f = 0; f < 6; ++f)
			if ((mask & (1u << f)) && !m_resident.contains(n[f]))
				return false;
		return true;
	}

	// Views for exactly the in-window neighbors (all resident — readiness
	// was checked); out-of-window sides mesh as absent. NOTE the Face ->
	// NeighborViews member correspondence: the view named for the side of
	// THIS chunk (negX) is the neighbor at cx - 1.
	mesh::NeighborViews neighborViews(ChunkKey k, std::uint8_t mask) const
	{
		const auto cells = [&](std::int32_t cx, std::int32_t cy, std::int32_t cz,
								   int bit) -> std::span<const CellValue> {
			if (!(mask & (1u << bit)))
				return {};
			const auto it = m_resident.find({cx, cy, cz});
			assert(it != m_resident.end());
			return it->second.chunk.cells;
		};
		mesh::NeighborViews v;
		v.posX = cells(k.cx + 1, k.cy, k.cz, 0);
		v.negX = cells(k.cx - 1, k.cy, k.cz, 1);
		v.posY = cells(k.cx, k.cy + 1, k.cz, 2);
		v.negY = cells(k.cx, k.cy - 1, k.cz, 3);
		v.posZ = cells(k.cx, k.cy, k.cz + 1, 4);
		v.negZ = cells(k.cx, k.cy, k.cz - 1, 5);
		return v;
	}

	KvStore &m_store;
	std::uint64_t m_seed;
	StreamConfig m_cfg;
	mesh::OpaquePredicate m_isOpaque;
	std::int32_t m_centerX = 0, m_centerZ = 0;
	std::map<ChunkKey, ResidentChunk> m_resident;
};

} // namespace world
