// Contract 3 schema tests: the identity widths and encodings frozen in
// docs/contracts/contract-3-identity.md, pinned against the reference codecs
// in src/world/identity.hpp. These are layout facts — if one fails, either
// the codec or the contract text is wrong, and the contract text wins only
// via RFC.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "world/identity.hpp"

TEST_CASE("entity handle packs index low and generation high, EnTT-aligned")
{
	// Golden bit pattern: the EnTT 64-bit entity convention the ECS substrate
	// will use (index in the low 32, version/generation in the high 32).
	STATIC_REQUIRE(world::makeHandle(5, 7).bits == ((7ull << 32) | 5ull));

	// Round-trip at boundary values.
	for (const std::uint32_t index : {0u, 1u, 0x7FFFFFFFu, 0xFFFFFFFEu})
		for (const std::uint32_t gen : {0u, 1u, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
			const world::EntityHandle h = world::makeHandle(index, gen);
			CAPTURE(index, gen, h.bits);
			REQUIRE(world::handleIndex(h) == index);
			REQUIRE(world::handleGeneration(h) == gen);
			REQUIRE(h != world::kInvalidHandle);
		}

	// All-ones is the canonical invalid value (EnTT null)...
	STATIC_REQUIRE(world::kInvalidHandle.bits == ~0ull);
	STATIC_REQUIRE(world::makeHandle(0xFFFFFFFFu, 0xFFFFFFFFu) == world::kInvalidHandle);
	STATIC_REQUIRE_FALSE(world::handleValid(world::kInvalidHandle));

	// ...and the index field's whole domain excludes 0xFFFFFFFF (EnTT's null
	// comparison masks to the index field, so that index collides with null
	// at EVERY generation — schema fact, not allocation policy).
	for (const std::uint32_t gen : {0u, 1u, 0x12345678u, 0xFFFFFFFEu}) {
		CAPTURE(gen);
		REQUIRE_FALSE(world::handleValid(world::makeHandle(0xFFFFFFFFu, gen)));
	}
	STATIC_REQUIRE(world::handleValid(world::makeHandle(0xFFFFFFFEu, 0xFFFFFFFFu)));
	STATIC_REQUIRE(world::handleValid(world::makeHandle(0u, 0u)));
}

TEST_CASE("UUIDv7 codec reproduces the RFC 9562 appendix example")
{
	// RFC 9562 A.6: 017F22E2-79B0-7CC3-98C4-DC0C0C07398F
	//   unix_ts_ms = 0x017F22E279B0, rand_a = 0xCC3,
	//   rand_b    = 0x18C4DC0C0C07398F (62 bits).
	const world::Uuid u =
			world::makeUuidV7(0x017F22E279B0ull, 0x0CC3, 0x18C4DC0C0C07398Full);
	REQUIRE(world::formatUuid(u) == "017f22e2-79b0-7cc3-98c4-dc0c0c07398f");
	REQUIRE(world::uuidVersion(u) == 7);
	REQUIRE(world::uuidVariantBits(u) == 0b10);
	REQUIRE(world::uuidV7TimestampMs(u) == 0x017F22E279B0ull);
}

TEST_CASE("UUIDv7 inputs are masked to their schema widths")
{
	// Oversized inputs cannot smear into neighboring fields or the
	// version/variant bits.
	const world::Uuid u = world::makeUuidV7(~0ull, 0xFFFF, ~0ull);
	REQUIRE(world::uuidVersion(u) == 7);
	REQUIRE(world::uuidVariantBits(u) == 0b10);
	REQUIRE(world::uuidV7TimestampMs(u) == 0xFFFFFFFFFFFFull);
	REQUIRE(u == world::makeUuidV7(0xFFFFFFFFFFFFull, 0x0FFF, 0x3FFFFFFFFFFFFFFFull));
}

TEST_CASE("UUIDv7 lexicographic byte order is creation-time order")
{
	const world::Uuid earlier = world::makeUuidV7(1000, 0xFFF, 0x3FFFFFFFFFFFFFFFull);
	const world::Uuid later = world::makeUuidV7(1001, 0x000, 0);
	REQUIRE(world::uuidLess(earlier, later));
	REQUIRE_FALSE(world::uuidLess(later, earlier));

	// Same millisecond: rand_a then rand_b break the tie, still total order.
	const world::Uuid a = world::makeUuidV7(1000, 0x001, 5);
	const world::Uuid b = world::makeUuidV7(1000, 0x001, 6);
	REQUIRE(world::uuidLess(a, b));
	REQUIRE_FALSE(world::uuidLess(a, a));
}

TEST_CASE("UUID parse and format round-trip; parse rejects malformed input")
{
	const world::Uuid u =
			world::makeUuidV7(0x017F22E279B0ull, 0x0CC3, 0x18C4DC0C0C07398Full);
	const std::string canonical = world::formatUuid(u);
	auto parsed = world::parseUuid(canonical);
	REQUIRE(parsed.has_value());
	REQUIRE(*parsed == u);

	// Uppercase input is accepted (RFC: hex is case-insensitive on input);
	// canonical output stays lowercase.
	auto upper = world::parseUuid("017F22E2-79B0-7CC3-98C4-DC0C0C07398F");
	REQUIRE(upper.has_value());
	REQUIRE(*upper == u);
	REQUIRE(world::formatUuid(*upper) == canonical);

	REQUIRE_FALSE(world::parseUuid("").has_value());
	REQUIRE_FALSE(world::parseUuid("017f22e2-79b0-7cc3-98c4-dc0c0c07398").has_value());
	REQUIRE_FALSE(world::parseUuid("017f22e2-79b0-7cc3-98c4-dc0c0c07398ff").has_value());
	REQUIRE_FALSE(world::parseUuid("017f22e2x79b0-7cc3-98c4-dc0c0c07398f").has_value());
	REQUIRE_FALSE(world::parseUuid("017f22e2-79b0-7cc3-98c4-dc0c0c07398g").has_value());
}

TEST_CASE("content name validation enforces the frozen charset")
{
	using world::isValidContentName;
	STATIC_REQUIRE(isValidContentName("default:stone"));
	STATIC_REQUIRE(isValidContentName("a:b"));
	STATIC_REQUIRE(isValidContentName("mod_1:thing_2"));
	STATIC_REQUIRE(isValidContentName("x0:y9"));

	STATIC_REQUIRE_FALSE(isValidContentName(""));
	STATIC_REQUIRE_FALSE(isValidContentName("nocolon"));
	STATIC_REQUIRE_FALSE(isValidContentName(":x"));
	STATIC_REQUIRE_FALSE(isValidContentName("x:"));
	STATIC_REQUIRE_FALSE(isValidContentName("a:b:c"));
	STATIC_REQUIRE_FALSE(isValidContentName("Mod:stone"));   // uppercase
	STATIC_REQUIRE_FALSE(isValidContentName("a-b:c"));       // hyphen
	STATIC_REQUIRE_FALSE(isValidContentName("a b:c"));       // space
	STATIC_REQUIRE_FALSE(isValidContentName("mod:st\xC3\xB6ne")); // non-ASCII

	// 64-char parts are the inclusive bound; 65 fails.
	const std::string part64(64, 'a');
	const std::string part65(65, 'a');
	REQUIRE(isValidContentName(part64 + ":" + part64));
	REQUIRE_FALSE(isValidContentName(part65 + ":" + part64));
	REQUIRE_FALSE(isValidContentName(part64 + ":" + part65));
}

TEST_CASE("content id zero is the reserved unknown placeholder")
{
	STATIC_REQUIRE(world::kContentUnknown == 0u);
}
