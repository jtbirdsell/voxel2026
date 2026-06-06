// Morton (Z-order) codec for chunk-local voxel coordinates.
//
// Contract 1 (docs/contracts/contract-1-voxel-blob.md) specifies Morton-ordered
// SoA chunk storage with 16^3 chunks and 8^3 brick granularity. This is the
// codec that ordering rests on: 5 bits per axis covers coordinates 0..31,
// which spans both the 16-node chunk stride and a 2x2x2 brick neighborhood.
//
// Round-trip identity (decode(encode(p)) == p) is enforced exhaustively by
// tests/test_morton.cpp per the property-test mandate in CONTRIBUTING.md.

#pragma once

#include <cstdint>

namespace voxel {

inline constexpr unsigned kMortonBitsPerAxis = 5;
inline constexpr std::uint32_t kMortonAxisMask = (1u << kMortonBitsPerAxis) - 1;

namespace detail {

// Spread the low 5 bits of v so each lands 3 positions apart: bit i -> bit 3i.
// Bit positions of b4..b0 after each step:
constexpr std::uint32_t spread3(std::uint32_t v)
{
	v &= kMortonAxisMask;            // {4,3,2,1,0}
	v = (v | (v << 8)) & 0x0100Fu;   // {12,3,2,1,0}   mask 0b1'0000'0000'1111
	v = (v | (v << 4)) & 0x010C3u;   // {12,7,6,1,0}   mask 0b1'0000'1100'0011
	v = (v | (v << 2)) & 0x09249u;   // {12,9,6,3,0}   mask 0b1001'0010'0100'1001
	return v;
}

// Inverse of spread3: gather bits 0,3,6,9,12 back into the low 5 bits.
constexpr std::uint32_t gather3(std::uint32_t v)
{
	v &= 0x09249u;
	v = (v | (v >> 2)) & 0x010C3u;
	v = (v | (v >> 4)) & 0x0100Fu;
	v = (v | (v >> 8)) & kMortonAxisMask;
	return v;
}

} // namespace detail

struct LocalPos {
	std::uint32_t x = 0, y = 0, z = 0;

	friend constexpr bool operator==(const LocalPos &, const LocalPos &) = default;
};

// Interleave x/y/z (5 bits each) into a 15-bit Morton index: x owns bit 0.
constexpr std::uint32_t mortonEncode(LocalPos p)
{
	return detail::spread3(p.x) | (detail::spread3(p.y) << 1) | (detail::spread3(p.z) << 2);
}

constexpr LocalPos mortonDecode(std::uint32_t code)
{
	return {
		detail::gather3(code),
		detail::gather3(code >> 1),
		detail::gather3(code >> 2),
	};
}

} // namespace voxel
