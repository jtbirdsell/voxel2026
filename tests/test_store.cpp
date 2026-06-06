// Issue #19: persistence conformance. One suite runs against EVERY backend
// (the in-memory reference is the oracle: obviously-correct std::map
// semantics), plus a randomized cross-backend agreement test, the
// Contract-2 storage-key ordering integration (lexicographic BLOB order ==
// numeric (level,x,y,z) order — what the big-endian sign-flipped keys were
// designed for), real chunk/registry round-trips, and reopen persistence.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gen/minigen.hpp"
#include "store/memory_store.hpp"
#include "store/sqlite_store.hpp"
#include "voxel/morton.hpp"
#include "world/lod_pyramid.hpp"
#include "world/registry.hpp"
#include "world/voxel_blob.hpp"

namespace {

std::vector<std::byte> bytes(std::initializer_list<int> vals)
{
	std::vector<std::byte> out;
	for (const int v : vals)
		out.push_back(static_cast<std::byte>(v));
	return out;
}

using Factory = std::function<std::unique_ptr<world::KvStore>()>;

void conformance(const Factory &make, const char *label)
{
	CAPTURE(label);
	auto store = make();
	auto &s = *store;
	using Space = world::StoreSpace;

	// Basics: absent, put, get, overwrite, erase reports existence.
	REQUIRE_FALSE(s.get(Space::kMeta, bytes({1})).has_value());
	REQUIRE(s.put(Space::kMeta, bytes({1}), bytes({10, 20})));
	REQUIRE(s.get(Space::kMeta, bytes({1})).value() == bytes({10, 20}));
	REQUIRE(s.put(Space::kMeta, bytes({1}), bytes({30})));
	REQUIRE(s.get(Space::kMeta, bytes({1})).value() == bytes({30}));
	REQUIRE(s.erase(Space::kMeta, bytes({1})));
	REQUIRE_FALSE(s.erase(Space::kMeta, bytes({1})));
	REQUIRE_FALSE(s.get(Space::kMeta, bytes({1})).has_value());

	// Empty VALUE is stored and distinct from absent; empty KEY is an error.
	REQUIRE(s.put(Space::kMeta, bytes({2}), {}));
	REQUIRE(s.get(Space::kMeta, bytes({2})).has_value());
	REQUIRE(s.get(Space::kMeta, bytes({2}))->empty());
	REQUIRE_FALSE(s.put(Space::kMeta, {}, bytes({1})));
	REQUIRE_FALSE(s.erase(Space::kMeta, {}));

	// Space isolation: same key, different spaces.
	REQUIRE(s.put(Space::kChunks, bytes({7}), bytes({1})));
	REQUIRE(s.put(Space::kLodNodes, bytes({7}), bytes({2})));
	REQUIRE(s.get(Space::kChunks, bytes({7})).value() == bytes({1}));
	REQUIRE(s.get(Space::kLodNodes, bytes({7})).value() == bytes({2}));

	// Batch atomicity: an empty key anywhere fails the WHOLE batch.
	{
		std::vector<world::KvStore::BatchOp> ops;
		ops.push_back({Space::kMeta, bytes({50}), bytes({5})});
		ops.push_back({Space::kMeta, {}, bytes({6})}); // poison
		REQUIRE_FALSE(s.applyBatch(ops));
		REQUIRE_FALSE(s.get(Space::kMeta, bytes({50})).has_value());
	}
	// Valid batch: puts and erases land together.
	{
		REQUIRE(s.put(Space::kMeta, bytes({60}), bytes({6})));
		std::vector<world::KvStore::BatchOp> ops;
		ops.push_back({Space::kMeta, bytes({61}), bytes({7})});
		ops.push_back({Space::kMeta, bytes({60}), std::nullopt}); // erase
		REQUIRE(s.applyBatch(ops));
		REQUIRE(s.get(Space::kMeta, bytes({61})).value() == bytes({7}));
		REQUIRE_FALSE(s.get(Space::kMeta, bytes({60})).has_value());
	}

	// Prefix scans: order, boundaries, early stop, 0xFF edge, empty prefix.
	{
		auto scan = make();
		REQUIRE(scan->put(Space::kMeta, bytes({0xAA, 0x01}), bytes({1})));
		REQUIRE(scan->put(Space::kMeta, bytes({0xAA, 0x02}), bytes({2})));
		REQUIRE(scan->put(Space::kMeta, bytes({0xAA}), bytes({0})));
		REQUIRE(scan->put(Space::kMeta, bytes({0xAB}), bytes({9})));
		REQUIRE(scan->put(Space::kMeta, bytes({0xFF, 0xFF}), bytes({99})));

		std::vector<std::vector<std::byte>> seen;
		scan->scanPrefix(Space::kMeta, bytes({0xAA}),
				[&](std::span<const std::byte> k, std::span<const std::byte>) {
					seen.emplace_back(k.begin(), k.end());
					return true;
				});
		REQUIRE(seen.size() == 3);
		REQUIRE(seen[0] == bytes({0xAA}));
		REQUIRE(seen[1] == bytes({0xAA, 0x01}));
		REQUIRE(seen[2] == bytes({0xAA, 0x02}));

		// All-0xFF prefix (no upper bound exists) still scans correctly.
		seen.clear();
		scan->scanPrefix(Space::kMeta, bytes({0xFF}),
				[&](std::span<const std::byte> k, std::span<const std::byte>) {
					seen.emplace_back(k.begin(), k.end());
					return true;
				});
		REQUIRE(seen.size() == 1);
		REQUIRE(seen[0] == bytes({0xFF, 0xFF}));

		// Early stop after the first visit.
		std::size_t visits = 0;
		scan->scanPrefix(Space::kMeta, {},
				[&](std::span<const std::byte>, std::span<const std::byte>) {
					++visits;
					return false;
				});
		REQUIRE(visits == 1);

		// Empty prefix = the whole space.
		visits = 0;
		scan->scanPrefix(Space::kMeta, {},
				[&](std::span<const std::byte>, std::span<const std::byte>) {
					++visits;
					return true;
				});
		REQUIRE(visits == 5);
	}
}

struct Prng {
	std::uint64_t s = 0xC0FFEE123456789ull;
	std::uint64_t next()
	{
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return s;
	}
};

} // namespace

TEST_CASE("store conformance: in-memory reference and SQLite agree with the contract")
{
	conformance([] { return std::make_unique<world::MemoryStore>(); }, "memory");
	conformance(
			[]() -> std::unique_ptr<world::KvStore> {
				auto s = std::make_unique<world::SqliteStore>(":memory:");
				REQUIRE(s->ok());
				return s;
			},
			"sqlite");
}

TEST_CASE("store cross-backend oracle: random op sequences end bit-identical")
{
	world::MemoryStore oracle;
	world::SqliteStore subject(":memory:");
	REQUIRE(subject.ok());

	Prng rng;
	for (int op = 0; op < 2000; ++op) {
		const auto space = static_cast<world::StoreSpace>(rng.next() % 4);
		std::vector<std::byte> key;
		const std::size_t keyLen = 1 + rng.next() % 6;
		for (std::size_t i = 0; i < keyLen; ++i)
			key.push_back(static_cast<std::byte>(rng.next() % 16)); // forced collisions
		if (rng.next() % 3 == 0) {
			REQUIRE(oracle.erase(space, key) == subject.erase(space, key));
		} else {
			std::vector<std::byte> value;
			const std::size_t valLen = rng.next() % 24;
			for (std::size_t i = 0; i < valLen; ++i)
				value.push_back(static_cast<std::byte>(rng.next() & 0xFF));
			REQUIRE(oracle.put(space, key, value));
			REQUIRE(subject.put(space, key, value));
		}
	}

	for (int s = 0; s < 4; ++s) {
		const auto space = static_cast<world::StoreSpace>(s);
		std::vector<std::pair<std::vector<std::byte>, std::vector<std::byte>>> a, b;
		oracle.scanPrefix(space, {},
				[&](std::span<const std::byte> k, std::span<const std::byte> v) {
					a.emplace_back(std::vector<std::byte>(k.begin(), k.end()),
							std::vector<std::byte>(v.begin(), v.end()));
					return true;
				});
		subject.scanPrefix(space, {},
				[&](std::span<const std::byte> k, std::span<const std::byte> v) {
					b.emplace_back(std::vector<std::byte>(k.begin(), k.end()),
							std::vector<std::byte>(v.begin(), v.end()));
					return true;
				});
		CAPTURE(s, a.size());
		REQUIRE(a == b); // same content AND same order
	}
}

TEST_CASE("store scan order matches Contract 2 storage-key numeric order")
{
	// The design claim, now against a real database: SQLite's memcmp BLOB
	// ordering must equal (level, x, y, z) numeric order for the big-endian
	// sign-flipped keys — including across the sign boundary.
	const world::LodNodeKey ordered[] = {
		{0, -5, 0, 0}, {0, -1, -1, -1}, {0, -1, 2, 3}, {0, 0, 0, 0}, {0, 3, -7, 1},
		{1, -2, 0, 0}, {1, 0, 0, 0}, {2, -1, -1, -1}, {2, 0, 1, 0},
	};
	world::SqliteStore store(":memory:");
	REQUIRE(store.ok());
	// Insert in deliberately shuffled order.
	const int shuffled[] = {4, 8, 0, 6, 2, 7, 1, 5, 3};
	for (const int i : shuffled) {
		const auto key = world::lodStorageKey(ordered[i]);
		const std::byte payload[] = {static_cast<std::byte>(i)};
		REQUIRE(store.put(world::StoreSpace::kLodNodes,
				std::span<const std::byte>(
						reinterpret_cast<const std::byte *>(key.data()), key.size()),
				payload));
	}
	std::vector<int> visitOrder;
	store.scanPrefix(world::StoreSpace::kLodNodes, {},
			[&](std::span<const std::byte>, std::span<const std::byte> v) {
				visitOrder.push_back(std::to_integer<int>(v[0]));
				return true;
			});
	REQUIRE(visitOrder.size() == 9);
	for (int i = 0; i < 9; ++i) {
		CAPTURE(i);
		REQUIRE(visitOrder[static_cast<std::size_t>(i)] == i);
	}
}

TEST_CASE("store round-trips real chunk blobs and the registry manifest")
{
	world::SqliteStore store(":memory:");
	REQUIRE(store.ok());

	// Chunk: minigen -> canonical Contract-1 blob -> store -> back -> cells.
	gen::GenChunk g;
	gen::generateChunk(gen::kStandardSeed, {1, 0, 2}, g);
	std::vector<world::CellValue> cells(4096);
	for (int z = 0; z < gen::kGenChunk; ++z)
		for (int y = 0; y < gen::kGenChunk; ++y)
			for (int x = 0; x < gen::kGenChunk; ++x)
				cells[voxel::mortonEncode({static_cast<std::uint32_t>(x),
						static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(z)})] =
						world::CellValue{g.at(x, y, z), 0};
	const auto blob = world::packVoxelBlob(cells, 4);
	const auto key = bytes({1, 0, 2});
	REQUIRE(store.put(world::StoreSpace::kChunks, key, blob));
	const auto loaded = store.get(world::StoreSpace::kChunks, key);
	REQUIRE(loaded.has_value());
	REQUIRE(*loaded == blob); // byte-identical at rest
	const auto chunk = world::unpackVoxelBlob(*loaded);
	REQUIRE(chunk.has_value());
	REQUIRE(chunk->cells == cells);

	// Registry manifest through the kRegistry space.
	world::ContentRegistry reg;
	REQUIRE(reg.registerContent("base:stone") == 1);
	REQUIRE(reg.registerContent("base:dirt") == 2);
	REQUIRE(reg.tombstone(2));
	const auto manifest = reg.serialize();
	const auto regKey = bytes({0});
	REQUIRE(store.put(world::StoreSpace::kRegistry, regKey, manifest));
	const auto back = world::ContentRegistry::deserialize(
			*store.get(world::StoreSpace::kRegistry, regKey));
	REQUIRE(back.has_value());
	REQUIRE(back->nameOf(2) == "base:dirt");
	REQUIRE_FALSE(back->isActive(2));
}

TEST_CASE("sqlite store persists across close and reopen")
{
	const auto path = (std::filesystem::temp_directory_path() /
			"voxel2026_store_reopen_test.sqlite3")
							  .string();
	std::filesystem::remove(path);

	{
		world::SqliteStore store(path);
		REQUIRE(store.ok());
		REQUIRE(store.put(world::StoreSpace::kMeta, bytes({1, 2, 3}), bytes({42})));
	}
	{
		world::SqliteStore store(path);
		REQUIRE(store.ok());
		const auto v = store.get(world::StoreSpace::kMeta, bytes({1, 2, 3}));
		REQUIRE(v.has_value());
		REQUIRE(*v == bytes({42}));
	}
	REQUIRE(std::filesystem::remove(path));
}
