// Persistence interface (issue #19, ADR-0003/0013) — the thin seam every
// backend implements and every consumer writes against. Shaped by the
// frozen contracts, not by any one database:
//
//   - SPACES are the column-family analog (ADR-0003's layout), a small
//     closed enum: chunk blobs, LOD nodes, the registry manifest, misc
//     world metadata.
//   - Keys are raw bytes, non-empty (empty keys are an API error, rejected
//     uniformly). Values are raw bytes, possibly empty (empty != absent).
//   - scanPrefix visits in LEXICOGRAPHIC BYTE ORDER — the property
//     Contract 2's big-endian sign-flipped storage keys were designed
//     against, so level/region grouping needs zero comparator code in any
//     backend.
//   - applyBatch is all-or-nothing: on false, NOTHING was applied.
//
// Threading: v1 stores are single-writer, externally synchronized — the
// async write-back layer (architecture section 5) is a later component that
// will own a store, not a property of the store.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace world {

enum class StoreSpace : std::uint8_t {
	// Contract-1 canonical blobs, keyed by world/chunk_key.hpp's
	// chunkStoreKey(x,y,z): 12 bytes, big-endian sign-flipped per axis
	// (lexicographic order == numeric order).
	kChunks = 0,
	kLodNodes = 1, // Contract-2 payloads, keyed by the 13-byte storage key
	kRegistry = 2, // the content-registry manifest (single well-known key)
	kMeta = 3,     // world manifest / format-version odds and ends
};
inline constexpr std::size_t kStoreSpaceCount = 4;

class KvStore {
public:
	virtual ~KvStore() = default;

	// False on invalid input (empty key) or backend failure.
	virtual bool put(StoreSpace space, std::span<const std::byte> key,
			std::span<const std::byte> value) = 0;

	virtual std::optional<std::vector<std::byte>> get(StoreSpace space,
			std::span<const std::byte> key) const = 0;

	// False if the key was absent (erasing nothing is reported, not hidden).
	virtual bool erase(StoreSpace space, std::span<const std::byte> key) = 0;

	// One write op: value present = put, absent = erase.
	struct BatchOp {
		StoreSpace space = StoreSpace::kMeta;
		std::vector<std::byte> key;
		std::optional<std::vector<std::byte>> value;
	};

	// Atomic: applies every op or none (false = nothing changed). An empty
	// key in ANY op fails the whole batch — that rule is itself the
	// testable atomicity probe.
	virtual bool applyBatch(std::span<const BatchOp> ops) = 0;

	// Visit every (key, value) whose key starts with `prefix` (empty prefix
	// = the whole space), in ascending lexicographic byte order; stop early
	// when the visitor returns false. LIFETIME: the spans are valid only
	// DURING the visit (they may point into backend-owned buffers) — copy
	// what you keep.
	using Visitor = std::function<bool(std::span<const std::byte> key,
			std::span<const std::byte> value)>;
	virtual void scanPrefix(StoreSpace space, std::span<const std::byte> prefix,
			const Visitor &visit) const = 0;
};

} // namespace world
