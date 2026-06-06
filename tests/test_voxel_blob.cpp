// Contract 1 layout tests: the packed-voxel-blob format frozen in
// docs/contracts/contract-1-voxel-blob.md, pinned against the reference
// codec in src/world/voxel_blob.hpp. The byte-level golden pins endianness
// and every offset; the property tests sweep the width boundaries with a
// seeded PRNG (deterministic — no time, no global RNG).

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "world/voxel_blob.hpp"

namespace {

// Deterministic xorshift64 — the property tests must reproduce bit-for-bit.
struct Prng {
	std::uint64_t s = 0x9E3779B97F4A7C15ull;
	std::uint64_t next()
	{
		s ^= s << 13;
		s ^= s >> 7;
		s ^= s << 17;
		return s;
	}
};

std::vector<world::CellValue> randomCells(std::size_t cellCount, std::size_t distinct,
		Prng &rng)
{
	std::vector<world::CellValue> values;
	values.reserve(distinct);
	for (std::size_t j = 0; j < distinct; ++j)
		values.push_back({static_cast<std::uint32_t>(1000 + j), rng.next()});
	std::vector<world::CellValue> cells(cellCount);
	// Force every distinct value to appear (so the palette count is exact,
	// and first-appearance order is the forced prefix order).
	for (std::size_t i = 0; i < cellCount; ++i)
		cells[i] = i < distinct ? values[i] : values[rng.next() % distinct];
	return cells;
}

} // namespace

TEST_CASE("voxel blob byte-level golden pins endianness and every offset")
{
	// 2^3 chunk, two palette entries, alternating cells -> 56 bytes, all
	// hand-computed from the frozen layout.
	const world::CellValue a{7, 0x0123456789ABCDEFull};
	const world::CellValue b{9, 0};
	std::vector<world::CellValue> cells(8);
	for (std::size_t i = 0; i < 8; ++i)
		cells[i] = (i & 1) ? b : a;
	const std::vector<std::byte> blob = world::packVoxelBlob(cells, 1);

	const std::uint8_t expected[56] = {
		// header
		0x56, 0x58, 0x42, 0x31, // magic 'VXB1'
		0x01,                   // version
		0x01,                   // log2Extent (2^3)
		0x01,                   // indexBits
		0x00,                   // reserved
		0x02, 0x00, 0x00, 0x00, // paletteCount
		0x00, 0x00, 0x00, 0x00, // reserved2
		// palette[0] = a
		0x07, 0x00, 0x00, 0x00, // content
		0x00, 0x00, 0x00, 0x00, // flags (writers zero)
		0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01, // state LE
		// palette[1] = b
		0x09, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		// index stream: cell i -> i & 1, bits 01010101... -> byte 0xAA
		0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	REQUIRE(blob.size() == 56);
	for (std::size_t i = 0; i < 56; ++i) {
		CAPTURE(i, std::to_integer<unsigned>(blob[i]), unsigned{expected[i]});
		REQUIRE(std::to_integer<std::uint8_t>(blob[i]) == expected[i]);
	}
}

TEST_CASE("voxel blob round-trips across every index-width boundary")
{
	Prng rng;
	const std::uint32_t log2Extent = world::kVoxelBlobChunkLog2ExtentV1; // 16^3
	const std::size_t cellCount = 4096;
	const struct {
		std::size_t distinct;
		std::uint8_t expectBits;
	} rows[] = {{1, 0}, {2, 1}, {3, 2}, {4, 2}, {5, 4}, {16, 4}, {17, 8}, {255, 8},
			{256, 8}, {257, 16}, {4095, 16}, {4096, 16}};
	for (const auto &row : rows) {
		CAPTURE(row.distinct);
		const auto cells = randomCells(cellCount, row.distinct, rng);
		const auto blob = world::packVoxelBlob(cells, log2Extent);
		REQUIRE_FALSE(blob.empty());
		REQUIRE(std::to_integer<std::uint8_t>(blob[6]) == row.expectBits);

		const auto chunk = world::unpackVoxelBlob(blob);
		REQUIRE(chunk.has_value());
		REQUIRE(chunk->log2Extent == log2Extent);
		REQUIRE(chunk->palette.size() == row.distinct);
		REQUIRE(chunk->cells == cells);

		// Canonical palette order is first appearance — which randomCells
		// forces to be the value-construction order.
		for (std::size_t j = 0; j < row.distinct; ++j)
			REQUIRE(chunk->palette[j] == cells[j]);

		// Canonical determinism: re-packing the unpacked cells is
		// byte-identical.
		REQUIRE(world::packVoxelBlob(chunk->cells, log2Extent) == blob);
	}
}

TEST_CASE("voxel blob random access agrees with full unpack")
{
	Prng rng;
	const auto cells = randomCells(4096, 17, rng);
	const auto blob = world::packVoxelBlob(cells, 4);
	for (int probe = 0; probe < 200; ++probe) {
		const voxel::LocalPos pos{static_cast<std::uint32_t>(rng.next() % 16),
				static_cast<std::uint32_t>(rng.next() % 16),
				static_cast<std::uint32_t>(rng.next() % 16)};
		const auto cell = world::blobCellAt(blob, pos);
		CAPTURE(probe, pos.x, pos.y, pos.z);
		REQUIRE(cell.has_value());
		REQUIRE(*cell == cells[voxel::mortonEncode(pos)]);
	}
	// Out-of-bounds coordinates are rejected, not wrapped.
	REQUIRE_FALSE(world::blobCellAt(blob, {16, 0, 0}).has_value());
	REQUIRE_FALSE(world::blobCellAt(blob, {0, 16, 0}).has_value());
	REQUIRE_FALSE(world::blobCellAt(blob, {0, 0, 16}).has_value());
}

TEST_CASE("Morton order makes aligned 8x8x8 bricks contiguous 512-cell runs")
{
	const world::CellValue filler{1, 0};
	const world::CellValue brick{2, 0};
	std::vector<world::CellValue> cells(4096, filler);
	for (std::uint32_t dz = 0; dz < 8; ++dz)
		for (std::uint32_t dy = 0; dy < 8; ++dy)
			for (std::uint32_t dx = 0; dx < 8; ++dx)
				cells[voxel::mortonEncode({8 + dx, dy, 8 + dz})] = brick;

	// The aligned brick's base code is 512-aligned and the brick occupies
	// exactly [base, base + 512) — the contiguity the GPU brick paths rely on.
	const std::uint32_t base = voxel::mortonEncode({8, 0, 8});
	REQUIRE(base % 512 == 0);
	const auto chunk = world::unpackVoxelBlob(world::packVoxelBlob(cells, 4));
	REQUIRE(chunk.has_value());
	std::size_t brickCount = 0;
	for (std::size_t i = 0; i < 4096; ++i)
		brickCount += chunk->cells[i] == brick ? 1 : 0;
	REQUIRE(brickCount == 512);
	for (std::size_t i = base; i < base + 512; ++i) {
		CAPTURE(i);
		REQUIRE(chunk->cells[i] == brick);
	}
}

TEST_CASE("voxel blob unpack rejects malformed input and accepts reserved bytes")
{
	Prng rng;
	const auto cells = randomCells(4096, 3, rng);
	const auto blob = world::packVoxelBlob(cells, 4);
	REQUIRE(world::unpackVoxelBlob(blob).has_value());

	// Truncations at every structural boundary, and one trailing byte.
	for (const std::size_t cut : {std::size_t{0}, std::size_t{8}, std::size_t{15},
				 std::size_t{16}, std::size_t{24}, blob.size() - 1}) {
		CAPTURE(cut);
		REQUIRE_FALSE(world::unpackVoxelBlob({blob.data(), cut}).has_value());
	}
	{
		auto longer = blob;
		longer.push_back(std::byte{0});
		REQUIRE_FALSE(world::unpackVoxelBlob(longer).has_value());
	}

	// Field corruption: magic, version, extent, width, paletteCount.
	const auto corrupt = [&](std::size_t at, std::uint8_t value) {
		auto bad = blob;
		bad[at] = static_cast<std::byte>(value);
		return world::unpackVoxelBlob(bad).has_value();
	};
	REQUIRE_FALSE(corrupt(0, 0x00));  // magic
	REQUIRE_FALSE(corrupt(4, 2));     // version
	REQUIRE_FALSE(corrupt(5, 0));     // log2Extent below range
	REQUIRE_FALSE(corrupt(5, 6));     // log2Extent above Morton support
	REQUIRE_FALSE(corrupt(6, 3));     // illegal width
	REQUIRE_FALSE(corrupt(8, 0));     // paletteCount 0 (also a size mismatch)

	// Out-of-range palette index inside the stream: 3 entries -> width 2;
	// force cell 0's two bits to 0b11 == 3.
	{
		auto bad = blob;
		const std::size_t streamAt = 16 + 3 * 16;
		bad[streamAt] = static_cast<std::byte>(
				std::to_integer<std::uint8_t>(bad[streamAt]) | 0x03);
		REQUIRE_FALSE(world::unpackVoxelBlob(bad).has_value());
		REQUIRE_FALSE(world::blobCellAt(bad, {0, 0, 0}).has_value());
	}

	// Reserved bytes are ignored by readers (forward compatibility): header
	// reserved, reserved2, and a palette flags byte.
	{
		auto reserved = blob;
		reserved[7] = std::byte{0xFF};
		reserved[12] = std::byte{0xFF};
		reserved[16 + 4] = std::byte{0xFF}; // palette[0].flags low byte
		const auto chunk = world::unpackVoxelBlob(reserved);
		REQUIRE(chunk.has_value());
		REQUIRE(chunk->cells == cells);
	}
}

TEST_CASE("voxel blob accepts valid over-wide blobs but pack stays canonical")
{
	// Hand-build a width-2 blob holding only 2 palette entries (legal,
	// non-canonical — the in-place-edit allowance), 2^3 extent.
	std::vector<std::byte> blob;
	const auto pushU32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
			blob.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
	};
	const auto pushU64 = [&](std::uint64_t v) {
		for (int i = 0; i < 8; ++i)
			blob.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
	};
	pushU32(world::kVoxelBlobMagic);
	blob.push_back(std::byte{1});  // version
	blob.push_back(std::byte{1});  // log2Extent
	blob.push_back(std::byte{2});  // indexBits: WIDER than minimal (1)
	blob.push_back(std::byte{0});
	pushU32(2); // paletteCount
	pushU32(0);
	pushU32(7), pushU32(0), pushU64(0); // palette[0]
	pushU32(9), pushU32(0), pushU64(0); // palette[1]
	// 8 cells x 2 bits, alternating 0,1: bits 01 00 01 00 ... -> 0x44, 0x44.
	pushU64(0x4444ull);

	const auto chunk = world::unpackVoxelBlob(blob);
	REQUIRE(chunk.has_value());
	for (std::size_t i = 0; i < 8; ++i)
		REQUIRE(chunk->cells[i] == world::CellValue{(i & 1) ? 9u : 7u, 0});

	// Re-packing canonicalizes to minimal width 1. (At this tiny extent both
	// widths round up to one stream word, so the blobs are equal-SIZE but
	// must differ in content — the width byte and the packed bits.)
	const auto canonical = world::packVoxelBlob(chunk->cells, 1);
	REQUIRE(std::to_integer<std::uint8_t>(canonical[6]) == 1);
	REQUIRE(canonical != blob);
	const auto reUnpacked = world::unpackVoxelBlob(canonical);
	REQUIRE(reUnpacked.has_value());
	REQUIRE(reUnpacked->cells == chunk->cells);
}

TEST_CASE("voxel blob pack rejects caller errors")
{
	const std::vector<world::CellValue> eight(8);
	REQUIRE(world::packVoxelBlob(eight, 0).empty());  // extent below range
	REQUIRE(world::packVoxelBlob(eight, 6).empty());  // beyond Morton support
	REQUIRE(world::packVoxelBlob(eight, 2).empty());  // size mismatch (needs 64)
}
