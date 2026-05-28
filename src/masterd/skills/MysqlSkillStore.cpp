// Wave 5 Persistence (Phase 4.39b) - Implementation MysqlSkillStore.
// N1-E : converti en prepared statements (1 SELECT + 1 INSERT upsert).

#include "src/masterd/skills/MysqlSkillStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

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
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT character_id, skill_id, value, cap, bonus "
			"FROM character_skills WHERE character_id = ?");
		if (!stmt || !stmt->Bind(0, characterId) || !stmt->Execute())
		{
			LOG_WARN(Net, "[MysqlSkillStore] LoadForCharacter query failed character={}", characterId);
			return out;
		}
		while (stmt->FetchRow())
		{
			SkillRow r;
			r.characterId = stmt->GetUInt64(0);
			r.skillId     = static_cast<uint32_t>(stmt->GetUInt64(1));
			r.value       = static_cast<uint32_t>(stmt->GetUInt64(2));
			r.cap         = static_cast<uint32_t>(stmt->GetUInt64(3));
			r.bonus       = static_cast<uint32_t>(stmt->GetUInt64(4));
			out.push_back(r);
		}
		return out;
	}

	bool MysqlSkillStore::Upsert(const SkillRow& row)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO character_skills (character_id, skill_id, value, cap, bonus) "
			"VALUES (?, ?, ?, ?, ?) "
			"ON DUPLICATE KEY UPDATE "
			"value = VALUES(value), cap = VALUES(cap), bonus = VALUES(bonus)");
		const bool ok = stmt
			&& stmt->Bind(0, row.characterId)
			&& stmt->Bind(1, row.skillId)
			&& stmt->Bind(2, row.value)
			&& stmt->Bind(3, row.cap)
			&& stmt->Bind(4, row.bonus)
			&& stmt->Execute();
		if (!ok)
			LOG_WARN(Net, "[MysqlSkillStore] Upsert failed character={} skill={}",
				row.characterId, row.skillId);
		return ok;
	}
}
