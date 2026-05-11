// CMANGOS.16 (Phase 1b) — Tests GraveyardManager.

#include "src/shardd/internals/globals/GraveyardManager.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

namespace
{
	using engine::server::shard::globals::GraveyardManager;
	using engine::server::db::ConnectionPool;

	bool TestClosest(GraveyardManager& mgr)
	{
		// Seeds 0042 :
		//  G1 : (  0,0,0) faction=0 (neutral)
		//  G2 : (100,0,0) faction=1
		//  G3 : (200,0,0) faction=2
		// Position joueur (50, 0, 0) faction=1 → G2 (à 50) plus proche que G1 (à 50)
		// (égalité distance sur x, mais G1 est neutral et G2 matche faction=1).
		// Pour départager, on choisit (40,0,0) → G1 distance 40, G2 distance 60.

		auto a = mgr.ClosestGraveyard(0, 40.0f, 0.0f, 0.0f, 1);
		if (!a || a->id != 1)
		{
			LOG_ERROR(Core, "[GraveyardMgrTests] (40,0,0) faction=1 expected G1 (neutral closer)");
			return false;
		}
		auto b = mgr.ClosestGraveyard(0, 60.0f, 0.0f, 0.0f, 1);
		if (!b || b->id != 2)
		{
			LOG_ERROR(Core, "[GraveyardMgrTests] (60,0,0) faction=1 expected G2 (faction match closer)");
			return false;
		}
		// Faction 3 (inexistante en seed) : seul G1 (neutral) est valide.
		auto c = mgr.ClosestGraveyard(0, 1000.0f, 0.0f, 0.0f, 3);
		if (!c || c->id != 1)
		{
			LOG_ERROR(Core, "[GraveyardMgrTests] (1000,0,0) faction=3 expected G1 (only neutral)");
			return false;
		}
		// Map inexistante : nullopt.
		auto d = mgr.ClosestGraveyard(999, 0.0f, 0.0f, 0.0f, 1);
		if (d)
		{
			LOG_ERROR(Core, "[GraveyardMgrTests] map=999 expected nullopt");
			return false;
		}
		LOG_INFO(Core, "[GraveyardMgrTests] Closest graveyard filtering OK");
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
		LOG_INFO(Core, "[GraveyardMgrTests] db.host not set, skipping");
		engine::core::Log::Shutdown();
		return 0;
	}

	ConnectionPool pool;
	if (!pool.Init(config))
	{
		engine::core::Log::Shutdown();
		return 1;
	}

	GraveyardManager mgr;
	bool ok = mgr.Load(pool) && TestClosest(mgr);

	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
