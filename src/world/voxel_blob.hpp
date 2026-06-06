// Contract 1 reference codec — the compiled anchor for the packed-voxel-blob
// freeze (docs/contracts/contract-1-voxel-blob.md). Header-only; pack,
// unpack, and random access over the palette-compressed chunk blob every
// pillar reads (renderer in-shader, scripting snapshots, persistence at
// rest).
//
// Frozen layout (little-endian throughout):
//
//   Header, 16 bytes:
//     u32 magic        'VXB1' (bytes 56 58 42 31)
//     u8  version      1
//     u8  log2Extent   chunk edge = 1 << log2Extent (v1 value: 4 -> 16^3;
//                      format supports 1..5, bounded by the Morton codec)
//     u8  indexBits    0, 1, 2, 4, 8, or 16 (0 = uniform chunk, no stream)
//     u8  reserved     0 on write; readers ignore
//     u32 paletteCount >= 1, <= extent^3, <= 2^indexBits (when indexBits > 0)
//     u32 reserved2    0 on write; readers ignore
//   Palette, paletteCount x 16 bytes:
//     u32 content      global content id (Contract 3)
//     u32 flags        0 on write; readers ignore (reserved)
//     u64 state        typed in-cell state; wide enough for a 4^3 microblock
//                      occupancy mask exactly (the state *schema* per content
//                      type is registry data, outside this contract)
//   Index stream (absent when indexBits == 0):
//     ceil(extent^3 * indexBits / 64) u64 words. Cell i (i = Morton index,
//     x -> bit 0 per src/voxel/morton.hpp) occupies bits
//     [i*indexBits, (i+1)*indexBits) of the stream, little-endian within
//     each word. Every legal width divides 64, so entries never straddle
//     words. Morton order makes every aligned 8^3 brick a contiguous
//     512-cell run by construction.
//
// Format-vs-policy split (the same discipline as Contract 3): a blob is
// VALID with any width whose range covers paletteCount — over-wide blobs are
// legal so in-place edits need not repack instantly. The CANONICAL form
// (what pack() emits, what persistence/network hash) is minimal width,
// first-appearance palette order, no duplicate or unused entries. Repack
// *hysteresis* (when the engine re-canonicalizes) is engine policy, not
// schema.

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "voxel/morton.hpp"

namespace world {

// One cell as consumers see it: a (content, state) value pair. flags is
// reserved blob plumbing, deliberately absent here.
struct CellValue {
	std::uint32_t content = 0;
	std::uint64_t state = 0;

	friend constexpr bool operator==(const CellValue &, const CellValue &) = default;
};

struct UnpackedChunk {
	std::uint32_t log2Extent = 0;
	// Palette in blob order (canonical blobs: first-appearance order) —
	// exposed so consumers and tests can see canonicality, not just values.
	std::vector<CellValue> palette;
	std::vector<CellValue> cells; // extent^3, Morton order
};

inline constexpr std::uint32_t kVoxelBlobMagic = 0x31425856u; // 'VXB1' LE
inline constexpr std::uint8_t kVoxelBlobVersion = 1;
inline constexpr std::uint32_t kVoxelBlobHeaderBytes = 16;
inline constexpr std::uint32_t kVoxelBlobEntryBytes = 16;
inline constexpr std::uint32_t kVoxelBlobChunkLog2ExtentV1 = 4; // 16^3

namespace detail {

constexpr bool legalIndexBits(std::uint8_t bits)
{
	return bits == 0 || bits == 1 || bits == 2 || bits == 4 || bits == 8 || bits == 16;
}

// Smallest legal width whose range covers `count` palette entries.
constexpr std::uint8_t minimalIndexBits(std::size_t count)
{
	if (count <= 1)
		return 0;
	if (count <= 2)
		return 1;
	if (count <= 4)
		return 2;
	if (count <= 16)
		return 4;
	if (count <= 256)
		return 8;
	return 16;
}

inline void putU32(std::vector<std::byte> &out, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
}

inline void putU64(std::vector<std::byte> &out, std::uint64_t v)
{
	for (int i = 0; i < 8; ++i)
		out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
}

inline std::uint32_t getU32(std::span<const std::byte> in, std::size_t at)
{
	std::uint32_t v = 0;
	for (int i = 0; i < 4; ++i)
		v |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(in[at + i])) << (8 * i);
	return v;
}

inline std::uint64_t getU64(std::span<const std::byte> in, std::size_t at)
{
	std::uint64_t v = 0;
	for (int i = 0; i < 8; ++i)
		v |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(in[at + i])) << (8 * i);
	return v;
}

struct HeaderInfo {
	std::uint8_t log2Extent = 0;
	std::uint8_t indexBits = 0;
	std::uint32_t paletteCount = 0;
	std::size_t cellCount = 0;
	std::size_t indexWords = 0;
	std::size_t totalBytes = 0;
};

// Validate the header and the blob's exact size; everything else (index
// range checks) happens where the indices are read.
inline std::optional<HeaderInfo> readHeader(std::span<const std::byte> blob)
{
	if (blob.size() < kVoxelBlobHeaderBytes)
		return std::nullopt;
	if (getU32(blob, 0) != kVoxelBlobMagic)
		return std::nullopt;
	if (std::to_integer<std::uint8_t>(blob[4]) != kVoxelBlobVersion)
		return std::nullopt;
	HeaderInfo h;
	h.log2Extent = std::to_integer<std::uint8_t>(blob[5]);
	h.indexBits = std::to_integer<std::uint8_t>(blob[6]);
	// blob[7] reserved: readers ignore.
	h.paletteCount = getU32(blob, 8);
	// bytes 12..15 reserved: readers ignore.
	if (h.log2Extent < 1 || h.log2Extent > voxel::kMortonBitsPerAxis)
		return std::nullopt;
	if (!legalIndexBits(h.indexBits))
		return std::nullopt;
	const std::size_t extent = std::size_t{1} << h.log2Extent;
	h.cellCount = extent * extent * extent;
	if (h.paletteCount < 1 || h.paletteCount > h.cellCount)
		return std::nullopt;
	if (h.indexBits == 0) {
		if (h.paletteCount != 1)
			return std::nullopt;
	} else {
		if (h.indexBits < 64 && (std::uint64_t{1} << h.indexBits) < h.paletteCount)
			return std::nullopt;
		// Canonicality is NOT required here: over-wide blobs are valid (the
		// schema hook that makes edit-without-instant-repack legal).
	}
	h.indexWords = h.indexBits == 0
			? 0
			: (h.cellCount * h.indexBits + 63) / 64;
	h.totalBytes = kVoxelBlobHeaderBytes +
			static_cast<std::size_t>(h.paletteCount) * kVoxelBlobEntryBytes +
			h.indexWords * 8;
	if (blob.size() != h.totalBytes)
		return std::nullopt;
	return h;
}

inline CellValue readPaletteEntry(std::span<const std::byte> blob, std::uint32_t slot)
{
	const std::size_t at =
			kVoxelBlobHeaderBytes + static_cast<std::size_t>(slot) * kVoxelBlobEntryBytes;
	// flags (bytes at+4..at+7) are reserved: readers ignore.
	return CellValue{getU32(blob, at), getU64(blob, at + 8)};
}

inline std::uint32_t readIndex(std::span<const std::byte> blob, const HeaderInfo &h,
		std::size_t cell)
{
	const std::size_t bitAt = cell * h.indexBits;
	const std::size_t wordAt = kVoxelBlobHeaderBytes +
			static_cast<std::size_t>(h.paletteCount) * kVoxelBlobEntryBytes +
			(bitAt / 64) * 8;
	const std::uint64_t word = getU64(blob, wordAt);
	const std::uint64_t mask =
			h.indexBits >= 64 ? ~0ull : ((std::uint64_t{1} << h.indexBits) - 1);
	return static_cast<std::uint32_t>((word >> (bitAt % 64)) & mask);
}

} // namespace detail

// Canonical packer: cells (extent^3, Morton order) -> blob with
// first-appearance palette order and minimal index width. Returns empty on
// size mismatch (the one caller error this API can detect).
inline std::vector<std::byte> packVoxelBlob(std::span<const CellValue> cells,
		std::uint32_t log2Extent)
{
	std::vector<std::byte> out;
	if (log2Extent < 1 || log2Extent > voxel::kMortonBitsPerAxis)
		return out;
	const std::size_t extent = std::size_t{1} << log2Extent;
	const std::size_t cellCount = extent * extent * extent;
	if (cells.size() != cellCount)
		return out;

	// First-appearance palette (canonical order). Chunk palettes are small;
	// linear probing over a vector beats a map for the realistic sizes and
	// keeps this reference implementation dependency-free and obvious.
	std::vector<CellValue> palette;
	std::vector<std::uint32_t> indices(cellCount);
	for (std::size_t i = 0; i < cellCount; ++i) {
		std::uint32_t slot = 0;
		const std::size_t n = palette.size();
		for (; slot < n; ++slot)
			if (palette[slot] == cells[i])
				break;
		if (slot == palette.size())
			palette.push_back(cells[i]);
		indices[i] = slot;
	}

	const std::uint8_t bits = detail::minimalIndexBits(palette.size());
	out.reserve(kVoxelBlobHeaderBytes + palette.size() * kVoxelBlobEntryBytes +
			(bits ? ((cellCount * bits + 63) / 64) * 8 : 0));

	detail::putU32(out, kVoxelBlobMagic);
	out.push_back(static_cast<std::byte>(kVoxelBlobVersion));
	out.push_back(static_cast<std::byte>(log2Extent));
	out.push_back(static_cast<std::byte>(bits));
	out.push_back(std::byte{0}); // reserved
	detail::putU32(out, static_cast<std::uint32_t>(palette.size()));
	detail::putU32(out, 0); // reserved2

	for (const CellValue &e : palette) {
		detail::putU32(out, e.content);
		detail::putU32(out, 0); // flags: writers zero
		detail::putU64(out, e.state);
	}

	if (bits) {
		const std::size_t words = (cellCount * bits + 63) / 64;
		std::vector<std::uint64_t> stream(words, 0);
		for (std::size_t i = 0; i < cellCount; ++i) {
			const std::size_t bitAt = i * bits;
			stream[bitAt / 64] |= static_cast<std::uint64_t>(indices[i]) << (bitAt % 64);
		}
		for (const std::uint64_t w : stream)
			detail::putU64(out, w);
	}
	return out;
}

// Full unpack with full validation: every structural rule above plus an
// index-range check over the whole stream. nullopt on ANY violation.
inline std::optional<UnpackedChunk> unpackVoxelBlob(std::span<const std::byte> blob)
{
	const auto h = detail::readHeader(blob);
	if (!h)
		return std::nullopt;
	UnpackedChunk chunk;
	chunk.log2Extent = h->log2Extent;
	chunk.palette.reserve(h->paletteCount);
	for (std::uint32_t s = 0; s < h->paletteCount; ++s)
		chunk.palette.push_back(detail::readPaletteEntry(blob, s));
	chunk.cells.resize(h->cellCount);
	for (std::size_t i = 0; i < h->cellCount; ++i) {
		const std::uint32_t slot = h->indexBits == 0
				? 0u
				: detail::readIndex(blob, *h, i);
		if (slot >= h->paletteCount)
			return std::nullopt;
		chunk.cells[i] = chunk.palette[slot];
	}
	return chunk;
}

// Random access without unpacking: header + palette-slot validation for the
// one cell touched. nullopt on structural violation or out-of-range
// coordinate/index. Cost model: O(1) reads (the contract's read-unpack
// budget hook).
inline std::optional<CellValue> blobCellAt(std::span<const std::byte> blob,
		voxel::LocalPos pos)
{
	const auto h = detail::readHeader(blob);
	if (!h)
		return std::nullopt;
	const std::uint32_t extent = 1u << h->log2Extent;
	if (pos.x >= extent || pos.y >= extent || pos.z >= extent)
		return std::nullopt;
	const std::size_t cell = voxel::mortonEncode(pos);
	const std::uint32_t slot = h->indexBits == 0 ? 0u : detail::readIndex(blob, *h, cell);
	if (slot >= h->paletteCount)
		return std::nullopt;
	return detail::readPaletteEntry(blob, slot);
}

} // namespace world
