// Contract 3 reference codecs — the compiled anchor for the identity freeze
// (docs/contracts/contract-3-identity.md). Header-only; the bit-layout
// codecs are constexpr (so layout invariants are compile-time facts, pinned
// with STATIC_REQUIRE), while format/parse are plain runtime utilities:
//
//   - ContentId:    u32, world-scoped append-only registry; 0 reserved as the
//                   converter's "unknown content" placeholder.
//   - EntityHandle: u64 {generation:32 | index:32}, EnTT-aligned (index in
//                   the low half, all-ones = invalid/null).
//   - Uuid:         16 bytes; v7 layout per RFC 9562 (48-bit unix-ms
//                   timestamp, ver=7, 12-bit rand_a, var=10, 62-bit rand_b),
//                   lexicographic byte order == creation-time order.
//
// Generation/randomness POLICY (who bumps generations, where entropy comes
// from) is engine code, not schema — this header deliberately contains no
// RNG and no clock.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace world {

// ---- Content identity ------------------------------------------------------

using ContentId = std::uint32_t;

// Reserved: foreign/unknown content maps here (one-way converter semantics);
// a registry never allocates it.
inline constexpr ContentId kContentUnknown = 0;

// `namespace:name` validation: each part is [a-z0-9_]{1,64}, exactly one
// colon, ASCII only. The namespace part equals Luanti 1.x's
// MODNAME_ALLOWED_CHARS exactly; the name part is deliberately STRICTER
// than 1.x (which permits uppercase via Lua %w) — lowercase-only canonical
// identity, with the one-way converter case-folding/disambiguating 1.x
// names that fall outside it. Total bound follows from the parts (<= 129).
constexpr bool isValidContentName(std::string_view name) noexcept
{
	const std::size_t colon = name.find(':');
	if (colon == std::string_view::npos || name.find(':', colon + 1) != std::string_view::npos)
		return false;
	const std::string_view ns = name.substr(0, colon);
	const std::string_view id = name.substr(colon + 1);
	const auto okPart = [](std::string_view part) {
		if (part.empty() || part.size() > 64)
			return false;
		for (const char c : part)
			if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
				return false;
		return true;
	};
	return okPart(ns) && okPart(id);
}

// ---- Entity identity --------------------------------------------------------

// u64 runtime handle, EnTT-aligned for the planned ECS substrate: index in
// the LOW 32 bits, generation in the HIGH 32 (EnTT's 64-bit entity traits),
// all-ones as the canonical invalid value (EnTT's null). The index field's
// DOMAIN is [0, 0xFFFFFFFE]: EnTT's null comparison masks to the index
// field, so any handle with index 0xFFFFFFFF collides with null semantics
// at every generation — a schema fact about the field, pinned by
// handleValid(), not an allocation policy.
struct EntityHandle {
	std::uint64_t bits = ~0ull;

	friend constexpr bool operator==(EntityHandle, EntityHandle) noexcept = default;
};

inline constexpr EntityHandle kInvalidHandle{};

constexpr EntityHandle makeHandle(std::uint32_t index, std::uint32_t generation) noexcept
{
	return EntityHandle{(static_cast<std::uint64_t>(generation) << 32) | index};
}

constexpr std::uint32_t handleIndex(EntityHandle h) noexcept
{
	return static_cast<std::uint32_t>(h.bits & 0xFFFFFFFFull);
}

constexpr std::uint32_t handleGeneration(EntityHandle h) noexcept
{
	return static_cast<std::uint32_t>(h.bits >> 32);
}

// True iff the handle's index lies inside the field's domain. This is the
// schema-level validity test; generation-recency validation (is this handle
// stale?) is registry behavior, outside the schema.
constexpr bool handleValid(EntityHandle h) noexcept
{
	return handleIndex(h) != 0xFFFFFFFFu;
}

// ---- Player / persistent-entity identity (UUIDv7, RFC 9562) -----------------

struct Uuid {
	std::array<std::uint8_t, 16> bytes{};

	friend constexpr bool operator==(const Uuid &, const Uuid &) noexcept = default;
};

// Assemble a v7 UUID from its three fields. Inputs are masked to their
// schema widths (48 / 12 / 62 bits); version and variant bits are supplied
// by the codec, never by the caller.
constexpr Uuid makeUuidV7(std::uint64_t unixMs, std::uint16_t randA,
		std::uint64_t randB) noexcept
{
	unixMs &= 0xFFFFFFFFFFFFull;       // 48 bits
	randA &= 0x0FFF;                   // 12 bits
	randB &= 0x3FFFFFFFFFFFFFFFull;    // 62 bits
	Uuid u;
	u.bytes[0] = static_cast<std::uint8_t>(unixMs >> 40);
	u.bytes[1] = static_cast<std::uint8_t>(unixMs >> 32);
	u.bytes[2] = static_cast<std::uint8_t>(unixMs >> 24);
	u.bytes[3] = static_cast<std::uint8_t>(unixMs >> 16);
	u.bytes[4] = static_cast<std::uint8_t>(unixMs >> 8);
	u.bytes[5] = static_cast<std::uint8_t>(unixMs);
	u.bytes[6] = static_cast<std::uint8_t>(0x70 | (randA >> 8)); // ver=7
	u.bytes[7] = static_cast<std::uint8_t>(randA);
	u.bytes[8] = static_cast<std::uint8_t>(0x80 | ((randB >> 56) & 0x3F)); // var=10
	u.bytes[9] = static_cast<std::uint8_t>(randB >> 48);
	u.bytes[10] = static_cast<std::uint8_t>(randB >> 40);
	u.bytes[11] = static_cast<std::uint8_t>(randB >> 32);
	u.bytes[12] = static_cast<std::uint8_t>(randB >> 24);
	u.bytes[13] = static_cast<std::uint8_t>(randB >> 16);
	u.bytes[14] = static_cast<std::uint8_t>(randB >> 8);
	u.bytes[15] = static_cast<std::uint8_t>(randB);
	return u;
}

constexpr std::uint8_t uuidVersion(const Uuid &u) noexcept
{
	return static_cast<std::uint8_t>(u.bytes[6] >> 4);
}

// RFC 9562 variant field: the two most significant bits of byte 8 (0b10 for
// every UUID this engine mints).
constexpr std::uint8_t uuidVariantBits(const Uuid &u) noexcept
{
	return static_cast<std::uint8_t>(u.bytes[8] >> 6);
}

constexpr std::uint64_t uuidV7TimestampMs(const Uuid &u) noexcept
{
	return (static_cast<std::uint64_t>(u.bytes[0]) << 40) |
			(static_cast<std::uint64_t>(u.bytes[1]) << 32) |
			(static_cast<std::uint64_t>(u.bytes[2]) << 24) |
			(static_cast<std::uint64_t>(u.bytes[3]) << 16) |
			(static_cast<std::uint64_t>(u.bytes[4]) << 8) |
			static_cast<std::uint64_t>(u.bytes[5]);
}

// Lexicographic byte comparison — for v7 UUIDs this IS creation-time order
// (the property that motivated choosing v7 for player keys).
constexpr bool uuidLess(const Uuid &a, const Uuid &b) noexcept
{
	for (std::size_t i = 0; i < 16; ++i)
		if (a.bytes[i] != b.bytes[i])
			return a.bytes[i] < b.bytes[i];
	return false;
}

// Canonical form: lowercase 8-4-4-4-12. Parse accepts either case (RFC 9562
// section 4: hex digits are case-insensitive on input); format always emits
// the canonical lowercase form.
inline std::string formatUuid(const Uuid &u)
{
	constexpr char kHex[] = "0123456789abcdef";
	std::string out;
	out.reserve(36);
	for (std::size_t i = 0; i < 16; ++i) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			out.push_back('-');
		out.push_back(kHex[u.bytes[i] >> 4]);
		out.push_back(kHex[u.bytes[i] & 0x0F]);
	}
	return out;
}

inline std::optional<Uuid> parseUuid(std::string_view text)
{
	if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' ||
			text[23] != '-')
		return std::nullopt;
	const auto nibble = [](char c) -> int {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	};
	Uuid u;
	std::size_t at = 0;
	for (std::size_t i = 0; i < 36;) {
		if (text[i] == '-') {
			++i;
			continue;
		}
		const int hi = nibble(text[i]);
		const int lo = nibble(text[i + 1]);
		if (hi < 0 || lo < 0)
			return std::nullopt;
		u.bytes[at++] = static_cast<std::uint8_t>((hi << 4) | lo);
		i += 2;
	}
	return u;
}

} // namespace world
