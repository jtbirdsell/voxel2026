// Issue #20: job-system facade + the first migrated pool. The TSan CI leg
// runs this whole file under -fsanitize=thread — the race coverage is the
// sanitizer's, the SEMANTIC gates are here: exactly-once index delivery,
// and the parallel meshing pool's bit-equal-to-serial determinism (meshing
// is pure per chunk, so the schedule must be unobservable in the output).

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <vector>

#include "gen/minigen.hpp"
#include "jobs/jobs.hpp"
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

TEST_CASE("parallelFor delivers every index exactly once")
{
	jobs::JobSystem js;
	REQUIRE(js.threadCount() >= 1);

	for (const std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{7},
				 std::size_t{1000}, std::size_t{4096}})
		for (const std::size_t grain : {std::size_t{1}, std::size_t{64}}) {
			CAPTURE(count, grain);
			std::vector<std::atomic<std::uint32_t>> hits(count);
			js.parallelFor(count, grain,
					[&hits](std::size_t i) { hits[i].fetch_add(1); });
			for (std::size_t i = 0; i < count; ++i) {
				CAPTURE(i);
				REQUIRE(hits[i].load() == 1);
			}
		}
}

TEST_CASE("parallelFor sums match serial across repeated waves")
{
	jobs::JobSystem js;
	std::vector<std::uint64_t> data(10000);
	for (std::size_t i = 0; i < data.size(); ++i)
		data[i] = i * 2654435761ull;

	// Several waves through one scheduler instance (reuse, not just one-shot).
	for (int wave = 0; wave < 5; ++wave) {
		std::atomic<std::uint64_t> sum{0};
		js.parallelFor(data.size(), 128,
				[&](std::size_t i) { sum.fetch_add(data[i]); });
		std::uint64_t serial = 0;
		for (const std::uint64_t v : data)
			serial += v;
		CAPTURE(wave);
		REQUIRE(sum.load() == serial);
	}
}

TEST_CASE("parallel meshing pool is bit-equal to serial whatever the schedule")
{
	// The pool-migration gate (architecture section 2: old pool behavior must
	// be reproduced, not approximated). 64 real chunks: minigen terrain band.
	std::vector<world::UnpackedChunk> chunks;
	for (int cz = 0; cz < 4; ++cz)
		for (int cy = 0; cy < 4; ++cy)
			for (int cx = 0; cx < 4; ++cx)
				chunks.push_back(minigenChunk(cx, cy, cz));

	const auto serial = mesh::meshChunksSerial(chunks, opaqueNonZero);

	jobs::JobSystem js;
	// Repeat the parallel run: different schedules each time, same bits.
	for (int round = 0; round < 3; ++round) {
		const auto parallel = mesh::meshChunksParallel(chunks, opaqueNonZero, js);
		CAPTURE(round);
		REQUIRE(parallel.size() == serial.size());
		for (std::size_t i = 0; i < serial.size(); ++i) {
			CAPTURE(i);
			REQUIRE(parallel[i] == serial[i]);
		}
	}
}
