// Contract 2 structural tests: octree topology over signed chunk space,
// storage-key ordering, and the Merkle hash discipline — pinned against the
// reference codec in src/world/lod_pyramid.hpp, with the spike-1 minigen
// generator and the Contract-1 blob codec supplying real chunk content (the
// killer test: one cell edit dirties exactly the ancestor chain).

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "gen/minigen.hpp"
#include "voxel/morton.hpp"
#include "world/lod_pyramid.hpp"
#include "world/voxel_blob.hpp"

namespace {

// Canonical Contract-1 blob for a minigen chunk at the given chunk coords.
std::vector<std::byte> minigenChunkBlob(int cx, int cy, int cz)
{
	gen::GenChunk chunk;
	gen::generateChunk(gen::kStandardSeed, {cx, cy, cz}, chunk);
	std::vector<world::CellValue> cells(4096);
	for (int z = 0; z < gen::kGenChunk; ++z)
		for (int y = 0; y < gen::kGenChunk; ++y)
			for (int x = 0; x < gen::kGenChunk; ++x)
				cells[voxel::mortonEncode({static_cast<std::uint32_t>(x),
						static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(z)})] =
						world::CellValue{chunk.at(x, y, z), 0};
	return world::packVoxelBlob(cells, 4);
}

} // namespace

TEST_CASE("LOD key navigation uses floor semantics and tops out at the octant forest")
{
	using world::lodChild;
	using world::lodKeyForChunk;
	using world::lodOctantOf;
	using world::lodParent;

	// Floor (arithmetic-shift) parent semantics at the sign boundary —
	// truncation would send -1 to 0 and merge the negative octant into the
	// positive one.
	STATIC_REQUIRE(lodParent(lodKeyForChunk(-1, -1, -1))->x == -1);
	STATIC_REQUIRE(lodParent(lodKeyForChunk(-2, -2, -2))->x == -1);
	STATIC_REQUIRE(lodParent(lodKeyForChunk(1, 1, 1))->x == 0);
	STATIC_REQUIRE(lodParent(lodKeyForChunk(-3, 5, 0)) ==
			world::LodNodeKey{1, -2, 2, 0});

	// child(parent(k), octantOf(k)) == k across signs.
	for (const std::int32_t c : {-9, -8, -2, -1, 0, 1, 2, 7, 8})
		for (const std::int32_t d : {-5, -1, 0, 3}) {
			const world::LodNodeKey k = lodKeyForChunk(c, d, c ^ d);
			const auto p = lodParent(k);
			REQUIRE(p.has_value());
			const auto back = lodChild(*p, lodOctantOf(k));
			CAPTURE(c, d);
			REQUIRE(back.has_value());
			REQUIRE(*back == k);
		}

	// The dirty chain runs level 0..28 inclusive and ends at a sign-octant
	// root with coords in {-1, 0} — the fixed point of floor-halving.
	const auto chain = world::lodDirtyChain(-123456, 77, -1);
	REQUIRE(chain.size() == static_cast<std::size_t>(world::kLodTopLevel) + 1);
	const world::LodNodeKey root = chain.back();
	REQUIRE(root.level == world::kLodTopLevel);
	REQUIRE((root.x == -1 || root.x == 0));
	REQUIRE((root.y == -1 || root.y == 0));
	REQUIRE((root.z == -1 || root.z == 0));
	REQUIRE(root == world::LodNodeKey{28, -1, 0, -1});
	REQUIRE_FALSE(lodParent(root).has_value()); // forest of 8, no single root
}

TEST_CASE("LOD storage keys order lexicographically as (level, x, y, z)")
{
	// Numeric tuple order must equal byte order, ACROSS the sign boundary —
	// that is what the sign-bit flip + big-endian encoding exists for.
	const world::LodNodeKey ordered[] = {
		{0, -2, 0, 0}, {0, -1, -1, 5}, {0, -1, 0, -7}, {0, -1, 0, 6}, {0, 0, -3, 0},
		{0, 0, 0, 0}, {0, 0, 0, 1}, {0, 7, -1, -1}, {1, -1, 0, 0}, {2, 0, 0, 0},
	};
	for (std::size_t i = 0; i + 1 < std::size(ordered); ++i) {
		const auto a = world::lodStorageKey(ordered[i]);
		const auto b = world::lodStorageKey(ordered[i + 1]);
		CAPTURE(i);
		REQUIRE(a < b); // std::array lexicographic comparison
	}
}

TEST_CASE("LOD FNV-1a-64 matches the published test vectors")
{
	// Frozen hash function: standard FNV-1a 64-bit offset basis and prime.
	REQUIRE(world::lodFnv1a64({}) == 0xCBF29CE484222325ull);
	const std::byte a[] = {std::byte{'a'}};
	REQUIRE(world::lodFnv1a64(a) == 0xAF63DC4C8601EC8Cull);
	const std::byte foobar[] = {std::byte{'f'}, std::byte{'o'}, std::byte{'o'},
			std::byte{'b'}, std::byte{'a'}, std::byte{'r'}};
	REQUIRE(world::lodFnv1a64(foobar) == 0x85944171F73967E8ull);
}

TEST_CASE("LOD parent hash consults absent children only through the mask")
{
	std::array<std::uint64_t, 8> hashes{1, 2, 3, 4, 5, 6, 7, 8};
	const std::uint64_t partial = world::lodParentHash(3, 0x01, hashes);
	const std::uint64_t full = world::lodParentHash(3, 0xFF, hashes);
	REQUIRE(partial != full);

	// Varying an ABSENT child's hash cannot change the parent hash.
	std::array<std::uint64_t, 8> varied = hashes;
	varied[7] = 0xDEADBEEF;
	REQUIRE(world::lodParentHash(3, 0x01, varied) == partial);
	// Varying a PRESENT child's must.
	varied = hashes;
	varied[0] = 0xDEADBEEF;
	REQUIRE(world::lodParentHash(3, 0x01, varied) != partial);
	// Level participates in identity too.
	REQUIRE(world::lodParentHash(4, 0x01, hashes) != partial);
}

TEST_CASE("LOD Merkle pyramid over minigen: an edit dirties exactly the ancestor chain")
{
	// 4x4x4-chunk region pyramid (levels 0..2): 64 + 8 + 1 nodes, built from
	// real generated content through the canonical Contract-1 packer.
	using Key = std::array<std::uint8_t, world::kLodStorageKeyBytes>;
	const auto buildPyramid = [](const std::vector<std::vector<std::byte>> &blobs) {
		std::map<Key, std::uint64_t> nodes;
		for (int cz = 0; cz < 4; ++cz)
			for (int cy = 0; cy < 4; ++cy)
				for (int cx = 0; cx < 4; ++cx)
					nodes[world::lodStorageKey(world::lodKeyForChunk(cx, cy, cz))] =
							world::lodChunkHash(
									blobs[static_cast<std::size_t>((cz * 4 + cy) * 4 + cx)]);
		for (std::uint8_t level = 1; level <= 2; ++level) {
			const int n = 4 >> level;
			for (int pz = 0; pz < n; ++pz)
				for (int py = 0; py < n; ++py)
					for (int px = 0; px < n; ++px) {
						const world::LodNodeKey parent{level, px, py, pz};
						std::array<std::uint64_t, 8> child{};
						for (std::uint8_t o = 0; o < 8; ++o)
							child[o] = nodes.at(world::lodStorageKey(
									*world::lodChild(parent, o)));
						nodes[world::lodStorageKey(parent)] =
								world::lodParentHash(level, 0xFF, child);
					}
		}
		return nodes;
	};

	std::vector<std::vector<std::byte>> blobs;
	for (int cz = 0; cz < 4; ++cz)
		for (int cy = 0; cy < 4; ++cy)
			for (int cx = 0; cx < 4; ++cx)
				blobs.push_back(minigenChunkBlob(cx, cy, cz));
	const auto before = buildPyramid(blobs);
	REQUIRE(before.size() == 64 + 8 + 1);

	// Determinism: a rebuild from regenerated content is hash-identical.
	{
		std::vector<std::vector<std::byte>> again;
		for (int cz = 0; cz < 4; ++cz)
			for (int cy = 0; cy < 4; ++cy)
				for (int cx = 0; cx < 4; ++cx)
					again.push_back(minigenChunkBlob(cx, cy, cz));
		REQUIRE(buildPyramid(again) == before);
	}

	// Edit one cell of chunk (2,1,3): unpack, change, repack canonically.
	{
		const std::size_t idx = (3 * 4 + 1) * 4 + 2;
		auto chunk = world::unpackVoxelBlob(blobs[idx]);
		REQUIRE(chunk.has_value());
		chunk->cells[voxel::mortonEncode({5, 5, 5})] = world::CellValue{0xBEEF, 0};
		blobs[idx] = world::packVoxelBlob(chunk->cells, 4);
	}
	const auto after = buildPyramid(blobs);

	// Exactly the ancestor chain changed: L0 (2,1,3), L1 (1,0,1), L2 (0,0,0).
	const Key dirty[] = {
		world::lodStorageKey({0, 2, 1, 3}),
		world::lodStorageKey({1, 1, 0, 1}),
		world::lodStorageKey({2, 0, 0, 0}),
	};
	std::size_t changed = 0;
	for (const auto &[key, hash] : before) {
		const bool expectDirty = key == dirty[0] || key == dirty[1] || key == dirty[2];
		const bool isDirty = after.at(key) != hash;
		CAPTURE(unsigned{key[0]});
		REQUIRE(isDirty == expectDirty);
		changed += isDirty ? 1 : 0;
	}
	REQUIRE(changed == 3);
}

TEST_CASE("LOD v1 payload round-trips and rejects version or size mismatch")
{
	const world::LodPayloadV1 p{0x0123456789ABCDEFull, 0xFFFF0000FFFF0000ull, 42, 4095};
	const auto bytes = world::lodPayloadSerialize(p);
	REQUIRE(bytes.size() == world::kLodPayloadBytes);
	REQUIRE(std::to_integer<std::uint8_t>(bytes[0]) == world::kLodPayloadVersion);

	const auto back = world::lodPayloadDeserialize(bytes);
	REQUIRE(back.has_value());
	REQUIRE(*back == p);

	auto bad = bytes;
	bad[0] = std::byte{2}; // future version: old readers reject, never misread
	REQUIRE_FALSE(world::lodPayloadDeserialize(bad).has_value());
	REQUIRE_FALSE(world::lodPayloadDeserialize({bytes.data(), bytes.size() - 1})
						  .has_value());
}
