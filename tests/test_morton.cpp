// Round-trip and ordering properties of the Morton codec.
//
// CONTRIBUTING.md mandates property-based round-trip tests for every codec;
// the coordinate space is small enough to test exhaustively rather than
// sample randomly.

#include <catch2/catch_test_macros.hpp>

#include "voxel/morton.hpp"

namespace {

constexpr std::uint32_t kAxisRange = voxel::kMortonAxisMask + 1; // 32

} // namespace

TEST_CASE("Morton codec round-trips exhaustively over the 5-bit axis range")
{
	for (std::uint32_t z = 0; z < kAxisRange; ++z)
		for (std::uint32_t y = 0; y < kAxisRange; ++y)
			for (std::uint32_t x = 0; x < kAxisRange; ++x) {
				const voxel::LocalPos p{x, y, z};
				CAPTURE(x, y, z);
				REQUIRE(voxel::mortonDecode(voxel::mortonEncode(p)) == p);
			}
}

TEST_CASE("Morton encoding is a bijection over the full code space")
{
	// Every 15-bit code decodes to a position that encodes back to itself,
	// so encode is onto (and with the round-trip above, one-to-one).
	for (std::uint32_t code = 0; code < (1u << (3 * voxel::kMortonBitsPerAxis)); ++code) {
		CAPTURE(code);
		REQUIRE(voxel::mortonEncode(voxel::mortonDecode(code)) == code);
	}
}

TEST_CASE("Axis bit assignment matches Contract 1 (x -> bit 0, y -> bit 1, z -> bit 2)")
{
	REQUIRE(voxel::mortonEncode({1, 0, 0}) == 0b001u);
	REQUIRE(voxel::mortonEncode({0, 1, 0}) == 0b010u);
	REQUIRE(voxel::mortonEncode({0, 0, 1}) == 0b100u);
	REQUIRE(voxel::mortonEncode({2, 0, 0}) == 0b001000u);
}
