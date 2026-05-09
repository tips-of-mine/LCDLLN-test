// CMANGOS.13 (Phase 1a) — Tests SqlDelayThread.

#include "engine/server/db/SqlDelayThread.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
	using engine::server::db::SqlDelayThread;
	using engine::server::db::ConnectionPool;

	bool TestEnqueueAndCallback(ConnectionPool& pool)
	{
		SqlDelayThread worker(pool, 1024);
		worker.Start();

		std::atomic<int> okCount{0};
		std::atomic<int> failCount{0};
		const int N = 5;
		for (int i = 0; i < N; ++i)
		{
			// Requête bénigne : SELECT 1. Pas de side-effect DB.
			worker.EnqueueExecute("DO 1", [&](bool ok) {
				if (ok) okCount.fetch_add(1);
				else failCount.fetch_add(1);
			});
		}

		// Attente complétion (timeout 2s).
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (okCount.load() + failCount.load() < N
			&& std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}

		worker.Stop();

		if (okCount.load() != N)
		{
			LOG_ERROR(Core, "[SqlDelayThreadTests] expected {} OK callbacks, got {} (fail={})",
				N, okCount.load(), failCount.load());
			return false;
		}
		LOG_INFO(Core, "[SqlDelayThreadTests] {} jobs all OK", N);
		return true;
	}

	bool TestQueueOverflow(ConnectionPool& pool)
	{
		// Queue size = 2 pour forcer overflow rapide.
		SqlDelayThread worker(pool, 2);
		// On ne démarre PAS le worker → la queue se remplit sans drain.
		const bool a = worker.EnqueueExecute("DO 1", nullptr);
		const bool b = worker.EnqueueExecute("DO 1", nullptr);
		const bool c = worker.EnqueueExecute("DO 1", nullptr);  // doit être rejeté
		if (!a || !b || c)
		{
			LOG_ERROR(Core, "[SqlDelayThreadTests] expected a=true b=true c=false, got {} {} {}",
				a, b, c);
			return false;
		}
		// On démarre puis on stop pour drain proprement (le destructeur appellera Stop).
		worker.Start();
		// Petite attente pour que la queue se vide.
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		worker.Stop();
		LOG_INFO(Core, "[SqlDelayThreadTests] Queue overflow rejection OK");
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
		LOG_INFO(Core, "[SqlDelayThreadTests] db.host not set, skipping");
		engine::core::Log::Shutdown();
		return 0;
	}

	ConnectionPool pool;
	if (!pool.Init(config))
	{
		LOG_ERROR(Core, "[SqlDelayThreadTests] Pool Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}

	const bool ok = TestEnqueueAndCallback(pool) && TestQueueOverflow(pool);

	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
