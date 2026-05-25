/**
 * M22.5: Integration tests for server list (reflects registry) and shard selection policy (lowest load ratio).
 * No external test framework; returns 0 if all pass, non-zero on first failure.
 */

#include "src/masterd/shards/ShardRegistry.h"
#include "src/shared/network/ServerListPayloads.h"
#include "src/shared/core/Log.h"

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
using namespace engine::network;

static void TestServerListReflectsRegistry()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	ShardRegistry reg;
	auto id1 = reg.RegisterShard("s1", "ep1", 100, "eu", "Royaume Un",
		engine::network::ShardGameMode::PvP, engine::network::ShardRuleset::Hardcore);
	Assert(id1.has_value(), "Register s1");
	reg.UpdateHeartbeat(*id1, 25);

	auto list = reg.ListShards();
	Assert(list.size() == 1u, "ListShards size 1");
	Assert(list[0].display_name == "Royaume Un", "registry display_name");
	Assert(list[0].region == "eu", "registry region");
	Assert(list[0].game_mode == engine::network::ShardGameMode::PvP, "registry game_mode");
	Assert(list[0].ruleset == engine::network::ShardRuleset::Hardcore, "registry ruleset");
	std::vector<ServerListEntry> entries;
	for (const auto& s : list)
	{
		ServerListEntry e;
		e.shard_id = s.shard_id;
		e.status = static_cast<uint8_t>(s.state);
		e.current_load = s.current_load;
		e.max_capacity = s.max_capacity;
		e.character_count = 0;
		e.endpoint = s.endpoint;
		e.display_name = s.display_name;
		e.game_mode = s.game_mode;
		e.ruleset = s.ruleset;
		entries.push_back(e);
	}
	auto payload = BuildServerListResponsePayload(entries);
	Assert(!payload.empty(), "BuildServerListResponsePayload");
	auto parsed = ParseServerListResponsePayload(payload.data(), payload.size());
	Assert(parsed.size() == 1u, "Parse returns 1 entry");
	Assert(parsed[0].shard_id == *id1, "entry shard_id");
	Assert(parsed[0].status == static_cast<uint8_t>(ShardState::Online), "entry status Online");
	Assert(parsed[0].current_load == 25u, "entry current_load");
	Assert(parsed[0].max_capacity == 100u, "entry max_capacity");
	Assert(parsed[0].display_name == "Royaume Un", "entry display_name round-trip");
	Assert(parsed[0].game_mode == engine::network::ShardGameMode::PvP, "entry game_mode round-trip");
	Assert(parsed[0].ruleset == engine::network::ShardRuleset::Hardcore, "entry ruleset round-trip");

	engine::core::Log::Shutdown();
}

static void TestPolicyChoosesLessLoadedShard()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	ShardRegistry reg;
	auto id1 = reg.RegisterShard("s1", "ep1", 100, "eu");
	auto id2 = reg.RegisterShard("s2", "ep2", 100, "eu");
	Assert(id1.has_value() && id2.has_value(), "Register 2 shards");
	reg.UpdateHeartbeat(*id1, 60);
	reg.UpdateHeartbeat(*id2, 20);

	auto selected = reg.SelectShard();
	Assert(selected.has_value(), "SelectShard returns one");
	Assert(selected->shard_id == *id2, "SelectShard chooses less loaded (s2 load 20 < s1 load 60)");
	Assert(selected->current_load == 20u, "selected load 20");

	engine::core::Log::Shutdown();
}

int main()
{
	TestServerListReflectsRegistry();
	TestPolicyChoosesLessLoadedShard();
	return s_failCount != 0 ? 1 : 0;
}
