// Bit-exactness properties of the deterministic math helpers.
// CONTRIBUTING.md: round-trip/property tests are mandatory for every codec —
// these helpers are the float half of Contract 5's arithmetic vocabulary.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

#include "sim/det_math.hpp"

TEST_CASE("detFloor/detTrunc agree with the C library: fine sweep over +/-1024")
{
	// 2M+1 values at 1/1024 granularity. std::floor/std::trunc are exact for
	// these inputs (IEEE), so agreement here is agreement with ground truth.
	for (std::int32_t i = -1048576; i <= 1048576; ++i) {
		const float f = static_cast<float>(i) * (1.0f / 1024.0f);
		REQUIRE(sim::detFloor(f) == std::floor(f));
		REQUIRE(sim::detTrunc(f) == std::trunc(f));
		REQUIRE(sim::detFloorToInt(f) == static_cast<std::int32_t>(std::floor(f)));
	}
}

TEST_CASE("detFloor/detTrunc agree with the C library: coarse sweep over +/-32768")
{
	// (review finding: the kernel uses coordinates up to ±30000 but the fine
	// sweep only reached ±1024.) 1/32 granularity across the full kernel
	// coordinate range.
	for (std::int32_t i = -1048576; i <= 1048576; ++i) {
		const float f = static_cast<float>(i) * (1.0f / 32.0f);
		REQUIRE(sim::detFloor(f) == std::floor(f));
		REQUIRE(sim::detTrunc(f) == std::trunc(f));
		REQUIRE(sim::detFloorToInt(f) == static_cast<std::int32_t>(std::floor(f)));
	}
}

TEST_CASE("detFloor/detTrunc at the 2^24 precondition boundary")
{
	// (review finding: the edge of the lossless-conversion guarantee was
	// untested.) Largest representable values below the exclusive bound, both
	// signs, integral and just-off-integral. 2^23 - 0.5 is representable;
	// above 2^23 the spacing is >= 1, so 2^24 - 1 is the last pre-bound float.
	const float nearBound = 16777215.0f; // 2^24 - 1
	REQUIRE(sim::detFloor(nearBound) == nearBound);
	REQUIRE(sim::detTrunc(nearBound) == nearBound);
	REQUIRE(sim::detFloorToInt(nearBound) == 16777215);
	REQUIRE(sim::detFloor(-nearBound) == -nearBound);
	REQUIRE(sim::detFloorToInt(-nearBound) == -16777215);

	const float halfStep = 8388607.5f; // 2^23 - 0.5, representable
	REQUIRE(sim::detFloor(halfStep) == 8388607.0f);
	REQUIRE(sim::detTrunc(halfStep) == 8388607.0f);
	REQUIRE(sim::detFloor(-halfStep) == -8388608.0f);
	REQUIRE(sim::detTrunc(-halfStep) == -8388607.0f);
	REQUIRE(sim::detFloorToInt(-halfStep) == -8388608);
}

TEST_CASE("detFloor edge cases")
{
	REQUIRE(sim::detFloor(0.0f) == 0.0f);
	REQUIRE(sim::detFloor(-0.0f) == 0.0f);
	REQUIRE(sim::detFloor(1.0f) == 1.0f);
	REQUIRE(sim::detFloor(-1.0f) == -1.0f);
	REQUIRE(sim::detFloor(0.5f) == 0.0f);
	REQUIRE(sim::detFloor(-0.5f) == -1.0f);
	REQUIRE(sim::detFloor(29999.96875f) == 29999.0f);
	REQUIRE(sim::detFloor(-29999.96875f) == -30000.0f);

	// Exactly-representable values adjacent to an integer.
	const float belowOne = 0.99999994f; // largest float < 1
	REQUIRE(sim::detFloor(belowOne) == 0.0f);
	REQUIRE(sim::detFloorToInt(belowOne) == 0);
}

TEST_CASE("detClamp is exact and total within range")
{
	REQUIRE(sim::detClamp(5.0f, -1.0f, 1.0f) == 1.0f);
	REQUIRE(sim::detClamp(-5.0f, -1.0f, 1.0f) == -1.0f);
	REQUIRE(sim::detClamp(0.25f, -1.0f, 1.0f) == 0.25f);
}

TEST_CASE("floatBits distinguishes representations, not just values")
{
	REQUIRE(sim::floatBits(0.0f) != sim::floatBits(-0.0f));
	REQUIRE(sim::floatBits(1.0f) == 0x3F800000u);
}
