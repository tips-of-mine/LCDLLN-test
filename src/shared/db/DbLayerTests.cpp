// M21.6 — Smoke tests for DB layer: pool Acquire/Release, Execute/Query.

#include "engine/server/db/ConnectionPool.h"
#include "engine/server/db/DbHelpers.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <mysql.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	if (config.GetString("db.host", "").empty())
	{
		LOG_INFO(Core, "[DbLayerTests] db.host not set, skipping (smoke test optional without DB)");
		engine::core::Log::Shutdown();
		return 0;
	}

	engine::server::db::ConnectionPool pool;
	if (!pool.Init(config))
	{
		LOG_ERROR(Core, "[DbLayerTests] Pool Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}

	auto guard = pool.Acquire();
	if (!guard)
	{
		LOG_ERROR(Core, "[DbLayerTests] Acquire failed");
		pool.Shutdown();
		engine::core::Log::Shutdown();
		return 1;
	}

	MYSQL_RES* res = engine::server::db::DbQuery(guard.get(), "SELECT 1 AS n");
	if (!res)
	{
		LOG_ERROR(Core, "[DbLayerTests] DbQuery(SELECT 1) failed");
		pool.Shutdown();
		engine::core::Log::Shutdown();
		return 1;
	}
	engine::server::db::DbFreeResult(res);
	LOG_INFO(Core, "[DbLayerTests] SELECT 1 OK");

	guard = engine::server::db::ConnectionPool::Guard();
	pool.Shutdown();
	LOG_INFO(Core, "[DbLayerTests] Smoke test passed");
	engine::core::Log::Shutdown();
	return 0;
}
