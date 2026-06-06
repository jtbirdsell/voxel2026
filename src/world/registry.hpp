// Identity registry implementation (issue #18) — the allocation POLICY that
// Contract 3 deliberately left outside its schema freeze, built on the
// frozen codecs in world/identity.hpp:
//
//   ContentRegistry     name -> u32 id, world-scoped, append-only. Bindings
//                       are PERMANENT (an id never means a different name);
//                       a tombstone is an inactive flag, and re-registering
//                       a tombstoned name REVIVES the same id — so removing
//                       and re-adding a mod brings its world content back,
//                       the 1.x name-id-map semantics worlds rely on.
//                       kContentUnknown (0) is never allocated. Ids are
//                       dense from 1 in append order, so the manifest needs
//                       no explicit id column.
//   EntitySlotAllocator generation-bump policy over Contract 3's u64
//                       handles: freelist reuse with the generation bumped
//                       at RELEASE (stale handles die immediately); the
//                       index-field domain rule is enforced (0xFFFFFFFF is
//                       never handed out — testable via the firstIndex
//                       seam). Stale releases are rejected: the typed
//                       "gone" result, never a double-free.
//   UuidIndex           the documented resolve paths: persistent UUID <->
//                       live handle, with resolution validated against the
//                       allocator (a dead handle resolves to invalid).
//
// Manifest blob (little-endian, versioned like every other codec here):
//   u32 magic 'VXRG' | u8 version=1 | u8[3] reserved (write 0, read ignore)
//   u32 count | count x { u8 active | u16 nameLen | name bytes }
// Full validation on load: magic/version, exact size, name grammar, count
// and length bounds — nullopt on any violation, never partial state.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "world/identity.hpp"

namespace world {

inline constexpr std::uint32_t kRegistryMagic = 0x47525856u; // 'VXRG' LE
inline constexpr std::uint8_t kRegistryVersion = 1;
inline constexpr std::uint32_t kRegistryMaxEntries = 0xFFFFFFFEu; // ids 1..max

class ContentRegistry {
public:
	// Returns the id bound to `name`, allocating (or reviving) as needed;
	// kContentUnknown on invalid names or id-space exhaustion.
	ContentId registerContent(std::string_view name)
	{
		if (!isValidContentName(name))
			return kContentUnknown;
		if (const auto it = m_byName.find(name); it != m_byName.end()) {
			m_entries[it->second - 1].active = true; // revival is idempotent
			return it->second;
		}
		if (m_entries.size() >= kRegistryMaxEntries)
			return kContentUnknown;
		m_entries.push_back(Entry{std::string(name), true});
		const auto id = static_cast<ContentId>(m_entries.size()); // dense from 1
		m_byName.emplace(m_entries.back().name, id);
		return id;
	}

	// Tombstone: the binding survives (lookups still name it), the content
	// is inactive (renderers placeholder it). False on unknown/invalid id.
	bool tombstone(ContentId id)
	{
		if (id == kContentUnknown || id > m_entries.size())
			return false;
		m_entries[id - 1].active = false;
		return true;
	}

	ContentId idOf(std::string_view name) const
	{
		const auto it = m_byName.find(name); // heterogeneous via less<>
		return it == m_byName.end() ? kContentUnknown : it->second;
	}

	// The permanent binding: valid ids always resolve to their name, active
	// or not. Empty view for kContentUnknown / out-of-range. LIFETIME: the
	// view is valid only until the next registerContent() — entry storage
	// can reallocate, and short (SSO) strings move their bytes with it.
	// Re-fetch after mutation; copy if you must hold it.
	std::string_view nameOf(ContentId id) const
	{
		if (id == kContentUnknown || id > m_entries.size())
			return {};
		return m_entries[id - 1].name;
	}

	bool isActive(ContentId id) const
	{
		return id != kContentUnknown && id <= m_entries.size() && m_entries[id - 1].active;
	}

	std::uint32_t count() const { return static_cast<std::uint32_t>(m_entries.size()); }

	std::vector<std::byte> serialize() const
	{
		std::vector<std::byte> out;
		const auto putU32 = [&out](std::uint32_t v) {
			for (int i = 0; i < 4; ++i)
				out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
		};
		putU32(kRegistryMagic);
		out.push_back(static_cast<std::byte>(kRegistryVersion));
		out.push_back(std::byte{0});
		out.push_back(std::byte{0});
		out.push_back(std::byte{0});
		putU32(static_cast<std::uint32_t>(m_entries.size()));
		for (const Entry &e : m_entries) {
			out.push_back(static_cast<std::byte>(e.active ? 1 : 0));
			const auto len = static_cast<std::uint16_t>(e.name.size());
			out.push_back(static_cast<std::byte>(len & 0xFF));
			out.push_back(static_cast<std::byte>(len >> 8));
			for (const char c : e.name)
				out.push_back(static_cast<std::byte>(c));
		}
		return out;
	}

	static std::optional<ContentRegistry> deserialize(std::span<const std::byte> blob)
	{
		const auto getU32 = [&blob](std::size_t at) {
			std::uint32_t v = 0;
			for (int i = 0; i < 4; ++i)
				v |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(blob[at + i]))
						<< (8 * i);
			return v;
		};
		if (blob.size() < 12 || getU32(0) != kRegistryMagic ||
				std::to_integer<std::uint8_t>(blob[4]) != kRegistryVersion)
			return std::nullopt;
		const std::uint32_t count = getU32(8);
		if (count > kRegistryMaxEntries)
			return std::nullopt;
		ContentRegistry reg;
		std::size_t at = 12;
		for (std::uint32_t i = 0; i < count; ++i) {
			if (blob.size() < at + 3)
				return std::nullopt;
			const std::uint8_t active = std::to_integer<std::uint8_t>(blob[at]);
			if (active > 1)
				return std::nullopt;
			const std::uint16_t len = static_cast<std::uint16_t>(
					std::to_integer<std::uint8_t>(blob[at + 1]) |
					(std::to_integer<std::uint8_t>(blob[at + 2]) << 8));
			at += 3;
			if (blob.size() < at + len)
				return std::nullopt;
			std::string name(len, '\0');
			for (std::uint16_t k = 0; k < len; ++k)
				name[k] = static_cast<char>(std::to_integer<std::uint8_t>(blob[at + k]));
			at += len;
			if (!isValidContentName(name))
				return std::nullopt;
			if (reg.m_byName.contains(name))
				return std::nullopt; // duplicate binding: malformed manifest
			reg.m_entries.push_back(Entry{std::move(name), active == 1});
			reg.m_byName.emplace(reg.m_entries.back().name,
					static_cast<ContentId>(reg.m_entries.size()));
		}
		if (at != blob.size())
			return std::nullopt; // trailing bytes
		return reg;
	}

private:
	struct Entry {
		std::string name;
		bool active = false;
	};
	std::vector<Entry> m_entries;
	std::map<std::string, ContentId, std::less<>> m_byName;
};

class EntitySlotAllocator {
public:
	// firstIndex is a TEST SEAM: it lets the index-domain boundary rule
	// (0xFFFFFFFF is never allocated) be exercised without 4 billion
	// allocations. Engine code uses the default.
	explicit EntitySlotAllocator(std::uint32_t firstIndex = 0)
			: m_firstIndex(firstIndex), m_nextIndex(firstIndex)
	{
	}

	// kInvalidHandle when the index domain is exhausted (the contract's
	// boundary rule, enforced rather than assumed).
	EntityHandle allocate()
	{
		if (!m_free.empty()) {
			const std::uint32_t index = m_free.back();
			m_free.pop_back();
			const std::size_t s = index - m_firstIndex;
			m_live[s] = true;
			return makeHandle(index, m_generation[s]);
		}
		if (m_nextIndex == 0xFFFFFFFFu)
			return kInvalidHandle; // index domain boundary: never hand out
		const std::uint32_t index = m_nextIndex++;
		m_generation.push_back(0);
		m_live.push_back(true);
		return makeHandle(index, 0);
	}

	// Generation-validated release: stale, dead, or never-allocated handles
	// return false (the typed "gone" — a double release cannot free someone
	// else's slot). The generation bumps HERE, so stale handles die
	// immediately; ABA needs 2^32 reuse cycles of one slot (documented).
	bool release(EntityHandle h)
	{
		const auto s = liveSlot(h);
		if (!s.has_value())
			return false;
		m_live[*s] = false;
		++m_generation[*s];
		m_free.push_back(handleIndex(h));
		return true;
	}

	bool isLive(EntityHandle h) const { return liveSlot(h).has_value(); }

private:
	std::optional<std::size_t> liveSlot(EntityHandle h) const
	{
		if (!handleValid(h) || handleIndex(h) < m_firstIndex)
			return std::nullopt;
		const std::size_t s = handleIndex(h) - m_firstIndex;
		if (s >= m_generation.size() || !m_live[s] ||
				m_generation[s] != handleGeneration(h))
			return std::nullopt;
		return s;
	}

	std::uint32_t m_firstIndex = 0;
	std::uint32_t m_nextIndex = 0;
	std::vector<std::uint32_t> m_generation;
	std::vector<bool> m_live;
	std::vector<std::uint32_t> m_free;
};

// Persistent-UUID <-> live-handle resolve paths (Contract 3 invariant 5).
// LIFECYCLE (review finding, stated plainly): the index does not observe
// the allocator — releasing a handle does NOT remove its binding, and dead
// bindings accumulate until unbind(). Entity destruction MUST unbind as
// part of teardown; both resolve directions validate liveness so a missed
// unbind degrades to memory growth, never to a stale answer.
class UuidIndex {
public:
	// False if the uuid or handle is already bound (bindings are 1:1).
	bool bind(const Uuid &uuid, EntityHandle h)
	{
		if (!handleValid(h) || m_byUuid.contains(uuid) || m_byHandle.contains(h.bits))
			return false;
		m_byUuid.emplace(uuid, h);
		m_byHandle.emplace(h.bits, uuid);
		return true;
	}

	bool unbind(EntityHandle h)
	{
		const auto it = m_byHandle.find(h.bits);
		if (it == m_byHandle.end())
			return false;
		m_byUuid.erase(it->second);
		m_byHandle.erase(it);
		return true;
	}

	// UUID -> current handle, validated against the allocator: a dead or
	// stale binding resolves to kInvalidHandle (the "gone" result).
	EntityHandle resolve(const Uuid &uuid, const EntitySlotAllocator &alloc) const
	{
		const auto it = m_byUuid.find(uuid);
		if (it == m_byUuid.end() || !alloc.isLive(it->second))
			return kInvalidHandle;
		return it->second;
	}

	// Handle -> UUID, liveness-validated like resolve() (review finding: a
	// dead handle must read as gone in BOTH directions, or the two resolve
	// paths disagree about what exists).
	std::optional<Uuid> uuidOf(EntityHandle h, const EntitySlotAllocator &alloc) const
	{
		if (!alloc.isLive(h))
			return std::nullopt;
		const auto it = m_byHandle.find(h.bits);
		if (it == m_byHandle.end())
			return std::nullopt;
		return it->second;
	}

private:
	struct UuidLessCmp {
		bool operator()(const Uuid &a, const Uuid &b) const { return uuidLess(a, b); }
	};
	std::map<Uuid, EntityHandle, UuidLessCmp> m_byUuid;
	std::map<std::uint64_t, Uuid> m_byHandle;
};

} // namespace world
