#include "src/shardd/internals/globals/GraveyardManager.h"

#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"
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
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;

		// N1-I : prepared statement no-param. Floats via GetString + strtof.
		auto* stmt = cache->Acquire(mysql,
			"SELECT id, map_id, position_x, position_y, position_z, faction, zone_id "
			"FROM graveyards");
		if (!stmt || !stmt->Execute())
			return false;

		while (stmt->FetchRow())
		{
			Graveyard g{};
			g.id        = static_cast<uint32_t>(stmt->GetUInt64(0));
			g.mapId     = static_cast<uint32_t>(stmt->GetUInt64(1));
			g.positionX = std::strtof(stmt->GetString(2).c_str(), nullptr);
			g.positionY = std::strtof(stmt->GetString(3).c_str(), nullptr);
			g.positionZ = std::strtof(stmt->GetString(4).c_str(), nullptr);
			g.faction   = static_cast<FactionId>(stmt->GetInt32(5));
			g.zoneId    = static_cast<uint32_t>(stmt->GetUInt64(6));
			m_graveyards.push_back(g);
		}

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
