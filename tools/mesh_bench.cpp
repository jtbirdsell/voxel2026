// Greedy-mesher perf probe (issue #17) — the Part VII "mesh time per chunk"
// budget's first measured baseline. Meshes a band of minigen chunks through
// the REAL pipeline (generate -> canonical Contract-1 pack -> unpack ->
// greedy mesh) and reports the median per-chunk mesh time plus the greedy
// compression ratio against the naive per-face oracle.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "gen/minigen.hpp"
#include "jobs/jobs.hpp"
#include "mesh/greedy.hpp"
#include "mesh/parallel.hpp"
#include "voxel/morton.hpp"
#include "world/voxel_blob.hpp"

namespace {

bool opaqueNonZero(const world::CellValue &c)
{
	return c.content != 0;
}

world::UnpackedChunk minigenChunk(int cx, int cy, int cz)
{
	gen::GenChunk g;
	gen::generateChunk(gen::kStandardSeed, {cx, cy, cz}, g);
	std::vector<world::CellValue> cells(4096);
	for (int z = 0; z < gen::kGenChunk; ++z)
		for (int y = 0; y < gen::kGenChunk; ++y)
			for (int x = 0; x < gen::kGenChunk; ++x)
				cells[voxel::mortonEncode({static_cast<std::uint32_t>(x),
						static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(z)})] =
						world::CellValue{g.at(x, y, z), 0};
	return *world::unpackVoxelBlob(world::packVoxelBlob(cells, 4));
}

} // namespace

int main()
{
	// A 8x2x8 chunk band around the minigen terrain layer: surface chunks
	// (the realistic meshing load) plus some air and some solid interiors.
	std::vector<world::UnpackedChunk> chunks;
	for (int cz = 0; cz < 8; ++cz)
		for (int cy = 0; cy < 2; ++cy)
			for (int cx = 0; cx < 8; ++cx)
				chunks.push_back(minigenChunk(cx, cy, cz));

	const mesh::NeighborViews none{};
	std::uint64_t greedyQuads = 0, naiveQuads = 0, vertices = 0;
	std::vector<double> micros;
	micros.reserve(chunks.size());

	// Warm pass (allocator + cache), then the timed pass.
	for (const auto &c : chunks)
		greedyQuads += mesh::greedyMesh(c, none, opaqueNonZero).quadCount;
	greedyQuads = 0;

	for (const auto &c : chunks) {
		const auto t0 = std::chrono::steady_clock::now();
		const mesh::ChunkMesh m = mesh::greedyMesh(c, none, opaqueNonZero);
		const auto t1 = std::chrono::steady_clock::now();
		micros.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
		greedyQuads += m.quadCount;
		vertices += m.vertices.size();
		naiveQuads += mesh::naiveMesh(c, none, opaqueNonZero).quadCount;
	}

	std::sort(micros.begin(), micros.end());
	const double median = micros[micros.size() / 2];
	const double worst = micros.back();

	std::printf("chunks meshed:        %zu (minigen 8x2x8 band, no neighbors)\n",
			chunks.size());
	std::printf("median mesh time:     %.1f us/chunk (worst %.1f)\n", median, worst);
	std::printf("greedy quads:         %llu (naive %llu, compression %.2fx)\n",
			static_cast<unsigned long long>(greedyQuads),
			static_cast<unsigned long long>(naiveQuads),
			greedyQuads ? static_cast<double>(naiveQuads) / static_cast<double>(greedyQuads)
						: 0.0);
	std::printf("vertex bytes/chunk:   %.0f (8-byte packed vertices)\n",
			chunks.empty() ? 0.0
						   : static_cast<double>(vertices * sizeof(mesh::PackedVertex)) /
							static_cast<double>(chunks.size()));

	// The migrated pool (issue #20): same chunks through the job system,
	// median of 5 batch runs after a warm batch; speedup over serial.
	{
		jobs::JobSystem js;
		(void)mesh::meshChunksParallel(chunks, opaqueNonZero, js); // warm
		std::vector<double> serialMs, parallelMs;
		for (int round = 0; round < 5; ++round) {
			auto t0 = std::chrono::steady_clock::now();
			const auto s = mesh::meshChunksSerial(chunks, opaqueNonZero);
			auto t1 = std::chrono::steady_clock::now();
			const auto p = mesh::meshChunksParallel(chunks, opaqueNonZero, js);
			auto t2 = std::chrono::steady_clock::now();
			serialMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
			parallelMs.push_back(std::chrono::duration<double, std::milli>(t2 - t1).count());
			if (p != s) {
				std::printf("parallel/serial MISMATCH — determinism gate broken\n");
				return 1;
			}
		}
		std::sort(serialMs.begin(), serialMs.end());
		std::sort(parallelMs.begin(), parallelMs.end());
		const double sMed = serialMs[serialMs.size() / 2];
		const double pMed = parallelMs[parallelMs.size() / 2];
		std::printf("pool batch (serial):  %.2f ms for %zu chunks\n", sMed, chunks.size());
		std::printf("pool batch (jobs):    %.2f ms on %u threads (%.1fx, bit-equal)\n",
				pMed, js.threadCount(), pMed > 0.0 ? sMed / pMed : 0.0);
	}
	return 0;
}
