#include "src/shardd/internals/globals/ConditionMgr.h"

#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"
#include "src/shared/core/Log.h"

#include <mysql.h>

namespace engine::server::shard::globals
{
	namespace
	{
		// Convention de l'overlap condition_id / group_id : voir doc.
		constexpr uint32_t kGroupIdMinInclusive = 10000;

		bool IsGroupIdRange(uint32_t id)
		{
			return id >= kGroupIdMinInclusive;
		}
	}

	bool ConditionMgr::Load(engine::server::db::ConnectionPool& pool)
	{
		if (m_loaded)
			return false;

		auto guard = pool.Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;

		// 1) Conditions atomiques. N1-I : prepared statement no-param.
		{
			auto* stmt = cache->Acquire(mysql,
				"SELECT condition_id, type, value1, value2, value3 FROM conditions");
			if (!stmt || !stmt->Execute())
				return false;
			while (stmt->FetchRow())
			{
				Condition c{};
				c.conditionId = static_cast<uint32_t>(stmt->GetUInt64(0));
				c.type        = static_cast<ConditionType>(stmt->GetInt32(1));
				c.value1      = stmt->GetInt32(2);
				c.value2      = stmt->GetInt32(3);
				c.value3      = stmt->GetInt32(4);
				m_conditions.emplace(c.conditionId, c);
			}
		}

		// 2) Condition groups (rows multiples par group_id). N1-I : prepared statement.
		{
			auto* stmt = cache->Acquire(mysql,
				"SELECT group_id, logic, member_id, member_type FROM condition_groups "
				"ORDER BY group_id, member_id");
			if (!stmt || !stmt->Execute())
				return false;
			while (stmt->FetchRow())
			{
				const uint32_t gid = static_cast<uint32_t>(stmt->GetUInt64(0));
				ConditionGroup& g = m_groups[gid];
				g.groupId = gid;
				g.logic   = static_cast<ConditionLogic>(stmt->GetInt32(1));
				ConditionGroupMember m{};
				m.memberId   = static_cast<uint32_t>(stmt->GetUInt64(2));
				m.memberType = static_cast<ConditionMemberType>(stmt->GetInt32(3));
				g.members.push_back(m);
			}
		}

		// 3) Détection cycles dans les groups.
		if (DetectCycles())
		{
			LOG_ERROR(Core, "[ConditionMgr] Cycle detected in condition_groups, aborting load");
			m_conditions.clear();
			m_groups.clear();
			return false;
		}

		m_loaded = true;
		LOG_INFO(Core, "[ConditionMgr] Loaded {} conditions, {} groups",
			m_conditions.size(), m_groups.size());
		return true;
	}

	bool ConditionMgr::DetectCycles() const
	{
		std::unordered_map<uint32_t, int> color;  // 0=white, 1=gray, 2=black
		for (const auto& [gid, _] : m_groups)
		{
			color[gid] = 0;
		}
		for (const auto& [gid, _] : m_groups)
		{
			if (color[gid] == 0 && DetectCyclesFrom(gid, color))
				return true;
		}
		return false;
	}

	bool ConditionMgr::DetectCyclesFrom(uint32_t groupId,
		std::unordered_map<uint32_t, int>& color) const
	{
		color[groupId] = 1;  // gray
		auto it = m_groups.find(groupId);
		if (it == m_groups.end())
		{
			color[groupId] = 2;
			return false;
		}
		for (const auto& m : it->second.members)
		{
			if (m.memberType != ConditionMemberType::Group)
				continue;
			auto cit = color.find(m.memberId);
			if (cit == color.end())
				continue;  // group référencé mais inexistant — pas un cycle, juste broken data
			if (cit->second == 1)
				return true;  // back edge → cycle
			if (cit->second == 0 && DetectCyclesFrom(m.memberId, color))
				return true;
		}
		color[groupId] = 2;  // black
		return false;
	}

	bool ConditionMgr::EvaluateAtom(const Condition& cond, const EvaluationContext& ctx) const
	{
		switch (cond.type)
		{
			case ConditionType::LevelGE:
				return ctx.sourceLevel >= cond.value1;
			case ConditionType::LevelLE:
				return ctx.sourceLevel <= cond.value1;
			case ConditionType::HasItem:
			{
				const uint32_t itemEntry = static_cast<uint32_t>(cond.value1);
				const uint32_t minCount  = static_cast<uint32_t>(cond.value2 > 0 ? cond.value2 : 1);
				auto it = ctx.sourceItems.find(itemEntry);
				return it != ctx.sourceItems.end() && it->second >= minCount;
			}
			case ConditionType::ZoneId:
				return ctx.sourceZoneId == static_cast<uint32_t>(cond.value1);
			case ConditionType::InGroup:
				return ctx.inGroup;
		}
		return false;  // unknown enum value
	}

	bool ConditionMgr::EvaluateCondition(uint32_t conditionId, const EvaluationContext& ctx) const
	{
		auto it = m_conditions.find(conditionId);
		if (it == m_conditions.end())
			return false;
		return EvaluateAtom(it->second, ctx);
	}

	bool ConditionMgr::EvaluateGroup(uint32_t groupId, const EvaluationContext& ctx) const
	{
		auto it = m_groups.find(groupId);
		if (it == m_groups.end())
			return false;
		const ConditionGroup& g = it->second;

		if (g.logic == ConditionLogic::Not)
		{
			// 1 seul membre attendu.
			if (g.members.empty())
				return false;
			const auto& m = g.members.front();
			const bool inner = (m.memberType == ConditionMemberType::Group)
				? EvaluateGroup(m.memberId, ctx)
				: EvaluateCondition(m.memberId, ctx);
			return !inner;
		}

		const bool isAnd = (g.logic == ConditionLogic::And);
		if (g.members.empty())
			return isAnd;  // AND vide = vrai, OR vide = faux (convention)

		for (const auto& m : g.members)
		{
			const bool ok = (m.memberType == ConditionMemberType::Group)
				? EvaluateGroup(m.memberId, ctx)
				: EvaluateCondition(m.memberId, ctx);
			if (isAnd && !ok)
				return false;
			if (!isAnd && ok)
				return true;
		}
		return isAnd;
	}

	bool ConditionMgr::Evaluate(uint32_t id, const EvaluationContext& ctx) const
	{
		if (IsGroupIdRange(id))
			return EvaluateGroup(id, ctx);
		return EvaluateCondition(id, ctx);
	}
}
