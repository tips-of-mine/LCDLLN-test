// Wave 5 Persistence (Phase 4.39b) - Implementation MysqlSkillStore.

#include "src/masterd/skills/MysqlSkillStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>

namespace engine::server::skills
{
	bool MysqlSkillStore::IsAvailable() const noexcept
	{
		return m_pool && m_pool->IsInitialized();
	}

	std::vector<SkillRow> MysqlSkillStore::LoadForCharacter(uint64_t characterId) const
	{
		std::vector<SkillRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return out;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT character_id, skill_id, value, cap, bonus "
			"FROM character_skills WHERE character_id = %llu",
			static_cast<unsigned long long>(characterId));
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res)
		{
			LOG_WARN(Net, "[MysqlSkillStore] LoadForCharacter query failed character={}", characterId);
			return out;
		}
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			SkillRow r;
			if (row[0]) r.characterId = std::strtoull(row[0], nullptr, 10);
			if (row[1]) r.skillId     = static_cast<uint32_t>(std::strtoul(row[1], nullptr, 10));
			if (row[2]) r.value       = static_cast<uint32_t>(std::strtoul(row[2], nullptr, 10));
			if (row[3]) r.cap         = static_cast<uint32_t>(std::strtoul(row[3], nullptr, 10));
			if (row[4]) r.bonus       = static_cast<uint32_t>(std::strtoul(row[4], nullptr, 10));
			out.push_back(r);
		}
		engine::server::db::DbFreeResult(res);
		return out;
	}

	bool MysqlSkillStore::Upsert(const SkillRow& row)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		char sql[512];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO character_skills (character_id, skill_id, value, cap, bonus) "
			"VALUES (%llu, %u, %u, %u, %u) "
			"ON DUPLICATE KEY UPDATE "
			"value = VALUES(value), cap = VALUES(cap), bonus = VALUES(bonus)",
			static_cast<unsigned long long>(row.characterId),
			row.skillId, row.value, row.cap, row.bonus);

		const bool ok = engine::server::db::DbExecute(mysql, sql);
		if (!ok)
			LOG_WARN(Net, "[MysqlSkillStore] Upsert failed character={} skill={}",
				row.characterId, row.skillId);
		return ok;
	}
}
