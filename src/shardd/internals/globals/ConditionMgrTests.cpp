// CMANGOS.16 (Phase 1b) — Tests ConditionMgr.

#include "engine/server/shard/globals/ConditionMgr.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::shard::globals::ConditionMgr;
	using engine::server::shard::globals::EvaluationContext;
	using engine::server::db::ConnectionPool;

	bool TestAtomEvaluation(ConditionMgr& mgr)
	{
		EvaluationContext ctx{};
		ctx.sourceLevel = 5;
		ctx.sourceZoneId = 10;
		ctx.inGroup = false;

		// C1=LevelGE 10. Niveau 5 → false, niveau 15 → true.
		if (mgr.EvaluateCondition(1, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C1(LvlGE10) lvl=5 expected false");
			return false;
		}
		ctx.sourceLevel = 15;
		if (!mgr.EvaluateCondition(1, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C1(LvlGE10) lvl=15 expected true");
			return false;
		}

		// C3=ZoneId 42. Zone 10 → false, zone 42 → true.
		if (mgr.EvaluateCondition(3, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C3(Zone42) zone=10 expected false");
			return false;
		}
		ctx.sourceZoneId = 42;
		if (!mgr.EvaluateCondition(3, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C3(Zone42) zone=42 expected true");
			return false;
		}

		// C4=InGroup. Pas en groupe → false.
		if (mgr.EvaluateCondition(4, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C4(InGroup) inGroup=false expected false");
			return false;
		}
		ctx.inGroup = true;
		if (!mgr.EvaluateCondition(4, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C4(InGroup) inGroup=true expected true");
			return false;
		}
		LOG_INFO(Core, "[ConditionMgrTests] Atom evaluation OK");
		return true;
	}

	bool TestGroupComposition(ConditionMgr& mgr)
	{
		EvaluationContext ctx{};
		// G100 = AND(C1=LvlGE10, C2=LvlLE20).
		// lvl=15 → true. lvl=5 → false. lvl=25 → false.
		ctx.sourceLevel = 15;
		if (!mgr.EvaluateGroup(100, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G100(AND) lvl=15 expected true");
			return false;
		}
		ctx.sourceLevel = 5;
		if (mgr.EvaluateGroup(100, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G100(AND) lvl=5 expected false");
			return false;
		}
		ctx.sourceLevel = 25;
		if (mgr.EvaluateGroup(100, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G100(AND) lvl=25 expected false");
			return false;
		}

		// G101 = OR(G100, C3=Zone42). lvl=5+zone=42 → true via C3.
		ctx.sourceLevel = 5;
		ctx.sourceZoneId = 42;
		if (!mgr.EvaluateGroup(101, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G101(OR) lvl=5,zone=42 expected true");
			return false;
		}
		// lvl=5 + zone=10 → false.
		ctx.sourceZoneId = 10;
		if (mgr.EvaluateGroup(101, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G101(OR) lvl=5,zone=10 expected false");
			return false;
		}

		// G102 = NOT(C4=InGroup). inGroup=false → NOT(false)=true.
		ctx.inGroup = false;
		if (!mgr.EvaluateGroup(102, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G102(NOT) inGroup=false expected true");
			return false;
		}
		ctx.inGroup = true;
		if (mgr.EvaluateGroup(102, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G102(NOT) inGroup=true expected false");
			return false;
		}
		LOG_INFO(Core, "[ConditionMgrTests] Group composition AND/OR/NOT OK");
		return true;
	}

	bool TestEvaluateHelper(ConditionMgr& mgr)
	{
		// Convention : id < 10000 → condition, id >= 10000 → group.
		// Mais nos tests utilisent ids 1-4 pour conditions et 100-102 pour groups.
		// Le helper Evaluate dispatch via la convention ; donc Evaluate(100, ctx)
		// va aller chercher dans les conditions (1-9999) et NE TROUVERA RIEN.
		// On teste donc directement EvaluateGroup ci-dessus. Le helper Evaluate
		// est testé avec un id de condition seul.
		EvaluationContext ctx{};
		ctx.sourceLevel = 15;
		if (!mgr.Evaluate(1, ctx))  // Evaluate(1) → EvaluateCondition(1) → C1 ok
		{
			LOG_ERROR(Core, "[ConditionMgrTests] Evaluate(1) expected true");
			return false;
		}
		LOG_INFO(Core, "[ConditionMgrTests] Evaluate dispatch OK");
		return true;
	}

	bool TestDoubleLoadRejected(ConnectionPool& pool)
	{
		ConditionMgr mgr;
		const bool ok1 = mgr.Load(pool);
		if (!ok1)
		{
			LOG_ERROR(Core, "[ConditionMgrTests] First Load failed");
			return false;
		}
		const bool ok2 = mgr.Load(pool);
		if (ok2)
		{
			LOG_ERROR(Core, "[ConditionMgrTests] Second Load should fail");
			return false;
		}
		LOG_INFO(Core, "[ConditionMgrTests] Double-load rejected OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	if (config.GetString("db.host", "").empty())
	{
		LOG_INFO(Core, "[ConditionMgrTests] db.host not set, skipping");
		engine::core::Log::Shutdown();
		return 0;
	}

	ConnectionPool pool;
	if (!pool.Init(config))
	{
		LOG_ERROR(Core, "[ConditionMgrTests] Pool Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}

	bool ok = false;
	{
		ConditionMgr mgr;
		if (!mgr.Load(pool))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] Load failed (migration 0042 applied?)");
		}
		else
		{
			ok = TestAtomEvaluation(mgr) && TestGroupComposition(mgr) && TestEvaluateHelper(mgr);
		}
	}
	if (ok)
		ok = TestDoubleLoadRejected(pool);

	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
