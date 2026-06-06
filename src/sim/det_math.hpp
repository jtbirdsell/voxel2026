// Deterministic float math for the shared simulation step (Contract 5).
//
// Cross-build bit-identity rests on using ONLY operations whose results
// IEEE 754 fully determines:
//   - +, -, *, / and comparisons (correctly rounded by definition),
//   - int<->float conversions within exact range,
//   - the bit-manipulation helpers below.
// Transcendentals (sin/cos/exp/pow) are libm-implementation-defined and are
// BANNED from pinned simulation translation units. sqrt is correctly rounded
// per IEEE 754 and would be permissible, but the kernel does not need it.
//
// HEADER RULE (review finding): these helpers are header-inline and therefore
// also get compiled into UNPINNED translation units that include this header;
// the linker may keep any one COMDAT instance. Every function in this header
// must remain free of contractable (a*b + c) and reassociable expressions so
// that an unpinned instantiation is necessarily bit-identical to a pinned
// one. FP expressions with fusible products belong in the pinned .cpp files,
// never here.
//
// The guards below fail the build loudly on any platform where float
// arithmetic would not be IEC 559 binary32 evaluated at declared precision.
// Note this catches x87 EXCESS PRECISION (GCC/Clang targeting the x87 ABI,
// FLT_EVAL_METHOD == 2) — not 32-bit x86 per se; SSE2 32-bit builds
// legitimately pass.

#pragma once

#include <bit>
#include <cassert>
#include <cfloat>
#include <cstdint>
#include <limits>

static_assert(std::numeric_limits<float>::is_iec559,
		"Contract 5 requires IEC 559 (IEEE 754) binary32 floats");
static_assert(sizeof(float) == 4, "float must be binary32");

#if !defined(FLT_EVAL_METHOD)
#error "Contract 5 requires FLT_EVAL_METHOD to be defined (non-conforming <cfloat>)"
#elif FLT_EVAL_METHOD != 0 && !defined(VOXEL2026_SIM_UNPINNED_CONTROL)
// The determinism NEGATIVE CONTROL build is exempt by design: it exists to be
// non-conforming (MSVC /fp:fast reports FLT_EVAL_METHOD == -1), and its test
// asserts that its trajectories DIVERGE from the goldens. Everything else
// must satisfy the contract.
#error "Contract 5 requires FLT_EVAL_METHOD == 0 (no excess precision; x87 is unsupported)"
#endif

namespace sim {

// Exclusive bound of the integer range in which float->int32 truncation is
// guaranteed exact and lossless both ways (2^24; binary32 has 24-bit mantissa).
inline constexpr float kExactIntBound = 16777216.0f;

// Truncate toward zero. Bit-deterministic: built from an exact int conversion.
// Precondition: f finite and |f| < kExactIntBound — asserted (debug builds)
// because float->int conversion outside int range is UB with
// compiler-divergent behavior, which would silently corrupt a determinism
// verdict. The range comparison is false for NaN, so the assert also catches
// non-finite inputs.
constexpr float detTrunc(float f)
{
	assert(f < kExactIntBound && f > -kExactIntBound);
	return static_cast<float>(static_cast<std::int32_t>(f));
}

// Largest integer-valued float <= f. Same preconditions as detTrunc.
constexpr float detFloor(float f)
{
	const float t = detTrunc(f);
	return (t > f) ? t - 1.0f : t;
}

// Floor directly to int32. Same preconditions as detTrunc.
constexpr std::int32_t detFloorToInt(float f)
{
	assert(f < kExactIntBound && f > -kExactIntBound);
	const std::int32_t t = static_cast<std::int32_t>(f);
	return (static_cast<float>(t) > f) ? t - 1 : t;
}

// Branch-deterministic clamp. NOTE: NaN propagates (both comparisons false);
// this is a clamp, not a sanitizer — the conversion-site asserts above are
// the NaN tripwire.
constexpr float detClamp(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

// Bit pattern for hashing — total over all floats including NaN payloads and
// signed zero, so trajectory hashes compare *representations*, not values.
inline std::uint32_t floatBits(float f)
{
	return std::bit_cast<std::uint32_t>(f);
}

} // namespace sim
