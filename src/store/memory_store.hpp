// In-memory reference backend (issue #19): the conformance ORACLE. Plain
// std::map over byte vectors — std::vector<std::byte>'s operator< is
// lexicographic element order, i.e. exactly the interface's scan-order
// contract — so this backend is small enough to be obviously correct, and
// the conformance suite requires every real backend to agree with it.

#pragma once

#include <cstring>
#include <map>
#include <vector>

#include "store/store.hpp"

namespace world {

class MemoryStore final : public KvStore {
public:
	bool put(StoreSpace space, std::span<const std::byte> key,
			std::span<const std::byte> value) override
	{
		if (key.empty())
			return false;
		m_spaces[index(space)][toVec(key)] = toVec(value);
		return true;
	}

	std::optional<std::vector<std::byte>> get(StoreSpace space,
			std::span<const std::byte> key) const override
	{
		const auto &map = m_spaces[index(space)];
		const auto it = map.find(toVec(key));
		if (it == map.end())
			return std::nullopt;
		return it->second;
	}

	bool erase(StoreSpace space, std::span<const std::byte> key) override
	{
		return m_spaces[index(space)].erase(toVec(key)) > 0;
	}

	bool applyBatch(std::span<const BatchOp> ops) override
	{
		for (const BatchOp &op : ops)
			if (op.key.empty())
				return false; // nothing applied
		for (const BatchOp &op : ops) {
			if (op.value.has_value())
				m_spaces[index(op.space)][op.key] = *op.value;
			else
				m_spaces[index(op.space)].erase(op.key);
		}
		return true;
	}

	void scanPrefix(StoreSpace space, std::span<const std::byte> prefix,
			const Visitor &visit) const override
	{
		const auto &map = m_spaces[index(space)];
		for (auto it = map.lower_bound(toVec(prefix)); it != map.end(); ++it) {
			const std::vector<std::byte> &k = it->first;
			// Explicit memcmp with the checked size (GCC 13's
			// -Wstringop-overread false-fires on the 3-arg std::equal here,
			// modeling the span-derived bound as possibly negative).
			if (k.size() < prefix.size() ||
					(!prefix.empty() &&
							std::memcmp(k.data(), prefix.data(), prefix.size()) != 0))
				break; // past the prefix range (keys are ordered)
			if (!visit(k, it->second))
				return;
		}
	}

private:
	static std::size_t index(StoreSpace s) { return static_cast<std::size_t>(s); }
	static std::vector<std::byte> toVec(std::span<const std::byte> s)
	{
		return std::vector<std::byte>(s.begin(), s.end());
	}

	std::map<std::vector<std::byte>, std::vector<std::byte>> m_spaces[kStoreSpaceCount];
};

} // namespace world
