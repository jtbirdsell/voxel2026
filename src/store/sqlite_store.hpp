// SQLite-backed default store (issue #19, ADR-0013): the zero-config
// single-file backend ADR-0003 elected for casual/single-player worlds.
// One WITHOUT-ROWID table per space, keys as BLOB primary keys (SQLite
// orders BLOBs by memcmp — the interface's scan-order contract for free),
// batches as transactions. SQLite symbols stay PRIVATE to this library;
// consumers see only KvStore.

#pragma once

#include <memory>
#include <string>

#include "store/store.hpp"

namespace world {

class SqliteStore final : public KvStore {
public:
	// path: a filesystem path, or ":memory:" for an ephemeral database.
	// Check ok() before use; failure detail is in error().
	explicit SqliteStore(const std::string &path);
	~SqliteStore() override;

	SqliteStore(const SqliteStore &) = delete;
	SqliteStore &operator=(const SqliteStore &) = delete;

	bool ok() const;
	const std::string &error() const;

	bool put(StoreSpace space, std::span<const std::byte> key,
			std::span<const std::byte> value) override;
	std::optional<std::vector<std::byte>> get(StoreSpace space,
			std::span<const std::byte> key) const override;
	bool erase(StoreSpace space, std::span<const std::byte> key) override;
	bool applyBatch(std::span<const BatchOp> ops) override;
	void scanPrefix(StoreSpace space, std::span<const std::byte> prefix,
			const Visitor &visit) const override;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace world
