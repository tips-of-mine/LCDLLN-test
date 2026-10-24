// MysqlMapDiscoveryStore — implémentation (prepared statements, pattern des
// autres stores masterd). Voir le header.

#include "src/masterd/mapdiscovery/MysqlMapDiscoveryStore.h"

#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"
#include "src/shared/core/Log.h"

#include <mysql.h>

#include <span>
#include <string>

namespace engine::server
{
	namespace disc = engine::world::discovery;

	bool MysqlMapDiscoveryStore::IsAvailable() const
	{
		return m_pool != nullptr && m_pool->IsInitialized();
	}

	bool MysqlMapDiscoveryStore::ListPage(uint64_t characterId, uint32_t startIndex, uint32_t maxCount,
		std::vector<StoredCell>& outCells, uint32_t& outTotal)
	{
		outCells.clear();
		outTotal = 0;
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* countStmt = cache->Acquire(mysql,
			"SELECT COUNT(*) FROM character_map_discovery WHERE character_id = ?");
		if (!countStmt || !countStmt->Bind(0, characterId) || !countStmt->Execute() || !countStmt->FetchRow())
		{
			LOG_WARN(Db, "[MysqlMapDiscoveryStore] ListPage count({}) failed: {}", characterId, mysql_error(mysql));
			return false;
		}
		outTotal = static_cast<uint32_t>(countStmt->GetUInt64(0));
		while (countStmt->FetchRow()) {}

		auto* stmt = cache->Acquire(mysql,
			"SELECT cell_zone_id, bits FROM character_map_discovery "
			"WHERE character_id = ? ORDER BY cell_zone_id ASC LIMIT ?, ?");
		if (!stmt || !stmt->Bind(0, characterId) || !stmt->Bind(1, startIndex) || !stmt->Bind(2, maxCount)
			|| !stmt->Execute())
		{
			LOG_WARN(Db, "[MysqlMapDiscoveryStore] ListPage({}) failed: {}", characterId, mysql_error(mysql));
			return false;
		}
		while (stmt->FetchRow())
		{
			StoredCell c;
			c.cellZoneId = static_cast<uint32_t>(stmt->GetUInt64(0));
			const std::string bits = stmt->GetString(1);
			if (!c.bitmap.Assign(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bits.data()), bits.size())))
			{
				LOG_WARN(Db, "[MysqlMapDiscoveryStore] masque invalide (character={}, cell={}, {} octets) ignore",
					characterId, c.cellZoneId, bits.size());
				continue;
			}
			outCells.push_back(c);
		}
		return true;
	}

	int MysqlMapDiscoveryStore::Merge(uint64_t characterId, const std::vector<StoredCell>& cells)
	{
		if (!IsAvailable()) return -1;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return -1;

		int changed = 0;
		for (const StoredCell& in : cells)
		{
			// Lecture du masque existant puis écriture de la fusion OU (un seul
			// client par personnage : pas de concurrence sur une même ligne).
			disc::CellBitmap merged;
			auto* sel = cache->Acquire(mysql,
				"SELECT bits FROM character_map_discovery WHERE character_id = ? AND cell_zone_id = ?");
			if (!sel || !sel->Bind(0, characterId) || !sel->Bind(1, in.cellZoneId) || !sel->Execute())
			{
				LOG_WARN(Db, "[MysqlMapDiscoveryStore] Merge read({}, {}) failed: {}", characterId, in.cellZoneId, mysql_error(mysql));
				return -1;
			}
			bool existed = false;
			if (sel->FetchRow())
			{
				const std::string bits = sel->GetString(0);
				existed = merged.Assign(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bits.data()), bits.size()));
				while (sel->FetchRow()) {}
			}
			const bool grew = merged.Merge(in.bitmap);
			if (existed && !grew)
				continue;
			if (merged.Empty())
				continue;

			auto* up = cache->Acquire(mysql,
				"INSERT INTO character_map_discovery (character_id, cell_zone_id, bits) VALUES (?, ?, ?) "
				"ON DUPLICATE KEY UPDATE bits = VALUES(bits)");
			const auto& bytes = merged.Bytes();
			if (!up || !up->Bind(0, characterId) || !up->Bind(1, in.cellZoneId)
				|| !up->BindBlob(2, bytes.data(), bytes.size()) || !up->Execute())
			{
				LOG_WARN(Db, "[MysqlMapDiscoveryStore] Merge write({}, {}) failed: {}", characterId, in.cellZoneId, mysql_error(mysql));
				return -1;
			}
			++changed;
		}
		return changed;
	}
}
