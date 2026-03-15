/**
 * M22.3: Integration test for shard down detection.
 * EvictStaleHeartbeats(timeout, as_of) marks shards offline and invokes shard_down callback.
 * No external test framework; returns 0 if all pass, non-zero on first failure.
 */

#include "engine/server/ShardRegistry.h"
#include "engine/core/Log.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace
{
	static int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

using namespace engine::server;

static void TestEvictStaleHeartbeatsMarksOfflineAndInvokesCallback()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	ShardRegistry reg;
	auto t0 = std::chrono::steady_clock::now();
	auto idOpt = reg.RegisterShard("s1", "ep1", 100, "eu");
	Assert(idOpt.has_value(), "RegisterShard returns id");
	uint32_t shardId = *idOpt;

	bool callbackInvoked = false;
	uint32_t callbackShardId = 0;
	reg.SetShardDownCallback([&](uint32_t sid) {
		callbackInvoked = true;
		callbackShardId = sid;
	});

	// As-of t0+5s, timeout 2s => threshold = t0+3. last_heartbeat was set at register (~t0), so shard is stale.
	reg.EvictStaleHeartbeats(2, t0 + std::chrono::seconds(5));

	auto info = reg.GetShard(shardId);
	Assert(info.has_value(), "GetShard returns entry");
	Assert(info->state == ShardState::Offline, "Shard state is Offline after EvictStaleHeartbeats");
	Assert(callbackInvoked, "Shard down callback was invoked");
	Assert(callbackShardId == shardId, "Callback received correct shard_id");

	engine::core::Log::Shutdown();
}

int main()
{
	TestEvictStaleHeartbeatsMarksOfflineAndInvokesCallback();
	return s_failCount != 0 ? 1 : 0;
}
