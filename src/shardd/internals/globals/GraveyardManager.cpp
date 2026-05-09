#include "src/shardd/internals/globals/GraveyardManager.h"

#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"
#include "src/shared/core/Log.h"

#include <mysql.h>

#include <cmath>
#include <cstdlib>
#include <limits>

namespace engine::server::shard::globals
{
	bool GraveyardManager::Load(engine::server::db::ConnectionPool& pool)
	{
		if (m_loaded)
			return false;

		auto guard = pool.Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;

		MYSQL_RES* res = engine::server::db::DbQuery(mysql,
			"SELECT id, map_id, position_x, position_y, position_z, faction, zone_id "
			"FROM graveyards");
		if (!res)
			return false;

		MYSQL_ROW row;
		while ((row = mysql_fetch_row(res)) != nullptr)
		{
			if (!row[0]) continue;
			Graveyard g{};
			g.id        = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
			g.mapId     = static_cast<uint32_t>(std::strtoul(row[1], nullptr, 10));
			g.positionX = static_cast<float>(std::atof(row[2]));
			g.positionY = static_cast<float>(std::atof(row[3]));
			g.positionZ = static_cast<float>(std::atof(row[4]));
			g.faction   = static_cast<FactionId>(std::atoi(row[5]));
			g.zoneId    = static_cast<uint32_t>(std::strtoul(row[6], nullptr, 10));
			m_graveyards.push_back(g);
		}
		engine::server::db::DbFreeResult(res);

		m_loaded = true;
		LOG_INFO(Core, "[GraveyardManager] Loaded {} graveyards", m_graveyards.size());
		return true;
	}

	std::optional<Graveyard> GraveyardManager::ClosestGraveyard(uint32_t mapId,
		float posX, float posY, float posZ, FactionId requestedFaction) const
	{
		std::optional<Graveyard> best;
		float bestDistSq = std::numeric_limits<float>::max();
		for (const auto& g : m_graveyards)
		{
			if (g.mapId != mapId)
				continue;
			if (g.faction != 0 && g.faction != requestedFaction)
				continue;
			const float dx = g.positionX - posX;
			const float dy = g.positionY - posY;
			const float dz = g.positionZ - posZ;
			const float distSq = dx*dx + dy*dy + dz*dz;
			if (distSq < bestDistSq)
			{
				bestDistSq = distSq;
				best = g;
			}
		}
		return best;
	}
}
