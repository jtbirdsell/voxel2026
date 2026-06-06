#include "store/sqlite_store.hpp"

#include <array>

#include <sqlite3.h>

namespace world {

namespace {

constexpr const char *kTableNames[kStoreSpaceCount] = {
		"kv_chunks", "kv_lod", "kv_registry", "kv_meta"};

// Smallest byte string strictly greater than every key with `prefix`:
// increment the last non-0xFF byte and truncate. Empty result = unbounded
// (all-0xFF or empty prefix scans to the end of the space).
std::vector<std::byte> prefixUpperBound(std::span<const std::byte> prefix)
{
	std::vector<std::byte> upper(prefix.begin(), prefix.end());
	while (!upper.empty()) {
		if (upper.back() != std::byte{0xFF}) {
			upper.back() = static_cast<std::byte>(
					std::to_integer<std::uint8_t>(upper.back()) + 1);
			return upper;
		}
		upper.pop_back();
	}
	return upper;
}

} // namespace

struct SqliteStore::Impl {
	sqlite3 *db = nullptr;
	std::string error;

	// Prepared per space: get / put (UPSERT) / erase.
	std::array<sqlite3_stmt *, kStoreSpaceCount> stGet{};
	std::array<sqlite3_stmt *, kStoreSpaceCount> stPut{};
	std::array<sqlite3_stmt *, kStoreSpaceCount> stDel{};

	~Impl()
	{
		for (auto &arr : {&stGet, &stPut, &stDel})
			for (sqlite3_stmt *s : *arr)
				if (s)
					sqlite3_finalize(s);
		if (db)
			sqlite3_close(db);
	}

	bool fail(const char *what)
	{
		error = std::string(what) + ": " +
				(db ? sqlite3_errmsg(db) : "no database handle");
		return false;
	}

	bool open(const std::string &path)
	{
		// Header-vs-runtime version agreement (review finding): a DLL/so
		// search-path override substituting a different SQLite than the
		// pinned amalgamation must fail loudly, not corrupt subtly.
		if (sqlite3_libversion_number() != SQLITE_VERSION_NUMBER) {
			error = "SQLite runtime/header version mismatch";
			return false;
		}
		if (sqlite3_open_v2(path.c_str(), &db,
					SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
			return fail("open");
		// WITHOUT ROWID: the BLOB key IS the b-tree key — memcmp order, no
		// shadow rowid, which is the interface's scan-order contract.
		for (std::size_t s = 0; s < kStoreSpaceCount; ++s) {
			const std::string create = std::string("CREATE TABLE IF NOT EXISTS ") +
					kTableNames[s] + " (k BLOB PRIMARY KEY, v BLOB NOT NULL) WITHOUT ROWID";
			if (sqlite3_exec(db, create.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
				return fail("create table");
		}
		for (std::size_t s = 0; s < kStoreSpaceCount; ++s) {
			const std::string table = kTableNames[s];
			const std::string qGet = "SELECT v FROM " + table + " WHERE k = ?1";
			const std::string qPut = "INSERT INTO " + table +
					" (k, v) VALUES (?1, ?2) ON CONFLICT(k) DO UPDATE SET v = ?2";
			const std::string qDel = "DELETE FROM " + table + " WHERE k = ?1";
			if (sqlite3_prepare_v2(db, qGet.c_str(), -1, &stGet[s], nullptr) != SQLITE_OK ||
					sqlite3_prepare_v2(db, qPut.c_str(), -1, &stPut[s], nullptr) !=
							SQLITE_OK ||
					sqlite3_prepare_v2(db, qDel.c_str(), -1, &stDel[s], nullptr) !=
							SQLITE_OK)
				return fail("prepare");
		}
		return true;
	}

	// One put/erase against the prepared statements; shared by the single
	// ops and the batch path.
	bool execPut(StoreSpace space, std::span<const std::byte> key,
			std::span<const std::byte> value)
	{
		sqlite3_stmt *st = stPut[static_cast<std::size_t>(space)];
		sqlite3_reset(st);
		if (sqlite3_bind_blob(st, 1, key.data(), static_cast<int>(key.size()),
					SQLITE_STATIC) != SQLITE_OK ||
				sqlite3_bind_blob(st, 2, value.empty() ? "" : reinterpret_cast<const char *>(
															  value.data()),
						static_cast<int>(value.size()), SQLITE_STATIC) != SQLITE_OK)
			return fail("bind");
		const int rc = sqlite3_step(st);
		sqlite3_clear_bindings(st);
		return rc == SQLITE_DONE || fail("put step");
	}

	bool execErase(StoreSpace space, std::span<const std::byte> key, bool &existed)
	{
		sqlite3_stmt *st = stDel[static_cast<std::size_t>(space)];
		sqlite3_reset(st);
		if (sqlite3_bind_blob(st, 1, key.data(), static_cast<int>(key.size()),
					SQLITE_STATIC) != SQLITE_OK)
			return fail("bind");
		const int rc = sqlite3_step(st);
		sqlite3_clear_bindings(st);
		if (rc != SQLITE_DONE)
			return fail("erase step");
		existed = sqlite3_changes(db) > 0;
		return true;
	}
};

SqliteStore::SqliteStore(const std::string &path) : m_impl(std::make_unique<Impl>())
{
	m_impl->open(path);
}

SqliteStore::~SqliteStore() = default;

bool SqliteStore::ok() const
{
	return m_impl->error.empty();
}

const std::string &SqliteStore::error() const
{
	return m_impl->error;
}

bool SqliteStore::put(StoreSpace space, std::span<const std::byte> key,
		std::span<const std::byte> value)
{
	if (key.empty() || !ok())
		return false;
	return m_impl->execPut(space, key, value);
}

std::optional<std::vector<std::byte>> SqliteStore::get(StoreSpace space,
		std::span<const std::byte> key) const
{
	if (key.empty() || !ok())
		return std::nullopt;
	sqlite3_stmt *st = m_impl->stGet[static_cast<std::size_t>(space)];
	sqlite3_reset(st);
	if (sqlite3_bind_blob(st, 1, key.data(), static_cast<int>(key.size()),
				SQLITE_STATIC) != SQLITE_OK)
		return std::nullopt;
	std::optional<std::vector<std::byte>> result;
	if (sqlite3_step(st) == SQLITE_ROW) {
		// sqlite3_column_blob returns nullptr for zero-length BLOBs (review
		// finding): branch rather than doing arithmetic on a null pointer —
		// null+0 is defined in modern C++, but a world-storage layer does
		// not trade on "arguably defined".
		const int size = sqlite3_column_bytes(st, 0);
		if (size > 0) {
			const auto *data =
					static_cast<const std::byte *>(sqlite3_column_blob(st, 0));
			result.emplace(data, data + size);
		} else {
			result.emplace();
		}
	}
	sqlite3_clear_bindings(st);
	return result;
}

bool SqliteStore::erase(StoreSpace space, std::span<const std::byte> key)
{
	if (key.empty() || !ok())
		return false;
	bool existed = false;
	if (!m_impl->execErase(space, key, existed))
		return false;
	return existed;
}

bool SqliteStore::applyBatch(std::span<const BatchOp> ops)
{
	if (!ok())
		return false;
	for (const BatchOp &op : ops)
		if (op.key.empty())
			return false; // uniform rule: nothing applied
	if (sqlite3_exec(m_impl->db, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
		return m_impl->fail("begin");
	for (const BatchOp &op : ops) {
		bool stepOk;
		if (op.value.has_value()) {
			stepOk = m_impl->execPut(op.space, op.key, *op.value);
		} else {
			bool existedIgnored = false;
			stepOk = m_impl->execErase(op.space, op.key, existedIgnored);
		}
		if (!stepOk) {
			// Statements keep their (cleared) state; every op resets before
			// binding, so no rollback-specific statement cleanup is needed —
			// noted explicitly so the invariant is a decision, not luck.
			sqlite3_exec(m_impl->db, "ROLLBACK", nullptr, nullptr, nullptr);
			return false;
		}
	}
	if (sqlite3_exec(m_impl->db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
		sqlite3_exec(m_impl->db, "ROLLBACK", nullptr, nullptr, nullptr);
		return m_impl->fail("commit");
	}
	return true;
}

void SqliteStore::scanPrefix(StoreSpace space, std::span<const std::byte> prefix,
		const Visitor &visit) const
{
	if (!ok())
		return;
	const std::vector<std::byte> upper = prefixUpperBound(prefix);
	const std::string table = kTableNames[static_cast<std::size_t>(space)];
	const std::string sql = upper.empty()
			? "SELECT k, v FROM " + table + " WHERE k >= ?1 ORDER BY k"
			: "SELECT k, v FROM " + table + " WHERE k >= ?1 AND k < ?2 ORDER BY k";
	sqlite3_stmt *st = nullptr;
	if (sqlite3_prepare_v2(m_impl->db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
		return;
	// An empty prefix binds a zero-length BLOB lower bound (>= '' is all keys).
	sqlite3_bind_blob(st, 1, prefix.empty() ? "" : reinterpret_cast<const char *>(
													   prefix.data()),
			static_cast<int>(prefix.size()), SQLITE_STATIC);
	if (!upper.empty())
		sqlite3_bind_blob(st, 2, upper.data(), static_cast<int>(upper.size()),
				SQLITE_STATIC);
	while (sqlite3_step(st) == SQLITE_ROW) {
		const auto *k = static_cast<const std::byte *>(sqlite3_column_blob(st, 0));
		const int kn = sqlite3_column_bytes(st, 0);
		const auto *v = static_cast<const std::byte *>(sqlite3_column_blob(st, 1));
		const int vn = sqlite3_column_bytes(st, 1);
		if (!visit({k, static_cast<std::size_t>(kn)}, {v, static_cast<std::size_t>(vn)}))
			break;
	}
	sqlite3_finalize(st);
}

} // namespace world
