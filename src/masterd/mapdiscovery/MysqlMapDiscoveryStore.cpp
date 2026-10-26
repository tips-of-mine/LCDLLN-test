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
}
