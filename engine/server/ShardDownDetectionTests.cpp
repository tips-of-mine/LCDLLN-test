/**
 * M22.3: Integration test for shard down detection.
 * EvictStaleHeartbeats(timeout, as_of) marks shards offline and invokes shard_down callback.
 * STAB.10: Tests for Online/Degraded transitions and SelectShard excluding Degraded.
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

/// STAB.10: Shard Online → load >= threshold → Degraded after UpdateHeartbeat.
static void TestOnlineToDegradedWhenLoadAboveThreshold()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	ShardRegistry reg;
	reg.SetDegradedLoadThreshold(0.90);
	auto idOpt = reg.RegisterShard("s1", "ep1", 100, "eu");
	Assert(idOpt.has_value(), "RegisterShard returns id");
	uint32_t shardId = *idOpt;
	// First heartbeat: Registering → Online
	reg.UpdateHeartbeat(shardId, 50);
	auto info = reg.GetShard(shardId);
	Assert(info.has_value() && info->state == ShardState::Online, "Shard is Online after first heartbeat");
	// Second heartbeat: load 95/100 = 0.95 >= 0.90 → Degraded
	reg.UpdateHeartbeat(shardId, 95);
	info = reg.GetShard(shardId);
	Assert(info.has_value(), "GetShard returns entry");
	Assert(info->state == ShardState::Degraded, "Shard is Degraded when load/cap >= threshold");

	engine::core::Log::Shutdown();
}

/// STAB.10: Shard Degraded → load < threshold → Online after UpdateHeartbeat.
static void TestDegradedToOnlineWhenLoadBelowThreshold()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	ShardRegistry reg;
	reg.SetDegradedLoadThreshold(0.90);
	auto idOpt = reg.RegisterShard("s1", "ep1", 100, "eu");
	Assert(idOpt.has_value(), "RegisterShard returns id");
	uint32_t shardId = *idOpt;
	reg.UpdateHeartbeat(shardId, 95);
	auto info = reg.GetShard(shardId);
	Assert(info.has_value() && info->state == ShardState::Degraded, "Shard is Degraded");
	reg.UpdateHeartbeat(shardId, 80);
	info = reg.GetShard(shardId);
	Assert(info.has_value(), "GetShard returns entry");
	Assert(info->state == ShardState::Online, "Shard recovered to Online when load/cap < threshold");

	engine::core::Log::Shutdown();
}

/// STAB.10: Shard Degraded → timeout heartbeat → Offline via EvictStaleHeartbeats.
static void TestDegradedToOfflineOnHeartbeatTimeout()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	ShardRegistry reg;
	reg.SetDegradedLoadThreshold(0.90);
	auto t0 = std::chrono::steady_clock::now();
	auto idOpt = reg.RegisterShard("s1", "ep1", 100, "eu");
	Assert(idOpt.has_value(), "RegisterShard returns id");
	uint32_t shardId = *idOpt;
	reg.UpdateHeartbeat(shardId, 95);
	auto info = reg.GetShard(shardId);
	Assert(info.has_value() && info->state == ShardState::Degraded, "Shard is Degraded");
	// EvictStaleHeartbeats: as_of t0+5s, timeout 2s => threshold t0+3; last_heartbeat at register (~t0) => stale
	reg.EvictStaleHeartbeats(2, t0 + std::chrono::seconds(5));
	info = reg.GetShard(shardId);
	Assert(info.has_value(), "GetShard returns entry");
	Assert(info->state == ShardState::Offline, "Degraded shard goes Offline on heartbeat timeout");

	engine::core::Log::Shutdown();
}

/// STAB.10: SelectShard does not return a Degraded shard.
static void TestSelectShardExcludesDegraded()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	ShardRegistry reg;
	reg.SetDegradedLoadThreshold(0.90);
	auto id1Opt = reg.RegisterShard("s1", "ep1", 100, "eu");
	auto id2Opt = reg.RegisterShard("s2", "ep2", 100, "eu");
	Assert(id1Opt.has_value() && id2Opt.has_value(), "Both shards registered");
	uint32_t shard1 = *id1Opt;
	uint32_t shard2 = *id2Opt;
	reg.UpdateHeartbeat(shard1, 10);
	reg.UpdateHeartbeat(shard2, 95);
	auto info1 = reg.GetShard(shard1);
	auto info2 = reg.GetShard(shard2);
	Assert(info1.has_value() && info2.has_value(), "GetShard returns both entries");
	Assert(info1->state == ShardState::Online && info2->state == ShardState::Degraded, "s1 Online, s2 Degraded");
	auto selected = reg.SelectShard();
	Assert(selected.has_value(), "SelectShard returns a shard");
	Assert(selected->shard_id == shard1, "SelectShard returns Online shard, not Degraded");

	engine::core::Log::Shutdown();
}

int main()
{
	TestEvictStaleHeartbeatsMarksOfflineAndInvokesCallback();
	TestOnlineToDegradedWhenLoadAboveThreshold();
	TestDegradedToOnlineWhenLoadBelowThreshold();
	TestDegradedToOfflineOnHeartbeatTimeout();
	TestSelectShardExcludesDegraded();
	return s_failCount != 0 ? 1 : 0;
}
