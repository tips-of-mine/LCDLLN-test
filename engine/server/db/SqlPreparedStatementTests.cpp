// CMANGOS.13 (Phase 1a) — Tests SqlPreparedStatement + Cache.

#include "engine/server/db/SqlPreparedStatement.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <mysql.h>

namespace
{
	using engine::server::db::SqlPreparedStatementCache;
	using engine::server::db::SqlPreparedStatement;
	using engine::server::db::ConnectionPool;

	bool TestBindExecuteFetch(MYSQL* mysql)
	{
		SqlPreparedStatementCache cache(8);
		SqlPreparedStatement* stmt = cache.Acquire(mysql,
			"SELECT name, value FROM phase_1a_test_storage WHERE entry = ?");
		if (!stmt)
		{
			LOG_ERROR(Core, "[SqlPSTests] Acquire failed (table 0041 missing?)");
			return false;
		}
		if (!stmt->Bind(0, static_cast<int32_t>(2)))
		{
			LOG_ERROR(Core, "[SqlPSTests] Bind(0, 2) failed");
			return false;
		}
		if (!stmt->Execute())
		{
			LOG_ERROR(Core, "[SqlPSTests] Execute failed");
			return false;
		}
		if (!stmt->FetchRow())
		{
			LOG_ERROR(Core, "[SqlPSTests] FetchRow failed (no data?)");
			return false;
		}
		const std::string name = stmt->GetString(0);
		const int32_t value = stmt->GetInt32(1);
		if (name != "beta" || value != 200)
		{
			LOG_ERROR(Core, "[SqlPSTests] Expected beta/200, got {}/{}", name, value);
			return false;
		}
		LOG_INFO(Core, "[SqlPSTests] BindExecuteFetch beta/200 OK");
		return true;
	}

	bool TestCacheHitMiss(MYSQL* mysql)
	{
		SqlPreparedStatementCache cache(2);  // capacité 2 pour forcer éviction
		const char* sql1 = "SELECT name FROM phase_1a_test_storage WHERE entry = ?";
		const char* sql2 = "SELECT value FROM phase_1a_test_storage WHERE entry = ?";
		const char* sql3 = "SELECT entry FROM phase_1a_test_storage WHERE entry = ?";

		SqlPreparedStatement* s1 = cache.Acquire(mysql, sql1);
		SqlPreparedStatement* s2 = cache.Acquire(mysql, sql2);
		if (!s1 || !s2 || cache.Size() != 2u)
		{
			LOG_ERROR(Core, "[SqlPSTests] Cache size after 2 misses != 2");
			return false;
		}
		// Hit sur s1 : devrait retourner le même pointeur.
		SqlPreparedStatement* s1bis = cache.Acquire(mysql, sql1);
		if (s1bis != s1)
		{
			LOG_ERROR(Core, "[SqlPSTests] Cache hit on sql1 returned different pointer");
			return false;
		}
		// Miss sur s3 : éviction LRU (s2 doit sortir, pas s1 qui vient d'être acquis).
		SqlPreparedStatement* s3 = cache.Acquire(mysql, sql3);
		if (!s3 || cache.Size() != 2u)
		{
			LOG_ERROR(Core, "[SqlPSTests] Cache size after eviction != 2");
			return false;
		}
		// Re-acquérir s2 doit donner un nouveau pointeur (il a été évincé).
		SqlPreparedStatement* s2bis = cache.Acquire(mysql, sql2);
		if (s2bis == s2)
		{
			LOG_ERROR(Core, "[SqlPSTests] sql2 should have been evicted, got same pointer");
			return false;
		}
		LOG_INFO(Core, "[SqlPSTests] Cache hit/miss/LRU eviction OK");
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
		LOG_INFO(Core, "[SqlPSTests] db.host not set, skipping");
		engine::core::Log::Shutdown();
		return 0;
	}

	ConnectionPool pool;
	if (!pool.Init(config))
	{
		LOG_ERROR(Core, "[SqlPSTests] Pool Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}

	auto guard = pool.Acquire();
	MYSQL* mysql = guard.get();
	bool ok = mysql && TestBindExecuteFetch(mysql) && TestCacheHitMiss(mysql);

	guard = ConnectionPool::Guard();
	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
