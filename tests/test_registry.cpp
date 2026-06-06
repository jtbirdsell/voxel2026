// Issue #18: identity-registry policy tests over the frozen Contract-3
// codecs — permanent name-id bindings with tombstone/revival, manifest
// round-trips with full rejection, generation-bump release semantics, the
// index-domain boundary rule (via the test seam), and the UUID resolve
// paths.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "world/identity.hpp"
#include "world/registry.hpp"

TEST_CASE("content registry: dense ids, permanent bindings, tombstone revival")
{
	world::ContentRegistry reg;

	// Dense from 1; idempotent registration; 0 never allocated.
	REQUIRE(reg.registerContent("base:stone") == 1);
	REQUIRE(reg.registerContent("base:dirt") == 2);
	REQUIRE(reg.registerContent("base:stone") == 1);
	REQUIRE(reg.count() == 2);
	REQUIRE(reg.idOf("base:dirt") == 2);
	REQUIRE(reg.idOf("base:missing") == world::kContentUnknown);

	// Invalid names never allocate (Contract 3's charset is the gate).
	REQUIRE(reg.registerContent("Bad:Name") == world::kContentUnknown);
	REQUIRE(reg.registerContent("nocolon") == world::kContentUnknown);
	REQUIRE(reg.count() == 2);

	// Tombstone: binding survives, activity does not; revival restores the
	// SAME id (mod removed and re-added -> old world content comes back).
	REQUIRE(reg.tombstone(2));
	REQUIRE_FALSE(reg.isActive(2));
	REQUIRE(reg.nameOf(2) == "base:dirt");
	REQUIRE(reg.idOf("base:dirt") == 2);
	REQUIRE(reg.registerContent("base:dirt") == 2);
	REQUIRE(reg.isActive(2));

	// Tombstoning the reserved or out-of-range ids is refused.
	REQUIRE_FALSE(reg.tombstone(world::kContentUnknown));
	REQUIRE_FALSE(reg.tombstone(99));
	REQUIRE(reg.nameOf(world::kContentUnknown).empty());
	REQUIRE(reg.nameOf(99).empty());
}

TEST_CASE("content registry manifest round-trips and rejects malformed blobs")
{
	world::ContentRegistry reg;
	REQUIRE(reg.registerContent("base:stone") == 1);
	REQUIRE(reg.registerContent("base:dirt") == 2);
	REQUIRE(reg.registerContent("mod_x:thing") == 3);
	REQUIRE(reg.tombstone(2));

	const auto blob = reg.serialize();
	const auto back = world::ContentRegistry::deserialize(blob);
	REQUIRE(back.has_value());
	REQUIRE(back->count() == 3);
	REQUIRE(back->idOf("base:stone") == 1);
	REQUIRE(back->nameOf(2) == "base:dirt");
	REQUIRE_FALSE(back->isActive(2)); // tombstone survives the round-trip
	REQUIRE(back->isActive(3));
	// Round-trip determinism: re-serialization is byte-identical.
	REQUIRE(back->serialize() == blob);

	// Byte golden for the header (LE magic 'VXRG', version, count).
	REQUIRE(std::to_integer<std::uint8_t>(blob[0]) == 0x56); // 'V'
	REQUIRE(std::to_integer<std::uint8_t>(blob[1]) == 0x58); // 'X'
	REQUIRE(std::to_integer<std::uint8_t>(blob[2]) == 0x52); // 'R'
	REQUIRE(std::to_integer<std::uint8_t>(blob[3]) == 0x47); // 'G'
	REQUIRE(std::to_integer<std::uint8_t>(blob[4]) == 1);
	REQUIRE(std::to_integer<std::uint8_t>(blob[8]) == 3);

	// Rejections: truncation at every boundary, trailing bytes, corrupt
	// fields, invalid stored names, duplicate bindings.
	for (const std::size_t cut : {std::size_t{0}, std::size_t{4}, std::size_t{11},
				 std::size_t{13}, blob.size() - 1})
		REQUIRE_FALSE(world::ContentRegistry::deserialize({blob.data(), cut}).has_value());
	{
		auto longer = blob;
		longer.push_back(std::byte{0});
		REQUIRE_FALSE(world::ContentRegistry::deserialize(longer).has_value());
	}
	const auto corrupt = [&](std::size_t at, std::uint8_t v) {
		auto bad = blob;
		bad[at] = static_cast<std::byte>(v);
		return world::ContentRegistry::deserialize(bad).has_value();
	};
	REQUIRE_FALSE(corrupt(0, 0x00));  // magic
	REQUIRE_FALSE(corrupt(4, 2));     // version
	REQUIRE_FALSE(corrupt(12, 2));    // active flag > 1
	REQUIRE_FALSE(corrupt(15, 'B'));  // first name byte uppercase -> invalid name
	{
		// Duplicate binding: serialize a registry, then duplicate the entry
		// bytes by hand -> deserialize must refuse.
		world::ContentRegistry one;
		REQUIRE(one.registerContent("a:b") == 1);
		auto dup = one.serialize();
		const std::vector<std::byte> entry(dup.begin() + 12, dup.end());
		dup.insert(dup.end(), entry.begin(), entry.end());
		dup[8] = std::byte{2}; // count = 2
		REQUIRE_FALSE(world::ContentRegistry::deserialize(dup).has_value());
	}
}

TEST_CASE("entity slot allocator bumps generations and rejects stale handles")
{
	world::EntitySlotAllocator alloc;
	const world::EntityHandle a = alloc.allocate();
	const world::EntityHandle b = alloc.allocate();
	REQUIRE(world::handleValid(a));
	REQUIRE(world::handleIndex(a) == 0);
	REQUIRE(world::handleIndex(b) == 1);
	REQUIRE(alloc.isLive(a));
	REQUIRE(alloc.isLive(b));

	// Release validates the generation; double release is the typed gone.
	REQUIRE(alloc.release(a));
	REQUIRE_FALSE(alloc.isLive(a));
	REQUIRE_FALSE(alloc.release(a));

	// Reuse: same index, bumped generation; the stale handle stays dead.
	const world::EntityHandle a2 = alloc.allocate();
	REQUIRE(world::handleIndex(a2) == 0);
	REQUIRE(world::handleGeneration(a2) == 1);
	REQUIRE(alloc.isLive(a2));
	REQUIRE_FALSE(alloc.isLive(a));
	REQUIRE_FALSE(alloc.release(a));
	REQUIRE(alloc.isLive(a2)); // the stale release freed nothing

	// Forged/never-allocated handles are gone, not UB.
	REQUIRE_FALSE(alloc.isLive(world::makeHandle(77, 0)));
	REQUIRE_FALSE(alloc.release(world::makeHandle(77, 0)));
	REQUIRE_FALSE(alloc.isLive(world::kInvalidHandle));
	REQUIRE_FALSE(alloc.release(world::kInvalidHandle));
}

TEST_CASE("entity slot allocator never hands out the reserved index")
{
	// The Contract-3 domain rule, exercised via the test seam: start one
	// below the boundary; the second allocation must refuse, not collide
	// with null semantics.
	world::EntitySlotAllocator alloc(0xFFFFFFFEu);
	const world::EntityHandle last = alloc.allocate();
	REQUIRE(world::handleValid(last));
	REQUIRE(world::handleIndex(last) == 0xFFFFFFFEu);

	const world::EntityHandle refused = alloc.allocate();
	REQUIRE(refused == world::kInvalidHandle);

	// Released capacity at the boundary is still reusable.
	REQUIRE(alloc.release(last));
	const world::EntityHandle again = alloc.allocate();
	REQUIRE(world::handleIndex(again) == 0xFFFFFFFEu);
	REQUIRE(world::handleGeneration(again) == 1);
}

TEST_CASE("uuid index resolves both directions and respects liveness")
{
	world::EntitySlotAllocator alloc;
	world::UuidIndex index;
	const world::EntityHandle h = alloc.allocate();
	const world::Uuid u = world::makeUuidV7(1234, 5, 6);

	REQUIRE(index.bind(u, h));
	REQUIRE_FALSE(index.bind(u, alloc.allocate()));     // uuid already bound
	REQUIRE_FALSE(index.bind(world::makeUuidV7(9, 9, 9), h)); // handle bound
	REQUIRE(index.resolve(u, alloc) == h);
	REQUIRE(index.uuidOf(h, alloc).has_value());
	REQUIRE(*index.uuidOf(h, alloc) == u);

	// Death makes resolution return the typed gone IN BOTH DIRECTIONS, even
	// while the binding still exists (review finding: the two resolve paths
	// must never disagree about what is alive).
	REQUIRE(alloc.release(h));
	REQUIRE(index.resolve(u, alloc) == world::kInvalidHandle);
	REQUIRE_FALSE(index.uuidOf(h, alloc).has_value());

	REQUIRE(index.unbind(h));
	REQUIRE_FALSE(index.unbind(h));
	REQUIRE(index.resolve(u, alloc) == world::kInvalidHandle);

	// Invalid handles never bind.
	REQUIRE_FALSE(index.bind(world::makeUuidV7(1, 1, 1), world::kInvalidHandle));
}

TEST_CASE("uuid index stays coherent across slot reuse without explicit unbind")
{
	// The realistic churn pattern the review flagged: release WITHOUT
	// unbinding (the lifecycle bug an engine could have), then reuse the
	// same index. The dead binding may occupy memory, but it must never
	// produce a stale answer in either direction.
	world::EntitySlotAllocator alloc;
	world::UuidIndex index;

	const world::EntityHandle h1 = alloc.allocate();
	const world::Uuid u1 = world::makeUuidV7(111, 1, 1);
	REQUIRE(index.bind(u1, h1));
	REQUIRE(alloc.release(h1)); // no unbind — the leak path

	const world::EntityHandle h2 = alloc.allocate(); // same index, new gen
	REQUIRE(world::handleIndex(h2) == world::handleIndex(h1));
	REQUIRE(world::handleGeneration(h2) == world::handleGeneration(h1) + 1);

	// Old uuid: dead in both directions. New handle: unbound, not aliased.
	REQUIRE(index.resolve(u1, alloc) == world::kInvalidHandle);
	REQUIRE_FALSE(index.uuidOf(h1, alloc).has_value());
	REQUIRE_FALSE(index.uuidOf(h2, alloc).has_value());

	// The new incarnation can take its own uuid; the dead binding never
	// blocks it (different bits) and never resurrects.
	const world::Uuid u2 = world::makeUuidV7(222, 2, 2);
	REQUIRE(index.bind(u2, h2));
	REQUIRE(index.resolve(u2, alloc) == h2);
	REQUIRE(index.resolve(u1, alloc) == world::kInvalidHandle);
}
