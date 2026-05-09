#include "engine/server/pools/PoolManager.h"
#include "engine/core/Log.h"

#include <random>
#include <unordered_set>

namespace
{
	using engine::server::pools::Pool;
	using engine::server::pools::PoolEntry;
	using engine::server::pools::PoolManager;
	using engine::server::pools::SpawnId;

	bool TestEmptyPool()
	{
		PoolManager mgr;
		std::mt19937 rng(1);
		auto out = mgr.Roll(99, rng);
		if (!out.empty()) return false;
		LOG_INFO(Core, "[PoolManagerTests] empty/unknown OK");
		return true;
	}

	bool TestSinglePick()
	{
		PoolManager mgr;
		Pool p;
		p.poolId = 1;
		p.maxActive = 1;
		p.entries = {{100, 1.0f}, {200, 1.0f}, {300, 1.0f}};
		mgr.Register(p);
		std::mt19937 rng(42);
		auto out = mgr.Roll(1, rng);
		if (out.size() != 1) return false;
		if (out[0] != 100 && out[0] != 200 && out[0] != 300) return false;
		LOG_INFO(Core, "[PoolManagerTests] single pick OK");
		return true;
	}

	bool TestNoReplacement()
	{
		PoolManager mgr;
		Pool p;
		p.poolId = 1;
		p.maxActive = 3;
		p.entries = {{100, 1.0f}, {200, 1.0f}, {300, 1.0f}};
		mgr.Register(p);
		std::mt19937 rng(42);
		auto out = mgr.Roll(1, rng);
		if (out.size() != 3) return false;
		std::unordered_set<SpawnId> set(out.begin(), out.end());
		if (set.size() != 3) return false;  // unique
		LOG_INFO(Core, "[PoolManagerTests] no replacement OK");
		return true;
	}

	bool TestWeightedDistribution()
	{
		// 1000 rolls : entry weight 99 doit dominer.
		PoolManager mgr;
		Pool p;
		p.poolId = 1;
		p.maxActive = 1;
		p.entries = {{100, 99.0f}, {200, 1.0f}};
		mgr.Register(p);

		std::mt19937 rng(1);
		int count100 = 0, count200 = 0;
		for (int i = 0; i < 1000; ++i)
		{
			auto out = mgr.Roll(1, rng);
			if (out[0] == 100) ++count100;
			else if (out[0] == 200) ++count200;
		}
		// Avec 99/1, on attend ~990/10. Tolerance large.
		if (count100 < 950 || count200 > 60)
		{
			LOG_ERROR(Core, "[PoolManagerTests] weighted dist out of bounds: 100={} 200={}",
				count100, count200);
			return false;
		}
		LOG_INFO(Core, "[PoolManagerTests] weighted dist OK (100={}, 200={})", count100, count200);
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestEmptyPool() && TestSinglePick()
		&& TestNoReplacement() && TestWeightedDistribution();

	if (ok) LOG_INFO(Core, "[PoolManagerTests] ALL OK");
	else LOG_ERROR(Core, "[PoolManagerTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
